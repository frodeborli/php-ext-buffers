/* The operations of compiled code on single values: the semantics shared by constant folding (front.c), the interpreter
 * (interp.c) and the runtime helpers the native backend calls. Each is the rule of Tensor arithmetic (docs/arithmetic.md)
 * for one element, bit for bit what the eager operators compute.
 *
 * Values live in a cc_val. Integers and Bool: 64 bits, sign-extended (signed dtypes) or zero-extended (unsigned dtypes,
 * Bool) from the dtype's width. Float16, BFloat16 and Float32: a float holding a value of the dtype. Float64: a double. */
#ifndef BUF_CC_OPS_H
#define BUF_CC_OPS_H

#include "../../php_buffers.h"
#include "../../codec.h"
#include <math.h>

typedef union { uint64_t i; int64_t s; double d; float f; void *p; } cc_val;

/* PHP's int as a dtype */
#if SIZEOF_ZEND_LONG == 4
# define CC_PHPINT BUF_T_INT32
#else
# define CC_PHPINT BUF_T_INT64
#endif

/* storage of a value of a dtype */
enum { CC_MI, CC_MF, CC_MD, CC_MP };   /* integer (64 bits), float, double, pointer */
#define CC_DT_PTR (-100)                /* the dtype of an address */

static inline int cc_mt(int dt)
{
	switch (dt) {
	case BUF_T_FLOAT64: return CC_MD;
	case BUF_T_FLOAT32: case BUF_T_FLOAT16: case BUF_T_BFLOAT16: return CC_MF;
	case CC_DT_PTR: return CC_MP;
	}
	return CC_MI;
}

/* the dtypes with arithmetic (the storage formats and complex dtypes have none) */
static inline bool cc_arith(int dt)
{
	switch (dt) {
	case BUF_T_BOOL: case BUF_T_INT8: case BUF_T_INT16: case BUF_T_INT32: case BUF_T_INT64:
	case BUF_T_UINT8: case BUF_T_UINT16: case BUF_T_UINT32: case BUF_T_UINT64:
	case BUF_T_FLOAT16: case BUF_T_BFLOAT16: case BUF_T_FLOAT32: case BUF_T_FLOAT64:
		return true;
	}
	return false;
}

static inline bool cc_int(int dt) { return cc_arith(dt) && cc_mt(dt) == CC_MI && dt != BUF_T_BOOL; }
static inline bool cc_float(int dt) { return cc_arith(dt) && (cc_mt(dt) == CC_MF || cc_mt(dt) == CC_MD); }
static inline bool cc_signed(int dt) { return dt >= 0 && dt != BUF_T_BOOL && buf_dtypes[dt].kind == BUFFERS_KIND_INT; }
static inline int cc_bits(int dt) { return dt == BUF_T_BOOL ? 1 : (int)buf_dtypes[dt].bits; }

/* an integer result of any width brought to dt's width */
static inline uint64_t cc_norm(uint64_t x, int dt)
{
	int bits;

	if (dt == BUF_T_BOOL) return x != 0;
	bits = buf_dtypes[dt].bits;
	if (bits == 64) return x;
	if (cc_signed(dt)) return (uint64_t)((int64_t)(x << (64 - bits)) >> (64 - bits));
	return x & (((uint64_t)1 << bits) - 1);
}

/* rounding a float result to Float16 or BFloat16 (Float32 itself: unchanged) */
static inline float cc_round16(float f, int dt)
{
	if (dt == BUF_T_FLOAT16) return (float)buf_dec_FLOAT16(buf_enc_FLOAT16(f));
	if (dt == BUF_T_BFLOAT16) return (float)buf_dec_BFLOAT16(buf_enc_BFLOAT16(f));
	return f;
}

/* a double rounded once to a dtype of float storage */
static inline float cc_round_to(double d, int dt)
{
	switch (dt) {
	case BUF_T_FLOAT16: return (float)buf_dec_FLOAT16(buf_enc_FLOAT16(d));
	case BUF_T_BFLOAT16: return (float)buf_dec_BFLOAT16(buf_enc_BFLOAT16(d));
	}
	return (float)d;
}

/* integer operators: + - * & | ^ always; % with a divisor that is not 0; ** with an exponent that is not negative (signed
 * dtypes); << >> with a count that is not negative (signed dtypes). The checks are the caller's. */
