/* Tensor::each() and Tensor::assign(): element-wise operations written as PHP arrow functions (docs/extras.md).
 *
 * This file is an optional part of ext-buffers (--enable-buffers-extras); nothing in the core refers to it.
 *
 * The closure is never called. Its compiled opcodes are read once per call and turned into an expression graph with
 * the rules of Tensor arithmetic (docs/arithmetic.md): the same promotion, wrapping integers, one IEEE operation per
 * float operation, weak PHP scalars. Anything outside the supported subset throws before any element is computed. The
 * graph is evaluated in chunks along the last axis: every node computes a chunk of values with a tight loop, so
 * temporaries stay chunk-sized and the expression is fused without generating machine code.
 *
 * Control flow (?:, ?:, &&, ||) is evaluated on both sides and selected per element; an error (modulo by zero, an index
 * out of range ...) in an element that is not selected is not raised. Errors travel with the values as per-element codes
 * and are raised for the first element, in row-major order, of the result that carries one. */
#include "../php_buffers.h"
#include "../codec.h"
#include "zend_closures.h"
#include "zend_exceptions.h"
#include "zend_interfaces.h"
#include "zend_vm_opcodes.h"
#include "zend_smart_str.h"
#if PHP_VERSION_ID >= 80400
# include "zend_frameless_function.h"
#endif
#include "extras.h"

#define EV_CHUNK 512
#define MAX_INPUTS 32
#define MAX_CAPTURES 16
#define FR_DEPTH 8
#define FR_ARGS 8
#define MAX_OUT 64

/* PHP's int as a dtype: coordinates, (int) casts and integer constants that stand alone. 32-bit PHP has no Int64 tensors. */
#if SIZEOF_ZEND_LONG == 4
# define DT_PHPINT BUF_T_INT32
#else
# define DT_PHPINT BUF_T_INT64
#endif

#if defined(__x86_64__) && defined(__GLIBC__) && defined(__GNUC__) && !defined(__clang__) && __GNUC__ >= 12
# define EVCLONE __attribute__((target_clones("default", "arch=x86-64-v3", "arch=x86-64-v4")))
#else
# define EVCLONE
#endif

/* ---- nodes of the expression graph ---- */

enum { NK_CONST, NK_INPUT, NK_COORD, NK_BIN, NK_CMP, NK_TOBOOL, NK_NOT, NK_BWNOT, NK_CAST, NK_SELECT, NK_FN1, NK_FN2, NK_GATHER,
	NK_ERRS,      /* Bool 0; the error codes of a (the errors so far on a path), then b's */
	NK_WITHERR,   /* the value of b, with the error codes of a first */
	NK_FAILC,     /* an operation on constants that failed: 0, with its error (k.i) in every element */
	NK_NOERR      /* the value of a without its errors: a condition as a path predicate (its errors are the path's) */
};
enum { FN_ABS, FN_FLOOR, FN_CEIL, FN_ROUND, FN_SQRT, FN_ISNAN, FN_ISFINITE, FN_ISINF, FN_MIN, FN_MAX, FN_INTDIV, FN_FMOD, FN_COUNT };
enum { CMP_EQ, CMP_NE, CMP_LT, CMP_LE };
enum { E_OK, E_MOD0, E_SHIFTNEG, E_POWNEG, E_DIV0, E_RANGE, E_CUSTOM = 16 };   /* E_CUSTOM + i: comp.ferr[i] */
enum { RC_I32, RC_I64, RC_F32, RC_F64 };   /* computation representations, see the evaluator */

static const char *const fn_names[FN_COUNT] = {
	"abs", "floor", "ceil", "round", "sqrt", "is_nan", "is_finite", "is_infinite", "min", "max", "intdiv", "fmod"
};

typedef struct {
	uint8_t kind, op;
	int dt;                 /* a dtype, or BUFFERS_SCALAR_* for a weak constant (folded; never evaluated) */
	int a, b, c;            /* operands; NK_GATHER: a = first index in the pool, b = count */
	zval cv;                /* weak constant: int, float or bool */
	union { uint64_t i; double d; float f; } k;   /* typed constant, in the computation representation */
	int slot;               /* NK_INPUT: input; NK_COORD: axis; NK_GATHER: capture */
	int64_t off;            /* NK_INPUT: byte offset of an array parameter's element */
	bool fails;             /* carries per-element error codes */
	bool owns;              /* can raise an error of its own (not only pass on its operands') */
	bool ealias;            /* shares the error codes of the operand they come from (no array of its own) */
	bool live;
	int last;               /* the last node that reads it */
	int reg, ereg;
} enode;

/* a value of the symbolic execution */
enum { V_UNDEF, V_NODE, V_ARRPARAM, V_LIST, V_TENSOR, V_BAD };
typedef struct { uint8_t t; int n, a, len; } sval;   /* NODE: n; ARRPARAM: input n, len; LIST: pool a, len; TENSOR: capture n, pool a, len */

typedef struct {
	int pred;               /* Bool node: when this path is taken; -1 always */
	int depth;
	int fid[FR_DEPTH], nargs[FR_DEPTH];
	sval cell[1];           /* slots, the path's errors, then FR_DEPTH * FR_ARGS call arguments */
} pstate;

/* cell of argument k of the call open at depth d; PERR: the errors raised so far on the path (V_UNDEF: none) */
#define FRAME_CELL(c, d, k) ((c)->nslots + 1 + (d) * FR_ARGS + (k))
#define PERR(c, s) ((s)->cell[(c)->nslots])

typedef struct {
	bool scalar;            /* a PHP int, float or bool */
	zval z;
	buf_view *v;
	bool arr;               /* bound to an `array` parameter */
	int64_t sb[BUF_MAX_NDIM];   /* byte strides aligned to the iteration axes, 0 where broadcast */
} input;

typedef struct {
	enode *nodes;
	int nn, ncap;
	int *ht;                /* hash of the pure nodes (common subexpressions), open addressing; -1 empty */
	int hcap, hn;
	int *pool;
	int npool, pcap;
	const zend_op_array *oa;
	const char *method;
	uint32_t line;
	int ncells, nslots;
	input in[MAX_INPUTS];
	int nin;
	int nd;                 /* iteration axes */
	int64_t shape[BUF_MAX_NDIM];
	int ncoords;
	buf_view *cap[MAX_CAPTURES];
	zval capz[MAX_CAPTURES];   /* holds the captured tensors */
	int ncapt;
	sval ret;
	bool returned;
	/* the function being evaluated: the closure, or a function or static method it calls (inlined) */
	const zend_function *fn;
	const sval *args;       /* arguments of an inlined call; NULL for the closure itself */
	int nargs, depth;
	const zend_function *ufn[64];   /* user functions called: fid FN_COUNT + i */
	int nufn;
	const zend_function *chain[8];  /* the calls being inlined, outermost first (no recursion) */
	struct { zend_class_entry *ce; zend_string *msg; } ferr[64];   /* errors of operations on constants */
	int nferr;
} comp;

static bool is_weak(const comp *c, int n) { return c->nodes[n].dt < 0; }

static int rclass(int dt)
{
	switch (dt) {
	case BUF_T_FLOAT64: return RC_F64;
	case BUF_T_FLOAT32: case BUF_T_FLOAT16: case BUF_T_BFLOAT16: return RC_F32;
	case BUF_T_INT64: case BUF_T_UINT64: return RC_I64;
	}
	return RC_I32;
}

/* an integer or Bool dtype (integer representation) */
static bool is_ic(int dt) { int rc = rclass(dt); return rc == RC_I32 || rc == RC_I64; }

static bool dt_arith(int dt)
{
	switch (dt) {
	case BUF_T_BOOL: case BUF_T_INT8: case BUF_T_INT16: case BUF_T_INT32: case BUF_T_INT64:
	case BUF_T_UINT8: case BUF_T_UINT16: case BUF_T_UINT32: case BUF_T_UINT64:
	case BUF_T_FLOAT16: case BUF_T_BFLOAT16: case BUF_T_FLOAT32: case BUF_T_FLOAT64:
		return true;
	}
	return false;
}

static bool dt_int(int dt) { int k = buf_dtypes[dt].kind; return k == BUFFERS_KIND_INT || k == BUFFERS_KIND_UINT; }
static bool dt_signed(int dt) { return buf_dtypes[dt].kind == BUFFERS_KIND_INT; }

static const char *tname(int dt)
{
	switch (dt) {
	case BUFFERS_SCALAR_INT: return "int";
	case BUFFERS_SCALAR_FLOAT: return "float";
	case BUFFERS_SCALAR_BOOL: return "bool";
	}
	return buf_dtypes[dt].name;
}

/* ---- errors while compiling ---- */

static int unsupported(comp *c, const char *fmt, ...)
{
	va_list ap;
	char *msg;

	va_start(ap, fmt);
	zend_vspprintf(&msg, 0, fmt, ap);
	va_end(ap);
	if (!EG(exception)) {
		zend_throw_exception_ex(zend_ce_value_error, 0, "Buffers\\Tensor::%s(): %s is not supported in a tensor expression (line %u)", c->method, msg, c->line);
	}
	efree(msg);
	return -1;
}

static int type_err(comp *c, const char *fmt, ...)
{
	va_list ap;
	char *msg;

	va_start(ap, fmt);
	zend_vspprintf(&msg, 0, fmt, ap);
	va_end(ap);
	if (!EG(exception)) zend_type_error("Buffers\\Tensor::%s(): %s (line %u)", c->method, msg, c->line);
	efree(msg);
	return -1;
}

/* ---- building nodes ---- */

static bool cse_kind(int kind)
{
	switch (kind) {
	case NK_BIN: case NK_CMP: case NK_TOBOOL: case NK_NOT: case NK_BWNOT: case NK_CAST: case NK_SELECT: case NK_FN1: case NK_FN2:
		return true;
	}
	return false;
}

static uint32_t cse_hash(int kind, int op, int dt, int a, int b, int cc)
{
	uint32_t h = 2166136261u;
	int v[6] = {kind, op, dt, a, b, cc};

	for (int i = 0; i < 6; i++) h = (h ^ (uint32_t)v[i]) * 16777619u;
	return h;
}

static void cse_insert(comp *c, int id)
{
	const enode *x = &c->nodes[id];
	uint32_t m;

	if (c->hn * 2 >= c->hcap) {   /* grow and rehash */
		int old = c->hcap, *oht = c->ht;

		c->hcap = c->hcap ? c->hcap * 2 : 256;
		c->ht = emalloc(sizeof(int) * (size_t)c->hcap);
		for (int i = 0; i < c->hcap; i++) c->ht[i] = -1;
		c->hn = 0;
		for (int i = 0; i < old; i++) {
			if (oht[i] >= 0) cse_insert(c, oht[i]);
		}
		if (oht) efree(oht);
	}
	m = (uint32_t)c->hcap - 1;
	for (uint32_t i = cse_hash(x->kind, x->op, x->dt, x->a, x->b, x->c) & m;; i = (i + 1) & m) {
		if (c->ht[i] < 0) { c->ht[i] = id; c->hn++; return; }
	}
}

/* a node with these fields: an identical pure node made before, or a new one */
static int mk(comp *c, int kind, int op, int dt, int a, int b, int cc)
{
	enode *n;

	if (cse_kind(kind) && c->hcap) {
		uint32_t m = (uint32_t)c->hcap - 1;

		for (uint32_t i = cse_hash(kind, op, dt, a, b, cc) & m; c->ht[i] >= 0; i = (i + 1) & m) {
			const enode *y = &c->nodes[c->ht[i]];

			if (y->kind == kind && y->op == op && y->dt == dt && y->a == a && y->b == b && y->c == cc) return c->ht[i];
		}
	}

	if (c->nn == c->ncap) {
		c->ncap = c->ncap ? c->ncap * 2 : 64;
		c->nodes = erealloc(c->nodes, sizeof(enode) * (size_t)c->ncap);
	}
	n = &c->nodes[c->nn];
	memset(n, 0, sizeof *n);
	n->kind = (uint8_t)kind;
	n->op = (uint8_t)op;
	n->dt = dt;
	n->a = a;
	n->b = b;
	n->c = cc;
	n->reg = n->ereg = -1;
	ZVAL_UNDEF(&n->cv);
	n->fails = kind != NK_GATHER && ((a >= 0 && c->nodes[a].fails) || (b >= 0 && c->nodes[b].fails) || (cc >= 0 && c->nodes[cc].fails));
	c->nn++;
	if (cse_kind(kind)) cse_insert(c, c->nn - 1);
	return c->nn - 1;
}

static int pool_add(comp *c, int v)
{
	if (c->npool == c->pcap) {
		c->pcap = c->pcap ? c->pcap * 2 : 64;
		c->pool = erealloc(c->pool, sizeof(int) * (size_t)c->pcap);
	}
	c->pool[c->npool] = v;
	return c->npool++;
}

