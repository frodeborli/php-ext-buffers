/* Tensor arithmetic: the result dtype of an operator (type promotion).
 *
 * The rules of docs/arithmetic.md: the Python Array API standard's promotion, with its gaps filled. Operands
 * are dtypes or the three kinds of PHP scalar, which are "weak" (they take the tensor's dtype). tests/promotion.tsv lists
 * the result of every pair and operator; tools/promotion-table.php generates it from a separate implementation of the
 * same rules, and tests/api-consumer/tests/promotion.phpt checks this one against it. */
#include "php_buffers.h"
#include "zend_vm_opcodes.h"

enum { CLS_NONE, CLS_BOOL, CLS_INT, CLS_UINT, CLS_FLOAT };

/* The class of an operand: dtypes that take part in arithmetic, or a PHP scalar. CLS_NONE: storage-only formats
 * (8/6/4-bit floats, packed integers) and complex dtypes, which have no arithmetic. */
static int operand_class(int t, bool *scalar)
{
	*scalar = t <= BUFFERS_SCALAR_BOOL && t >= BUFFERS_SCALAR_FLOAT;
	switch (t) {
	case BUFFERS_SCALAR_BOOL: case BUF_T_BOOL: return CLS_BOOL;
	case BUFFERS_SCALAR_INT: return CLS_INT;
	case BUFFERS_SCALAR_FLOAT: return CLS_FLOAT;
	case BUF_T_INT8: case BUF_T_INT16: case BUF_T_INT32: case BUF_T_INT64: return CLS_INT;
	case BUF_T_UINT8: case BUF_T_UINT16: case BUF_T_UINT32: case BUF_T_UINT64: return CLS_UINT;
	case BUF_T_FLOAT16: case BUF_T_BFLOAT16: case BUF_T_FLOAT32: case BUF_T_FLOAT64: return CLS_FLOAT;
	}
	return CLS_NONE;
}

static int int_of_bits(bool is_signed, int bits)
{
	switch (bits) {
	case 8: return is_signed ? BUF_T_INT8 : BUF_T_UINT8;
	case 16: return is_signed ? BUF_T_INT16 : BUF_T_UINT16;
	case 32: return is_signed ? BUF_T_INT32 : BUF_T_UINT32;
	case 64: return is_signed ? BUF_T_INT64 : BUF_T_UINT64;
	}
	return -1;
}

/* two integer dtypes: the narrowest that holds both; -1 for UInt64 with a signed type */
static int promote_ints(int a, int b)
{
	bool sa = buf_dtypes[a].kind == BUFFERS_KIND_INT, sb = buf_dtypes[b].kind == BUFFERS_KIND_INT;
	int ba = buf_dtypes[a].bits, bb = buf_dtypes[b].bits;

	if (sa == sb) {
		return int_of_bits(sa, ba > bb ? ba : bb);
	}
	{
		int s = sa ? ba : bb, u = sa ? bb : ba;

		if (s > u) return int_of_bits(true, s);
		return u < 64 ? int_of_bits(true, u * 2) : -1;
	}
}

/* two float dtypes: the wider; Float16 with BFloat16 has no common type */
static int promote_floats(int a, int b)
{
	if (a == b) return a;
	if (a == BUF_T_FLOAT64 || b == BUF_T_FLOAT64) return BUF_T_FLOAT64;
	if (a == BUF_T_FLOAT32 || b == BUF_T_FLOAT32) return BUF_T_FLOAT32;
	return -1;   /* Float16 and BFloat16 */
}

static bool is_bitwise(uint8_t op) { return op == ZEND_BW_AND || op == ZEND_BW_OR || op == ZEND_BW_XOR; }
static bool is_shift(uint8_t op) { return op == ZEND_SL || op == ZEND_SR; }
static bool is_arith(uint8_t op)
{
	return op == ZEND_ADD || op == ZEND_SUB || op == ZEND_MUL || op == ZEND_DIV || op == ZEND_MOD || op == ZEND_POW;
}

int buf_dtype_promote(int a, int b, uint8_t op)
{
	bool sca, scb;
	int ca, cb, r;

	if (op == ZEND_BW_NOT) {   /* unary: integers and Bool keep their dtype */
		ca = operand_class(a, &sca);
		return !sca && (ca == CLS_INT || ca == CLS_UINT || ca == CLS_BOOL) ? a : -1;
	}
	if (!is_arith(op) && !is_bitwise(op) && !is_shift(op)) return -1;
	ca = operand_class(a, &sca);
	cb = operand_class(b, &scb);
	if (ca == CLS_NONE || cb == CLS_NONE || (sca && scb)) return -1;

	/* Bool: only & | ^ between Bools; never mixed with numbers (no promotion between them, as in the Array API) */
	if (ca == CLS_BOOL || cb == CLS_BOOL) {
		return ca == CLS_BOOL && cb == CLS_BOOL && is_bitwise(op) ? (sca ? b : a) : -1;
	}
	if (is_bitwise(op) || is_shift(op)) {
		if (ca == CLS_FLOAT || cb == CLS_FLOAT) return -1;
		if (sca) return b;
		if (scb) return a;
		return promote_ints(a, b);
	}

	/* arithmetic */
	if (sca || scb) {
		int t = sca ? b : a, cs = sca ? ca : cb, ct = sca ? cb : ca;

		/* a weak scalar takes the tensor's dtype; a PHP float with an integer tensor gives Float64 */
		r = (cs == CLS_FLOAT && ct != CLS_FLOAT) ? BUF_T_FLOAT64 : t;
	} else if (ca == CLS_FLOAT && cb == CLS_FLOAT) {
		r = promote_floats(a, b);
	} else if (ca == CLS_FLOAT) {
		r = a;
	} else if (cb == CLS_FLOAT) {
		r = b;
	} else {
		r = promote_ints(a, b);
	}
	if (r >= 0 && op == ZEND_DIV && operand_class(r, &sca) != CLS_FLOAT) {
		r = BUF_T_FLOAT64;   /* `/` always gives a float, as PHP's does */
	}
	return r;
}

/* ======================================================================================================================
 * The evaluator: eager element-wise arithmetic on host memory (the reference implementation).
 *
 * Every chunk of up to CHUNK elements of the result is computed in three passes: each operand is gathered into a scratch
 * array of the computation type, one tight loop applies the operator, and the result is written in the result dtype.
 * Computation types: integers in 64 bits (wrapping; the stored result is truncated to its width); Float16, BFloat16 and
 * Float32 results in float (Float32 has more than twice their precision plus two bits, so rounding the exact float result
 * once more to Float16/BFloat16 equals rounding the exact value once); Float64 in double. An integer operand of a float
 * operation is first rounded to the result dtype, once. Built with -ffp-contract=off: one IEEE operation per element.
 * ==================================================================================================================== */

