/* Floating point codecs following ml_dtypes 0.5.4 (the reference for every format).
 *
 * Encoders work on integer bit patterns only (no FP rounding modes involved), so
 * they are exact and portable. All but float16 convert double -> float32 first,
 * as ml_dtypes does; float16 converts directly from double. The functions are
 * always-inline with constant arguments, so every format compiles to its own
 * specialised straight-line code. Header-only so tools/oracle can reuse it. */
#ifndef DLP_CODEC_H
#define DLP_CODEC_H

#include <stdint.h>
#include <string.h>
#include <math.h>

#ifndef DLP_INLINE
#define DLP_INLINE static inline __attribute__((always_inline))
#endif

enum { DLP_K_IEEE, DLP_K_FN, DLP_K_FNUZ, DLP_K_SAT };

/* SMALL(id, exponent bits, mantissa bits, bias, kind) - all formats of at most 8 bits except e8m0 */
#define DLP_SMALL(X) \
	X(F8E3M4,        3, 4, 3,  DLP_K_IEEE) \
	X(F8E4M3,        4, 3, 7,  DLP_K_IEEE) \
	X(F8E4M3B11FNUZ, 4, 3, 11, DLP_K_FNUZ) \
	X(F8E4M3FN,      4, 3, 7,  DLP_K_FN) \
	X(F8E4M3FNUZ,    4, 3, 8,  DLP_K_FNUZ) \
	X(F8E5M2,        5, 2, 15, DLP_K_IEEE) \
	X(F8E5M2FNUZ,    5, 2, 16, DLP_K_FNUZ) \
	X(F6E2M3FN,      2, 3, 1,  DLP_K_SAT) \
	X(F6E3M2FN,      3, 2, 3,  DLP_K_SAT) \
	X(F4E2M1FN,      2, 1, 1,  DLP_K_SAT)

/* u = raw IEEE bits of a float (SE=8, SM=23, SB=127) or double (SE=11, SM=52, SB=1023);
 * returns the target code including the sign bit. */
DLP_INLINE uint32_t dlp_enc(uint64_t u, int SE, int SM, int SB, int E, int M, int B, int kind)
{
	uint32_t s = (uint32_t)(u >> (SE + SM)) & 1;
	uint64_t a = u & ((1ull << (SE + SM)) - 1);
	uint64_t infbits = ((1ull << SE) - 1) << SM;
	uint32_t sbit = 1u << (E + M), sgn = s ? sbit : 0;
	uint32_t inf = ((1u << E) - 1) << M;
	uint32_t maxc = kind == DLP_K_IEEE ? inf - 1 : kind == DLP_K_FN ? sbit - 2 : sbit - 1;
	uint64_t code;

	if (a > infbits) { /* NaN */
		if (kind == DLP_K_FNUZ) return sbit;
		if (kind == DLP_K_SAT) return s ? 0 : sbit; /* ml_dtypes quirk: the sign is inverted */
		return sgn | (kind == DLP_K_IEEE ? inf | (1u << (M - 1)) : sbit - 1);
	}
	if (a == infbits) {
		code = ~0ull;
	} else {
		int se = (int)(a >> SM);
		if (se >= SB + 1 - B) { /* normal in the target */
			int sh = SM - M;
			a += ((1ull << (sh - 1)) - 1) + ((a >> sh) & 1); /* round to nearest even */
			code = (a >> sh) - ((uint64_t)(SB - B) << M);
		} else { /* subnormal in the target (or zero) */
			int sh = SB + SM + 1 - B - M - se;
			if (sh > SM + 1) {
				code = 0;
			} else {
				uint64_t m = (a & ((1ull << SM) - 1)) | (se ? 1ull << SM : 0);
				uint64_t q = m >> sh, rem = m & ((1ull << sh) - 1), half = 1ull << (sh - 1);
				code = q + (rem > half || (rem == half && (q & 1)));
			}
		}
	}
	if (code > maxc) {
		if (kind == DLP_K_IEEE) return sgn | inf;
		if (kind == DLP_K_FN) return sgn | (sbit - 1);
		if (kind == DLP_K_FNUZ) return sbit;
		return sgn | maxc;
	}
	if (kind == DLP_K_FNUZ && code == 0) return 0; /* no negative zero */
	return sgn | (uint32_t)code;
}