/* a PHP scalar as a weak constant; -1 (after throwing) for other values */
static int mk_weak(comp *c, zval *z)
{
	int n, t;

	ZVAL_DEREF(z);
	switch (Z_TYPE_P(z)) {
	case IS_LONG: t = BUFFERS_SCALAR_INT; break;
	case IS_DOUBLE: t = BUFFERS_SCALAR_FLOAT; break;
	case IS_TRUE: case IS_FALSE: t = BUFFERS_SCALAR_BOOL; break;
	default: return unsupported(c, "a value of type %s", zend_zval_type_name(z));
	}
	n = mk(c, NK_CONST, 0, t, -1, -1, -1);
	ZVAL_COPY_VALUE(&c->nodes[n].cv, z);
	return n;
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

/* rounding to a float dtype, once (the rules of arith.c) */
static float round_to(double d, int dt)
{
	switch (dt) {
	case BUF_T_FLOAT16: return (float)buf_dec_FLOAT16(buf_enc_FLOAT16(d));
	case BUF_T_BFLOAT16: { uint64_t u; memcpy(&u, &d, 8); return (float)buf_dec_BFLOAT16((uint16_t)buf_enc(u, 11, 52, 1023, 8, 7, 127, BUF_K_IEEE)); }
	}
	return (float)d;
}

static float int_to_f(uint64_t bits, bool sgn, int dt) { return round_to(buf_int_for_rounding(bits, sgn), dt); }

static uint64_t norm1(uint64_t x, int dt)
{
	int bits = buf_dtypes[dt].bits;

	if (dt == BUF_T_BOOL) return x != 0;
	if (bits == 64) return x;
	if (dt_signed(dt)) return (uint64_t)((int64_t)(x << (64 - bits)) >> (64 - bits));
	return x & (((uint64_t)1 << bits) - 1);
}

/* a weak constant as a typed constant of dt */
static int concretize(comp *c, int n, int dt)
{
	zval *z = &c->nodes[n].cv;
	int r;

	if (Z_TYPE_P(z) == IS_TRUE || Z_TYPE_P(z) == IS_FALSE) {
		if (dt != BUF_T_BOOL) return type_err(c, "a bool combines only with Bool, not %s", buf_dtypes[dt].name);
	} else if (dt == BUF_T_BOOL) {
		return type_err(c, "Bool combines only with Bool, not %s", zend_zval_type_name(z));
	} else if (Z_TYPE_P(z) == IS_DOUBLE && is_ic(dt)) {
		return type_err(c, "the float %.17G would need a float dtype, not %s", Z_DVAL_P(z), buf_dtypes[dt].name);
	} else if (Z_TYPE_P(z) == IS_LONG && is_ic(dt) && !int_fits(Z_LVAL_P(z), dt)) {
		zend_value_error("Buffers\\Tensor::%s(): operand " ZEND_LONG_FMT " does not fit the dtype %s of the other operand (line %u)",
			c->method, Z_LVAL_P(z), buf_dtypes[dt].name, c->line);
		return -1;
	}
	{
		enode k;

		memset(&k.k, 0, sizeof k.k);
		switch (rclass(dt)) {
		case RC_I32: case RC_I64: k.k.i = Z_TYPE_P(z) == IS_LONG ? norm1((uint64_t)Z_LVAL_P(z), dt) : (Z_TYPE_P(z) == IS_TRUE); break;
		case RC_F64: k.k.d = Z_TYPE_P(z) == IS_LONG ? (double)Z_LVAL_P(z) : Z_DVAL_P(z); break;
		default: k.k.f = Z_TYPE_P(z) == IS_LONG ? int_to_f((uint64_t)Z_LVAL_P(z), true, dt) : round_to(Z_DVAL_P(z), dt); break;
		}
		/* the same constant made before (compared bit for bit: -0.0 is not 0.0) */
		for (int i = 0; i < c->nn; i++) {
			if (c->nodes[i].kind == NK_CONST && c->nodes[i].dt == dt && !memcmp(&c->nodes[i].k, &k.k, sizeof k.k)) return i;
		}
		r = mk(c, NK_CONST, 0, dt, -1, -1, -1);
		c->nodes[r].k = k.k;
	}
	return r;
}

/* a weak constant in its natural dtype: int is Int64, float Float64, bool Bool */
static int natural(comp *c, int n)
{
	if (n < 0 || !is_weak(c, n)) return n;
	switch (c->nodes[n].dt) {
	case BUFFERS_SCALAR_INT: return concretize(c, n, DT_PHPINT);
	case BUFFERS_SCALAR_FLOAT: return concretize(c, n, BUF_T_FLOAT64);
	}
	return concretize(c, n, BUF_T_BOOL);
}

static int cast(comp *c, int n, int dt)
{
	if (n < 0) return -1;
	if (is_weak(c, n)) return concretize(c, n, dt);
	if (c->nodes[n].dt == dt) return n;
	return mk(c, NK_CAST, 0, dt, n, -1, -1);
}

static const char *op_symbol(int op)
{
	switch (op) {
	case ZEND_ADD: return "+"; case ZEND_SUB: return "-"; case ZEND_MUL: return "*"; case ZEND_DIV: return "/";
	case ZEND_MOD: return "%"; case ZEND_POW: return "**"; case ZEND_SL: return "<<"; case ZEND_SR: return ">>";
	case ZEND_BW_AND: return "&"; case ZEND_BW_OR: return "|"; case ZEND_BW_XOR: return "^"; case ZEND_BW_NOT: return "~";
	}
	return "?";
}

/* an operation on constants threw (PHP's own operator or function): an error of every element that evaluates it, of dtype
 * dt; -1 if it cannot be recorded */
static int fail_const(comp *c, int dt)
{
	zend_object *ex = EG(exception);
	zval rv, *msg;
	int n;

	if (!ex || c->nferr == 64) return -1;
	msg = zend_read_property_ex(zend_ce_exception, ex, ZSTR_KNOWN(ZEND_STR_MESSAGE), 1, &rv);
	c->ferr[c->nferr].ce = ex->ce;
	c->ferr[c->nferr].msg = msg && Z_TYPE_P(msg) == IS_STRING ? zend_string_copy(Z_STR_P(msg)) : ZSTR_EMPTY_ALLOC();
	zend_clear_exception();
	n = mk(c, NK_FAILC, 0, dt, -1, -1, -1);
	c->nodes[n].k.i = (uint64_t)(E_CUSTOM + c->nferr++);
	c->nodes[n].fails = c->nodes[n].owns = true;
	return n;
}

/* a PHP operator on two weak constants: what PHP itself computes */
static int fold_binary(comp *c, int opcode, int a, int b)
{
	zval r;
	binary_op_type f = get_binary_op(opcode);
	int n;

	if (!f || f(&r, &c->nodes[a].cv, &c->nodes[b].cv) == FAILURE || EG(exception)) {
		if (EG(exception)) {
			bool fl = c->nodes[a].dt == BUFFERS_SCALAR_FLOAT || c->nodes[b].dt == BUFFERS_SCALAR_FLOAT;

			return fail_const(c, fl ? BUF_T_FLOAT64 : DT_PHPINT);
		}
		unsupported(c, "this operation on constants");
		return -1;
	}
	n = mk_weak(c, &r);
	zval_ptr_dtor(&r);
	return n;
}

static int bin(comp *c, int op, int a, int b)
{
	int rt, n;

	if (a < 0 || b < 0) return -1;
	if (is_weak(c, a) && is_weak(c, b)) return fold_binary(c, op, a, b);
	rt = buf_dtype_promote(c->nodes[a].dt, c->nodes[b].dt, (uint8_t)op);
	if (rt < 0) {
		return type_err(c, "unsupported operand dtypes for %s: %s and %s", op_symbol(op), tname(c->nodes[a].dt), tname(c->nodes[b].dt));
	}
	a = cast(c, a, rt);
	b = cast(c, b, rt);
	if (a < 0 || b < 0) return -1;
	n = mk(c, NK_BIN, op, rt, a, b, -1);
	if (is_ic(rt) && (op == ZEND_MOD || op == ZEND_POW || op == ZEND_SL || op == ZEND_SR)) {
		/* a constant right operand that cannot fail (a divisor other than 0, a count or exponent that is not negative) */
		const enode *k = &c->nodes[b];
		bool safe = k->kind == NK_CONST && (op == ZEND_MOD ? k->k.i != 0 : !dt_signed(rt) || (int64_t)k->k.i >= 0);

		if (!safe) c->nodes[n].fails = c->nodes[n].owns = true;
	}
	return n;
}

static int tobool(comp *c, int n)
{
	if (n < 0) return -1;
	if (is_weak(c, n)) {
		zval z;
		int r;

		ZVAL_BOOL(&z, zend_is_true(&c->nodes[n].cv));
		r = mk_weak(c, &z);
		return r;
	}
	if (c->nodes[n].dt == BUF_T_BOOL) return n;
	return mk(c, NK_TOBOOL, 0, BUF_T_BOOL, n, -1, -1);
}

static int lnot(comp *c, int n)
{
	n = tobool(c, n);
	if (n < 0) return -1;
	if (is_weak(c, n)) {
		zval z;

		ZVAL_BOOL(&z, Z_TYPE(c->nodes[n].cv) != IS_TRUE);
		return mk_weak(c, &z);
	}
	if (c->nodes[n].kind == NK_NOT) return c->nodes[n].a;
	return mk(c, NK_NOT, 0, BUF_T_BOOL, n, -1, -1);
}

/* the common dtype of two values that meet in ?: , min(), max() or comparisons */
static int common(comp *c, int a, int b)
{
	int da, db, rt;

	if (is_weak(c, a) && is_weak(c, b)) {
		a = natural(c, a);
		b = natural(c, b);
		if (a < 0 || b < 0) return -1;
	}
	da = c->nodes[a].dt;
	db = c->nodes[b].dt;
	if (da == BUF_T_BOOL || db == BUF_T_BOOL || da == BUFFERS_SCALAR_BOOL || db == BUFFERS_SCALAR_BOOL) {
		if ((da == BUF_T_BOOL || da == BUFFERS_SCALAR_BOOL) && (db == BUF_T_BOOL || db == BUFFERS_SCALAR_BOOL)) return BUF_T_BOOL;
		return type_err(c, "Bool and %s have no common dtype", tname(da == BUF_T_BOOL || da == BUFFERS_SCALAR_BOOL ? db : da));
	}
	rt = buf_dtype_promote(da, db, ZEND_ADD);
	if (rt < 0) return type_err(c, "%s and %s have no common dtype", tname(da), tname(db));
	return rt;
}

static int cmp(comp *c, int which, int opcode, int a, int b)
{
	int rt;

	if (a < 0 || b < 0) return -1;
	if (is_weak(c, a) && is_weak(c, b)) return fold_binary(c, opcode, a, b);
	rt = common(c, a, b);
	if (rt < 0) return -1;
	a = cast(c, a, rt);
	b = cast(c, b, rt);
	if (a < 0 || b < 0) return -1;
	return mk(c, NK_CMP, which, BUF_T_BOOL, a, b, -1);
}

static int sel(comp *c, int cond, int a, int b)
{
	int rt;

	if (cond < 0 || a < 0 || b < 0) return -1;
	if (a == b) return a;
	cond = tobool(c, cond);
	if (cond < 0) return -1;
	if (is_weak(c, cond)) return Z_TYPE(c->nodes[cond].cv) == IS_TRUE ? a : b;
	if (c->nodes[cond].kind == NK_NOT) {
		int t = a;

		cond = c->nodes[cond].a;
		a = b;
		b = t;
	}
	rt = common(c, a, b);
	if (rt < 0) return -1;
	a = cast(c, a, rt);
	b = cast(c, b, rt);
	if (a < 0 || b < 0) return -1;
	return mk(c, NK_SELECT, 0, rt, cond, a, b);
}

static int land(comp *c, int a, int b)
{
	if (a == -1) return b;           /* -1 is "always" for path predicates */
	if (b == -1) return a;
	return bin(c, ZEND_BW_AND, a, b);
}

static int lor(comp *c, int a, int b)
{
	if (a == -1 || b == -1) return -1;
	return bin(c, ZEND_BW_OR, a, b);
}

/* ---- functions ---- */

static int fn_id(zend_string *lcname)
{
	for (int i = 0; i < FN_COUNT; i++) {
		if (zend_string_equals_cstr(lcname, fn_names[i], strlen(fn_names[i]))) return i;
	}
	return -1;
}

/* a call on weak constants only: what PHP's own function returns */
static int fold_call(comp *c, int fid, const int *args, int nargs)
{
	zval params[FR_ARGS], r;
	zend_function *f = zend_hash_str_find_ptr(EG(function_table), fn_names[fid], strlen(fn_names[fid]));
	int n;

	if (!f) return unsupported(c, "%s()", fn_names[fid]);
	for (int i = 0; i < nargs; i++) ZVAL_COPY_VALUE(&params[i], &c->nodes[args[i]].cv);
	zend_call_known_function(f, NULL, NULL, &r, (uint32_t)nargs, params, NULL);
	if (EG(exception)) return fail_const(c, fid == FN_FMOD || fid == FN_SQRT ? BUF_T_FLOAT64 : DT_PHPINT);
	n = mk_weak(c, &r);
	zval_ptr_dtor(&r);
	return n;
}

static int call(comp *c, int fid, const int *args, int nargs)
{
	bool weak = true;
	int x, rt;

	for (int i = 0; i < nargs; i++) {
		if (args[i] < 0) return -1;
		weak = weak && is_weak(c, args[i]);
	}
	switch (fid) {
	case FN_MIN: case FN_MAX:
		if (nargs < 2) return unsupported(c, "%s() with fewer than two arguments", fn_names[fid]);
		break;
	case FN_INTDIV: case FN_FMOD:
		if (nargs != 2) return unsupported(c, "%s() with %d arguments", fn_names[fid], nargs);
		break;
	default:
		if (nargs != 1) return unsupported(c, "%s() with %d arguments", fn_names[fid], nargs);
	}
	if (weak) return fold_call(c, fid, args, nargs);
	x = args[0];
	switch (fid) {
	case FN_ABS: case FN_FLOOR: case FN_CEIL: case FN_ROUND: case FN_SQRT: case FN_ISNAN: case FN_ISFINITE: case FN_ISINF:
		rt = c->nodes[x].dt;
		if (rt == BUF_T_BOOL) return type_err(c, "%s() of a Bool", fn_names[fid]);
		if (is_ic(rt)) {
			zval z;

			switch (fid) {
			case FN_FLOOR: case FN_CEIL: case FN_ROUND: return x;   /* integers keep their dtype and value */
			case FN_ABS: if (!dt_signed(rt)) return x; break;
			case FN_SQRT: x = cast(c, x, BUF_T_FLOAT64); rt = BUF_T_FLOAT64; break;   /* as `/`: integers give Float64 */
			default: ZVAL_BOOL(&z, fid == FN_ISFINITE); return concretize(c, mk_weak(c, &z), BUF_T_BOOL);
			}
		}
		return mk(c, NK_FN1, fid, fid >= FN_ISNAN ? BUF_T_BOOL : rt, x, -1, -1);
	case FN_MIN: case FN_MAX:
		for (int i = 1; i < nargs; i++) {
			int y = args[i];

			rt = common(c, x, y);
			if (rt < 0) return -1;
			x = cast(c, x, rt);
			y = cast(c, y, rt);
			if (x < 0 || y < 0) return -1;
			x = mk(c, NK_FN2, fid, rt, x, y, -1);
		}
		return x;
	case FN_INTDIV: {
		int y = args[1];

		rt = buf_dtype_promote(c->nodes[x].dt, c->nodes[y].dt, ZEND_BW_AND);   /* integers only, as the bitwise operators */
		if (rt < 0 || rt == BUF_T_BOOL) return type_err(c, "intdiv() takes integers, not %s and %s", tname(c->nodes[x].dt), tname(c->nodes[y].dt));
		x = cast(c, x, rt);
		y = cast(c, y, rt);
		if (x < 0 || y < 0) return -1;
		x = mk(c, NK_FN2, fid, rt, x, y, -1);
		c->nodes[x].fails = c->nodes[x].owns = true;
		return x;
	}
	case FN_FMOD: {
		int y = args[1];

		rt = buf_dtype_promote(c->nodes[x].dt, c->nodes[y].dt, ZEND_DIV);   /* a float dtype; integers give Float64 */
		if (rt < 0) return type_err(c, "fmod() of %s and %s", tname(c->nodes[x].dt), tname(c->nodes[y].dt));
		x = cast(c, x, rt);
		y = cast(c, y, rt);
		if (x < 0 || y < 0) return -1;
		return mk(c, NK_FN2, fid, rt, x, y, -1);
	}
	}
	return -1;
}

/* ---- symbolic execution of the opcodes ---- */

static pstate *st_new(comp *c)
{
	pstate *s = ecalloc(1, sizeof(pstate) + sizeof(sval) * (size_t)c->ncells);

	s->pred = -1;
	return s;
}

static pstate *st_copy(comp *c, const pstate *s)
{
	pstate *d = emalloc(sizeof(pstate) + sizeof(sval) * (size_t)c->ncells);

	memcpy(d, s, sizeof(pstate) + sizeof(sval) * (size_t)c->ncells);
	return d;
}

static bool sval_eq(const sval *a, const sval *b)
{
	return a->t == b->t && a->n == b->n && a->a == b->a && a->len == b->len;
}

static int input_node(comp *c, int inp, int64_t elem);

/* the elements of an array parameter as a list */
static sval arrparam_list(comp *c, const sval *v)
{
	sval r = {V_LIST, 0, c->npool, v->len};

	for (int i = 0; i < v->len; i++) pool_add(c, -1);
	for (int i = 0; i < v->len; i++) c->pool[r.a + i] = input_node(c, v->n, i);
	return r;
}

/* whether two values can meet in a select without an error (checked before building one: a slot that holds values of
 * different dtypes on two paths is often dead after they join, as when opcache reuses a temporary, and must only fail
 * when it is read) */
static bool can_select(comp *c, int a, int b)
{
	int da = c->nodes[a].dt, db = c->nodes[b].dt;
	bool ba = da == BUF_T_BOOL || da == BUFFERS_SCALAR_BOOL, bb = db == BUF_T_BOOL || db == BUFFERS_SCALAR_BOOL;

	if (ba || bb) return ba && bb;
	if (da < 0 && db < 0) return true;
	if (da < 0 || db < 0) {
		int t = da < 0 ? db : da, w = da < 0 ? a : b;

		if (rclass(t) == RC_F32 || rclass(t) == RC_F64) return true;
		return Z_TYPE(c->nodes[w].cv) == IS_LONG && int_fits(Z_LVAL(c->nodes[w].cv), t);
	}
	return buf_dtype_promote(da, db, ZEND_ADD) >= 0;
}

/* one value from two disjoint paths: where pa holds, a; else b. Values that cannot be combined give V_BAD, an error only
 * when read. */
static sval sval_merge(comp *c, int pa, const sval *a, const sval *b)
{
	sval r = {V_BAD, 0, 0, 0};

	sval la, lb;

	if (sval_eq(a, b)) return *a;
	if (a->t == V_UNDEF || b->t == V_UNDEF) { r.t = V_UNDEF; return r; }
	if (a->t == V_BAD || b->t == V_BAD) return r;
	/* an array parameter meeting an array literal: its elements */
	if ((a->t == V_ARRPARAM && b->t == V_LIST) || (a->t == V_LIST && b->t == V_ARRPARAM)) {
		la = a->t == V_ARRPARAM ? arrparam_list(c, a) : *a;
		lb = b->t == V_ARRPARAM ? arrparam_list(c, b) : *b;
		a = &la;
		b = &lb;
	}
	if (a->t == V_NODE && b->t == V_NODE) {
		if (pa == -1) return *a;
		if (!can_select(c, a->n, b->n)) return r;
		r.t = V_NODE;
		r.n = sel(c, pa, a->n, b->n);
		if (r.n < 0) r.t = V_BAD;
		return r;
	}
	if (a->t == V_LIST && b->t == V_LIST && a->len == b->len) {
		int start = c->npool;

		for (int i = 0; i < a->len; i++) pool_add(c, -1);
		for (int i = 0; i < a->len; i++) {
			int x = c->pool[a->a + i], y = c->pool[b->a + i];

			if (pa != -1 && x != y && !can_select(c, x, y)) return r;
			c->pool[start + i] = pa == -1 ? x : sel(c, pa, x, y);
			if (c->pool[start + i] < 0) return r;
		}
		r.t = V_LIST;
		r.a = start;
		r.len = a->len;
		return r;
	}
	return r;
}

/* the states that reach one opline, merged into one */
static pstate *st_merge(comp *c, pstate **in, int n)
{
	pstate *r = in[n - 1];

	for (int i = n - 2; i >= 0; i--) {
		pstate *s = in[i];

		bool same = s->depth == r->depth;
		int live;

		/* only the calls still open matter: entries above the depth are left over from finished calls */
		for (int d = 0; same && d < r->depth; d++) same = s->fid[d] == r->fid[d] && s->nargs[d] == r->nargs[d];
		if (!same) {
			unsupported(c, "control flow inside function arguments of this shape");
			return NULL;
		}
		live = c->nslots + 1 + r->depth * FR_ARGS;
		if (PERR(c, s).t == V_NODE || PERR(c, r).t == V_NODE) {   /* errors on one path only: none on the other */
			sval *pa = &PERR(c, s), *pb = &PERR(c, r);
			zval f;

			ZVAL_FALSE(&f);
			if (pa->t != V_NODE) { pa->t = V_NODE; pa->n = concretize(c, mk_weak(c, &f), BUF_T_BOOL); }
			if (pb->t != V_NODE) { pb->t = V_NODE; pb->n = concretize(c, mk_weak(c, &f), BUF_T_BOOL); }
		}
		for (int k = 0; k < c->ncells; k++) {
			if (k >= live) { r->cell[k].t = V_UNDEF; continue; }
			r->cell[k] = sval_merge(c, s->pred, &s->cell[k], &r->cell[k]);
			if (EG(exception)) return NULL;
		}
		r->pred = lor(c, s->pred, r->pred);
		if (EG(exception)) return NULL;
		efree(s);
	}
	return r;
}

static sval *slot(comp *c, pstate *s, uint32_t var) { (void)c; return &s->cell[EX_VAR_TO_NUM(var)]; }


/* the errors of the path joined with those of node n, which was evaluated after them. Only sources of errors join (an
 * operation that can fail itself, or the value of an inlined call, which carries its own path's errors): any other
 * failing node has only the errors of sources made, and joined, before it. */
static void perr_join(comp *c, pstate *s, int n)
{
	sval *pe = &PERR(c, s);

	if (n < 0 || !c->nodes[n].fails || !(c->nodes[n].owns || c->nodes[n].kind == NK_WITHERR)) return;
	if (pe->t == V_NODE && pe->n == n) return;
	pe->n = mk(c, NK_ERRS, 0, BUF_T_BOOL, pe->t == V_NODE ? pe->n : -1, n, -1);
	pe->t = V_NODE;
}

/* value v with the errors of the path before its own */
static sval with_path_errors(comp *c, pstate *s, sval v)
{
	sval *pe = &PERR(c, s);

	if (pe->t != V_NODE) return v;
	if (v.t == V_NODE) {
		v.n = mk(c, NK_WITHERR, 0, c->nodes[v.n].dt, pe->n, v.n, -1);
	} else if (v.t == V_LIST) {
		int start = c->npool;

		for (int i = 0; i < v.len; i++) pool_add(c, -1);
		for (int i = 0; i < v.len; i++) {
			int e = c->pool[v.a + i];

			c->pool[start + i] = is_weak(c, e) ? e : mk(c, NK_WITHERR, 0, c->nodes[e].dt, pe->n, e, -1);
		}
		v.a = start;
	}
	return v;
}

/* the value of an operand of an opline */
static sval operand_val(comp *c, pstate *s, const zend_op *op, zend_uchar type, znode_op node)
{
	sval r = {V_BAD, -1, 0, 0};

	if (type == IS_CONST) {
		zval *z = RT_CONSTANT(op, node);

		if (Z_TYPE_P(z) == IS_ARRAY) {
			zval *e;
			zend_ulong want = 0, idx;
			zend_string *key;
			int start = c->npool;

			ZEND_HASH_FOREACH_KEY_VAL(Z_ARRVAL_P(z), idx, key, e) {
				if (key || idx != want++) { unsupported(c, "an array with keys"); return r; }
				pool_add(c, -1);
			} ZEND_HASH_FOREACH_END();
			want = 0;
			ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(z), e) {
				int n = mk_weak(c, e);

				if (n < 0) return r;
				c->pool[start + want++] = n;
			} ZEND_HASH_FOREACH_END();
			r.t = V_LIST;
			r.a = start;
			r.len = (int)want;
			return r;
		}
		r.t = V_NODE;
		r.n = mk_weak(c, z);
		if (r.n < 0) r.t = V_BAD;
		return r;
	}
	if (type == IS_CV || type == IS_TMP_VAR || type == IS_VAR) {
		r = *slot(c, s, node.var);
		if (r.t == V_UNDEF) {
			if (type == IS_CV) {
				unsupported(c, "the undefined variable $%s", ZSTR_VAL(c->oa->vars[EX_VAR_TO_NUM(node.var)]));
			} else {
				unsupported(c, "this expression");
			}
			r.t = V_BAD;
		}
		return r;
	}
	unsupported(c, "this operand");
	return r;
}