static inline uint64_t cc_ibin(int op, int dt, uint64_t a, uint64_t b)
{
	bool sgn = cc_signed(dt);
	int bits = cc_bits(dt);

	switch (op) {
	case ZEND_ADD: return cc_norm(a + b, dt);
	case ZEND_SUB: return cc_norm(a - b, dt);
	case ZEND_MUL: return cc_norm(a * b, dt);
	case ZEND_BW_AND: return a & b;
	case ZEND_BW_OR: return a | b;
	case ZEND_BW_XOR: return a ^ b;
	case ZEND_DIV:   /* intdiv() with a divisor that is not 0: truncated; the minimum by -1 wraps */
		if (sgn) return (int64_t)b == -1 ? cc_norm(0 - a, dt) : cc_norm((uint64_t)((int64_t)a / (int64_t)b), dt);
		return a / b;
	case ZEND_MOD:
		if (sgn) return (int64_t)b == -1 ? 0 : cc_norm((uint64_t)((int64_t)a % (int64_t)b), dt);
		return a % b;
	case ZEND_POW: {
		uint64_t acc = 1, base = a, x = b;

		for (; x; x >>= 1, base *= base) {
			if (x & 1) acc *= base;
		}
		return cc_norm(acc, dt);
	}
	case ZEND_SL: return b >= (uint64_t)bits ? 0 : cc_norm(a << b, dt);
	case ZEND_SR:
		if (sgn) return (uint64_t)((int64_t)a >> (b > 63 ? 63 : b));
		return b > 63 ? 0 : a >> b;
	}
	return 0;
}

static inline double cc_dbin(int op, double a, double b)
{
	switch (op) {
	case ZEND_ADD: return a + b;
	case ZEND_SUB: return a - b;
	case ZEND_MUL: return a * b;
	case ZEND_DIV: return a / b;
	case ZEND_MOD: return fmod(a, b);
	case ZEND_POW: return pow(a, b);
	}
	return 0;
}

static inline float cc_fbin(int op, int dt, float a, float b)
{
	float r;

	switch (op) {
	case ZEND_ADD: r = a + b; break;
	case ZEND_SUB: r = a - b; break;
	case ZEND_MUL: r = a * b; break;
	case ZEND_DIV: r = a / b; break;
	case ZEND_MOD: r = fmodf(a, b); break;
	case ZEND_POW: r = (float)pow((double)a, (double)b); break;
	default: r = 0;
	}
	return cc_round16(r, dt);
}

enum { CC_EQ, CC_NE, CC_LT, CC_LE, CC_GT, CC_GE };

static inline bool cc_cmp(int which, int dt, cc_val a, cc_val b)
{
	switch (cc_mt(dt)) {
	case CC_MD:
		switch (which) {
		case CC_EQ: return a.d == b.d; case CC_NE: return a.d != b.d; case CC_LT: return a.d < b.d;
		case CC_LE: return a.d <= b.d; case CC_GT: return a.d > b.d; default: return a.d >= b.d;
		}
	case CC_MF:
		switch (which) {
		case CC_EQ: return a.f == b.f; case CC_NE: return a.f != b.f; case CC_LT: return a.f < b.f;
		case CC_LE: return a.f <= b.f; case CC_GT: return a.f > b.f; default: return a.f >= b.f;
		}
	}
	if (cc_signed(dt)) {
		switch (which) {
		case CC_EQ: return a.s == b.s; case CC_NE: return a.s != b.s; case CC_LT: return a.s < b.s;
		case CC_LE: return a.s <= b.s; case CC_GT: return a.s > b.s; default: return a.s >= b.s;
		}
	}
	switch (which) {
	case CC_EQ: return a.i == b.i; case CC_NE: return a.i != b.i; case CC_LT: return a.i < b.i;
	case CC_LE: return a.i <= b.i; case CC_GT: return a.i > b.i; default: return a.i >= b.i;
	}
}

/* PHP's (int) of a float: truncation; NaN, infinities and values outside the 64-bit range take PHP's own rule */
static inline uint64_t cc_f2i(double v)
{
	if (v > -9.2e18 && v < 9.2e18) return (uint64_t)(int64_t)v;
	return (uint64_t)(int64_t)buf_dval_to_lval(v);
}

