/* buffers_convert(): element conversion between any two dtypes, for extensions that compute on elements (C API).
 *
 * Every element is decoded into a wide value (int64, uint64, double or a pair of doubles), converted to the class of the
 * destination dtype and encoded. It is a loop with a switch per element: simple and exact, not fast. */
#include "php_buffers.h"
#include "codec.h"

extern double buf_tab[][256];

typedef struct { int cls; int64_t i; double re, im; } wide;   /* cls: BUFFERS_KIND_INT (also bool), _UINT, _FLOAT, _COMPLEX; uint64 kept in i */

#define LD(n, T) static T ld_##n(const uint8_t *p) { T v; buf_ld_le(&v, p, sizeof v); return v; }
#define ST(n, T) static void st_##n(uint8_t *p, T v) { buf_st_le(p, &v, sizeof v); }
LD(i8, int8_t) LD(u8, uint8_t) LD(i16, int16_t) LD(u16, uint16_t) LD(i32, int32_t) LD(u32, uint32_t)
LD(i64, int64_t) LD(u64, uint64_t) LD(f32, float) LD(f64, double)
ST(u16, uint16_t) ST(u32, uint32_t) ST(u64, uint64_t) ST(f32, float) ST(f64, double)

static void load(int dt, const uint8_t *p, int64_t i, wide *w)
{
	int bits = buf_dtypes[dt].bits;

	w->re = w->im = 0; w->i = 0; w->cls = BUF_INT;
	switch (dt) {
	case BUF_T_BOOL: w->cls = BUF_INT; w->i = p[i] != 0; return;
	case BUF_T_INT2: case BUF_T_INT4: {
		int64_t c = pk_get(p, (zend_ulong)i, bits);
		w->cls = BUF_INT; w->i = c >= (1 << (bits - 1)) ? c - (1 << bits) : c; return;
	}
	case BUF_T_UINT2: case BUF_T_UINT4: w->cls = BUF_UINT; w->i = pk_get(p, (zend_ulong)i, bits); return;
	case BUF_T_INT8: w->cls = BUF_INT; w->i = ld_i8(p + i); return;
	case BUF_T_INT16: w->cls = BUF_INT; w->i = ld_i16(p + i * 2); return;
	case BUF_T_INT32: w->cls = BUF_INT; w->i = ld_i32(p + i * 4); return;
	case BUF_T_INT64: w->cls = BUF_INT; w->i = ld_i64(p + i * 8); return;
	case BUF_T_UINT8: w->cls = BUF_UINT; w->i = ld_u8(p + i); return;
	case BUF_T_UINT16: w->cls = BUF_UINT; w->i = ld_u16(p + i * 2); return;
	case BUF_T_UINT32: w->cls = BUF_UINT; w->i = ld_u32(p + i * 4); return;
	case BUF_T_UINT64: w->cls = BUF_UINT; w->i = (int64_t)ld_u64(p + i * 8); return;
	case BUF_T_FLOAT16: w->cls = BUF_FLOAT; w->re = buf_dec_FLOAT16(ld_u16(p + i * 2)); return;
	case BUF_T_BFLOAT16: w->cls = BUF_FLOAT; w->re = buf_dec_BFLOAT16(ld_u16(p + i * 2)); return;
	case BUF_T_FLOAT32: w->cls = BUF_FLOAT; w->re = ld_f32(p + i * 4); return;
	case BUF_T_FLOAT64: w->cls = BUF_FLOAT; w->re = ld_f64(p + i * 8); return;
	case BUF_T_F8E3M4: case BUF_T_F8E4M3: case BUF_T_F8E4M3B11FNUZ: case BUF_T_F8E4M3FN: case BUF_T_F8E4M3FNUZ:
	case BUF_T_F8E5M2: case BUF_T_F8E5M2FNUZ: case BUF_T_F8E8M0FNU:
		w->cls = BUF_FLOAT; w->re = buf_tab[dt - BUF_T_F8E3M4][p[i]]; return;
	case BUF_T_F6E2M3FN: case BUF_T_F6E3M2FN: case BUF_T_F4E2M1FN:
		w->cls = BUF_FLOAT; w->re = buf_tab[dt - BUF_T_F8E3M4][pk_get(p, (zend_ulong)i, bits)]; return;
	case BUF_T_COMPLEX32: w->cls = BUF_COMPLEX; w->re = buf_dec_FLOAT16(ld_u16(p + i * 4)); w->im = buf_dec_FLOAT16(ld_u16(p + i * 4 + 2)); return;
	case BUF_T_BCOMPLEX32: w->cls = BUF_COMPLEX; w->re = buf_dec_BFLOAT16(ld_u16(p + i * 4)); w->im = buf_dec_BFLOAT16(ld_u16(p + i * 4 + 2)); return;
	case BUF_T_COMPLEX64: w->cls = BUF_COMPLEX; w->re = ld_f32(p + i * 8); w->im = ld_f32(p + i * 8 + 4); return;
	case BUF_T_COMPLEX128: w->cls = BUF_COMPLEX; w->re = ld_f64(p + i * 16); w->im = ld_f64(p + i * 16 + 8); return;
	}
}

