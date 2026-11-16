/* Tensor arithmetic: the result dtype of an operator (type promotion).
 *
 * The rules of docs/proposal-arithmetic.md: the Python Array API standard's promotion, with its gaps filled. Operands
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

#define LOADS(T) do { for (int64_t k = 0; k < n; k++) { T x; memcpy(&x, p + k * s, sizeof x); out[k] = CONV(x); } } while (0)

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
	case 2: for (int64_t k = 0; k < n; k++) { uint16_t x = (uint16_t)r[k]; memcpy(q + k * 2, &x, 2); } break;
	case 4: for (int64_t k = 0; k < n; k++) { uint32_t x = (uint32_t)r[k]; memcpy(q + k * 4, &x, 4); } break;
	case 8: memcpy(q, r, (size_t)n * 8); break;
	}
}

static void store_float(int rt, const float *r, uint8_t *q, int64_t n)
{
	switch (rt) {
	case BUF_T_FLOAT32: memcpy(q, r, (size_t)n * 4); break;
	case BUF_T_FLOAT16: for (int64_t k = 0; k < n; k++) { uint16_t x = buf_enc_FLOAT16(r[k]); memcpy(q + k * 2, &x, 2); } break;
	default: for (int64_t k = 0; k < n; k++) { uint16_t x = buf_enc_BFLOAT16(r[k]); memcpy(q + k * 2, &x, 2); } break;
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

/* after throwing: binary operators return FAILURE (PHP then skips its own TypeError because an exception is pending,
 * and `$a += ...` leaves $a unchanged); unary ~ must return SUCCESS, or PHP replaces the exception with "Cannot perform
 * bitwise not" */
#define THROWN() do { if (op == ZEND_BW_NOT) { ZVAL_NULL(result); return SUCCESS; } return FAILURE; } while (0)

zend_result buf_arith_do_operation(uint8_t op, zval *result, zval *op1, zval *op2)
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
			return FAILURE;
		}
		if (!ops[i].scalar && !buf_view_check_access(ops[i].v)) {
			THROWN();
		}
	}
	if (!broadcast(ops, nops, &ndim, shape)) {
		return FAILURE;
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
	bufobj = buf_buffer_new_host_uninit((uint64_t)(total * rsize));
	if (!bufobj) THROWN();
	rbase = buf_buffer_from(bufobj)->base;
	{
		int64_t acc = rsize;

		for (int j = ndim - 1; j >= 0; j--) {
			rsb[j] = acc;
			acc *= shape[j] ? shape[j] : 1;
		}
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

		for (int64_t row = 0; row < rows; row++) {
			const uint8_t *p[2] = {NULL, NULL};
			int64_t s[2] = {0, 0};

			for (int i = 0; i < nops; i++) {
				if (ops[i].scalar) continue;
				p[i] = ops[i].v->data;
				for (int j = 0; j + 1 < ndim; j++) p[i] += idx[j] * ops[i].sb[j];
				s[i] = ndim ? ops[i].sb[ndim - 1] : 0;
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
					memcpy(q, R.d, (size_t)n * 8);
				} else {
					gather_int(&ops[0], p[0] + at * s[0], s[0], n, A.i);
					if (nops == 2) gather_int(&ops[1], p[1] + at * s[1], s[1], n, B.i);
					if (!kernel_int(op, sgn, buf_dtypes[rt].bits, A.i, B.i, R.i, n, rt == BUF_T_BOOL)) {
						OBJ_RELEASE(bufobj);
						return FAILURE;
					}
					store_int(rt, R.i, q, n);
				}
				q += n * rsize;
			}
			for (int j = ndim - 2; j >= 0; j--) {
				if (++idx[j] < shape[j]) break;
				idx[j] = 0;
			}
		}
	}
	res = buf_view_new(bufobj, rt, ndim, shape, rsb, rbase, true, false);
	OBJ_RELEASE(bufobj);   /* the view holds it */
	ZVAL_OBJ(result, res);
	return SUCCESS;
}