/* a value that must be one element */
static int scalar_val(comp *c, pstate *s, const zend_op *op, zend_uchar type, znode_op node)
{
	sval v = operand_val(c, s, op, type, node);

	if (EG(exception)) return -1;
	switch (v.t) {
	case V_NODE: return v.n;
	case V_ARRPARAM: return unsupported(c, "an array parameter as a whole here (index it with a constant, or return it)");
	case V_LIST: return unsupported(c, "an array here (arrays may only be returned)");
	case V_TENSOR: return unsupported(c, "a captured tensor not indexed down to one element");
	}
	return type_err(c, "the paths that reach this value give it dtypes that have no common dtype (such as Bool and a number), or an array on one and an element on another");
}

static void set_result(comp *c, pstate *s, const zend_op *op, sval v)
{
	if (op->result_type & (IS_TMP_VAR | IS_VAR | IS_CV)) *slot(c, s, op->result.var) = v;
}

static sval node_val(int n) { sval v = {n < 0 ? V_BAD : V_NODE, n, 0, 0}; return v; }

/* the input node of element `elem` of input `inp` (an array parameter's element, or the scalar input itself) */
static int input_node(comp *c, int inp, int64_t elem)
{
	const input *in = &c->in[inp];
	int64_t off = 0;

	if (in->scalar) return mk_weak(c, (zval *)&in->z);
	if (in->arr) off = elem * in->v->sbytes[in->v->ndim - 1];
	for (int i = 0; i < c->nn; i++) {
		if (c->nodes[i].kind == NK_INPUT && c->nodes[i].slot == inp && c->nodes[i].off == off) return i;
	}
	{
		int n = mk(c, NK_INPUT, 0, in->v->dt, -1, -1, -1);

		c->nodes[n].slot = inp;
		c->nodes[n].off = off;
		return n;
	}
}

static zend_function *find_function(zval *name)
{
	return Z_TYPE_P(name) == IS_STRING ? zend_hash_find_ptr(EG(function_table), Z_STR_P(name)) : NULL;
}

/* a function written in PHP, to be inlined: its fid, or -1 after throwing */
static int user_fid(comp *c, const zend_function *f)
{
	for (int i = 0; i < c->nufn; i++) {
		if (c->ufn[i] == f) return FN_COUNT + i;
	}
	if (c->nufn == 64) return unsupported(c, "calls to more than 64 different functions");
	c->ufn[c->nufn] = f;
	return FN_COUNT + c->nufn++;
}

/* the static method an INIT_STATIC_METHOD_CALL calls (self::, a class name, or static:: when it cannot be overridden) */
static int resolve_static_call(comp *c, const zend_op *op)
{
	zend_class_entry *ce = NULL, *scope = c->fn->common.scope;
	zend_function *f;
	zval *mname;

	if (op->op1_type == IS_CONST) {
		zval *cn = RT_CONSTANT(op, op->op1);

		ce = zend_lookup_class_ex(Z_STR_P(cn), Z_STR_P(cn + 1), ZEND_FETCH_CLASS_NO_AUTOLOAD);
		if (!ce) return unsupported(c, "a call to a method of the class %s, which is not loaded", Z_STRVAL_P(cn));
	} else if (op->op1_type == IS_UNUSED) {
		switch (op->op1.num & ZEND_FETCH_CLASS_MASK) {
		case ZEND_FETCH_CLASS_SELF: ce = scope; break;
		case ZEND_FETCH_CLASS_STATIC: ce = scope; break;   /* checked below: only where it is the same as self:: */
		default: return unsupported(c, "parent:: calls");
		}
		if (!ce) return unsupported(c, "self:: outside a class");
	} else {
		return unsupported(c, "a call to a method of a class named at run time");
	}
	if (op->op2_type != IS_CONST) return unsupported(c, "a method named at run time");
	mname = RT_CONSTANT(op, op->op2);
	f = zend_hash_find_ptr(&ce->function_table, Z_STR_P(mname + 1));
	if (!f) return unsupported(c, "the undefined method %s::%s()", ZSTR_VAL(ce->name), Z_STRVAL_P(mname));
	if (!(f->common.fn_flags & ZEND_ACC_STATIC) || f->type != ZEND_USER_FUNCTION) {
		return unsupported(c, "the method %s::%s() (only static methods written in PHP)", ZSTR_VAL(ce->name), Z_STRVAL_P(mname));
	}
	if ((op->op1.num & ZEND_FETCH_CLASS_MASK) == ZEND_FETCH_CLASS_STATIC && op->op1_type == IS_UNUSED
		&& !(ce->ce_flags & ZEND_ACC_FINAL) && !(f->common.fn_flags & (ZEND_ACC_PRIVATE | ZEND_ACC_FINAL))) {
		return unsupported(c, "static::%s(), which a subclass could override (use self::)", Z_STRVAL_P(mname));
	}
	if ((f->common.fn_flags & ZEND_ACC_PRIVATE) && f->common.scope != scope) {
		return unsupported(c, "the private method %s::%s() from outside its class", ZSTR_VAL(ce->name), Z_STRVAL_P(mname));
	}
	if ((f->common.fn_flags & ZEND_ACC_PROTECTED) && (!scope || !instanceof_function(scope, f->common.scope))) {
		return unsupported(c, "the protected method %s::%s() from outside its class", ZSTR_VAL(ce->name), Z_STRVAL_P(mname));
	}
	return user_fid(c, f);
}

/* the function an INIT_* opline calls: one of ours (an internal function of that name), or -1 after throwing */
static int resolve_call(comp *c, const zend_op *op)
{
	zval *name = RT_CONSTANT(op, op->op2);
	zend_function *f;

	switch (op->opcode) {
	case ZEND_INIT_FCALL: f = find_function(name); break;
	case ZEND_INIT_FCALL_BY_NAME: name++; f = find_function(name); break;
	case ZEND_INIT_NS_FCALL_BY_NAME:
		f = find_function(name + 1);   /* a function of the namespace is what PHP calls */
		if (!f) f = find_function(name + 2);
		break;
	default: return unsupported(c, "this kind of call");
	}
	if (f && f->type == ZEND_USER_FUNCTION) return user_fid(c, f);
	if (!f || f->type != ZEND_INTERNAL_FUNCTION || fn_id(f->common.function_name) < 0) {
		/* the declared name (opcache may have bound the call to the lower-cased one) */
		const char *n = f ? ZSTR_VAL(f->common.function_name) : Z_TYPE_P(name) == IS_STRING ? Z_STRVAL_P(name) : "?";

		return unsupported(c, "the %sfunction %s()", strchr(n, '\\') ? "namespaced " : "", n);
	}
	return fn_id(f->common.function_name);
}

static int fetch_dim(comp *c, pstate *s, const zend_op *op, sval *out)
{
	sval base = operand_val(c, s, op, op->op1_type, op->op1);

	if (EG(exception)) return -1;
	if (base.t == V_ARRPARAM) {
		zval *z;

		if (op->op2_type != IS_CONST || Z_TYPE_P(z = RT_CONSTANT(op, op->op2)) != IS_LONG) {
			return unsupported(c, "indexing an array parameter with anything but an integer literal");
		}
		if (Z_LVAL_P(z) < 0 || Z_LVAL_P(z) >= base.len) {
			zend_value_error("Buffers\\Tensor::%s(): index " ZEND_LONG_FMT " is out of range for an array parameter of length %d (line %u)",
				c->method, Z_LVAL_P(z), base.len, c->line);
			return -1;
		}
		*out = node_val(input_node(c, base.n, Z_LVAL_P(z)));
		return out->n;
	}
	if (base.t == V_LIST) {
		zval *z;

		if (op->op2_type != IS_CONST || Z_TYPE_P(z = RT_CONSTANT(op, op->op2)) != IS_LONG || Z_LVAL_P(z) < 0 || Z_LVAL_P(z) >= base.len) {
			return unsupported(c, "this array index");
		}
		*out = node_val(c->pool[base.a + Z_LVAL_P(z)]);
		return out->n;
	}
	if (base.t == V_TENSOR) {
		buf_view *v = c->cap[base.n];
		int ix = scalar_val(c, s, op, op->op2_type, op->op2), start = c->npool;

		if (ix < 0) return -1;
		if (is_weak(c, ix) ? c->nodes[ix].dt != BUFFERS_SCALAR_INT : !dt_int(c->nodes[ix].dt)) {
			return type_err(c, "a tensor index must be an integer, not %s", tname(c->nodes[ix].dt));
		}
		ix = cast(c, ix, DT_PHPINT);
		if (ix < 0) return -1;
		for (int i = 0; i < base.len; i++) pool_add(c, c->pool[base.a + i]);
		pool_add(c, ix);
		if (base.len + 1 == v->ndim) {
			int n = mk(c, NK_GATHER, 0, v->dt, start, base.len + 1, -1);

			c->nodes[n].slot = base.n;
			c->nodes[n].fails = c->nodes[n].owns = true;
			*out = node_val(n);
			return n;
		}
		out->t = V_TENSOR;
		out->n = base.n;
		out->a = start;
		out->len = base.len + 1;
		return 0;
	}
	if (base.t == V_NODE) return unsupported(c, "indexing a scalar");
	return -1;
}

/* the closure's captured variable of a BIND_STATIC opline */
static int bind_static(comp *c, pstate *s, const zend_op *op, const zend_function *fn)
{
	HashTable *ht = ZEND_MAP_PTR_GET(fn->op_array.static_variables_ptr);
	zval *z;
	const char *name = ZSTR_VAL(c->oa->vars[EX_VAR_TO_NUM(op->op1.var)]);

	if (!ht) ht = fn->op_array.static_variables;
	if (!ht || (op->extended_value & ZEND_BIND_REF)) return unsupported(c, "the by-reference capture of $%s", name);
	z = (zval *)((char *)ht->arData + (op->extended_value & ~(ZEND_BIND_REF | ZEND_BIND_IMPLICIT | ZEND_BIND_EXPLICIT)));
	if (Z_ISREF_P(z)) return unsupported(c, "the by-reference capture of $%s", name);
	if (Z_TYPE_P(z) == IS_OBJECT && (Z_OBJCE_P(z) == buf_ce_tensor || instanceof_function(Z_OBJCE_P(z), buf_ce_tensor_agg))) {
		zval t;
		buf_view *v;

		if (c->ncapt == MAX_CAPTURES) return unsupported(c, "more than %d captured tensors", MAX_CAPTURES);
		if (Z_OBJCE_P(z) == buf_ce_tensor) {
			ZVAL_COPY(&t, z);
		} else {
			zend_call_method_with_0_params(Z_OBJ_P(z), Z_OBJCE_P(z), NULL, "gettensor", &t);
			if (EG(exception)) return -1;
			if (Z_TYPE(t) != IS_OBJECT || Z_OBJCE(t) != buf_ce_tensor) {
				zval_ptr_dtor(&t);
				zend_type_error("%s::getTensor(): Return value must be of type Buffers\\Tensor", ZSTR_VAL(Z_OBJCE_P(z)->name));
				return -1;
			}
		}
		v = buf_view_from(Z_OBJ(t));
		if (!dt_arith(v->dt)) {
			zval_ptr_dtor(&t);
			return type_err(c, "the captured tensor $%s has dtype %s, which has no arithmetic", name, buf_dtypes[v->dt].name);
		}
		if (v->ndim == 0) {
			zval_ptr_dtor(&t);
			return unsupported(c, "the captured 0-d tensor $%s (pass it as an input)", name);
		}
		c->cap[c->ncapt] = v;
		ZVAL_COPY_VALUE(&c->capz[c->ncapt], &t);
		{
			sval r = {V_TENSOR, c->ncapt, c->npool, 0};

			*slot(c, s, op->op1.var) = r;
		}
		c->ncapt++;
		return 0;
	}
	if (Z_TYPE_P(z) != IS_LONG && Z_TYPE_P(z) != IS_DOUBLE && Z_TYPE_P(z) != IS_TRUE && Z_TYPE_P(z) != IS_FALSE) {
		return unsupported(c, "the captured variable $%s of type %s", name, zend_zval_type_name(z));
	}
	*slot(c, s, op->op1.var) = node_val(mk_weak(c, z));
	return 0;
}

static int recv_param(comp *c, pstate *s, const zend_op *op)
{
	uint32_t i = op->op1.num - 1;   /* 0-based parameter */
	const zend_arg_info *ai = &c->oa->arg_info[i];
	uint32_t mask = ZEND_TYPE_PURE_MASK(ai->type);
	bool arr = ZEND_TYPE_IS_SET(ai->type) && mask == MAY_BE_ARRAY;

	if (ZEND_TYPE_IS_SET(ai->type) && (ZEND_TYPE_IS_COMPLEX(ai->type) || (mask & ~(MAY_BE_ARRAY | MAY_BE_LONG | MAY_BE_DOUBLE | MAY_BE_BOOL)))) {
		return unsupported(c, "the type of parameter $%s (use array, int, float, bool or none)", ZSTR_VAL(ai->name));
	}
	if ((int)i < c->nin) {
		const input *in = &c->in[i];

		if (arr) {
			sval v = {V_ARRPARAM, (int)i, 0, 0};

			if (!in->arr) return unsupported(c, "an array parameter for a scalar input");
			v.len = (int)in->v->shape[in->v->ndim - 1];
			*slot(c, s, op->result.var) = v;
			return 0;
		}
		if (ZEND_TYPE_IS_SET(ai->type)) {
			int dt = in->scalar ? (Z_TYPE(in->z) == IS_LONG ? DT_PHPINT : Z_TYPE(in->z) == IS_DOUBLE ? BUF_T_FLOAT64 : BUF_T_BOOL) : in->v->dt;
			bool ok = (mask & MAY_BE_LONG && dt_int(dt)) || (mask & MAY_BE_DOUBLE && !is_ic(dt)) || (mask & MAY_BE_BOOL && dt == BUF_T_BOOL);

			if (!ok) return type_err(c, "parameter $%s does not accept elements of dtype %s", ZSTR_VAL(ai->name), tname(dt));
		}
		*slot(c, s, op->result.var) = node_val(input_node(c, (int)i, 0));
		return 0;
	}
	/* a coordinate */
	{
		int axis = (int)i - c->nin, n;

		if (arr || (ZEND_TYPE_IS_SET(ai->type) && !(mask & MAY_BE_LONG))) {
			return type_err(c, "parameter $%s is a coordinate (an int)", ZSTR_VAL(ai->name));
		}
		if (axis >= c->nd) {
			zend_argument_count_error("Buffers\\Tensor::%s(): the closure takes %u parameters, but there are %d inputs and %d axes to iterate over",
				c->method, c->oa->num_args, c->nin, c->nd);
			return -1;
		}
		n = mk(c, NK_COORD, 0, DT_PHPINT, -1, -1, -1);
		c->nodes[n].slot = axis;
		*slot(c, s, op->result.var) = node_val(n);
		return 0;
	}
}

static bool run_func(comp *c, const zend_function *fn, const sval *args, int nargs);