#include "codec.h"
#include "zend_exceptions.h"
#include "zend_smart_str.h"
#include "zend_interfaces.h"
#include <math.h>

#define CHUNK 256

typedef struct {
	bool scalar;
	int dt;                 /* BUF_T_* of a tensor, BUFFERS_SCALAR_* of a scalar */
	zend_long l;            /* int and bool scalars */
	double d;               /* float scalars */
	buf_view *v;            /* tensors */
	int64_t sb[BUF_MAX_NDIM];   /* byte strides aligned to the result's axes, 0 along broadcast axes */
} operand;

static const char *op_symbol(uint8_t op)
{
	switch (op) {
	case ZEND_ADD: return "+"; case ZEND_SUB: return "-"; case ZEND_MUL: return "*"; case ZEND_DIV: return "/";
	case ZEND_MOD: return "%"; case ZEND_POW: return "**"; case ZEND_SL: return "<<"; case ZEND_SR: return ">>";
	case ZEND_BW_AND: return "&"; case ZEND_BW_OR: return "|"; case ZEND_BW_XOR: return "^"; case ZEND_BW_NOT: return "~";
	}
	return "?";
}

static const char *operand_name(int dt)
{
	switch (dt) {
	case BUFFERS_SCALAR_INT: return "int";
	case BUFFERS_SCALAR_FLOAT: return "float";
	case BUFFERS_SCALAR_BOOL: return "bool";
	}
	return buf_dtypes[dt].name;
}

/* 1 the operand is ours (filled in), 0 it is not (leave the operation to PHP and the other operand) */
static int classify(zval *z, operand *o)
{
	memset(o, 0, sizeof *o);
	switch (Z_TYPE_P(z)) {
	case IS_LONG: o->scalar = true; o->dt = BUFFERS_SCALAR_INT; o->l = Z_LVAL_P(z); return 1;
	case IS_DOUBLE: o->scalar = true; o->dt = BUFFERS_SCALAR_FLOAT; o->d = Z_DVAL_P(z); return 1;
	case IS_TRUE: case IS_FALSE: o->scalar = true; o->dt = BUFFERS_SCALAR_BOOL; o->l = Z_TYPE_P(z) == IS_TRUE; return 1;
	case IS_OBJECT:
		if (Z_OBJCE_P(z) == buf_ce_tensor) {
			o->v = buf_view_from(Z_OBJ_P(z));
			o->dt = o->v->dt;
			return 1;
		}
		/* not ours; one with arithmetic of its own (Skein\Expression) gets the operation, see buf_arith_do_operation() */
		return 0;
	}
	return 0;
}

/* why buf_dtype_promote() said no, as a TypeError */
static void promote_error(uint8_t op, int a, int b)
{
	bool s;
	int ca = operand_class(a, &s), cb = op == ZEND_BW_NOT ? ca : operand_class(b, &s);
	const char *sym = op_symbol(op);
	int bad = ca == CLS_NONE && a >= 0 ? a : cb == CLS_NONE && b >= 0 ? b : -1;

	if (bad >= 0) {
		zend_type_error("Unsupported operand dtype for %s: %s has no arithmetic; convert() it to a numeric dtype first", sym, buf_dtypes[bad].name);
	} else if (op == ZEND_BW_NOT) {
		zend_type_error("Unsupported operand dtype for ~: %s (integer and Bool tensors only)", operand_name(a));
	} else if ((ca == CLS_BOOL) != (cb == CLS_BOOL)) {
		zend_type_error("Unsupported operand dtypes for %s: %s and %s (Bool combines only with Bool; convert() to an integer dtype)", sym, operand_name(a), operand_name(b));
	} else if (ca == CLS_BOOL) {
		zend_type_error("Unsupported operand dtypes for %s: %s and %s (Bool supports only & | ^ ~; convert() to an integer dtype for arithmetic)", sym, operand_name(a), operand_name(b));
	} else if ((is_bitwise(op) || is_shift(op)) && (ca == CLS_FLOAT || cb == CLS_FLOAT)) {
		zend_type_error("Unsupported operand dtypes for %s: %s and %s (bitwise operators and shifts need integer operands)", sym, operand_name(a), operand_name(b));
	} else {
		zend_type_error("Unsupported operand dtypes for %s: %s and %s have no common dtype; convert() one of them", sym, operand_name(a), operand_name(b));
	}
}

static bool int_fits(zend_long l, int dt)
{
	switch (dt) {
	case BUF_T_INT8: return l >= INT8_MIN && l <= INT8_MAX;
	case BUF_T_INT16: return l >= INT16_MIN && l <= INT16_MAX;
	case BUF_T_INT32: return l >= INT32_MIN && l <= INT32_MAX;
	case BUF_T_UINT8: return l >= 0 && l <= UINT8_MAX;
	case BUF_T_UINT16: return l >= 0 && l <= UINT16_MAX;
	case BUF_T_UINT32: return l >= 0 && (uint64_t)l <= UINT32_MAX;
	case BUF_T_UINT64: return l >= 0;
	}
	return true;
}

/* ---- rounding an integer or a double to a float dtype, once ---- */

static uint16_t enc_bf16_from_double(double d)
{
	uint64_t u;

	memcpy(&u, &d, 8);
	return (uint16_t)buf_enc(u, 11, 52, 1023, 8, 7, 127, BUF_K_IEEE);
}

/* a 64-bit integer magnitude as a double that rounds like it to any format of at most 51 bits: exact below 2^53, else the
 * dropped bits are folded into a sticky lowest bit (rounding to odd), so the later rounding sees "more than half" */
static double sticky_double(uint64_t m)
{
	int k = 0;

	if (m < (1ull << 53)) return (double)m;
	while ((m >> k) >= (1ull << 53)) k++;
	return ldexp((double)((m >> k) | ((m & ((1ull << k) - 1)) != 0)), k);
}