DLP_INLINE uint32_t dlp_f32_bits(double d)
{
	float f = (float)d;
	uint32_t u;
	memcpy(&u, &f, 4);
	return u;
}

#define X(id, E, M, B, K) \
	DLP_INLINE uint8_t dlp_enc_##id(double d) { return (uint8_t)dlp_enc(dlp_f32_bits(d), 8, 23, 127, E, M, B, K); }
DLP_SMALL(X)
#undef X

DLP_INLINE uint16_t dlp_enc_FLOAT16(double d)
{
	uint64_t u;
	memcpy(&u, &d, 8);
	return (uint16_t)dlp_enc(u, 11, 52, 1023, 5, 10, 15, DLP_K_IEEE);
}

DLP_INLINE uint16_t dlp_enc_BFLOAT16(double d)
{
	uint32_t u = dlp_f32_bits(d);
	if ((u & 0x7fffffffu) > 0x7f800000u) return (uint16_t)(((u >> 16) & 0x8000u) | 0x7fc0u);
	u += 0x7fffu + ((u >> 16) & 1u);
	return (uint16_t)(u >> 16);
}

DLP_INLINE uint8_t dlp_enc_F8E8M0FNU(double d)
{
	uint32_t u = dlp_f32_bits(d), a = u & 0x7fffffffu, se = a >> 23, mant = a & 0x7fffffu;
	if ((u >> 31) || a == 0 || a >= 0x7f800000u) return 0xff;
	if (se == 0) return mant > 0x400000u ? 1 : 0;
	return (uint8_t)(se + (mant >= 0x400000u)); /* rounds half up, 254 + carry = NaN */
}

DLP_INLINE double dlp_dec_FLOAT16(uint16_t h)
{
	uint64_t s = (uint64_t)(h >> 15) << 63;
	unsigned e = (h >> 10) & 31, m = h & 1023;
	uint64_t bits;
	double v;
	if (e == 0) {
		v = (double)m * 0x1p-24;
		memcpy(&bits, &v, 8);
		return s ? -v : v;
	}
	bits = s | ((uint64_t)(e == 31 ? 2047 : e - 15 + 1023) << 52) | ((uint64_t)m << 42);
	memcpy(&v, &bits, 8);
	return v;
}

DLP_INLINE double dlp_dec_BFLOAT16(uint16_t h)
{
	uint32_t u = (uint32_t)h << 16;
	float f;
	memcpy(&f, &u, 4);
	return (double)f;
}

/* decode one code of a format, used to fill the lookup tables once at startup */
static inline double dlp_dec_generic(unsigned code, int E, int M, int B, int kind)
{
	unsigned sbit = 1u << (E + M), mag, e, m;
	double v;
	code &= (sbit << 1) - 1;
	mag = code & (sbit - 1);
	e = mag >> M;
	m = mag & ((1u << M) - 1);
	if (kind == DLP_K_FNUZ && code == sbit) return NAN;
	if (e == 0) v = ldexp((double)m, 1 - B - M);
	else if (kind == DLP_K_IEEE && e == (1u << E) - 1) v = m ? NAN : INFINITY;
	else if (kind == DLP_K_FN && mag == sbit - 1) v = NAN;
	else v = ldexp(1.0 + ldexp((double)m, -M), (int)e - B);
	return (code & sbit) ? -v : v;
}

static inline double dlp_dec_e8m0(unsigned code)
{
	return code == 0xff ? NAN : ldexp(1.0, (int)code - 127);
}

#endif