/* an argument of an inlined call converted as PHP converts it for the parameter's declared type */
static sval coerce_param(comp *c, const zend_arg_info *ai, sval v)
{
	uint32_t mask;
	int dt;

	if (!ZEND_TYPE_IS_SET(ai->type)) return v;
	mask = ZEND_TYPE_PURE_MASK(ai->type);
	if (ZEND_TYPE_IS_COMPLEX(ai->type) || (mask & ~(MAY_BE_ARRAY | MAY_BE_LONG | MAY_BE_DOUBLE | MAY_BE_BOOL))) {
		unsupported(c, "the type of parameter $%s", ZSTR_VAL(ai->name));
		v.t = V_BAD;
		return v;
	}
	if (v.t == V_LIST || v.t == V_ARRPARAM) {
		if (!(mask & MAY_BE_ARRAY)) { type_err(c, "parameter $%s does not take an array", ZSTR_VAL(ai->name)); v.t = V_BAD; }
		return v;
	}
	if (v.t != V_NODE) return v;
	dt = c->nodes[v.n].dt;
	if (dt == BUFFERS_SCALAR_INT || (dt >= 0 && is_ic(dt) && dt != BUF_T_BOOL)) {
		if (mask & MAY_BE_LONG) return v;
		if (mask & MAY_BE_DOUBLE) {   /* an int for a float parameter becomes a float */
			if (dt < 0) {
				zval d;

				ZVAL_DOUBLE(&d, (double)Z_LVAL(c->nodes[v.n].cv));
				v.n = mk_weak(c, &d);
			} else {
				v.n = cast(c, v.n, BUF_T_FLOAT64);
			}
			return v;
		}
	} else if (dt == BUFFERS_SCALAR_FLOAT || (dt >= 0 && rclass(dt) >= RC_F32)) {
		if (mask & MAY_BE_DOUBLE) return v;
	} else if (dt == BUFFERS_SCALAR_BOOL || dt == BUF_T_BOOL) {
		if (mask & MAY_BE_BOOL) return v;
	}
	type_err(c, "parameter $%s does not accept %s", ZSTR_VAL(ai->name), tname(dt));
	v.t = V_BAD;
	return v;
}

static bool push_state(comp *c, pstate ***pend, int *npend, int *cpend, int to, pstate *s)
{
	if (npend[to] == cpend[to]) {
		cpend[to] = cpend[to] ? cpend[to] * 2 : 2;
		pend[to] = erealloc(pend[to], sizeof(pstate *) * (size_t)cpend[to]);
	}
	pend[to][npend[to]++] = s;
	(void)c;
	return true;
}

static int jump_target(comp *c, const zend_op *op, znode_op node, uint32_t at)
{
	uint32_t to = (uint32_t)(OP_JMP_ADDR(op, node) - c->oa->opcodes);

	if (to <= at) return unsupported(c, "a loop");
	return (int)to;
}

/* compiles the closure; false after throwing */
/* the closure (args NULL) or an inlined function: its value, in c->ret; false after throwing */
static bool run_func(comp *c, const zend_function *fn, const sval *args, int nargs)
{
	const zend_op_array *oa = &fn->op_array;
	uint32_t nops = oa->last;
	pstate ***pend = NULL;
	int *npend = NULL, *cpend = NULL;
	sval *rv = NULL;
	int *rp = NULL, nret = 0;
	bool ok = false;
	int nn0;
	/* the caller's context, restored at the end */
	const zend_op_array *s_oa = c->oa;
	const zend_function *s_fn = c->fn;
	const sval *s_args = c->args;
	int s_nslots = c->nslots, s_ncells = c->ncells, s_nargs = c->nargs;
	uint32_t s_line = c->line;

	if (args) {
		for (int i = 0; i < c->depth; i++) {
			if (c->chain[i] == fn) return unsupported(c, "the recursive call of %s()", ZSTR_VAL(fn->common.function_name)) == 0;
		}
		if (c->depth == 8) return unsupported(c, "calls nested more than 8 deep") == 0;
		if ((uint32_t)nargs > oa->num_args) return unsupported(c, "more arguments than %s() takes", ZSTR_VAL(fn->common.function_name)) == 0;
		c->chain[c->depth++] = fn;
	}
	c->oa = oa;
	c->fn = fn;
	c->args = args;
	c->nargs = nargs;
	c->line = oa->line_start;
	c->nslots = (int)(oa->last_var + oa->T);
	c->ncells = c->nslots + 1 + FR_DEPTH * FR_ARGS;   /* slots, the errors of the path (PERR), call arguments */
	if (fn->common.fn_flags & (ZEND_ACC_VARIADIC | ZEND_ACC_GENERATOR | ZEND_ACC_RETURN_REFERENCE)) {
		unsupported(c, fn->common.fn_flags & ZEND_ACC_GENERATOR ? "a generator" : fn->common.fn_flags & ZEND_ACC_VARIADIC ? "a variadic parameter" : "returning by reference");
		goto done;
	}
	for (uint32_t i = 0; i < oa->num_args; i++) {
		if (ZEND_ARG_SEND_MODE(&oa->arg_info[i])) {
			unsupported(c, "the by-reference parameter $%s", ZSTR_VAL(oa->arg_info[i].name));
			goto done;
		}
	}
	if (!args && (int)oa->num_args < c->nin) {
		zend_argument_count_error("Buffers\\Tensor::%s(): the closure must take a parameter for each of the %d inputs, it takes %u", c->method, c->nin, oa->num_args);
		goto done;
	}
	pend = ecalloc(nops + 1, sizeof(pstate **));
	npend = ecalloc(nops + 1, sizeof(int));
	cpend = ecalloc(nops + 1, sizeof(int));
	rv = emalloc(sizeof(sval) * (nops + 1));
	rp = emalloc(sizeof(int) * (nops + 1));
	push_state(c, pend, npend, cpend, 0, st_new(c));

	for (uint32_t at = 0; at < nops; at++) {
		const zend_op *op = &oa->opcodes[at];
		pstate *s;

		if (!npend[at]) continue;   /* not reachable */
		s = st_merge(c, pend[at], npend[at]);
		npend[at] = 0;
		if (!s) goto done;
		c->line = op->lineno;
		nn0 = c->nn;

		switch (op->opcode) {
		case ZEND_NOP: case ZEND_EXT_STMT: case ZEND_EXT_NOP: case ZEND_EXT_FCALL_BEGIN: case ZEND_EXT_FCALL_END:
			break;
		case ZEND_RECV: case ZEND_RECV_INIT:
			if (!args) {
				if (op->opcode == ZEND_RECV_INIT) { unsupported(c, "a default value of a closure parameter"); goto fail; }
				if (recv_param(c, s, op) < 0) goto fail;
			} else {
				uint32_t i = op->op1.num - 1;
				sval v;

				if ((int)i < nargs) {
					v = args[i];
				} else if (op->opcode == ZEND_RECV_INIT) {
					v = operand_val(c, s, op, op->op2_type, op->op2);   /* the default (a constant) */
					if (EG(exception)) goto fail;
				} else {
					unsupported(c, "a call of %s() without its argument $%s", ZSTR_VAL(fn->common.function_name), ZSTR_VAL(oa->arg_info[i].name));
					goto fail;
				}
				v = coerce_param(c, &oa->arg_info[i], v);
				if (EG(exception)) goto fail;
				*slot(c, s, op->result.var) = v;
			}
			break;
		case ZEND_BIND_STATIC:
			if (args) { unsupported(c, "static variables"); goto fail; }
			if (bind_static(c, s, op, fn) < 0) goto fail;
			break;
		case ZEND_QM_ASSIGN: case ZEND_VERIFY_RETURN_TYPE:
			if (op->op1_type != IS_UNUSED) {
				sval v = operand_val(c, s, op, op->op1_type, op->op1);

				if (EG(exception)) goto fail;
				if (op->opcode == ZEND_VERIFY_RETURN_TYPE && (fn->common.fn_flags & ZEND_ACC_HAS_RETURN_TYPE)) {
					v = coerce_param(c, (const zend_arg_info *)oa->arg_info - 1, v);   /* the same rules as a parameter */
					if (EG(exception)) goto fail;
				}
				set_result(c, s, op, v);
			}
			break;
		case ZEND_ADD: case ZEND_SUB: case ZEND_MUL: case ZEND_DIV: case ZEND_MOD: case ZEND_POW:
		case ZEND_SL: case ZEND_SR: case ZEND_BW_AND: case ZEND_BW_OR: case ZEND_BW_XOR: {
			int a = scalar_val(c, s, op, op->op1_type, op->op1), b = a < 0 ? -1 : scalar_val(c, s, op, op->op2_type, op->op2);

			set_result(c, s, op, node_val(bin(c, op->opcode, a, b)));
			if (EG(exception)) goto fail;
			break;
		}
		case ZEND_BW_NOT: {
			int a = scalar_val(c, s, op, op->op1_type, op->op1), rt, n = -1;

			if (a < 0) goto fail;
			if (is_weak(c, a)) {
				zval r;

				if (get_unary_op(ZEND_BW_NOT)(&r, &c->nodes[a].cv) == FAILURE || EG(exception)) { unsupported(c, "~ on this constant"); goto fail; }
				n = mk_weak(c, &r);
			} else if ((rt = buf_dtype_promote(c->nodes[a].dt, -1, ZEND_BW_NOT)) < 0) {
				type_err(c, "unsupported operand dtype for ~: %s", tname(c->nodes[a].dt));
				goto fail;
			} else {
				n = mk(c, NK_BWNOT, 0, rt, a, -1, -1);
			}
			set_result(c, s, op, node_val(n));
			break;
		}
		case ZEND_BOOL_NOT: case ZEND_BOOL: {
			int a = scalar_val(c, s, op, op->op1_type, op->op1);

			set_result(c, s, op, node_val(op->opcode == ZEND_BOOL ? tobool(c, a) : lnot(c, a)));
			if (EG(exception)) goto fail;
			break;
		}
		case ZEND_BOOL_XOR: {
			int a = tobool(c, scalar_val(c, s, op, op->op1_type, op->op1));
			int b = a < 0 ? -1 : tobool(c, scalar_val(c, s, op, op->op2_type, op->op2));

			set_result(c, s, op, node_val(a < 0 || b < 0 ? -1 : bin(c, ZEND_BW_XOR, a, b)));
			if (EG(exception)) goto fail;
			break;
		}
		case ZEND_IS_EQUAL: case ZEND_IS_NOT_EQUAL: case ZEND_IS_IDENTICAL: case ZEND_IS_NOT_IDENTICAL:
		case ZEND_IS_SMALLER: case ZEND_IS_SMALLER_OR_EQUAL: {
			int a = scalar_val(c, s, op, op->op1_type, op->op1), b = a < 0 ? -1 : scalar_val(c, s, op, op->op2_type, op->op2);
			int which = op->opcode == ZEND_IS_SMALLER ? CMP_LT : op->opcode == ZEND_IS_SMALLER_OR_EQUAL ? CMP_LE
				: (op->opcode == ZEND_IS_EQUAL || op->opcode == ZEND_IS_IDENTICAL) ? CMP_EQ : CMP_NE;

			set_result(c, s, op, node_val(cmp(c, which, op->opcode, a, b)));
			if (EG(exception)) goto fail;
			break;
		}
		case ZEND_CAST: {
			int a = scalar_val(c, s, op, op->op1_type, op->op1), n;

			if (a < 0) goto fail;
			switch (op->extended_value) {
			case IS_LONG: case IS_DOUBLE: case _IS_BOOL:
				if (is_weak(c, a)) {
					zval r;

					ZVAL_COPY_VALUE(&r, &c->nodes[a].cv);
					if (op->extended_value == IS_LONG) convert_to_long(&r);
					else if (op->extended_value == IS_DOUBLE) convert_to_double(&r);
					else convert_to_boolean(&r);
					n = mk_weak(c, &r);
				} else if (op->extended_value == _IS_BOOL) {
					n = tobool(c, a);
				} else {
					n = cast(c, a, op->extended_value == IS_LONG ? DT_PHPINT : BUF_T_FLOAT64);
				}
				break;
			default:
				unsupported(c, "this cast");
				goto fail;
			}
			set_result(c, s, op, node_val(n));
			if (EG(exception)) goto fail;
			break;
		}
		case ZEND_ASSIGN: case ZEND_ASSIGN_OP: case ZEND_PRE_INC: case ZEND_PRE_DEC: case ZEND_POST_INC: case ZEND_POST_DEC: {
			/* a local variable takes a new value: the variable names that value from here on (no loops, so no phi) */
			sval old, v;

			if (op->op1_type != IS_CV) { unsupported(c, "assigning to anything but a local variable"); goto fail; }
			if (op->opcode == ZEND_ASSIGN) {
				v = operand_val(c, s, op, op->op2_type, op->op2);
				if (EG(exception)) goto fail;
				if (v.t == V_BAD) { unsupported(c, "this value"); goto fail; }
			} else {
				int cur = scalar_val(c, s, op, op->op1_type, op->op1), rhs, opc;
				zval one;

				if (cur < 0) goto fail;
				old = node_val(cur);
				if (op->opcode == ZEND_ASSIGN_OP) {
					rhs = scalar_val(c, s, op, op->op2_type, op->op2);
					opc = (int)op->extended_value;
				} else {
					ZVAL_LONG(&one, 1);
					rhs = mk_weak(c, &one);
					opc = op->opcode == ZEND_PRE_INC || op->opcode == ZEND_POST_INC ? ZEND_ADD : ZEND_SUB;
				}
				if (rhs < 0) goto fail;
				switch (opc) {
				case ZEND_ADD: case ZEND_SUB: case ZEND_MUL: case ZEND_DIV: case ZEND_MOD: case ZEND_POW:
				case ZEND_SL: case ZEND_SR: case ZEND_BW_AND: case ZEND_BW_OR: case ZEND_BW_XOR:
					break;
				default:
					unsupported(c, "this compound assignment");
					goto fail;
				}
				v = node_val(bin(c, opc, cur, rhs));
				if (EG(exception)) goto fail;
				if (op->opcode == ZEND_POST_INC || op->opcode == ZEND_POST_DEC) {
					*slot(c, s, op->op1.var) = v;
					set_result(c, s, op, old);
					break;
				}
			}
			*slot(c, s, op->op1.var) = v;
			set_result(c, s, op, v);
			break;
		}
		case ZEND_FREE:   /* the unused result of an assignment or an increment */
			break;
		case ZEND_FETCH_DIM_R: case ZEND_FETCH_LIST_R: {   /* LIST_R: [$a, $b] = $list */
			sval v = {V_BAD, 0, 0, 0};

			if (fetch_dim(c, s, op, &v) < 0) goto fail;
			set_result(c, s, op, v);
			break;
		}
		case ZEND_INIT_ARRAY: case ZEND_ADD_ARRAY_ELEMENT: {
			sval arr;

			if (op->op2_type != IS_UNUSED || (op->extended_value & ZEND_ARRAY_ELEMENT_REF)) { unsupported(c, "an array with keys or references"); goto fail; }
			if (op->opcode == ZEND_INIT_ARRAY) {
				arr.t = V_LIST; arr.n = 0; arr.a = c->npool; arr.len = 0;
			} else {
				arr = *slot(c, s, op->result.var);
				if (arr.t != V_LIST) { unsupported(c, "this array"); goto fail; }
				/* copy the list so that other paths keep theirs */
				{
					int start = c->npool;

					for (int i = 0; i < arr.len; i++) pool_add(c, c->pool[arr.a + i]);
					arr.a = start;
				}
			}
			if (op->op1_type != IS_UNUSED) {
				int n = scalar_val(c, s, op, op->op1_type, op->op1);

				if (n < 0) goto fail;
				if (arr.len >= MAX_OUT) { unsupported(c, "an array of more than %d elements", MAX_OUT); goto fail; }
				pool_add(c, n);
				arr.len++;
			}
			set_result(c, s, op, arr);
			break;
		}
		case ZEND_INIT_FCALL: case ZEND_INIT_FCALL_BY_NAME: case ZEND_INIT_NS_FCALL_BY_NAME: case ZEND_INIT_STATIC_METHOD_CALL: {
			int fid = op->opcode == ZEND_INIT_STATIC_METHOD_CALL ? resolve_static_call(c, op) : resolve_call(c, op);

			if (fid < 0) goto fail;
			if (s->depth == FR_DEPTH) { unsupported(c, "calls nested this deep"); goto fail; }
			s->fid[s->depth] = fid;
			s->nargs[s->depth] = 0;
			for (int k = 0; k < FR_ARGS; k++) s->cell[FRAME_CELL(c, s->depth, k)].t = V_UNDEF;
			s->depth++;
			break;
		}
		case ZEND_SEND_VAL: case ZEND_SEND_VAR: case ZEND_SEND_VAL_EX: case ZEND_SEND_VAR_EX: case ZEND_SEND_VAR_NO_REF_EX: {
			int n, pos;

			if (!s->depth || op->op2_type == IS_CONST) { unsupported(c, "this argument (named arguments are not supported)"); goto fail; }
			pos = (int)op->op2.num - 1;
			if (pos < 0 || pos >= FR_ARGS) { unsupported(c, "more than %d arguments", FR_ARGS); goto fail; }
			if (s->fid[s->depth - 1] >= FN_COUNT) {   /* to a function written in PHP: arrays too */
				sval v = operand_val(c, s, op, op->op1_type, op->op1);

				if (EG(exception)) goto fail;
				s->cell[FRAME_CELL(c, s->depth - 1, pos)] = v;
			} else {
				n = scalar_val(c, s, op, op->op1_type, op->op1);
				if (n < 0) goto fail;
				s->cell[FRAME_CELL(c, s->depth - 1, pos)] = node_val(n);
			}
			if (pos + 1 > s->nargs[s->depth - 1]) s->nargs[s->depth - 1] = pos + 1;
			break;
		}
		case ZEND_DO_ICALL: case ZEND_DO_FCALL: case ZEND_DO_FCALL_BY_NAME: case ZEND_DO_UCALL: {
			int args[FR_ARGS], d, n;

			if (!s->depth) { unsupported(c, "this call"); goto fail; }
			d = --s->depth;
			if (s->fid[d] >= FN_COUNT) {   /* a function written in PHP: inlined */
				sval av[FR_ARGS];
				const zend_function *f = c->ufn[s->fid[d] - FN_COUNT];

				for (int k = 0; k < s->nargs[d]; k++) {
					av[k] = s->cell[FRAME_CELL(c, d, k)];
					if (av[k].t == V_UNDEF || av[k].t == V_BAD) { unsupported(c, "a missing argument"); goto fail; }
				}
				if (!run_func(c, f, av, s->nargs[d])) goto fail;
				set_result(c, s, op, c->ret);
				break;
			}
			for (int k = 0; k < s->nargs[d]; k++) {
				sval *a = &s->cell[FRAME_CELL(c, d, k)];

				if (a->t != V_NODE) { unsupported(c, "a missing argument"); goto fail; }
				args[k] = a->n;
			}
			n = call(c, s->fid[d], args, s->nargs[d]);
			if (n < 0) goto fail;
			set_result(c, s, op, node_val(n));
			break;
		}
#ifdef ZEND_FRAMELESS_ICALL_0
		case ZEND_FRAMELESS_ICALL_1: case ZEND_FRAMELESS_ICALL_2: case ZEND_FRAMELESS_ICALL_3: {
			zend_function *f = ZEND_FLF_FUNC(op);
			int fid = f && f->type == ZEND_INTERNAL_FUNCTION ? fn_id(f->common.function_name) : -1, args[3], na = op->opcode - ZEND_FRAMELESS_ICALL_0, n;

			if (fid < 0) { unsupported(c, "the function %s()", f ? ZSTR_VAL(f->common.function_name) : "?"); goto fail; }
			if (na >= 1 && (args[0] = scalar_val(c, s, op, op->op1_type, op->op1)) < 0) goto fail;
			if (na >= 2 && (args[1] = scalar_val(c, s, op, op->op2_type, op->op2)) < 0) goto fail;
			if (na >= 3) {
				const zend_op *data = op + 1;

				if ((args[2] = scalar_val(c, s, data, data->op1_type, data->op1)) < 0) goto fail;
			}
			n = call(c, fid, args, na);
			if (n < 0) goto fail;
			set_result(c, s, op, node_val(n));
			if (na == 3) at++;   /* the OP_DATA opline */
			break;
		}
		case ZEND_JMP_FRAMELESS: {
			/* in a namespace: a function of that name in the namespace takes the dynamic call, which rejects it */
			int to = jump_target(c, op, op->op2, at);

			if (to < 0) goto fail;
			if (!find_function(RT_CONSTANT(op, op->op1))) {
				push_state(c, pend, npend, cpend, to, s);
				continue;
			}
			break;
		}
#endif
		case ZEND_JMP: {
			int to = jump_target(c, op, op->op1, at);

			if (to < 0) goto fail;
			push_state(c, pend, npend, cpend, to, s);
			continue;
		}
		case ZEND_JMPZ: case ZEND_JMPNZ: case ZEND_JMPZ_EX: case ZEND_JMPNZ_EX: case ZEND_JMP_SET: {
			int cond = tobool(c, scalar_val(c, s, op, op->op1_type, op->op1)), to, jcond;
			pstate *j;

			if (cond < 0) goto fail;
			to = jump_target(c, op, op->op2, at);
			if (to < 0) goto fail;
			if (op->opcode == ZEND_JMPZ_EX || op->opcode == ZEND_JMPNZ_EX) set_result(c, s, op, node_val(cond));
			if (op->opcode == ZEND_JMP_SET) set_result(c, s, op, operand_val(c, s, op, op->op1_type, op->op1));
			/* jump when cond is false (JMPZ*) or true (JMPNZ*, JMP_SET) */
			/* the path predicates take the condition's value; its errors are already the path's (PERR) */
			if (c->nodes[cond].fails) {
				cond = mk(c, NK_NOERR, 0, BUF_T_BOOL, cond, -1, -1);
				c->nodes[cond].fails = false;
			}
			jcond = (op->opcode == ZEND_JMPZ || op->opcode == ZEND_JMPZ_EX) ? lnot(c, cond) : cond;
			if (jcond < 0) goto fail;
			if (is_weak(c, jcond)) {
				if (Z_TYPE(c->nodes[jcond].cv) == IS_TRUE) {
					push_state(c, pend, npend, cpend, to, s);
					continue;
				}
				break;
			}
			j = st_copy(c, s);
			j->pred = land(c, s->pred, jcond);
			s->pred = land(c, s->pred, lnot(c, jcond));
			if (EG(exception)) { efree(j); goto fail; }
			push_state(c, pend, npend, cpend, to, j);
			break;
		}
		case ZEND_RETURN: {
			sval v = operand_val(c, s, op, op->op1_type, op->op1);

			if (EG(exception)) goto fail;
			if (v.t == V_ARRPARAM) v = arrparam_list(c, &v);   /* the whole array parameter: its elements */
			v = with_path_errors(c, s, v);   /* the errors met on the way to this return come first */
			if (v.t == V_BAD) {
				type_err(c, "the paths that reach this value give it dtypes that have no common dtype (such as Bool and a number), or an array on one and an element on another");
				goto fail;
			}
			if (v.t != V_NODE && v.t != V_LIST) { unsupported(c, "returning this value"); goto fail; }
			rv[nret] = v;
			rp[nret++] = s->pred;
			efree(s);
			continue;
		}
		case ZEND_FETCH_THIS: case ZEND_INIT_METHOD_CALL: case ZEND_FETCH_OBJ_R:
			unsupported(c, "$this, instance methods and properties");
			goto fail;
		default:
			unsupported(c, "the operation %s", zend_get_opcode_name(op->opcode) ? zend_get_opcode_name(op->opcode) + 5 : "?");
			goto fail;
		}
		/* an operation evaluated here that can fail: its errors join the path's, in evaluation order (PHP raises the
		 * first error an element meets, whether or not the value reaches the result) */
		if ((op->result_type & (IS_TMP_VAR | IS_VAR | IS_CV)) && slot(c, s, op->result.var)->t == V_NODE
			&& slot(c, s, op->result.var)->n >= nn0) {
			perr_join(c, s, slot(c, s, op->result.var)->n);
		}
		if (op->op1_type == IS_CV && slot(c, s, op->op1.var)->t == V_NODE && slot(c, s, op->op1.var)->n >= nn0) {
			perr_join(c, s, slot(c, s, op->op1.var)->n);   /* assignments and increments */
		}
		push_state(c, pend, npend, cpend, at + 1, s);
		continue;
	fail:
		efree(s);
		goto done;
	}
	if (!nret) {
		unsupported(c, "a closure without a return value");
		goto done;
	}
	/* the returns of disjoint paths, combined */
	c->ret = rv[nret - 1];
	for (int i = nret - 2; i >= 0; i--) {
		c->ret = sval_merge(c, rp[i], &rv[i], &c->ret);
		if (EG(exception)) goto done;
		if (c->ret.t == V_BAD) {
			type_err(c, "the paths that reach this value give it dtypes that have no common dtype (such as Bool and a number), or an array on one and an element on another");
			goto done;
		}
	}
	ok = true;
done:
	if (pend) {
		for (uint32_t i = 0; i <= nops; i++) {
			for (int k = 0; k < npend[i]; k++) efree(pend[i][k]);
			if (pend[i]) efree(pend[i]);
		}
		efree(pend);
		efree(npend);
		efree(cpend);
		efree(rv);
		efree(rp);
	}
	if (args) c->depth--;
	c->oa = s_oa;
	c->fn = s_fn;
	c->args = s_args;
	c->nargs = s_nargs;
	c->nslots = s_nslots;
	c->ncells = s_ncells;
	if (args) c->line = s_line;
	return ok;
}

