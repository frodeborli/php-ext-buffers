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