/* an integer (bits: two's complement when is_signed) rounded to the float result dtype rt, as a float (rt narrower than Float64) */
static float int_to_rt(uint64_t bits, bool is_signed, int rt)
{
	bool neg = is_signed && (int64_t)bits < 0;
	uint64_t mag = neg ? 0 - bits : bits;

	switch (rt) {
	case BUF_T_FLOAT32: return is_signed ? (float)(int64_t)bits : (float)bits;
	case BUF_T_FLOAT16: {
		double d = sticky_double(mag);
		return (float)buf_dec_FLOAT16(buf_enc_FLOAT16(neg ? -d : d));
	}
	default: {   /* BFloat16 */
		double d = sticky_double(mag);
		return (float)buf_dec_BFLOAT16(enc_bf16_from_double(neg ? -d : d));
	}
	}
}

/* a double scalar rounded to the float result dtype rt, as a float */
static float dbl_to_rt(double d, int rt)
{
	switch (rt) {
	case BUF_T_FLOAT32: return (float)d;
	case BUF_T_FLOAT16: return (float)buf_dec_FLOAT16(buf_enc_FLOAT16(d));
	default: return (float)buf_dec_BFLOAT16(enc_bf16_from_double(d));
	}
}

/* ---- gathering n elements of an operand, from p with byte stride s ---- */

#define LOADS(T) do { for (int64_t k = 0; k < n; k++) { T x; buf_ld_le(&x, p + k * s, sizeof x); out[k] = CONV(x); } } while (0)

/* integers: two's complement, sign- or zero-extended by the operand's own kind */
static void gather_int(const operand *o, const uint8_t *p, int64_t s, int64_t n, uint64_t *out)
{
	if (o->scalar) {
		for (int64_t k = 0; k < n; k++) out[k] = (uint64_t)o->l;
		return;
	}
#define CONV(x) (uint64_t)(x)
	switch (o->dt) {
	case BUF_T_BOOL: for (int64_t k = 0; k < n; k++) out[k] = p[k * s] != 0; break;
	case BUF_T_INT8: LOADS(int8_t); break;
	case BUF_T_INT16: LOADS(int16_t); break;
	case BUF_T_INT32: LOADS(int32_t); break;
	case BUF_T_INT64: LOADS(int64_t); break;
	case BUF_T_UINT8: LOADS(uint8_t); break;
	case BUF_T_UINT16: LOADS(uint16_t); break;
	case BUF_T_UINT32: LOADS(uint32_t); break;
	case BUF_T_UINT64: LOADS(uint64_t); break;
	}
#undef CONV
}

/* for a Float16, BFloat16 or Float32 result rt */
static void gather_float(const operand *o, const uint8_t *p, int64_t s, int64_t n, float *out, int rt)
{
	if (o->scalar) {
		float c = o->dt == BUFFERS_SCALAR_FLOAT ? dbl_to_rt(o->d, rt) : int_to_rt((uint64_t)o->l, true, rt);
		for (int64_t k = 0; k < n; k++) out[k] = c;
		return;
	}
	switch (o->dt) {
#define CONV(x) (float)buf_dec_FLOAT16(x)
	case BUF_T_FLOAT16: LOADS(uint16_t); break;
#undef CONV
#define CONV(x) (float)buf_dec_BFLOAT16(x)
	case BUF_T_BFLOAT16: LOADS(uint16_t); break;
#undef CONV
#define CONV(x) (x)
	case BUF_T_FLOAT32: LOADS(float); break;
#undef CONV
#define CONV(x) int_to_rt((uint64_t)(x), true, rt)
	case BUF_T_INT8: LOADS(int8_t); break;
	case BUF_T_INT16: LOADS(int16_t); break;
	case BUF_T_INT32: LOADS(int32_t); break;
	case BUF_T_INT64: LOADS(int64_t); break;
#undef CONV
#define CONV(x) int_to_rt((uint64_t)(x), false, rt)
	case BUF_T_UINT8: LOADS(uint8_t); break;
	case BUF_T_UINT16: LOADS(uint16_t); break;
	case BUF_T_UINT32: LOADS(uint32_t); break;
	case BUF_T_UINT64: LOADS(uint64_t); break;
#undef CONV
	}
}

/* for a Float64 result */
static void gather_double(const operand *o, const uint8_t *p, int64_t s, int64_t n, double *out)
{
	if (o->scalar) {
		double c = o->dt == BUFFERS_SCALAR_FLOAT ? o->d : (double)o->l;
		for (int64_t k = 0; k < n; k++) out[k] = c;
		return;
	}
#define CONV(x) (double)(x)
	switch (o->dt) {
	case BUF_T_INT8: LOADS(int8_t); break;
	case BUF_T_INT16: LOADS(int16_t); break;
	case BUF_T_INT32: LOADS(int32_t); break;
	case BUF_T_INT64: LOADS(int64_t); break;
	case BUF_T_UINT8: LOADS(uint8_t); break;
	case BUF_T_UINT16: LOADS(uint16_t); break;
	case BUF_T_UINT32: LOADS(uint32_t); break;
	case BUF_T_UINT64: LOADS(uint64_t); break;
	case BUF_T_FLOAT32: LOADS(float); break;
	case BUF_T_FLOAT64: LOADS(double); break;
#undef CONV
#define CONV(x) buf_dec_FLOAT16(x)
	case BUF_T_FLOAT16: LOADS(uint16_t); break;
#undef CONV
#define CONV(x) buf_dec_BFLOAT16(x)
	case BUF_T_BFLOAT16: LOADS(uint16_t); break;
#undef CONV
	}
}
#undef LOADS

/* ---- the operators ---- */