/* ======================================================================================================================
 * The evaluator
 * ==================================================================================================================== */

typedef struct {
	comp *c;
	uint64_t (*R)[EV_CHUNK];
	uint8_t (*E)[EV_CHUNK];
	void **P;
	int nreg, nereg;
	int *order, norder;
	int out[MAX_OUT], nout;
	const uint8_t *rowp[MAX_INPUTS];   /* this row's element 0 of each input */
	int64_t idx[BUF_MAX_NDIM];
	int64_t at;                         /* first element of the chunk along the last axis */
	uint8_t scratch[EV_CHUNK];          /* error codes of an operation whose errors cannot reach an output */
	int64_t off[EV_CHUNK];              /* byte offsets of a gather */
	uint8_t zero[EV_CHUNK];             /* no errors */
} evalr;

/* each node's values for the current chunk: its register, or (inputs) the input's own memory, or (the output of each())
 * the result's memory */
#define NP(x) (ev->P[(x) - ev->c->nodes])
#define PI(x) ((uint64_t *)NP(x))
#define PF(x) ((float *)NP(x))
#define PD(x) ((double *)NP(x))

/* Elements are computed in four representations: Bool and the integers up to 32 bits in 32-bit lanes (sign- or
 * zero-extended), Int64 and UInt64 in 64-bit lanes, Float16, BFloat16 and Float32 as float, Float64 as double. */
#define PU(x) ((uint32_t *)NP(x))

static EVCLONE void load_elems(int dt, const uint8_t *p, int64_t s, int n, void *out)
{
	uint32_t *ou = out;
	uint64_t *oi = out;
	float *of = out;
	double *od = out;

	/* contiguous elements: plain typed loads (alignment 1: views start at any byte), which the compiler vectorizes */
#define LD(T, DST, CONV) \
	if (!BUF_SWAP && s == (int64_t)sizeof(T)) { \
		typedef T U __attribute__((aligned(1), may_alias)); \
		const U *q = (const U *)p; \
		for (int k = 0; k < n; k++) { T x = q[k]; DST[k] = CONV; } \
	} else if (s == 0) { \
		T x; buf_ld_le(&x, p, sizeof x); \
		for (int k = 0; k < n; k++) DST[k] = CONV; \
	} else { \
		for (int k = 0; k < n; k++) { T x; buf_ld_le(&x, p + k * s, sizeof x); DST[k] = CONV; } \
	}
	switch (dt) {
	case BUF_T_BOOL: for (int k = 0; k < n; k++) ou[k] = p[k * s] != 0; break;
	case BUF_T_INT8: LD(int8_t, ou, (uint32_t)(int32_t)x) break;
	case BUF_T_INT16: LD(int16_t, ou, (uint32_t)(int32_t)x) break;
	case BUF_T_INT32: LD(int32_t, ou, (uint32_t)x) break;
	case BUF_T_UINT8: LD(uint8_t, ou, (uint32_t)x) break;
	case BUF_T_UINT16: LD(uint16_t, ou, (uint32_t)x) break;
	case BUF_T_UINT32: LD(uint32_t, ou, x) break;
	case BUF_T_INT64: LD(int64_t, oi, (uint64_t)x) break;
	case BUF_T_UINT64: LD(uint64_t, oi, x) break;
	case BUF_T_FLOAT16: LD(uint16_t, of, (float)buf_dec_FLOAT16(x)) break;
	case BUF_T_BFLOAT16: LD(uint16_t, of, (float)buf_dec_BFLOAT16(x)) break;
	case BUF_T_FLOAT32: LD(float, of, x) break;
	case BUF_T_FLOAT64: LD(double, od, x) break;
	}
#undef LD
}

static EVCLONE void store_elems(int dt, const void *in, uint8_t *q, int64_t s, int n)
{
	const uint32_t *iu = in;
	const uint64_t *ii = in;
	const float *fi = in;
	const double *di = in;

#define ST(T, V) \
	if (!BUF_SWAP && s == (int64_t)sizeof(T)) { \
		typedef T U __attribute__((aligned(1), may_alias)); \
		U *w = (U *)q; \
		for (int k = 0; k < n; k++) w[k] = (T)(V); \
	} else { \
		for (int k = 0; k < n; k++) { T x = (T)(V); buf_st_le(q + k * s, &x, sizeof x); } \
	}
	switch (dt) {
	case BUF_T_BOOL: case BUF_T_INT8: case BUF_T_UINT8: for (int k = 0; k < n; k++) q[k * s] = (uint8_t)iu[k]; break;
	case BUF_T_INT16: case BUF_T_UINT16: ST(uint16_t, iu[k]) break;
	case BUF_T_INT32: case BUF_T_UINT32: ST(uint32_t, iu[k]) break;
	case BUF_T_INT64: case BUF_T_UINT64: ST(uint64_t, ii[k]) break;
	case BUF_T_FLOAT16: ST(uint16_t, buf_enc_FLOAT16(fi[k])) break;
	case BUF_T_BFLOAT16: ST(uint16_t, buf_enc_BFLOAT16(fi[k])) break;
	case BUF_T_FLOAT32: ST(float, fi[k]) break;
	case BUF_T_FLOAT64: ST(double, di[k]) break;
	}
#undef ST
}

/* off[k] += index * stride along one axis; an index outside 0..len-1 adds 0. The number of those. */
#define GATHER_AXIS(NAME, T) \
	static EVCLONE int NAME(const T *ix, uint64_t len, int64_t sb, int64_t *off, int n) \
	{ \
		int bad = 0; \
		for (int k = 0; k < n; k++) { \
			uint64_t u = (uint64_t)(int64_t)ix[k]; \
			int b = u >= len; \
			bad += b; \
			off[k] += (int64_t)(b ? 0 : u) * sb; \
		} \
		return bad; \
	}
GATHER_AXIS(gather_axis32, int32_t)
GATHER_AXIS(gather_axis64, int64_t)

/* n elements at base + off[k] (any byte offsets), into the computation representation */
static EVCLONE void gather_elems(int dt, const uint8_t *base, const int64_t *off, int n, void *out)
{
	uint32_t *ou = out;
	uint64_t *oi = out;
	float *of = out;
	double *od = out;

#define LDG(T, DST, CONV) \
	if (!BUF_SWAP) { \
		typedef T U __attribute__((aligned(1), may_alias)); \
		for (int k = 0; k < n; k++) { T x = *(const U *)(base + off[k]); DST[k] = CONV; } \
	} else { \
		for (int k = 0; k < n; k++) { T x; buf_ld_le(&x, base + off[k], sizeof x); DST[k] = CONV; } \
	}
	switch (dt) {
	case BUF_T_BOOL: for (int k = 0; k < n; k++) ou[k] = base[off[k]] != 0; break;
	case BUF_T_INT8: LDG(int8_t, ou, (uint32_t)(int32_t)x) break;
	case BUF_T_INT16: LDG(int16_t, ou, (uint32_t)(int32_t)x) break;
	case BUF_T_INT32: LDG(int32_t, ou, (uint32_t)x) break;
	case BUF_T_UINT8: LDG(uint8_t, ou, (uint32_t)x) break;
	case BUF_T_UINT16: LDG(uint16_t, ou, (uint32_t)x) break;
	case BUF_T_UINT32: LDG(uint32_t, ou, x) break;
	case BUF_T_INT64: LDG(int64_t, oi, (uint64_t)x) break;
	case BUF_T_UINT64: LDG(uint64_t, oi, x) break;
	case BUF_T_FLOAT16: LDG(uint16_t, of, (float)buf_dec_FLOAT16(x)) break;
	case BUF_T_BFLOAT16: LDG(uint16_t, of, (float)buf_dec_BFLOAT16(x)) break;
	case BUF_T_FLOAT32: LDG(float, of, x) break;
	case BUF_T_FLOAT64: LDG(double, od, x) break;
	}
#undef LDG
}

/* after an operation in Float32 for a Float16 or BFloat16 result: round each value once to the dtype */
static void round_chunk(int dt, float *r, int n)
{
	if (dt == BUF_T_FLOAT16) {
		for (int k = 0; k < n; k++) r[k] = (float)buf_dec_FLOAT16(buf_enc_FLOAT16(r[k]));
	} else if (dt == BUF_T_BFLOAT16) {
		for (int k = 0; k < n; k++) r[k] = (float)buf_dec_BFLOAT16(buf_enc_BFLOAT16(r[k]));
	}
}

/* integer operators, each result wrapped to the width of the result dtype in the same loop: sign-extended (shift left then
 * arithmetic right by sh = lane width - bits) for signed dtypes, masked for unsigned ones */
#define INT_LOOPS(EXPR) \
	if (sgn) { for (int k = 0; k < n; k++) r[k] = (lane_t)((slane_t)((lane_t)(EXPR) << sh) >> sh); } \
	else { for (int k = 0; k < n; k++) r[k] = (lane_t)(EXPR) & mask; }
#define WRAP1(v) (sgn ? (lane_t)((slane_t)((lane_t)(v) << sh) >> sh) : (lane_t)(v) & mask)