/* double -> integer bits as PHP's (int) does, without its warning: truncated toward zero, NaN and infinities give 0, values
 * outside the 64-bit range wrap modulo 2^64; the same as an element write (view.c, buf_lval) */
static int64_t d2i(double d) { return (int64_t)buf_dval_to_lval(d); }
static uint64_t d2u(double d) { return (uint64_t)d2i(d); }

/* the wide value as an integer bit pattern (two's complement) */
static uint64_t as_bits(const wide *w)
{
	switch (w->cls) {
	case BUF_INT: case BUF_UINT: return (uint64_t)w->i;
	default: return w->re < 0 ? (uint64_t)d2i(w->re) : d2u(w->re);
	}
}

static double as_double(const wide *w)
{
	switch (w->cls) {
	case BUF_INT: return (double)w->i;
	case BUF_UINT: return (double)(uint64_t)w->i;
	default: return w->re;
	}
}

static void store(int dt, uint8_t *p, int64_t i, const wide *w)
{
	int bits = buf_dtypes[dt].bits;
	int kind = buf_dtypes[dt].kind;
	uint64_t u = 0;
	double d = 0;

	if (kind == BUF_BOOL) {
		p[i] = w->cls == BUF_COMPLEX ? (w->re != 0 || w->im != 0) : w->cls == BUF_FLOAT ? w->re != 0 : w->i != 0;
		return;
	}
	if (kind == BUF_INT || kind == BUF_UINT) u = as_bits(w);
	else if ((w->cls == BUF_INT || w->cls == BUF_UINT) && dt != BUF_T_FLOAT64 && dt != BUF_T_COMPLEX128)
		/* an integer to a format no wider than Float32: rounded to Float32 once (as numpy and ml_dtypes do), not through a
		 * double, which would round a 64-bit integer twice */
		d = w->cls == BUF_UINT ? (double)(float)(uint64_t)w->i : (double)(float)w->i;
	else d = as_double(w);
	switch (dt) {
	case BUF_T_INT2: case BUF_T_INT4: case BUF_T_UINT2: case BUF_T_UINT4:
		pk_put(p, (zend_ulong)i, bits, (unsigned)u & ((1u << bits) - 1)); return;
	case BUF_T_INT8: case BUF_T_UINT8: p[i] = (uint8_t)u; return;
	case BUF_T_INT16: case BUF_T_UINT16: st_u16(p + i * 2, (uint16_t)u); return;
	case BUF_T_INT32: case BUF_T_UINT32: st_u32(p + i * 4, (uint32_t)u); return;
	case BUF_T_INT64: case BUF_T_UINT64: st_u64(p + i * 8, u); return;
	case BUF_T_FLOAT16: st_u16(p + i * 2, buf_enc_FLOAT16(d)); return;
	case BUF_T_BFLOAT16: st_u16(p + i * 2, buf_enc_BFLOAT16(d)); return;
	case BUF_T_FLOAT32: st_f32(p + i * 4, (float)d); return;
	case BUF_T_FLOAT64: st_f64(p + i * 8, d); return;
#define X(id, E, M, B, K) case BUF_T_##id: p[i] = buf_enc_##id(d); return;
	BUF_SMALL_BYTE(X)
#undef X
	case BUF_T_F8E8M0FNU: p[i] = buf_enc_F8E8M0FNU(d); return;
#define X(id, E, M, B, K) case BUF_T_##id: pk_put(p, (zend_ulong)i, E + M + 1, buf_enc_##id(d)); return;
	BUF_SMALL_PACKED(X)
#undef X
	case BUF_T_COMPLEX32: st_u16(p + i * 4, buf_enc_FLOAT16(w->cls == BUF_COMPLEX ? w->re : d)); st_u16(p + i * 4 + 2, buf_enc_FLOAT16(w->im)); return;
	case BUF_T_BCOMPLEX32: st_u16(p + i * 4, buf_enc_BFLOAT16(w->cls == BUF_COMPLEX ? w->re : d)); st_u16(p + i * 4 + 2, buf_enc_BFLOAT16(w->im)); return;
	case BUF_T_COMPLEX64: st_f32(p + i * 8, (float)(w->cls == BUF_COMPLEX ? w->re : d)); st_f32(p + i * 8 + 4, (float)w->im); return;
	case BUF_T_COMPLEX128: st_f64(p + i * 16, w->cls == BUF_COMPLEX ? w->re : d); st_f64(p + i * 16 + 8, w->im); return;
	}
}

void buffers_convert(int src_dtype, const uint8_t *src, int64_t src_stride, int dst_dtype, uint8_t *dst, int64_t dst_stride, int64_t n)
{
	bool sp = buf_dtypes[src_dtype].size == 0, dp = buf_dtypes[dst_dtype].size == 0;

	for (int64_t k = 0; k < n; k++) {
		wide w;

		load(src_dtype, src, sp ? k : k * src_stride, &w);
		store(dst_dtype, dst, dp ? k : k * dst_stride, &w);
	}
}