/* integers; false after throwing (a data-dependent error) */
static bool kernel_int(uint8_t op, bool sgn, int bits, const uint64_t *a, const uint64_t *b, uint64_t *r, int64_t n, bool is_bool)
{
	switch (op) {
	case ZEND_ADD: for (int64_t k = 0; k < n; k++) r[k] = a[k] + b[k]; break;
	case ZEND_SUB: for (int64_t k = 0; k < n; k++) r[k] = a[k] - b[k]; break;
	case ZEND_MUL: for (int64_t k = 0; k < n; k++) r[k] = a[k] * b[k]; break;
	case ZEND_BW_AND: for (int64_t k = 0; k < n; k++) r[k] = a[k] & b[k]; break;
	case ZEND_BW_OR: for (int64_t k = 0; k < n; k++) r[k] = a[k] | b[k]; break;
	case ZEND_BW_XOR: for (int64_t k = 0; k < n; k++) r[k] = a[k] ^ b[k]; break;
	case ZEND_BW_NOT: for (int64_t k = 0; k < n; k++) r[k] = is_bool ? a[k] ^ 1 : ~a[k]; break;
	case ZEND_MOD:
		for (int64_t k = 0; k < n; k++) {
			if (b[k] == 0) {
				zend_throw_exception(zend_ce_division_by_zero_error, "Modulo by zero", 0);
				return false;
			}
			if (sgn) r[k] = (int64_t)b[k] == -1 ? 0 : (uint64_t)((int64_t)a[k] % (int64_t)b[k]);   /* sign of the dividend */
			else r[k] = a[k] % b[k];
		}
		break;
	case ZEND_POW:
		for (int64_t k = 0; k < n; k++) {
			uint64_t base = a[k], e = b[k], acc = 1;

			if (sgn && (int64_t)e < 0) {
				zend_value_error("Negative exponent in an integer power; convert() to a float dtype first");
				return false;
			}
			for (; e; e >>= 1, base *= base) {
				if (e & 1) acc *= base;
			}
			r[k] = acc;
		}
		break;
	case ZEND_SL: case ZEND_SR:
		for (int64_t k = 0; k < n; k++) {
			uint64_t c = b[k];

			if (sgn && (int64_t)c < 0) {
				zend_throw_exception(zend_ce_arithmetic_error, "Bit shift by negative number", 0);
				return false;
			}
			if (op == ZEND_SL) r[k] = c >= (uint64_t)bits ? 0 : a[k] << c;
			else if (sgn) r[k] = (uint64_t)((int64_t)a[k] >> (c > 63 ? 63 : c));
			else r[k] = c > 63 ? 0 : a[k] >> c;
		}
		break;
	}
	return true;
}

#define FLOAT_KERNEL(NAME, T, FMOD) \
	static void NAME(uint8_t op, const T *a, const T *b, T *r, int64_t n) \
	{ \
		switch (op) { \
		case ZEND_ADD: for (int64_t k = 0; k < n; k++) r[k] = a[k] + b[k]; break; \
		case ZEND_SUB: for (int64_t k = 0; k < n; k++) r[k] = a[k] - b[k]; break; \
		case ZEND_MUL: for (int64_t k = 0; k < n; k++) r[k] = a[k] * b[k]; break; \
		case ZEND_DIV: for (int64_t k = 0; k < n; k++) r[k] = a[k] / b[k]; break; \
		case ZEND_MOD: for (int64_t k = 0; k < n; k++) r[k] = FMOD(a[k], b[k]); break; \
		case ZEND_POW: for (int64_t k = 0; k < n; k++) r[k] = (T)pow((double)a[k], (double)b[k]); break; \
		} \
	}
FLOAT_KERNEL(kernel_float, float, fmodf)
FLOAT_KERNEL(kernel_double, double, fmod)

/* ---- storing n results of the result dtype at q (contiguous) ---- */

static void store_int(int rt, const uint64_t *r, uint8_t *q, int64_t n)
{
	switch (buf_dtypes[rt].size) {
	case 1: for (int64_t k = 0; k < n; k++) q[k] = (uint8_t)r[k]; break;
	case 2: for (int64_t k = 0; k < n; k++) { uint16_t x = (uint16_t)r[k]; buf_st_le(q + k * 2, &x, 2); } break;
	case 4: for (int64_t k = 0; k < n; k++) { uint32_t x = (uint32_t)r[k]; buf_st_le(q + k * 4, &x, 4); } break;
	case 8: if (BUF_SWAP) { for (int64_t k = 0; k < n; k++) buf_st_le(q + k * 8, &r[k], 8); } else memcpy(q, r, (size_t)n * 8); break;
	}
}

static void store_float(int rt, const float *r, uint8_t *q, int64_t n)
{
	switch (rt) {
	case BUF_T_FLOAT32: if (BUF_SWAP) { for (int64_t k = 0; k < n; k++) buf_st_le(q + k * 4, &r[k], 4); } else memcpy(q, r, (size_t)n * 4); break;
	case BUF_T_FLOAT16: for (int64_t k = 0; k < n; k++) { uint16_t x = buf_enc_FLOAT16(r[k]); buf_st_le(q + k * 2, &x, 2); } break;
	default: for (int64_t k = 0; k < n; k++) { uint16_t x = buf_enc_BFLOAT16(r[k]); buf_st_le(q + k * 2, &x, 2); } break;
	}
}

/* ---- direct kernels: operands already in the result dtype ----
 * When both operands have the result dtype and each is contiguous along the last axis or a single broadcast value
 * (stride 0), the operation runs straight over memory: no gathering. Integer kernels work on unsigned types of the
 * result's width (two's complement wrapping gives the same bits for signed results, without C's undefined signed
 * overflow); floats are one IEEE operation per element, so every vector width gives the same bits. Views may start at any
 * byte offset, so memory is read and written through types with an alignment of 1. On x86-64 with glibc each kernel is
 * built for SSE2, AVX2 and AVX-512, and the dynamic loader picks the best for the CPU. */

#if defined(__x86_64__) && defined(__GLIBC__) && defined(__GNUC__) && !defined(__clang__) && __GNUC__ >= 12
/* x86-64-v3: AVX2 and F16C; x86-64-v4: AVX-512 */
# define FASTCLONE __attribute__((target_clones("default", "arch=x86-64-v3", "arch=x86-64-v4")))
#endif
#ifndef FASTCLONE
# define FASTCLONE
#endif

typedef void (*direct_fn)(const uint8_t *a, int64_t sa, const uint8_t *b, int64_t sb, uint8_t *r, int64_t n);

#define DIRECT(NAME, T, EXPR) \
	static FASTCLONE void NAME(const uint8_t *pa, int64_t sa, const uint8_t *pb, int64_t sb, uint8_t *pr, int64_t n) \
	{ \
		typedef T U __attribute__((aligned(1), may_alias)); \
		const U *a = (const U *)pa, *b = (const U *)pb; \
		U *r = (U *)pr; \
		if (sa && sb) { for (int64_t k = 0; k < n; k++) { T x = a[k], y = b[k]; r[k] = (T)(EXPR); } } \
		else if (sa) { T y = b[0]; for (int64_t k = 0; k < n; k++) { T x = a[k]; r[k] = (T)(EXPR); } } \
		else if (sb) { T x = a[0]; for (int64_t k = 0; k < n; k++) { T y = b[k]; r[k] = (T)(EXPR); } } \
		else { T x = a[0], y = b[0], v = (T)(EXPR); for (int64_t k = 0; k < n; k++) r[k] = v; } \
	}