#define K_INT(SUF, T, S, LANE) \
static EVCLONE void k_int_bin_##SUF(int op, bool sgn, int bits, const T *a, const T *b, T *r, uint8_t *e, int n) \
{ \
	typedef T lane_t; typedef S slane_t; \
	unsigned sh = LANE - (unsigned)bits; \
	T mask = bits == LANE ? (T)~(T)0 : (T)(((T)1 << bits) - 1); \
	switch (op) { \
	case ZEND_ADD: INT_LOOPS(a[k] + b[k]) break; \
	case ZEND_SUB: INT_LOOPS(a[k] - b[k]) break; \
	case ZEND_MUL: INT_LOOPS(a[k] * b[k]) break; \
	case ZEND_BW_AND: for (int k = 0; k < n; k++) r[k] = a[k] & b[k]; break; \
	case ZEND_BW_OR: for (int k = 0; k < n; k++) r[k] = a[k] | b[k]; break; \
	case ZEND_BW_XOR: for (int k = 0; k < n; k++) r[k] = a[k] ^ b[k]; break; \
	case ZEND_MOD: \
		for (int k = 0; k < n; k++) { \
			if (b[k] == 0) { r[k] = 0; if (!e[k]) e[k] = E_MOD0; continue; } \
			if (sgn) r[k] = (S)b[k] == -1 ? 0 : (T)((S)a[k] % (S)b[k]); \
			else r[k] = a[k] % b[k]; \
		} \
		break; \
	case ZEND_POW: \
		for (int k = 0; k < n; k++) { \
			T base = a[k], x = b[k], acc = 1; \
			if (sgn && (S)x < 0) { r[k] = 0; if (!e[k]) e[k] = E_POWNEG; continue; } \
			for (; x; x >>= 1, base *= base) { if (x & 1) acc *= base; } \
			r[k] = WRAP1(acc); \
		} \
		break; \
	case ZEND_SL: case ZEND_SR: \
		for (int k = 0; k < n; k++) { \
			T cnt = b[k], v; \
			if (sgn && (S)cnt < 0) { r[k] = 0; if (!e[k]) e[k] = E_SHIFTNEG; continue; } \
			if (op == ZEND_SL) v = cnt >= (T)bits ? 0 : (T)(a[k] << cnt); \
			else if (sgn) v = (T)((S)a[k] >> (cnt > LANE - 1 ? LANE - 1 : cnt)); \
			else v = cnt > LANE - 1 ? 0 : a[k] >> cnt; \
			r[k] = WRAP1(v); \
		} \
		break; \
	} \
} \
/* a shift by a constant count that is not negative: no per-element checks */ \
static EVCLONE void k_shift_const_##SUF(int op, bool sgn, int bits, const T *a, T cnt, T *r, int n) \
{ \
	typedef T lane_t; typedef S slane_t; \
	unsigned sh = LANE - (unsigned)bits; \
	T mask = bits == LANE ? (T)~(T)0 : (T)(((T)1 << bits) - 1); \
	if (op == ZEND_SL) { \
		if (cnt >= (T)bits) { for (int k = 0; k < n; k++) r[k] = 0; } \
		else { INT_LOOPS(a[k] << cnt) } \
	} else if (sgn) { \
		unsigned c = cnt > LANE - 1 ? LANE - 1 : (unsigned)cnt; \
		for (int k = 0; k < n; k++) r[k] = (T)((S)a[k] >> c); \
	} else if (cnt > LANE - 1) { \
		for (int k = 0; k < n; k++) r[k] = 0; \
	} else { \
		for (int k = 0; k < n; k++) r[k] = a[k] >> cnt; \
	} \
} \
static EVCLONE __attribute__((unused)) void k_wrap_##SUF(bool sgn, int bits, T *r, int n) \
{ \
	typedef T lane_t; typedef S slane_t; \
	unsigned sh = LANE - (unsigned)bits; \
	T mask = bits == LANE ? (T)~(T)0 : (T)(((T)1 << bits) - 1); \
	if (bits == LANE) return; \
	INT_LOOPS(r[k]) \
}
K_INT(u32, uint32_t, int32_t, 32)
K_INT(u64, uint64_t, int64_t, 64)
#undef INT_LOOPS
#undef WRAP1

/* the wrapping parameters of an integer (or Bool) dtype on its lanes */
static bool dt_sgn(int dt) { return dt != BUF_T_BOOL && dt_signed(dt); }
static int dt_bits(int dt) { return dt == BUF_T_BOOL ? 32 : buf_dtypes[dt].bits; }

/* rounding and square roots: plain loops, vectorized (config.m4 builds with -fno-math-errno) */
static EVCLONE void k_math_f32(int fid, const float *a, float *r, int n)
{
	switch (fid) {
	case FN_FLOOR: for (int k = 0; k < n; k++) r[k] = floorf(a[k]); break;
	case FN_CEIL: for (int k = 0; k < n; k++) r[k] = ceilf(a[k]); break;
	case FN_ROUND: for (int k = 0; k < n; k++) r[k] = roundf(a[k]); break;
	case FN_SQRT: for (int k = 0; k < n; k++) r[k] = sqrtf(a[k]); break;
	case FN_ABS: for (int k = 0; k < n; k++) r[k] = fabsf(a[k]); break;
	}
}

static EVCLONE void k_math_f64(int fid, const double *a, double *r, int n)
{
	switch (fid) {
	case FN_FLOOR: for (int k = 0; k < n; k++) r[k] = floor(a[k]); break;
	case FN_CEIL: for (int k = 0; k < n; k++) r[k] = ceil(a[k]); break;
	case FN_ROUND: for (int k = 0; k < n; k++) r[k] = round(a[k]); break;
	case FN_SQRT: for (int k = 0; k < n; k++) r[k] = sqrt(a[k]); break;
	case FN_ABS: for (int k = 0; k < n; k++) r[k] = fabs(a[k]); break;
	}
}

#define FBIN(NAME, T, FMOD) \
	static EVCLONE void NAME(int op, const T *a, const T *b, T *r, int n) \
	{ \
		switch (op) { \
		case ZEND_ADD: for (int k = 0; k < n; k++) r[k] = a[k] + b[k]; break; \
		case ZEND_SUB: for (int k = 0; k < n; k++) r[k] = a[k] - b[k]; break; \
		case ZEND_MUL: for (int k = 0; k < n; k++) r[k] = a[k] * b[k]; break; \
		case ZEND_DIV: for (int k = 0; k < n; k++) r[k] = a[k] / b[k]; break; \
		case ZEND_MOD: for (int k = 0; k < n; k++) r[k] = FMOD(a[k], b[k]); break; \
		case ZEND_POW: for (int k = 0; k < n; k++) r[k] = (T)pow((double)a[k], (double)b[k]); break; \
		} \
	}
FBIN(k_f32_bin, float, fmodf)
FBIN(k_f64_bin, double, fmod)

/* comparisons give Bool: 32-bit lanes of 0 or 1 */
#define KCMP(NAME, T) \
	static EVCLONE void NAME(int which, const T *a, const T *b, uint32_t *r, int n) \
	{ \
		switch (which) { \
		case CMP_EQ: for (int k = 0; k < n; k++) r[k] = a[k] == b[k]; break; \
		case CMP_NE: for (int k = 0; k < n; k++) r[k] = a[k] != b[k]; break; \
		case CMP_LT: for (int k = 0; k < n; k++) r[k] = a[k] < b[k]; break; \
		case CMP_LE: for (int k = 0; k < n; k++) r[k] = a[k] <= b[k]; break; \
		} \
	}
KCMP(k_cmp_s32, int32_t)
KCMP(k_cmp_u32, uint32_t)
KCMP(k_cmp_s64, int64_t)
KCMP(k_cmp_u64, uint64_t)
KCMP(k_cmp_f32, float)
KCMP(k_cmp_f64, double)

/* min() and max() as PHP's: min keeps the earlier argument unless the later is smaller; max keeps it unless the later is
 * greater or they are unordered (a NaN) */
#define KMINMAX(NAME, T) \
	static EVCLONE void NAME(bool max, const T *a, const T *b, T *r, int n) \
	{ \
		if (max) { for (int k = 0; k < n; k++) r[k] = !(b[k] <= a[k]) ? b[k] : a[k]; } \
		else { for (int k = 0; k < n; k++) r[k] = b[k] < a[k] ? b[k] : a[k]; } \
	}
KMINMAX(k_mm_s32, int32_t)
KMINMAX(k_mm_u32, uint32_t)
KMINMAX(k_mm_s64, int64_t)
KMINMAX(k_mm_u64, uint64_t)
KMINMAX(k_mm_f32, float)
KMINMAX(k_mm_f64, double)

static EVCLONE void k_select64(const uint32_t *m, const uint64_t *a, const uint64_t *b, uint64_t *r, int n)
{
	for (int k = 0; k < n; k++) r[k] = m[k] ? a[k] : b[k];
}

static EVCLONE void k_select32(const uint32_t *m, const uint32_t *a, const uint32_t *b, uint32_t *r, int n)
{
	for (int k = 0; k < n; k++) r[k] = m[k] ? a[k] : b[k];
}

/* lane k of an integer representation as a 64-bit two's complement value */
static inline uint64_t int_lane(const void *in, int rc, bool sgn, int k)
{
	if (rc == RC_I64) return ((const uint64_t *)in)[k];
	return sgn ? (uint64_t)(int64_t)(int32_t)((const uint32_t *)in)[k] : (uint64_t)((const uint32_t *)in)[k];
}

static EVCLONE void k_cast(int from, int to, const void *in, void *out, int n)
{
	int fc = rclass(from), tc = rclass(to);
	const uint32_t *iu = in;
	const float *fi = in;
	const double *di = in;
	uint32_t *ou = out;
	uint64_t *oi = out;
	float *of = out;
	double *od = out;
	bool sgn = dt_sgn(from);

	if (to == BUF_T_BOOL) {
		switch (fc) {
		case RC_I32: for (int k = 0; k < n; k++) ou[k] = iu[k] != 0; break;
		case RC_I64: for (int k = 0; k < n; k++) ou[k] = ((const uint64_t *)in)[k] != 0; break;
		case RC_F32: for (int k = 0; k < n; k++) ou[k] = fi[k] != 0; break;
		default: for (int k = 0; k < n; k++) ou[k] = di[k] != 0; break;
		}
		return;
	}
	if (fc == RC_I32 || fc == RC_I64) {
		switch (tc) {
		case RC_I32: for (int k = 0; k < n; k++) ou[k] = (uint32_t)int_lane(in, fc, sgn, k); k_wrap_u32(dt_sgn(to), dt_bits(to), ou, n); return;
		case RC_I64: for (int k = 0; k < n; k++) oi[k] = int_lane(in, fc, sgn, k); return;
		case RC_F64:
			if (sgn) { for (int k = 0; k < n; k++) od[k] = (double)(int64_t)int_lane(in, fc, true, k); }
			else { for (int k = 0; k < n; k++) od[k] = (double)int_lane(in, fc, false, k); }
			return;
		default:   /* RC_F32: rounded once */
			if (to == BUF_T_FLOAT32 && fc == RC_I32) {
				if (sgn) { for (int k = 0; k < n; k++) of[k] = (float)(int32_t)iu[k]; }
				else { for (int k = 0; k < n; k++) of[k] = (float)iu[k]; }
			} else {
				for (int k = 0; k < n; k++) of[k] = int_to_f(int_lane(in, fc, sgn, k), sgn, to);
			}
			return;
		}
	}
	switch (fc * 4 + tc) {
	case RC_F32 * 4 + RC_F64: for (int k = 0; k < n; k++) od[k] = fi[k]; return;
	case RC_F32 * 4 + RC_F32: for (int k = 0; k < n; k++) of[k] = fi[k]; round_chunk(to, of, n); return;
	case RC_F64 * 4 + RC_F32: for (int k = 0; k < n; k++) of[k] = round_to(di[k], to); return;
	case RC_F64 * 4 + RC_F64: for (int k = 0; k < n; k++) od[k] = di[k]; return;
	/* (int) of a float: PHP's conversion, then wrapped to the dtype. Values well inside the 64-bit range truncate with
	 * the CPU's conversion (vectorized); NaN, infinities and anything else take PHP's own rule. */
#define F2I(SRC, DST, T) do { \
		int bad = 0; \
		for (int k = 0; k < n; k++) { \
			double v = (double)SRC[k]; \
			int ok = (v > -9.2e18) & (v < 9.2e18); \
			DST[k] = (T)(int64_t)(ok ? v : 0.0); \
			bad += !ok; \
		} \
		if (bad) { \
			for (int k = 0; k < n; k++) { \
				double v = (double)SRC[k]; \
				if (!(v > -9.2e18 && v < 9.2e18)) DST[k] = (T)buf_dval_to_lval(v); \
			} \
		} \
	} while (0)
	case RC_F32 * 4 + RC_I64: F2I(fi, oi, uint64_t); return;
	case RC_F64 * 4 + RC_I64: F2I(di, oi, uint64_t); return;
	case RC_F32 * 4 + RC_I32: F2I(fi, ou, uint32_t); k_wrap_u32(dt_sgn(to), dt_bits(to), ou, n); return;
	case RC_F64 * 4 + RC_I32: F2I(di, ou, uint32_t); k_wrap_u32(dt_sgn(to), dt_bits(to), ou, n); return;
#undef F2I
	}
}

/* keep e's error codes, take z's where e has none */
static EVCLONE void merge_err(uint8_t *e, const uint8_t *z, int n)
{
	for (int k = 0; k < n; k++) e[k] = e[k] ? e[k] : z[k];
}

/* a select's error codes: the condition's, else those of the operand it selects */
static EVCLONE void merge_err_select(uint8_t *e, const uint32_t *m, const uint8_t *zc, const uint8_t *za, const uint8_t *zb, int n)
{
	for (int k = 0; k < n; k++) {
		uint8_t v = m[k] ? za[k] : zb[k];

		e[k] = zc[k] ? zc[k] : v;
	}
}

/* error codes of a node from its operands' (the first that has one) */
static void errs_from(evalr *ev, const enode *x, int n)
{
	uint8_t *e = ev->E[x->ereg];
	int ops[3] = {x->a, x->b, x->c};

	if (x->ealias) return;   /* the array of the operand it passes on */
	memset(e, 0, (size_t)n);
	if (x->kind == NK_SELECT) {
		const enode *ec = &ev->c->nodes[x->a], *ea = &ev->c->nodes[x->b], *eb = &ev->c->nodes[x->c];

		merge_err_select(e, PU(ec), ec->ereg >= 0 ? ev->E[ec->ereg] : ev->zero, ea->ereg >= 0 ? ev->E[ea->ereg] : ev->zero,
			eb->ereg >= 0 ? ev->E[eb->ereg] : ev->zero, n);
		return;
	}
	if (x->kind == NK_GATHER) {
		for (int i = 0; i < x->b; i++) {
			const enode *ix = &ev->c->nodes[ev->c->pool[x->a + i]];
			const uint8_t *z;

			if (ix->ereg < 0) continue;
			z = ev->E[ix->ereg];
			merge_err(e, z, n);
		}
		return;
	}
	for (int i = 0; i < 3; i++) {
		const uint8_t *z;

		if (ops[i] < 0 || ev->c->nodes[ops[i]].ereg < 0) continue;
		z = ev->E[ev->c->nodes[ops[i]].ereg];
		merge_err(e, z, n);
	}
}

/* dtypes whose elements in memory are their computation representation: read and written in place */
static bool dt_in_place(int dt)
{
	switch (dt) {
	case BUF_T_FLOAT32: case BUF_T_FLOAT64: case BUF_T_INT64: case BUF_T_UINT64: case BUF_T_INT32: case BUF_T_UINT32: return !BUF_SWAP;
	}
	return false;
}

