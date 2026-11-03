/* Shared library exposing codec.h to the Python oracles (ctypes). Build: see run.sh */
#include "../../codec.h"

#define FORMATS(X) \
	X(0, F8E3M4) X(1, F8E4M3) X(2, F8E4M3B11FNUZ) X(3, F8E4M3FN) X(4, F8E4M3FNUZ) \
	X(5, F8E5M2) X(6, F8E5M2FNUZ) X(7, F6E2M3FN) X(8, F6E3M2FN) X(9, F4E2M1FN) \
	X(10, F8E8M0FNU) X(11, FLOAT16) X(12, BFLOAT16)

/* encode n consecutive float32 bit patterns starting at `start` (as doubles, exactly as PHP would) */
int oracle_enc_f32(int fmt, uint32_t start, uint64_t n, uint16_t *out)
{
	if (fmt == 11) { /* float16: from the float32 bits directly - a float32 signalling NaN would be quietened by the conversion to double */
		for (uint64_t k = 0; k < n; k++) out[k] = (uint16_t)dlp_enc((uint32_t)(start + k), 8, 23, 127, 5, 10, 15, DLP_K_HALF);
		return 0;
	}
	switch (fmt) {
#define X(i, id) case i: for (uint64_t k = 0; k < n; k++) { uint32_t u = (uint32_t)(start + k); float f; memcpy(&f, &u, 4); out[k] = dlp_enc_##id((double)f); } return 0;
	FORMATS(X)
#undef X
	}
	return -1;
}

int oracle_enc_f64(int fmt, const double *in, uint64_t n, uint16_t *out)
{
	switch (fmt) {
#define X(i, id) case i: for (uint64_t k = 0; k < n; k++) out[k] = dlp_enc_##id(in[k]); return 0;
	FORMATS(X)
#undef X
	}
	return -1;
}

/* the decode tables as the extension builds them; fmt 0..9 small, 10 e8m0, 11 fp16, 12 bf16 */
double oracle_dec(int fmt, unsigned code)
{
	switch (fmt) {
	case 0: return dlp_dec_generic(code, 3, 4, 3, DLP_K_IEEE);
	case 1: return dlp_dec_generic(code, 4, 3, 7, DLP_K_IEEE);
	case 2: return dlp_dec_generic(code, 4, 3, 11, DLP_K_FNUZ);
	case 3: return dlp_dec_generic(code, 4, 3, 7, DLP_K_FN);
	case 4: return dlp_dec_generic(code, 4, 3, 8, DLP_K_FNUZ);
	case 5: return dlp_dec_generic(code, 5, 2, 15, DLP_K_IEEE);
	case 6: return dlp_dec_generic(code, 5, 2, 16, DLP_K_FNUZ);
	case 7: return dlp_dec_generic(code, 2, 3, 1, DLP_K_SAT);
	case 8: return dlp_dec_generic(code, 3, 2, 3, DLP_K_SAT);
	case 9: return dlp_dec_generic(code, 2, 1, 1, DLP_K_SAT);
	case 10: return dlp_dec_e8m0(code);
	case 11: return dlp_dec_FLOAT16((uint16_t)code);
	case 12: return dlp_dec_BFLOAT16((uint16_t)code);
	}
	return 0;
}