/* a value of dtype from converted to dtype to, as convert() converts */
static inline cc_val cc_conv(int from, int to, cc_val v)
{
	cc_val r;
	int fm = cc_mt(from), tm = cc_mt(to);

	r.i = 0;
	if (to == BUF_T_BOOL) {
		r.i = fm == CC_MD ? v.d != 0 : fm == CC_MF ? v.f != 0 : v.i != 0;
		return r;
	}
	if (fm == CC_MI) {
		bool sgn = cc_signed(from);

		switch (tm) {
		case CC_MI: r.i = cc_norm(v.i, to); break;
		case CC_MD: r.d = sgn ? (double)v.s : (double)v.i; break;
		default:
			if (to == BUF_T_FLOAT32 && from != BUF_T_INT64 && from != BUF_T_UINT64) r.f = sgn ? (float)(int32_t)v.s : (float)(uint32_t)v.i;
			else r.f = cc_round_to(buf_int_for_rounding(v.i, sgn), to);
		}
		return r;
	}
	if (fm == CC_MF) {
		switch (tm) {
		case CC_MD: r.d = v.f; break;
		case CC_MF: r.f = cc_round16(v.f, to); break;
		default: r.i = cc_norm(cc_f2i((double)v.f), to);
		}
		return r;
	}
	switch (tm) {
	case CC_MD: r.d = v.d; break;
	case CC_MF: r.f = cc_round_to(v.d, to); break;
	default: r.i = cc_norm(cc_f2i(v.d), to);
	}
	return r;
}

enum { CC_FLOOR, CC_CEIL, CC_ROUND, CC_SQRT, CC_FABS,
	/* computed in double as PHP computes them, then rounded once to the dtype */
	CC_SIN, CC_COS, CC_TAN, CC_ASIN, CC_ACOS, CC_ATAN, CC_SINH, CC_COSH, CC_TANH, CC_ASINH, CC_ACOSH, CC_ATANH,
	CC_EXP, CC_EXPM1, CC_LOG, CC_LOG10, CC_LOG2, CC_LOG1P, CC_DEG2RAD, CC_RAD2DEG };
enum { CC_ATAN2, CC_HYPOT, CC_FDIV, CC_LOGB };   /* O_MATH2 */

static inline double cc_dmath1(int fn, double x)
{
	switch (fn) {
	case CC_SIN: return sin(x); case CC_COS: return cos(x); case CC_TAN: return tan(x);
	case CC_ASIN: return asin(x); case CC_ACOS: return acos(x); case CC_ATAN: return atan(x);
	case CC_SINH: return sinh(x); case CC_COSH: return cosh(x); case CC_TANH: return tanh(x);
	case CC_ASINH: return asinh(x); case CC_ACOSH: return acosh(x); case CC_ATANH: return atanh(x);
	case CC_EXP: return exp(x); case CC_EXPM1: return expm1(x); case CC_LOG: return log(x);
	case CC_LOG10: return log10(x); case CC_LOG2: return log2(x); case CC_LOG1P: return log1p(x);
	case CC_DEG2RAD: return (x / 180.0) * M_PI;   /* PHP's deg2rad() */
	case CC_RAD2DEG: return (x / M_PI) * 180;     /* PHP's rad2deg() */
	}
	return 0;
}

/* log($num, $base) as PHP computes it (a base that is not greater than 0 is the caller's error) */
static inline double cc_dmath2(int fn, double a, double b)
{
	switch (fn) {
	case CC_ATAN2: return atan2(a, b);
	case CC_HYPOT: return hypot(a, b);
	case CC_FDIV: return a / b;
	default:
		if (b == 2.0) return log2(a);
		if (b == 10.0) return log10(a);
		if (b == 1.0) return ZEND_NAN;
		return log(a) / log(b);
	}
}

static inline cc_val cc_math2(int fn, int dt, cc_val a, cc_val b)
{
	cc_val r;

	if (cc_mt(dt) == CC_MD) r.d = cc_dmath2(fn, a.d, b.d);
	else r.f = cc_round_to(cc_dmath2(fn, (double)a.f, (double)b.f), dt);
	return r;
}

static inline cc_val cc_math1(int fn, int dt, cc_val a)
{
	cc_val r;

	if (fn >= CC_SIN) {
		if (cc_mt(dt) == CC_MD) r.d = cc_dmath1(fn, a.d);
		else r.f = cc_round_to(cc_dmath1(fn, (double)a.f), dt);
		return r;
	}

	if (cc_mt(dt) == CC_MD) {
		switch (fn) {
		case CC_FLOOR: r.d = floor(a.d); break;
		case CC_CEIL: r.d = ceil(a.d); break;
		case CC_ROUND: r.d = round(a.d); break;
		case CC_SQRT: r.d = sqrt(a.d); break;
		default: r.d = fabs(a.d);
		}
		return r;
	}
	switch (fn) {
	case CC_FLOOR: r.f = floorf(a.f); break;
	case CC_CEIL: r.f = ceilf(a.f); break;
	case CC_ROUND: r.f = roundf(a.f); break;
	case CC_SQRT: r.f = cc_round16(sqrtf(a.f), dt); break;
	default: r.f = fabsf(a.f);
	}
	return r;
}