static void exec_node(evalr *ev, const enode *x, int n)
{
	comp *c = ev->c;
	int rc = rclass(x->dt);
	const enode *A = x->a >= 0 && x->kind != NK_GATHER ? &c->nodes[x->a] : NULL, *B = x->b >= 0 && x->kind != NK_GATHER ? &c->nodes[x->b] : NULL;
	uint8_t *e = x->ereg >= 0 ? ev->E[x->ereg] : NULL;

	if (e) errs_from(ev, x, n);
	switch (x->kind) {
	case NK_CONST:   /* filled once, before the first chunk (fill_constants) */
		return;
	case NK_INPUT: {
		const input *in = &c->in[x->slot];
		int64_t s = c->nd ? in->sb[c->nd - 1] : 0, sz = buf_dtypes[x->dt].size;
		const uint8_t *p = ev->rowp[x->slot] + x->off + ev->at * s;

		/* contiguous, aligned elements already in the computation representation: read in place */
		if (s == sz && ((uintptr_t)p % (uintptr_t)sz) == 0 && dt_in_place(x->dt)) {
			NP(x) = (void *)p;
			return;
		}
		NP(x) = ev->R[x->reg];
		load_elems(x->dt, p, s, n, NP(x));
		return;
	}
	case NK_COORD:   /* PHP's int: 64-bit lanes, or 32-bit lanes on 32-bit PHP (where no axis is that long) */
		if (rc == RC_I32) {
			if (x->slot == c->nd - 1) { for (int k = 0; k < n; k++) PU(x)[k] = (uint32_t)(ev->at + k); }
			else { for (int k = 0; k < n; k++) PU(x)[k] = (uint32_t)ev->idx[x->slot]; }
		} else {
			if (x->slot == c->nd - 1) { for (int k = 0; k < n; k++) PI(x)[k] = (uint64_t)(ev->at + k); }
			else { for (int k = 0; k < n; k++) PI(x)[k] = (uint64_t)ev->idx[x->slot]; }
		}
		return;
	case NK_BIN:
		switch (rc) {
		case RC_I32:
			if ((x->op == ZEND_SL || x->op == ZEND_SR) && B->kind == NK_CONST && !x->fails) {
				k_shift_const_u32(x->op, dt_sgn(x->dt), dt_bits(x->dt), PU(A), (uint32_t)B->k.i, PU(x), n);
			} else {
				k_int_bin_u32(x->op, dt_sgn(x->dt), dt_bits(x->dt), PU(A), PU(B), PU(x), e ? e : ev->scratch, n);
			}
			break;
		case RC_I64:
			if ((x->op == ZEND_SL || x->op == ZEND_SR) && B->kind == NK_CONST && !x->fails) {
				k_shift_const_u64(x->op, dt_sgn(x->dt), dt_bits(x->dt), PI(A), B->k.i, PI(x), n);
			} else {
				k_int_bin_u64(x->op, dt_sgn(x->dt), dt_bits(x->dt), PI(A), PI(B), PI(x), e ? e : ev->scratch, n);
			}
			break;
		case RC_F32: k_f32_bin(x->op, PF(A), PF(B), PF(x), n); round_chunk(x->dt, PF(x), n); break;
		default: k_f64_bin(x->op, PD(A), PD(B), PD(x), n); break;
		}
		return;
	case NK_CMP:
		switch (rclass(A->dt)) {
		case RC_I32:
			if (dt_sgn(A->dt)) k_cmp_s32(x->op, (const int32_t *)PU(A), (const int32_t *)PU(B), PU(x), n);
			else k_cmp_u32(x->op, PU(A), PU(B), PU(x), n);
			break;
		case RC_I64:
			if (dt_sgn(A->dt)) k_cmp_s64(x->op, (const int64_t *)PI(A), (const int64_t *)PI(B), PU(x), n);
			else k_cmp_u64(x->op, PI(A), PI(B), PU(x), n);
			break;
		case RC_F32: k_cmp_f32(x->op, PF(A), PF(B), PU(x), n); break;
		default: k_cmp_f64(x->op, PD(A), PD(B), PU(x), n); break;
		}
		return;
	case NK_TOBOOL: k_cast(A->dt, BUF_T_BOOL, NP(A), NP(x), n); return;
	case NK_NOT: for (int k = 0; k < n; k++) PU(x)[k] = PU(A)[k] ^ 1; return;
	case NK_BWNOT:
		if (x->dt == BUF_T_BOOL) { for (int k = 0; k < n; k++) PU(x)[k] = PU(A)[k] ^ 1; }
		else if (rc == RC_I32) { for (int k = 0; k < n; k++) PU(x)[k] = ~PU(A)[k]; k_wrap_u32(dt_sgn(x->dt), dt_bits(x->dt), PU(x), n); }
		else { for (int k = 0; k < n; k++) PI(x)[k] = ~PI(A)[k]; }
		return;
	case NK_CAST: k_cast(A->dt, x->dt, NP(A), NP(x), n); return;
	case NK_SELECT: {
		const enode *Y = &c->nodes[x->c];

		if (rc == RC_F32 || rc == RC_I32) k_select32(PU(A), PU(B), PU(Y), PU(x), n);
		else k_select64(PU(A), PI(B), PI(Y), PI(x), n);
		return;
	}
	case NK_FN1: {
		int ac = rclass(A->dt);

		switch (x->op) {
		case FN_ABS: case FN_FLOOR: case FN_CEIL: case FN_ROUND: case FN_SQRT:
			if (ac == RC_I32) { for (int k = 0; k < n; k++) { int32_t v = (int32_t)PU(A)[k]; PU(x)[k] = v < 0 ? 0 - (uint32_t)v : (uint32_t)v; } k_wrap_u32(dt_sgn(x->dt), dt_bits(x->dt), PU(x), n); }
			else if (ac == RC_I64) { for (int k = 0; k < n; k++) { int64_t v = (int64_t)PI(A)[k]; PI(x)[k] = v < 0 ? 0 - (uint64_t)v : (uint64_t)v; } }
			else if (ac == RC_F32) { k_math_f32(x->op, PF(A), PF(x), n); round_chunk(x->dt, PF(x), n); }
			else k_math_f64(x->op, PD(A), PD(x), n);
			return;
#define FB(ID, PRED) case ID: \
			if (ac == RC_F32) { for (int k = 0; k < n; k++) PU(x)[k] = PRED(PF(A)[k]) != 0; } \
			else { for (int k = 0; k < n; k++) PU(x)[k] = PRED(PD(A)[k]) != 0; } \
			return;
		FB(FN_ISNAN, isnan)
		FB(FN_ISFINITE, isfinite)
		FB(FN_ISINF, isinf)
#undef FB
		}
		return;
	}
	case NK_FN2:
		switch (x->op) {
		case FN_MIN: case FN_MAX: {
			bool mx = x->op == FN_MAX;

			switch (rc) {
			case RC_I32:
				if (dt_sgn(x->dt)) k_mm_s32(mx, (const int32_t *)PU(A), (const int32_t *)PU(B), (int32_t *)PU(x), n);
				else k_mm_u32(mx, PU(A), PU(B), PU(x), n);
				break;
			case RC_I64:
				if (dt_sgn(x->dt)) k_mm_s64(mx, (const int64_t *)PI(A), (const int64_t *)PI(B), (int64_t *)PI(x), n);
				else k_mm_u64(mx, PI(A), PI(B), PI(x), n);
				break;
			case RC_F32: k_mm_f32(mx, PF(A), PF(B), PF(x), n); break;
			default: k_mm_f64(mx, PD(A), PD(B), PD(x), n); break;
			}
			return;
		}
		case FN_INTDIV: {
			bool sgn = dt_sgn(x->dt);

			if (rc == RC_I32) {
				for (int k = 0; k < n; k++) {
					uint32_t a = PU(A)[k], b = PU(B)[k];

					if (b == 0) { PU(x)[k] = 0; if (!e[k]) e[k] = E_DIV0; continue; }
					if (sgn) PU(x)[k] = (int32_t)b == -1 ? 0 - a : (uint32_t)((int32_t)a / (int32_t)b);   /* MIN / -1 wraps */
					else PU(x)[k] = a / b;
				}
				k_wrap_u32(sgn, dt_bits(x->dt), PU(x), n);
			} else {
				for (int k = 0; k < n; k++) {
					uint64_t a = PI(A)[k], b = PI(B)[k];

					if (b == 0) { PI(x)[k] = 0; if (!e[k]) e[k] = E_DIV0; continue; }
					if (sgn) PI(x)[k] = (int64_t)b == -1 ? 0 - a : (uint64_t)((int64_t)a / (int64_t)b);
					else PI(x)[k] = a / b;
				}
			}
			return;
		}
		case FN_FMOD:
			if (rc == RC_F32) { for (int k = 0; k < n; k++) PF(x)[k] = fmodf(PF(A)[k], PF(B)[k]); round_chunk(x->dt, PF(x), n); }
			else { for (int k = 0; k < n; k++) PD(x)[k] = fmod(PD(A)[k], PD(B)[k]); }
			return;
		}
		return;
	case NK_ERRS:
		for (int k = 0; k < n; k++) PU(x)[k] = 0;
		return;
	case NK_NOERR:
		memcpy(NP(x), NP(A), (size_t)n * 4);
		return;
	case NK_WITHERR:
		memcpy(NP(x), NP(B), (size_t)n * (rc == RC_I64 || rc == RC_F64 ? 8 : 4));
		return;
	case NK_FAILC:
		memset(NP(x), 0, (size_t)n * 8);
		for (int k = 0; k < n; k++) e[k] = (uint8_t)x->k.i;
		return;
	case NK_GATHER: {
		const buf_view *v = c->cap[x->slot];
		int64_t *off = ev->off;

		const uint8_t *base = v->data;

		/* byte offsets, axis by axis; an index outside its axis (negative ones included) is an error of the element,
		 * which then reads index 0 (its value is never used). A constant index moves the base once. */
		for (int k = 0; k < n; k++) off[k] = 0;
		for (int i = 0; i < x->b; i++) {
			const enode *in = &c->nodes[c->pool[x->a + i]];
			uint64_t len = (uint64_t)v->shape[i];
			int64_t sb = v->sbytes[i];
			int nbad;

			if (in->kind == NK_CONST) {
				uint64_t u = rclass(in->dt) == RC_I32 ? (uint64_t)(int64_t)(int32_t)in->k.i : in->k.i;

				if (u < len) { base += (int64_t)u * sb; continue; }
				for (int k = 0; k < n; k++) if (!e[k]) e[k] = E_RANGE;
				continue;
			}
			nbad = rclass(in->dt) == RC_I32 ? gather_axis32((const int32_t *)PU(in), len, sb, off, n)
				: gather_axis64((const int64_t *)PI(in), len, sb, off, n);
			if (nbad) {
				for (int k = 0; k < n; k++) {
					uint64_t u = rclass(in->dt) == RC_I32 ? (uint64_t)(int64_t)((const int32_t *)PU(in))[k] : PI(in)[k];

					if (u >= len && !e[k]) e[k] = E_RANGE;
				}
			}
		}
		gather_elems(v->dt, base, off, n, NP(x));
		return;
	}
	}
}

/* constants keep their registers for the whole evaluation and are filled once */
static void fill_constants(evalr *ev)
{
	for (int o = 0; o < ev->norder; o++) {
		const enode *x = &ev->c->nodes[ev->order[o]];

		if (x->kind != NK_CONST) continue;
		switch (rclass(x->dt)) {
		case RC_I32: for (int k = 0; k < EV_CHUNK; k++) PU(x)[k] = (uint32_t)x->k.i; break;
		case RC_I64: for (int k = 0; k < EV_CHUNK; k++) PI(x)[k] = x->k.i; break;
		case RC_F32: for (int k = 0; k < EV_CHUNK; k++) PF(x)[k] = x->k.f; break;
		default: for (int k = 0; k < EV_CHUNK; k++) PD(x)[k] = x->k.d; break;
		}
	}
}

/* the first error among the outputs of this chunk, raised; false after throwing */
static bool check_errors(evalr *ev, int n)
{
	bool any = false;

	for (int j = 0; j < ev->nout; j++) {
		const enode *o = &ev->c->nodes[ev->out[j]];
		uint8_t acc = 0;

		if (o->ereg < 0) continue;
		for (int k = 0; k < n; k++) acc |= ev->E[o->ereg][k];
		any = any || acc;
	}
	if (!any) return true;
	for (int k = 0; k < n; k++) {
		for (int j = 0; j < ev->nout; j++) {
			const enode *o = &ev->c->nodes[ev->out[j]];
			uint8_t code;

			if (o->ereg < 0 || !(code = ev->E[o->ereg][k])) continue;
			switch (code) {
			case E_MOD0: zend_throw_exception(zend_ce_division_by_zero_error, "Modulo by zero", 0); break;
			case E_DIV0: zend_throw_exception(zend_ce_division_by_zero_error, "Division by zero", 0); break;
			case E_SHIFTNEG: zend_throw_exception(zend_ce_arithmetic_error, "Bit shift by negative number", 0); break;
			case E_POWNEG: zend_value_error("Negative exponent in an integer power; convert() to a float dtype first"); break;
			case E_RANGE: {
				smart_str s = {0};

				for (int i = 0; i < ev->c->nd; i++) {
					if (i) smart_str_appends(&s, ", ");
					smart_str_append_long(&s, (zend_long)(i == ev->c->nd - 1 ? ev->at + k : ev->idx[i]));
				}
				smart_str_0(&s);
				zend_value_error("Buffers\\Tensor::%s(): an index of a captured tensor is out of range (at element [%s])", ev->c->method, s.s ? ZSTR_VAL(s.s) : "");
				smart_str_free(&s);
				break;
			}
			default:   /* an operation on constants */
				zend_throw_exception(ev->c->ferr[code - E_CUSTOM].ce, ZSTR_VAL(ev->c->ferr[code - E_CUSTOM].msg), 0);
			}
			return false;
		}
	}
	return true;
}

/* live nodes, their order and registers (reused after the last read) */
static void plan(evalr *ev)
{
	comp *c = ev->c;
	int *stack = emalloc(sizeof(int) * (size_t)(c->nn + 1)), sp = 0, *freer = emalloc(sizeof(int) * (size_t)(c->nn + 1)), nfree = 0;
	int *freee = emalloc(sizeof(int) * (size_t)(c->nn + 1)), nfreee = 0, *eroot, *elast;

	for (int j = 0; j < ev->nout; j++) stack[sp++] = ev->out[j];
	while (sp) {
		enode *x = &c->nodes[stack[--sp]];

		if (x->live) continue;
		x->live = true;
		if (x->kind == NK_GATHER) {
			for (int i = 0; i < x->b; i++) stack[sp++] = c->pool[x->a + i];
		} else {
			if (x->a >= 0) stack[sp++] = x->a;
			if (x->b >= 0) stack[sp++] = x->b;
			if (x->c >= 0) stack[sp++] = x->c;
		}
	}
	ev->order = emalloc(sizeof(int) * (size_t)c->nn);
	ev->norder = 0;
	for (int i = 0; i < c->nn; i++) {
		enode *x = &c->nodes[i];

		if (!x->live) continue;
		x->last = i;
		if (x->kind == NK_GATHER) {
			for (int k = 0; k < x->b; k++) c->nodes[c->pool[x->a + k]].last = i;
		} else {
			if (x->a >= 0) c->nodes[x->a].last = i;
			if (x->b >= 0) c->nodes[x->b].last = i;
			if (x->c >= 0) c->nodes[x->c].last = i;
		}
		ev->order[ev->norder++] = i;
	}
	for (int j = 0; j < ev->nout; j++) c->nodes[ev->out[j]].last = INT_MAX;
	/* error arrays: a node that can fail itself, a select, or one with two failing operands has its own; any other
	 * failing node passes on the array of its one failing operand (eroot), which then lives until the last reader of
	 * any node sharing it (elast) */
	eroot = emalloc(sizeof(int) * (size_t)c->nn);
	elast = emalloc(sizeof(int) * (size_t)c->nn);
	for (int o = 0; o < ev->norder; o++) {
		int i = ev->order[o], nf = 0, only = -1;
		enode *x = &c->nodes[i];

		eroot[i] = -1;
		elast[i] = -1;
		if (!x->fails) continue;
		if (x->kind != NK_GATHER && x->kind != NK_SELECT && !x->owns) {
			int ops[3] = {x->a, x->b, x->c};

			for (int k = 0; k < 3; k++) {
				if (ops[k] >= 0 && c->nodes[ops[k]].fails && ops[k] != only) { nf++; only = ops[k]; }
			}
		}
		eroot[i] = nf == 1 ? eroot[only] : i;
		x->ealias = eroot[i] != i;
	}
	for (int o = 0; o < ev->norder; o++) {
		int i = ev->order[o];

		if (eroot[i] >= 0 && c->nodes[i].last > elast[eroot[i]]) elast[eroot[i]] = c->nodes[i].last;
	}
	ev->nreg = ev->nereg = 0;
	for (int o = 0; o < ev->norder; o++) {
		int i = ev->order[o];
		enode *x = &c->nodes[i];

		/* a constant is filled once, so its register is its own for the whole evaluation */
		x->reg = nfree && x->kind != NK_CONST ? freer[--nfree] : ev->nreg++;
		if (x->fails) x->ereg = x->ealias ? c->nodes[eroot[i]].ereg : nfreee ? freee[--nfreee] : ev->nereg++;
		/* operands read for the last time here: their registers are free for the nodes after this one */
		{
			int ops[3] = {-1, -1, -1};

			if (x->kind != NK_GATHER) { ops[0] = x->a; ops[1] = x->b; ops[2] = x->c; }
			for (int k = 0; k < 3; k++) {
				enode *y;

				if (ops[k] < 0) continue;
				y = &c->nodes[ops[k]];
				if (y->last == i && y->reg >= 0 && y->kind != NK_CONST) {
					bool dup = false;

					for (int q = 0; q < k; q++) dup = dup || ops[q] == ops[k];
					if (dup) continue;
					freer[nfree++] = y->reg;
				}
				/* the error array, when this is the last reader of every node that shares it */
				if (y->fails && elast[eroot[ops[k]]] == i) {
					bool dup = false;

					for (int q = 0; q < k; q++) dup = dup || (ops[q] >= 0 && c->nodes[ops[q]].fails && eroot[ops[q]] == eroot[ops[k]]);
					if (!dup) { freee[nfreee++] = c->nodes[eroot[ops[k]]].ereg; elast[eroot[ops[k]]] = -2; }
				}
			}
			if (x->kind == NK_GATHER) {
				for (int k = 0; k < x->b; k++) {
					enode *y = &c->nodes[c->pool[x->a + k]];
					bool dup = false;

					for (int q = 0; q < k; q++) dup = dup || c->pool[x->a + q] == c->pool[x->a + k];
					if (!dup && y->last == i && y->kind != NK_CONST) freer[nfree++] = y->reg;
					if (y->fails && elast[eroot[c->pool[x->a + k]]] == i) {
						freee[nfreee++] = c->nodes[eroot[c->pool[x->a + k]]].ereg;
						elast[eroot[c->pool[x->a + k]]] = -2;
					}
				}
			}
		}
	}
	efree(stack);
	efree(freer);
	efree(freee);
	efree(eroot);
	efree(elast);
}

/* the inputs: a Tensor, a TensorAggregate (its getTensor()) or a PHP scalar; false after throwing */
static bool take_input(comp *c, zval *z, int argn, zval *hold)
{
	input *in = &c->in[c->nin];

	memset(in, 0, sizeof *in);
	ZVAL_UNDEF(hold);
	ZVAL_DEREF(z);
	switch (Z_TYPE_P(z)) {
	case IS_LONG: case IS_DOUBLE: case IS_TRUE: case IS_FALSE:
		in->scalar = true;
		ZVAL_COPY_VALUE(&in->z, z);
		c->nin++;
		return true;
	case IS_OBJECT:
		if (Z_OBJCE_P(z) == buf_ce_tensor) {
			ZVAL_COPY(hold, z);
		} else if (instanceof_function(Z_OBJCE_P(z), buf_ce_tensor_agg)) {
			zend_call_method_with_0_params(Z_OBJ_P(z), Z_OBJCE_P(z), NULL, "gettensor", hold);
			if (EG(exception)) return false;
			if (Z_TYPE_P(hold) != IS_OBJECT || Z_OBJCE_P(hold) != buf_ce_tensor) {
				zend_type_error("%s::getTensor(): Return value must be of type Buffers\\Tensor", ZSTR_VAL(Z_OBJCE_P(z)->name));
				return false;
			}
		} else {
			break;
		}
		in->v = buf_view_from(Z_OBJ_P(hold));
		if (!dt_arith(in->v->dt)) {
			zval_ptr_dtor(hold);
			ZVAL_UNDEF(hold);
			zend_type_error("Buffers\\Tensor::%s(): input %d has dtype %s, which has no arithmetic; convert() it first", c->method, argn, buf_dtypes[in->v->dt].name);
			return false;
		}
		c->nin++;
		return true;
	}
	zend_argument_type_error((uint32_t)argn, "must be of type Buffers\\TensorAggregate|int|float|bool, %s given", zend_zval_type_name(z));
	return false;
}