#define DIRECT_FLOAT(T, S) \
	DIRECT(d_add_##S, T, x + y) DIRECT(d_sub_##S, T, x - y) DIRECT(d_mul_##S, T, x * y) DIRECT(d_div_##S, T, x / y)
DIRECT_FLOAT(float, f32)
DIRECT_FLOAT(double, f64)

/* 8 and 16 bits: C promotes to int, so multiply as unsigned int to stay out of signed overflow */
#define DIRECT_INT(T, S, W) \
	DIRECT(d_add_##S, T, x + y) DIRECT(d_sub_##S, T, x - y) DIRECT(d_mul_##S, T, (W)x * (W)y) \
	DIRECT(d_and_##S, T, x & y) DIRECT(d_or_##S, T, x | y) DIRECT(d_xor_##S, T, x ^ y)
DIRECT_INT(uint8_t, u8, unsigned)
DIRECT_INT(uint16_t, u16, unsigned)
DIRECT_INT(uint32_t, u32, uint32_t)
DIRECT_INT(uint64_t, u64, uint64_t)

/* Float16 and BFloat16: each element decoded to Float32 (exact), one Float32 operation, rounded once to the format: the
 * correctly rounded result, as on the general path. */
#ifdef __FLT16_MAX__
# define DIRECT_F16(NAME, OP) \
	static FASTCLONE void NAME(const uint8_t *pa, int64_t sa, const uint8_t *pb, int64_t sb, uint8_t *pr, int64_t n) \
	{ \
		typedef _Float16 U __attribute__((aligned(1), may_alias)); \
		const U *a = (const U *)pa, *b = (const U *)pb; \
		U *r = (U *)pr; \
		if (sa && sb) { for (int64_t k = 0; k < n; k++) r[k] = (_Float16)((float)a[k] OP (float)b[k]); } \
		else if (sa) { float y = (float)b[0]; for (int64_t k = 0; k < n; k++) r[k] = (_Float16)((float)a[k] OP y); } \
		else if (sb) { float x = (float)a[0]; for (int64_t k = 0; k < n; k++) r[k] = (_Float16)(x OP (float)b[k]); } \
		else { _Float16 v = (_Float16)((float)a[0] OP (float)b[0]); for (int64_t k = 0; k < n; k++) r[k] = v; } \
	}
DIRECT_F16(d_add_f16, +) DIRECT_F16(d_sub_f16, -) DIRECT_F16(d_mul_f16, *) DIRECT_F16(d_div_f16, /)
#endif

static inline float bf16_to_f32(uint16_t h) { uint32_t u = (uint32_t)h << 16; float f; memcpy(&f, &u, 4); return f; }
static inline uint16_t f32_to_bf16(float f)
{
	uint32_t u;

	memcpy(&u, &f, 4);
	if ((u & 0x7fffffffu) > 0x7f800000u) return (uint16_t)((u >> 16) | 0x40u);   /* NaN: quiet, sign and payload kept */
	return (uint16_t)((u + 0x7fffu + ((u >> 16) & 1u)) >> 16);                      /* round to nearest even */
}
#define DIRECT_BF16(NAME, OP) \
	static FASTCLONE void NAME(const uint8_t *pa, int64_t sa, const uint8_t *pb, int64_t sb, uint8_t *pr, int64_t n) \
	{ \
		typedef uint16_t U __attribute__((aligned(1), may_alias)); \
		const U *a = (const U *)pa, *b = (const U *)pb; \
		U *r = (U *)pr; \
		if (sa && sb) { for (int64_t k = 0; k < n; k++) r[k] = f32_to_bf16(bf16_to_f32(a[k]) OP bf16_to_f32(b[k])); } \
		else if (sa) { float y = bf16_to_f32(b[0]); for (int64_t k = 0; k < n; k++) r[k] = f32_to_bf16(bf16_to_f32(a[k]) OP y); } \
		else if (sb) { float x = bf16_to_f32(a[0]); for (int64_t k = 0; k < n; k++) r[k] = f32_to_bf16(x OP bf16_to_f32(b[k])); } \
		else { uint16_t v = f32_to_bf16(bf16_to_f32(a[0]) OP bf16_to_f32(b[0])); for (int64_t k = 0; k < n; k++) r[k] = v; } \
	}
DIRECT_BF16(d_add_bf16, +) DIRECT_BF16(d_sub_bf16, -) DIRECT_BF16(d_mul_bf16, *) DIRECT_BF16(d_div_bf16, /)

static direct_fn direct_kernel(uint8_t op, int rt)
{
#define PICK_F(S) switch (op) { case ZEND_ADD: return d_add_##S; case ZEND_SUB: return d_sub_##S; case ZEND_MUL: return d_mul_##S; \
	case ZEND_DIV: return d_div_##S; } return NULL;
#define PICK_I(S) switch (op) { case ZEND_ADD: return d_add_##S; case ZEND_SUB: return d_sub_##S; case ZEND_MUL: return d_mul_##S; \
	case ZEND_BW_AND: return d_and_##S; case ZEND_BW_OR: return d_or_##S; case ZEND_BW_XOR: return d_xor_##S; } return NULL;
	switch (rt) {
	case BUF_T_FLOAT32: PICK_F(f32)
	case BUF_T_FLOAT64: PICK_F(f64)
#ifdef __FLT16_MAX__
	case BUF_T_FLOAT16: PICK_F(f16)
#endif
	case BUF_T_BFLOAT16: PICK_F(bf16)
	case BUF_T_INT8: case BUF_T_UINT8: PICK_I(u8)
	case BUF_T_INT16: case BUF_T_UINT16: PICK_I(u16)
	case BUF_T_INT32: case BUF_T_UINT32: PICK_I(u32)
	case BUF_T_INT64: case BUF_T_UINT64: PICK_I(u64)
	}
#undef PICK_F
#undef PICK_I
	return NULL;
}

/* a scalar operand as one element of the result dtype rt (its value fits: checked before) */
static void scalar_element(const operand *o, int rt, uint8_t *out)
{
	switch (rt) {
	case BUF_T_FLOAT32: { float f = o->dt == BUFFERS_SCALAR_FLOAT ? (float)o->d : int_to_rt((uint64_t)o->l, true, rt); memcpy(out, &f, 4); return; }
	case BUF_T_FLOAT64: { double d = o->dt == BUFFERS_SCALAR_FLOAT ? o->d : (double)o->l; memcpy(out, &d, 8); return; }
	case BUF_T_FLOAT16: { uint16_t h = buf_enc_FLOAT16(o->dt == BUFFERS_SCALAR_FLOAT ? dbl_to_rt(o->d, rt) : int_to_rt((uint64_t)o->l, true, rt)); memcpy(out, &h, 2); return; }
	case BUF_T_BFLOAT16: { uint16_t h = f32_to_bf16(o->dt == BUFFERS_SCALAR_FLOAT ? dbl_to_rt(o->d, rt) : int_to_rt((uint64_t)o->l, true, rt)); memcpy(out, &h, 2); return; }
	default:
		switch (buf_dtypes[rt].size) {
		case 1: { uint8_t v = (uint8_t)o->l; memcpy(out, &v, 1); return; }
		case 2: { uint16_t v = (uint16_t)o->l; memcpy(out, &v, 2); return; }
		case 4: { uint32_t v = (uint32_t)o->l; memcpy(out, &v, 4); return; }
		default: { uint64_t v = (uint64_t)o->l; memcpy(out, &v, 8); return; }
		}
	}
}

/* ---- backends ---- */

#define MAX_BACKENDS 16
static const buffers_backend *backends[MAX_BACKENDS];
static int nbackends;

zend_result buffers_register_backend(const buffers_backend *backend)
{
	if (!backend || !backend->can || !backend->run || nbackends == MAX_BACKENDS) return FAILURE;
	backends[nbackends++] = backend;
	return SUCCESS;
}

static void backend_operand(const operand *o, buffers_operand *out)
{
	memset(out, 0, sizeof *out);
	out->is_scalar = o->scalar;
	out->dtype = o->dt;
	if (o->scalar) {
		out->lval = o->l;
		out->dval = o->d;
		return;
	}
	out->tensor = &o->v->std;
	buffers_tensor_get_info(&o->v->std, &out->info);
	out->device = buf_buffer_from(o->v->buf)->device;
}

static bool same_device(const buf_buffer *a, const buf_buffer *b)
{
	return a->host == b->host && a->dev_type == b->dev_type && a->dev_id == b->dev_id;
}

/* 1 a backend computed it (result set), 0 no backend can, -1 after throwing */
static int try_backends(uint8_t op, const operand *ops, int nops, int rt, int ndim, const int64_t *shape, zval *result)
{
	buffers_operand bo[2];
	zend_object *res;
	buffers_tensor_info info;

	if (!nbackends) return 0;
	for (int i = 0; i < nops; i++) backend_operand(&ops[i], &bo[i]);
	for (int k = nbackends - 1; k >= 0; k--) {
		const buffers_backend *be = backends[k];

		if (!be->can(op, &bo[0], nops == 2 ? &bo[1] : NULL, rt, ndim, shape)) continue;
		res = be->run(op, &bo[0], nops == 2 ? &bo[1] : NULL, rt, ndim, shape);
		if (!res) return -1;
		if (!buffers_is_tensor(res) || buffers_tensor_get_info(res, &info) == FAILURE || info.dtype != rt || info.ndim != ndim
			|| memcmp(info.shape, shape, sizeof(int64_t) * (size_t)ndim) != 0) {
			OBJ_RELEASE(res);
			zend_throw_error(NULL, "The arithmetic backend %s returned a result of the wrong dtype or shape", be->name);
			return -1;
		}
		ZVAL_OBJ(result, res);
		return 1;
	}
	return 0;
}

/* the name of a buffer's device, as its $name property says ("cpu", "cuda:0", an extension's own name) */
static zend_string *device_label(const buf_buffer *b)
{
	if (b->device) {
		zval rv, *n = zend_read_property(b->device->ce, b->device, "name", sizeof("name") - 1, 1, &rv);

		if (!EG(exception) && n && Z_TYPE_P(n) == IS_STRING) {
			zend_string *s = zend_string_copy(Z_STR_P(n));

			zval_ptr_dtor(&rv);
			return s;
		}
		zend_clear_exception();
	}
	return buf_device_name(b->dev_type, b->dev_id);
}

/* the operands are not all in host memory and no backend can compute the operation: an Error naming the devices */
static void no_backend_error(uint8_t op, const operand *ops, int nops)
{
	const buf_buffer *first = NULL;

	for (int i = 0; i < nops; i++) {
		const buf_buffer *b;

		if (ops[i].scalar) continue;
		b = buf_buffer_from(ops[i].v->buf);
		if (first && !same_device(first, b)) {
			zend_string *n1 = device_label(first), *n2 = device_label(b);

			zend_throw_error(NULL, "Cannot compute %s on operands on different devices (%s and %s); move one with to()",
				op_symbol(op), ZSTR_VAL(n1), ZSTR_VAL(n2));
			zend_string_release(n1);
			zend_string_release(n2);
			return;
		}
		if (!first) first = b;
	}
	{
		const buf_buffer *b = NULL;

		for (int i = 0; i < nops && !b; i++) {
			if (!ops[i].scalar && !buf_buffer_from(ops[i].v->buf)->host) b = buf_buffer_from(ops[i].v->buf);
		}
		if (b) {
			zend_string *n = device_label(b);

			zend_throw_error(NULL, "Cannot compute %s on device %s: no arithmetic backend for it is registered; move the operands to the host with to()",
				op_symbol(op), ZSTR_VAL(n));
			zend_string_release(n);
		}
	}
}

/* ---- the operation ---- */

/* the result shape of broadcasting a and b (Array API rules); false after throwing */
static bool broadcast(const operand *ops, int nops, int *ndim, int64_t *shape)
{
	int n = 0;

	for (int i = 0; i < nops; i++) {
		if (!ops[i].scalar && ops[i].v->ndim > n) n = ops[i].v->ndim;
	}
	for (int j = 0; j < n; j++) shape[j] = 1;
	for (int i = 0; i < nops; i++) {
		if (ops[i].scalar) continue;
		for (int j = 0; j < ops[i].v->ndim; j++) {
			int64_t d = ops[i].v->shape[j];
			int at = n - ops[i].v->ndim + j;

			if (d == shape[at] || d == 1) continue;
			if (shape[at] == 1) { shape[at] = d; continue; }
			{
				smart_str s = {0};

				for (int t = 0; t < nops; t++) {
					if (ops[t].scalar) continue;
					smart_str_appends(&s, s.s ? " and [" : "[");
					for (int u = 0; u < ops[t].v->ndim; u++) {
						if (u) smart_str_appends(&s, ", ");
						smart_str_append_long(&s, (zend_long)ops[t].v->shape[u]);
					}
					smart_str_appendc(&s, ']');
				}
				smart_str_0(&s);
				zend_value_error("Operands could not be broadcast together: shapes %s", ZSTR_VAL(s.s));
				smart_str_free(&s);
				return false;
			}
		}
	}
	*ndim = n;
	return true;
}

/* `$a op= $b` may write the result into $a's own memory when no one can observe the difference (docs/arithmetic.md,
 * Compound assignment): $a holds the only reference to its tensor, the tensor the only reference to its buffer (no other
 * view, DataView, DLPack export or extension holds it), the buffer is ext-buffers' own writable host memory (not mapped,
 * not foreign, not pending), and the result has the tensor's dtype and C-contiguous shape. Integer operators that can throw
 * part way through (% ** << >>) are excluded, so that a failed operation leaves $a unchanged. Each element of the result
 * depends only on the same element of the operands and every chunk is read before it is written, so `$a += $a` is safe. */
static bool can_write_in_place(uint8_t op, const operand *a, int rt, int ndim, const int64_t *shape, const int64_t *rsb)
{
	const buf_view *v = a->v;
	const buf_buffer *b;

	if (a->scalar || v->dt != rt || v->ndim != ndim || !v->writable) return false;
	if (buf_dtypes[rt].kind != BUFFERS_KIND_FLOAT && (op == ZEND_MOD || op == ZEND_POW || op == ZEND_SL || op == ZEND_SR)) return false;
	if (GC_REFCOUNT(&v->std) != 1 || GC_REFCOUNT(v->buf) != 1) return false;
	b = buf_buffer_from(v->buf);
	if (!b->alloc || !b->writable || !b->host || b->pending || b->ready || b->map_base || b->owner || b->deleter) return false;
	for (int j = 0; j < ndim; j++) {
		if (v->shape[j] != shape[j] || (shape[j] > 1 && v->sbytes[j] != rsb[j])) return false;
	}
	return true;
}

/* after throwing: binary operators return FAILURE (PHP then skips its own TypeError because an exception is pending,
 * and `$a += ...` leaves $a unchanged); unary ~ must return SUCCESS, or PHP replaces the exception with "Cannot perform
 * bitwise not" */
#define THROWN() do { if (op == ZEND_BW_NOT) { ZVAL_NULL(result); return SUCCESS; } return FAILURE; } while (0)

static zend_result arith_operation(uint8_t op, zval *result, zval *op1, zval *op2, bool compound)
{
	operand ops[2];
	int nops = op == ZEND_BW_NOT ? 1 : 2, rt, ndim;
	int64_t shape[BUF_MAX_NDIM], total = 1, rsize, rsb[BUF_MAX_NDIM];
	zend_object *bufobj, *res;
	uint8_t *rbase;
	int cls;

	if (EG(exception)) {
		return FAILURE;   /* the other operand's handler already threw (PHP asks both) */
	}
	switch (op) {
	case ZEND_ADD: case ZEND_SUB: case ZEND_MUL: case ZEND_DIV: case ZEND_MOD: case ZEND_POW:
	case ZEND_SL: case ZEND_SR: case ZEND_BW_AND: case ZEND_BW_OR: case ZEND_BW_XOR: case ZEND_BW_NOT:
		break;
	default:
		return FAILURE;
	}
	if (!classify(op1, &ops[0])) {
		/* the left operand is not ours: PHP only asks us (the right operand) when it has no operator handler */
		return FAILURE;
	}
	if (nops == 2 && !classify(op2, &ops[1])) {
		/* PHP asks the right operand's handler only when the left operand has none ("op1 else op2"), so a Tensor on the
		 * left hands the operation to an object with arithmetic of its own itself: `$tensor + $expression` reaches
		 * Skein\Expression exactly as `$expression + $tensor` does. The guard stops a handler that hands it back. */
		ZEND_TLS bool delegating;   /* per thread in ZTS builds */

		if (Z_TYPE_P(op2) == IS_OBJECT && Z_OBJ_HANDLER_P(op2, do_operation) && !delegating) {
			zend_result r;

			delegating = true;
			r = Z_OBJ_HANDLER_P(op2, do_operation)(op, result, op1, op2);
			delegating = false;
			return r;
		}
		return FAILURE;
	}
	rt = buf_dtype_promote(ops[0].dt, nops == 2 ? ops[1].dt : -1, op);
	if (rt < 0) {
		promote_error(op, ops[0].dt, nops == 2 ? ops[1].dt : -1);
		THROWN();
	}
	for (int i = 0; i < nops; i++) {
		if (ops[i].scalar && ops[i].dt == BUFFERS_SCALAR_INT && operand_class(rt, &(bool){0}) != CLS_FLOAT && !int_fits(ops[i].l, rt)) {
			zend_value_error("Operand " ZEND_LONG_FMT " does not fit the dtype %s of the other operand", ops[i].l, buf_dtypes[rt].name);
			THROWN();
		}
	}
	if (!broadcast(ops, nops, &ndim, shape)) {
		THROWN();
	}
	switch (try_backends(op, ops, nops, rt, ndim, shape, result)) {
	case 1: return SUCCESS;
	case -1: THROWN();
	}
	for (int i = 0; i < nops; i++) {
		if (!ops[i].scalar && !buf_buffer_from(ops[i].v->buf)->host) {
			no_backend_error(op, ops, nops);
			THROWN();
		}
	}
	for (int i = 0; i < nops; i++) {
		if (!ops[i].scalar && !buf_view_check_access(ops[i].v)) {
			THROWN();
		}
	}
	for (int j = 0; j < ndim; j++) {
		if (buf_mul_ovf(total, shape[j], &total)) {
			zend_value_error("The result is too large");
			return FAILURE;
		}
	}
	rsize = buf_dtypes[rt].size;
	if (total > INT64_MAX / rsize) {
		zend_value_error("The result is too large");
		return FAILURE;
	}
	{
		int64_t acc = rsize;

		for (int j = ndim - 1; j >= 0; j--) {
			rsb[j] = acc;
			acc *= shape[j] ? shape[j] : 1;
		}
	}
	if (compound && can_write_in_place(op, &ops[0], rt, ndim, shape, rsb)) {
		bufobj = NULL;
		rbase = ops[0].v->data;
	} else {
		bufobj = buf_buffer_new_host_uninit((uint64_t)(total * rsize));
		if (!bufobj) THROWN();
		rbase = buf_buffer_from(bufobj)->base;
	}
	/* operand strides aligned to the result's axes; 0 where an operand is broadcast */
	for (int i = 0; i < nops; i++) {
		for (int j = 0; j < ndim; j++) ops[i].sb[j] = 0;
		if (ops[i].scalar) continue;
		for (int j = 0; j < ops[i].v->ndim; j++) {
			int at = ndim - ops[i].v->ndim + j;

			ops[i].sb[at] = ops[i].v->shape[j] == 1 ? 0 : ops[i].v->sbytes[j];
		}
	}
	cls = operand_class(rt, &(bool){0});
	if (total > 0) {
		/* odometer over all axes but the last, chunks along the last */
		int64_t idx[BUF_MAX_NDIM] = {0}, inner = ndim ? shape[ndim - 1] : 1, rows = inner ? total / inner : 0;
		uint8_t *q = rbase;
		union { uint64_t i[CHUNK]; float f[CHUNK]; double d[CHUNK]; } A, B, R;
		bool sgn = buf_dtypes[rt].kind == BUFFERS_KIND_INT;
		/* the direct path: both operands in the result dtype (or a scalar of it), last axis contiguous or broadcast */
		direct_fn direct = nops == 2 && !BUF_SWAP ? direct_kernel(op, rt) : NULL;   /* the direct kernels work on native values */
		uint8_t scal[2][8];

		for (int i = 0; direct && i < 2; i++) {
			if (ops[i].scalar) {
				if (ops[i].dt == BUFFERS_SCALAR_BOOL) direct = NULL;
				else scalar_element(&ops[i], rt, scal[i]);
			} else if (ops[i].dt != rt || (ndim && ops[i].sb[ndim - 1] != 0 && ops[i].sb[ndim - 1] != rsize)) {
				direct = NULL;
			}
		}

		for (int64_t row = 0; row < rows; row++) {
			const uint8_t *p[2] = {NULL, NULL};
			int64_t s[2] = {0, 0};

			for (int i = 0; i < nops; i++) {
				if (ops[i].scalar) continue;
				p[i] = ops[i].v->data;
				for (int j = 0; j + 1 < ndim; j++) p[i] += idx[j] * ops[i].sb[j];
				s[i] = ndim ? ops[i].sb[ndim - 1] : 0;
			}
			if (direct) {
				direct(ops[0].scalar ? scal[0] : p[0], s[0], ops[1].scalar ? scal[1] : p[1], s[1], q, inner);
				q += inner * rsize;
				goto next_row;
			}
			for (int64_t at = 0; at < inner; at += CHUNK) {
				int64_t n = inner - at < CHUNK ? inner - at : CHUNK;

				if (cls == CLS_FLOAT && rt != BUF_T_FLOAT64) {
					gather_float(&ops[0], p[0] + at * s[0], s[0], n, A.f, rt);
					gather_float(&ops[1], p[1] + at * s[1], s[1], n, B.f, rt);
					kernel_float(op, A.f, B.f, R.f, n);
					store_float(rt, R.f, q, n);
				} else if (cls == CLS_FLOAT) {
					gather_double(&ops[0], p[0] + at * s[0], s[0], n, A.d);
					gather_double(&ops[1], p[1] + at * s[1], s[1], n, B.d);
					kernel_double(op, A.d, B.d, R.d, n);
					if (BUF_SWAP) { for (int64_t k = 0; k < n; k++) buf_st_le(q + k * 8, &R.d[k], 8); } else memcpy(q, R.d, (size_t)n * 8);
				} else {
					gather_int(&ops[0], p[0] + at * s[0], s[0], n, A.i);
					if (nops == 2) gather_int(&ops[1], p[1] + at * s[1], s[1], n, B.i);
					if (!kernel_int(op, sgn, buf_dtypes[rt].bits, A.i, B.i, R.i, n, rt == BUF_T_BOOL)) {
						if (bufobj) OBJ_RELEASE(bufobj);   /* never in place: those operators are excluded */
						return FAILURE;
					}
					store_int(rt, R.i, q, n);
				}
				q += n * rsize;
			}
		next_row:
			for (int j = ndim - 2; j >= 0; j--) {
				if (++idx[j] < shape[j]) break;
				idx[j] = 0;
			}
		}
	}
	if (!bufobj) {   /* written into the left operand's own memory: the result is that tensor */
		ZVAL_OBJ_COPY(result, &ops[0].v->std);
		return SUCCESS;
	}
	res = buf_view_new(bufobj, rt, ndim, shape, rsb, rbase, true, false);
	OBJ_RELEASE(bufobj);   /* the view holds it */
	ZVAL_OBJ(result, res);
	return SUCCESS;
}

/* Operands, rule 4 of docs/arithmetic.md: an operand of a class that implements TensorAggregate and has no arithmetic of its own (a PHP class:
 * a sensor, a database column) takes part through its getTensor(). Classes with their own do_operation keep the operation
 * (rule 3, in arith_operation()). 1 replaced (*tmp holds the tensor), 0 not an aggregate, -1 after throwing */
static int resolve_aggregate(zval *z, zval *tmp)
{
	zend_object *o;

	if (Z_TYPE_P(z) != IS_OBJECT) return 0;
	o = Z_OBJ_P(z);
	if (o->ce == buf_ce_tensor || o->handlers->do_operation || !instanceof_function(o->ce, buf_ce_tensor_agg)) return 0;
	zend_call_method_with_0_params(o, o->ce, NULL, "gettensor", tmp);
	if (EG(exception)) {
		zval_ptr_dtor(tmp);
		ZVAL_UNDEF(tmp);
		return -1;
	}
	if (Z_TYPE_P(tmp) != IS_OBJECT || Z_OBJCE_P(tmp) != buf_ce_tensor) {
		zend_type_error("%s::getTensor(): Return value must be of type Buffers\\Tensor, %s returned", ZSTR_VAL(o->ce->name), zend_zval_type_name(tmp));
		zval_ptr_dtor(tmp);
		ZVAL_UNDEF(tmp);
		return -1;
	}
	return 1;
}

zend_result buf_arith_do_operation(uint8_t op, zval *result, zval *op1, zval *op2, bool compound)
{
	zval t1, t2;
	zend_result r;
	int a, b = 0;

	if (EG(exception)) return FAILURE;
	ZVAL_UNDEF(&t1);
	ZVAL_UNDEF(&t2);
	a = resolve_aggregate(op1, &t1);
	if (a >= 0 && op != ZEND_BW_NOT) b = resolve_aggregate(op2, &t2);
	if (a < 0 || b < 0) {
		zval_ptr_dtor(&t1);
		if (op == ZEND_BW_NOT) { ZVAL_NULL(result); return SUCCESS; }
		return FAILURE;
	}
	r = arith_operation(op, result, a ? &t1 : op1, b ? &t2 : op2, compound && !a);
	zval_ptr_dtor(&t1);
	zval_ptr_dtor(&t2);
	return r;
}