/* elements in memory (little-endian; Float16 and BFloat16 decoded to float) */
#define CC_LD(T) static inline T cc_ld_##T(const uint8_t *p) { T v; buf_ld_le(&v, p, sizeof v); return v; }
#define CC_ST(T) static inline void cc_st_##T(uint8_t *p, T v) { buf_st_le(p, &v, sizeof v); }
CC_LD(int16_t) CC_LD(uint16_t) CC_LD(int32_t) CC_LD(uint32_t) CC_LD(int64_t) CC_LD(uint64_t) CC_LD(float) CC_LD(double)
CC_ST(uint16_t) CC_ST(uint32_t) CC_ST(uint64_t) CC_ST(float) CC_ST(double)
#undef CC_LD
#undef CC_ST
static inline cc_val cc_load(int dt, const uint8_t *p)
{
	cc_val r;

	switch (dt) {
	case BUF_T_BOOL: r.i = *p != 0; break;
	case BUF_T_INT8: r.s = (int8_t)*p; break;
	case BUF_T_UINT8: r.i = *p; break;
	case BUF_T_INT16: r.s = cc_ld_int16_t(p); break;
	case BUF_T_UINT16: r.i = cc_ld_uint16_t(p); break;
	case BUF_T_INT32: r.s = cc_ld_int32_t(p); break;
	case BUF_T_UINT32: r.i = cc_ld_uint32_t(p); break;
	case BUF_T_INT64: r.s = cc_ld_int64_t(p); break;
	case BUF_T_UINT64: r.i = cc_ld_uint64_t(p); break;
	case BUF_T_FLOAT16: r.f = (float)buf_dec_FLOAT16(cc_ld_uint16_t(p)); break;
	case BUF_T_BFLOAT16: r.f = (float)buf_dec_BFLOAT16(cc_ld_uint16_t(p)); break;
	case BUF_T_FLOAT32: r.f = cc_ld_float(p); break;
	default: r.d = cc_ld_double(p);
	}
	return r;
}

static inline void cc_store(int dt, uint8_t *p, cc_val v)
{
	switch (dt) {
	case BUF_T_BOOL: *p = v.i != 0; break;
	case BUF_T_INT8: case BUF_T_UINT8: *p = (uint8_t)v.i; break;
	case BUF_T_INT16: case BUF_T_UINT16: cc_st_uint16_t(p, (uint16_t)v.i); break;
	case BUF_T_INT32: case BUF_T_UINT32: cc_st_uint32_t(p, (uint32_t)v.i); break;
	case BUF_T_INT64: case BUF_T_UINT64: cc_st_uint64_t(p, v.i); break;
	case BUF_T_FLOAT16: cc_st_uint16_t(p, buf_enc_FLOAT16(v.f)); break;
	case BUF_T_BFLOAT16: cc_st_uint16_t(p, buf_enc_BFLOAT16(v.f)); break;
	case BUF_T_FLOAT32: cc_st_float(p, v.f); break;
	default: cc_st_double(p, v.d);
	}
}

/* values in this machine's own byte order (zvals): Int32, UInt32, Int64, Float64 */
static inline cc_val cc_load_host(int dt, const uint8_t *p)
{
	cc_val r;

	switch (dt) {
	case BUF_T_INT32: { int32_t v; memcpy(&v, p, 4); r.s = v; break; }
	case BUF_T_UINT32: { uint32_t v; memcpy(&v, p, 4); r.i = v; break; }
	case BUF_T_INT64: memcpy(&r.s, p, 8); break;
	default: memcpy(&r.d, p, 8);
	}
	return r;
}

static inline void cc_store_host(int dt, uint8_t *p, cc_val v)
{
	switch (dt) {
	case BUF_T_INT32: case BUF_T_UINT32: { uint32_t w = (uint32_t)v.i; memcpy(p, &w, 4); break; }
	case BUF_T_INT64: memcpy(p, &v.s, 8); break;
	default: memcpy(p, &v.d, 8);
	}
}

#endif