/* iteration shape: the broadcast of the inputs (array parameters without their last axis); false after throwing */
static bool iteration_shape(comp *c)
{
	int nd = 0;

	for (int i = 0; i < c->nin; i++) {
		input *in = &c->in[i];
		int ind;

		if (in->scalar) continue;
		ind = in->v->ndim - (in->arr ? 1 : 0);
		if (ind > nd) nd = ind;
	}
	for (int j = 0; j < nd; j++) c->shape[j] = 1;
	for (int i = 0; i < c->nin; i++) {
		input *in = &c->in[i];
		int ind;

		if (in->scalar) continue;
		ind = in->v->ndim - (in->arr ? 1 : 0);
		for (int j = 0; j < ind; j++) {
			int64_t d = in->v->shape[j];
			int at = nd - ind + j;

			if (d == c->shape[at] || d == 1) continue;
			if (c->shape[at] == 1) { c->shape[at] = d; continue; }
			zend_value_error("Buffers\\Tensor::%s(): the inputs could not be broadcast together (input %d has length " ZEND_LONG_FMT " where another has " ZEND_LONG_FMT ")",
				c->method, i + 1, (zend_long)d, (zend_long)c->shape[at]);
			return false;
		}
	}
	c->nd = nd;
	for (int i = 0; i < c->nin; i++) {
		input *in = &c->in[i];
		int ind;

		if (in->scalar) continue;
		ind = in->v->ndim - (in->arr ? 1 : 0);
		for (int j = 0; j < nd; j++) in->sb[j] = 0;
		for (int j = 0; j < ind; j++) {
			int at = nd - ind + j;

			in->sb[at] = in->v->shape[j] == 1 ? 0 : in->v->sbytes[j];
		}
	}
	return true;
}

/* which inputs are bound to `array` parameters (known before compiling: it decides the iteration shape) */
static bool mark_array_params(comp *c, const zend_function *fn)
{
	for (int i = 0; i < c->nin && i < (int)fn->op_array.num_args; i++) {
		const zend_arg_info *ai = &fn->op_array.arg_info[i];

		if (ZEND_TYPE_IS_SET(ai->type) && ZEND_TYPE_PURE_MASK(ai->type) == MAY_BE_ARRAY && !ZEND_TYPE_IS_COMPLEX(ai->type)) {
			if (c->in[i].scalar || c->in[i].v->ndim == 0) {
				zend_type_error("Buffers\\Tensor::%s(): parameter $%s is an array, so input %d must have at least one axis", c->method, ZSTR_VAL(ai->name), i + 1);
				return false;
			}
			if (buf_is_packed(c->in[i].v->dt)) {
				zend_type_error("Buffers\\Tensor::%s(): input %d has a packed dtype", c->method, i + 1);
				return false;
			}
			c->in[i].arr = true;
		}
	}
	return true;
}

static void comp_free(comp *c, zval *holds)
{
	for (int i = 0; i < c->nin; i++) zval_ptr_dtor(&holds[i]);
	for (int i = 0; i < c->ncapt; i++) zval_ptr_dtor(&c->capz[i]);
	if (c->nodes) efree(c->nodes);
	if (c->pool) efree(c->pool);
	if (c->ht) efree(c->ht);
	for (int i = 0; i < c->nferr; i++) zend_string_release(c->ferr[i].msg);
}

/* a captured tensor that shares memory with the destination is read from a copy made before the first write */
static bool snapshot_captures(comp *c, const buf_view *dst)
{
	for (int i = 0; i < c->ncapt; i++) {
		buf_view *v = c->cap[i];
		zend_object *copy;

		if (v->buf != dst->buf) continue;
		copy = buf_view_copy_host(v, v->data);   /* a C-contiguous tensor in a new buffer */
		if (!copy) return false;
		zval_ptr_dtor(&c->capz[i]);
		ZVAL_OBJ(&c->capz[i], copy);
		c->cap[i] = buf_view_from(copy);
	}
	return true;
}

/* each() (dst NULL: a new tensor in result) or assign() (into dst) */
static void each_impl(INTERNAL_FUNCTION_PARAMETERS, bool assign)
{
	zval *zfn, *args = NULL, holds[MAX_INPUTS];
	uint32_t argc = 0;
	comp c;
	evalr ev;
	const zend_function *fn;
	buf_view *self = buf_view_from(Z_OBJ_P(ZEND_THIS));
	int odt = -1, kout = 1;
	int64_t total = 1, rows, inner;
	zend_object *bufobj = NULL;
	uint8_t *rbase = NULL;
	int64_t osb[BUF_MAX_NDIM + 1];
	int ond = 0;
	bool direct = true;

	ZEND_PARSE_PARAMETERS_START(1, -1)
		Z_PARAM_OBJECT_OF_CLASS(zfn, zend_ce_closure)
		Z_PARAM_VARIADIC('*', args, argc)
	ZEND_PARSE_PARAMETERS_END();

	memset(&c, 0, sizeof c);
	memset(&ev, 0, sizeof ev);
	c.method = assign ? "assign" : "each";
	if (argc + 1 > MAX_INPUTS) {
		zend_argument_count_error("Buffers\\Tensor::%s(): at most %d inputs", c.method, MAX_INPUTS);
		RETURN_THROWS();
	}
	{
		zval self_z;

		ZVAL_OBJ(&self_z, Z_OBJ_P(ZEND_THIS));
		if (!take_input(&c, &self_z, 1, &holds[0])) { comp_free(&c, holds); RETURN_THROWS(); }
	}
	for (uint32_t i = 0; i < argc; i++) {
		if (!take_input(&c, &args[i], (int)i + 2, &holds[c.nin])) { comp_free(&c, holds); RETURN_THROWS(); }
	}
	fn = zend_get_closure_method_def(Z_OBJ_P(zfn));
	if (fn->type != ZEND_USER_FUNCTION) {
		zend_argument_value_error(1, "must be a closure written in PHP");
		comp_free(&c, holds);
		RETURN_THROWS();
	}
	if (!mark_array_params(&c, fn) || !iteration_shape(&c)) { comp_free(&c, holds); RETURN_THROWS(); }
	for (int i = 0; i < c.nin; i++) {
		if (!c.in[i].scalar && (!buf_buffer_from(c.in[i].v->buf)->host || !buf_view_check_access(c.in[i].v))) {
			if (!EG(exception)) zend_throw_error(NULL, "Buffers\\Tensor::%s(): input %d is not in host memory; move it with to()", c.method, i + 1);
			comp_free(&c, holds);
			RETURN_THROWS();
		}
	}
	if (!run_func(&c, fn, NULL, 0)) { comp_free(&c, holds); RETURN_THROWS(); }

	/* the outputs, in one dtype */
	if (c.ret.t == V_LIST) {
		kout = c.ret.len;
		if (kout == 0) { unsupported(&c, "returning an empty array"); comp_free(&c, holds); RETURN_THROWS(); }
		/* the common dtype of the elements */
		{
			int acc = c.pool[c.ret.a];

			for (int j = 1; j < kout; j++) {
				int rt = common(&c, acc, c.pool[c.ret.a + j]);

				if (rt < 0) { comp_free(&c, holds); RETURN_THROWS(); }
				acc = cast(&c, acc, rt);
			}
			acc = natural(&c, acc);
			if (acc < 0) { comp_free(&c, holds); RETURN_THROWS(); }
			odt = c.nodes[acc].dt;
		}
		for (int j = 0; j < kout; j++) {
			ev.out[j] = cast(&c, c.pool[c.ret.a + j], odt);
			if (ev.out[j] < 0) { comp_free(&c, holds); RETURN_THROWS(); }
		}
	} else {
		ev.out[0] = natural(&c, c.ret.n);
		if (ev.out[0] < 0) { comp_free(&c, holds); RETURN_THROWS(); }
		odt = c.nodes[ev.out[0]].dt;
	}
	ev.nout = kout;
	/* assign(): computed in the destination's dtype when the conversion gives what convert() gives (every arithmetic
	 * dtype; a Float64 result into BFloat16 is left to convert(), which rounds through Float32) */
	if (assign && dt_arith(self->dt) && self->dt != odt && !(self->dt == BUF_T_BFLOAT16 && odt == BUF_T_FLOAT64)) {
		for (int j = 0; j < kout; j++) {
			ev.out[j] = cast(&c, ev.out[j], self->dt);
			if (ev.out[j] < 0) { comp_free(&c, holds); RETURN_THROWS(); }
		}
		odt = self->dt;
	}

	for (int j = 0; j < c.nd; j++) {
		if (buf_mul_ovf(total, c.shape[j], &total)) { zend_value_error("The result is too large"); comp_free(&c, holds); RETURN_THROWS(); }
	}
	inner = c.nd ? c.shape[c.nd - 1] : 1;
	rows = inner ? total / inner : 0;

	/* the destination */
	if (assign) {
		int dnd = c.nd + (c.ret.t == V_LIST ? 1 : 0);
		bool same = self->ndim == dnd;

		for (int j = 0; same && j < c.nd; j++) same = self->shape[j] == c.shape[j];
		if (same && c.ret.t == V_LIST) same = self->shape[c.nd] == kout;
		if (!same) {
			zend_value_error("Buffers\\Tensor::%s(): the result does not have the shape of the tensor it is assigned to", c.method);
			comp_free(&c, holds);
			RETURN_THROWS();
		}
		if (!self->writable) {
			zend_throw_error(NULL, "Buffers\\Tensor::%s(): the tensor is read-only", c.method);
			comp_free(&c, holds);
			RETURN_THROWS();
		}
		if (buf_is_packed(self->dt)) {
			zend_type_error("Buffers\\Tensor::%s(): cannot assign into the packed dtype %s", c.method, buf_dtypes[self->dt].name);
			comp_free(&c, holds);
			RETURN_THROWS();
		}
		if (!snapshot_captures(&c, self)) { comp_free(&c, holds); RETURN_THROWS(); }
		/* an input that shares memory with the destination, other than the destination itself at the same elements, is
		 * read from a copy; so is everything when an error could stop the evaluation part way: the destination is then
		 * written only after the whole result is known */
		for (int i = 1; i < c.nin; i++) {
			if (!c.in[i].scalar && c.in[i].v->buf == self->buf) direct = false;
		}
		for (int j = 0; j < kout; j++) {
			if (c.nodes[ev.out[j]].fails) direct = false;
		}
		ond = dnd;
		for (int j = 0; j < dnd; j++) osb[j] = self->sbytes[j];
	}
	if (!assign || !direct) {
		int64_t sz = buf_dtypes[odt].size, acc;

		if (total > INT64_MAX / sz / kout) { zend_value_error("The result is too large"); comp_free(&c, holds); RETURN_THROWS(); }
		bufobj = buf_buffer_new_host_uninit((uint64_t)(total * kout * sz));
		if (!bufobj) { comp_free(&c, holds); RETURN_THROWS(); }
		rbase = buf_buffer_from(bufobj)->base;
		if (!assign) {
			ond = c.nd + (c.ret.t == V_LIST ? 1 : 0);
			acc = sz;
			for (int j = ond - 1; j >= 0; j--) { osb[j] = acc; acc *= j == c.nd ? kout : (c.shape[j] ? c.shape[j] : 1); }
		}
	} else {
		rbase = self->data;
	}

	ev.c = &c;
	plan(&ev);
	ev.R = emalloc(sizeof(*ev.R) * (size_t)(ev.nreg ? ev.nreg : 1));
	ev.E = emalloc(sizeof(*ev.E) * (size_t)(ev.nereg ? ev.nereg : 1));
	ev.P = emalloc(sizeof(void *) * (size_t)(c.nn ? c.nn : 1));
	for (int o = 0; o < ev.norder; o++) ev.P[ev.order[o]] = ev.R[c.nodes[ev.order[o]].reg];
	fill_constants(&ev);
	{
		bool ok = true;
		int64_t sz = buf_dtypes[odt].size;
		uint8_t *q = rbase;

		for (int j = 0; j < c.nd; j++) ev.idx[j] = 0;
		for (int64_t row = 0; ok && row < rows; row++) {
			uint8_t *drow = NULL;

			for (int i = 0; i < c.nin; i++) {
				const uint8_t *p;

				if (c.in[i].scalar) continue;
				p = c.in[i].v->data;
				for (int j = 0; j + 1 < c.nd; j++) p += ev.idx[j] * c.in[i].sb[j];
				ev.rowp[i] = p;
			}
			if (assign && direct) {
				drow = self->data;
				for (int j = 0; j + 1 < c.nd; j++) drow += ev.idx[j] * osb[j];
			}
			for (ev.at = 0; ok && ev.at < inner; ev.at += EV_CHUNK) {
				int n = (int)(inner - ev.at < EV_CHUNK ? inner - ev.at : EV_CHUNK);

				bool into[MAX_OUT] = {false};

				/* each() with one output in the computation representation: the node that computes it writes the result's
				 * memory itself (a fresh buffer that nothing else reads) */
				if (!assign && kout == 1 && dt_in_place(odt)) {
					const enode *x = &c.nodes[ev.out[0]];

					if (x->kind != NK_INPUT && x->kind != NK_CONST && x->kind != NK_COORD && ((uintptr_t)q % (uintptr_t)sz) == 0) into[0] = true;
				}
				for (int o = 0; o < ev.norder; o++) {
					int id = ev.order[o];
					const enode *x = &c.nodes[id];

					if (x->kind != NK_INPUT && x->kind != NK_CONST) ev.P[id] = into[0] && id == ev.out[0] ? (void *)q : (void *)ev.R[x->reg];
					exec_node(&ev, x, n);
				}
				if (!check_errors(&ev, n)) { ok = false; break; }
				for (int j = 0; j < kout; j++) {
					if (assign && direct) {
						int64_t es = c.nd ? osb[c.nd - 1] : 0;
						uint8_t *d = drow + (c.nd ? ev.at * es : 0) + (c.ret.t == V_LIST ? j * osb[c.nd] : 0);
						int64_t dsz = buf_dtypes[self->dt].size;

						if (self->dt == odt) store_elems(odt, ev.P[ev.out[j]], d, es, n);
						else {
							uint8_t tmp[EV_CHUNK * 8];

							store_elems(odt, ev.P[ev.out[j]], tmp, sz, n);
							buffers_convert(odt, tmp, 1, self->dt, d, es / dsz, n);
						}
					} else if (!into[j]) {
						store_elems(odt, ev.P[ev.out[j]], q + j * sz, sz * kout, n);
					}
				}
				if (!(assign && direct)) q += (int64_t)n * kout * sz;
			}
			for (int j = c.nd - 2; j >= 0; j--) {
				if (++ev.idx[j] < c.shape[j]) break;
				ev.idx[j] = 0;
			}
		}
		efree(ev.R);
		efree(ev.E);
		efree(ev.P);
		efree(ev.order);
		if (!ok) {
			if (bufobj) OBJ_RELEASE(bufobj);
			comp_free(&c, holds);
			RETURN_THROWS();
		}
	}
	if (!assign) {
		int64_t shape[BUF_MAX_NDIM + 1];

		if (ond > BUF_MAX_NDIM) {
			OBJ_RELEASE(bufobj);
			comp_free(&c, holds);
			zend_value_error("Buffers\\Tensor::each(): the result would have more than %d axes", BUF_MAX_NDIM);
			RETURN_THROWS();
		}
		for (int j = 0; j < c.nd; j++) shape[j] = c.shape[j];
		if (c.ret.t == V_LIST) shape[c.nd] = kout;
		RETVAL_OBJ(buf_view_new(bufobj, odt, ond, shape, osb, rbase, true, false));
		OBJ_RELEASE(bufobj);
	} else {
		if (!direct) {
			/* the whole result is known: copy it into the destination, converting to its dtype */
			int64_t csb[BUF_MAX_NDIM + 1], acc = buf_dtypes[odt].size, cnt = 1, dsz = buf_dtypes[self->dt].size;
			int64_t idx[BUF_MAX_NDIM + 1];
			int nd = self->ndim;

			for (int j = nd - 1; j >= 0; j--) { csb[j] = acc; acc *= self->shape[j] ? self->shape[j] : 1; }
			for (int j = 0; j < nd; j++) { cnt *= self->shape[j]; idx[j] = 0; }
			if (cnt > 0) {
				int64_t in = nd ? self->shape[nd - 1] : 1, rws = in ? cnt / in : 0;

				for (int64_t r = 0; r < rws; r++) {
					const uint8_t *s = rbase;
					uint8_t *d = self->data;

					for (int j = 0; j + 1 < nd; j++) { s += idx[j] * csb[j]; d += idx[j] * self->sbytes[j]; }
					if (odt == self->dt) {
						int64_t ds = nd ? self->sbytes[nd - 1] : 0;

						for (int64_t k = 0; k < in; k++) memcpy(d + k * ds, s + k * dsz, (size_t)dsz);
					} else {
						buffers_convert(odt, s, 1, self->dt, d, nd ? self->sbytes[nd - 1] / dsz : 1, in);
					}
					for (int j = nd - 2; j >= 0; j--) {
						if (++idx[j] < self->shape[j]) break;
						idx[j] = 0;
					}
				}
			}
			OBJ_RELEASE(bufobj);
		}
		RETVAL_OBJ_COPY(Z_OBJ_P(ZEND_THIS));
	}
	comp_free(&c, holds);
}

static ZEND_NAMED_FUNCTION(buf_tensor_each) { each_impl(INTERNAL_FUNCTION_PARAM_PASSTHRU, false); }
static ZEND_NAMED_FUNCTION(buf_tensor_assign) { each_impl(INTERNAL_FUNCTION_PARAM_PASSTHRU, true); }

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_buf_each, 0, 1, Buffers\\Tensor, 0)
	ZEND_ARG_OBJ_INFO(0, fn, Closure, 0)
	ZEND_ARG_VARIADIC_TYPE_INFO(0, inputs, IS_MIXED, 0)
ZEND_END_ARG_INFO()

static const zend_function_entry buf_extras_tensor_methods[] = {
	ZEND_RAW_FENTRY("each", buf_tensor_each, arginfo_buf_each, ZEND_ACC_PUBLIC, NULL, NULL)
	ZEND_RAW_FENTRY("assign", buf_tensor_assign, arginfo_buf_each, ZEND_ACC_PUBLIC, NULL, NULL)
	ZEND_FE_END
};

void buf_extras_minit(void)
{
	zend_register_functions(buf_ce_tensor, buf_extras_tensor_methods, &buf_ce_tensor->function_table, MODULE_PERSISTENT);
}
