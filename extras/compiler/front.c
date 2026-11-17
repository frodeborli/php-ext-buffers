/* The front end: a closure's opcodes compiled to CIR.
 *
 * The closure is never called. Its opcodes are split into basic blocks (PHP's own CFG builder) and processed in reverse
 * post-order with an abstract state: the value of every variable and temporary (a register of a dtype, a constant, a view
 * of a tensor, a fixed-length array...). Where paths meet, the values that differ become parameters of the block. A loop
 * header takes the variables the loop changes as parameters; which ones, and their dtypes, are found by compiling again
 * until nothing changes (rounds). Functions and static methods written in PHP are compiled into their caller.
 *
 * The rules are those of Tensor arithmetic (docs/arithmetic.md): typed values have a dtype; PHP ints, floats and bools
 * are weak and take the other operand's dtype; integers wrap. Anything outside the supported subset throws before the
 * code runs. */
#include "front.h"
#include "zend_closures.h"
#include "zend_exceptions.h"
#include "zend_interfaces.h"
#include "zend_vm_opcodes.h"
#include "zend_constants.h"
#include "Optimizer/zend_cfg.h"
#if PHP_VERSION_ID >= 80400
# include "zend_frameless_function.h"
#endif

#define FR_DEPTH 8
#define FR_ARGS 16
#define MAX_INLINE 8
#define MAX_ROUNDS 32

enum { FN_ABS, FN_FLOOR, FN_CEIL, FN_ROUND, FN_SQRT, FN_ISNAN, FN_ISFINITE, FN_ISINF, FN_MIN, FN_MAX, FN_INTDIV, FN_FMOD, FN_CNT, FN_ORD, FN_STRLEN,
	FN_SIN, FN_COS, FN_TAN, FN_ASIN, FN_ACOS, FN_ATAN, FN_SINH, FN_COSH, FN_TANH, FN_ASINH, FN_ACOSH, FN_ATANH,
	FN_EXP, FN_EXPM1, FN_LOG10, FN_LOG2, FN_LOG1P, FN_DEG2RAD, FN_RAD2DEG,
	FN_LOG, FN_ATAN2, FN_HYPOT, FN_FDIV, FN_POW, FN_PI, FN_COUNT };
static const char *const fn_names[FN_COUNT] = {
	"abs", "floor", "ceil", "round", "sqrt", "is_nan", "is_finite", "is_infinite", "min", "max", "intdiv", "fmod", "count", "ord", "strlen",
	"sin", "cos", "tan", "asin", "acos", "atan", "sinh", "cosh", "tanh", "asinh", "acosh", "atanh",
	"exp", "expm1", "log10", "", "log1p", "deg2rad", "rad2deg",
	"log", "atan2", "hypot", "fdiv", "pow", "pi"
};

/* ---- values of the abstract state ---- */

enum {
	V_UNDEF,     /* not assigned */
	V_BAD,       /* paths that meet give it values that cannot be combined: an error only if read */
	V_NUM,       /* dt: a dtype, or BUFFERS_SCALAR_* (weak); r: its register, or -1 for the constant k */
	V_VIEW,      /* a view of tensor binding n after a indices; r: the byte offset (-1: 0) */
	V_ELEM,      /* an element of binding n to be written (FETCH_DIM_W/RW); r: its byte offset */
	V_LIST,      /* a fixed-length array: a values from c->sv[n] */
	V_SHAPE,     /* $tensor->shape of binding n after a indices */
	V_THIS,      /* the closure's $this */
	V_CHAR,      /* $string[$i]: a one-byte string, of which only ord() is taken; r: the byte (UInt8) */
	V_NULL       /* null: the value of a return without a value */
};

typedef struct {
	uint8_t t;
	int16_t dt;
	int32_t r, n, a;
	cc_val k;
} sval;

typedef struct {
	int depth;
	int fid[FR_DEPTH], nargs[FR_DEPTH];
	sval cell[1];   /* nslots, then FR_DEPTH * FR_ARGS arguments of the calls being made */
} pstate;

/* an edge into a join, waiting for the join's block: from block blk, edge e (T_JMP/T_BR), with its state */
typedef struct { int blk, e; pstate *s; } incoming;

/* the loop-carried values of one loop header, kept between rounds */
typedef struct {
	int nvals;
	uint8_t *carried;
	sval *tmpl;     /* carried: V_NUM (dt) or V_VIEW (n, a) */
} hdrinfo;

typedef struct {
	int src;             /* index in k->src */
	int dt, ndim;
	int env;
} binding;

typedef struct comp {
	cc_kernel *k;
	cc_func *f;
	int cur;                     /* the block being filled; -1: the code here is not reached */
	/* the function whose opcodes are being read */
	const zend_function *fn;
	const zend_op_array *oa;
	int nslots, ncells;
	uint32_t line;
	const zend_function *chain[MAX_INLINE];
	int depth;
	/* the closure */
	const zend_function *top;
	zend_object *closure, *this_obj;
	zend_class_entry *scope;
	zval *args;
	uint32_t nargs;
	/* tensors and other values of the environment */
	binding bind[64];
	int nbind;
	int envreg[256];             /* register of an env slot, made in block 0; -1 none */
	/* values of lists */
	sval *sv;
	int nsv, csv;
	/* rounds */
	hdrinfo *hdr;
	int nhdr, chdr, hvisit;
	bool redo;
	/* user functions called (fid FN_COUNT + i) */
	const zend_function *ufn[64];
	int nufn;
} comp;

static bool is_weak(int dt) { return dt <= BUFFERS_SCALAR_BOOL && dt >= BUFFERS_SCALAR_FLOAT; }

/* the dtype a weak value is computed in */
static int nat(int dt)
{
	switch (dt) {
	case BUFFERS_SCALAR_INT: return CC_PHPINT;
	case BUFFERS_SCALAR_FLOAT: return BUF_T_FLOAT64;
	case BUFFERS_SCALAR_BOOL: return BUF_T_BOOL;
	}
	return dt;
}

static const char *tname(int dt)
{
	switch (dt) {
	case BUFFERS_SCALAR_INT: return "int";
	case BUFFERS_SCALAR_FLOAT: return "float";
	case BUFFERS_SCALAR_BOOL: return "bool";
	}
	return dt >= 0 ? buf_dtypes[dt].name : "?";
}

static sval s_bad(void) { sval v; memset(&v, 0, sizeof v); v.t = V_BAD; v.r = -1; return v; }
static sval s_num(int dt, int r) { sval v; memset(&v, 0, sizeof v); v.t = V_NUM; v.dt = (int16_t)dt; v.r = r; return v; }
static sval s_const(int dt, cc_val k) { sval v = s_num(dt, -1); v.k = k; return v; }
static sval s_int(int64_t l) { cc_val k; k.s = l; return s_const(BUFFERS_SCALAR_INT, k); }

/* ---- errors while compiling ---- */

static sval unsupported(comp *c, const char *fmt, ...)
{
	va_list ap;
	char *msg;

	va_start(ap, fmt);
	zend_vspprintf(&msg, 0, fmt, ap);
	va_end(ap);
	if (!EG(exception)) zend_throw_exception_ex(zend_ce_value_error, 0, "Buffers\\Compiled: %s is not supported (line %u)", msg, c->line);
	efree(msg);
	return s_bad();
}

static sval type_err(comp *c, const char *fmt, ...)
{
	va_list ap;
	char *msg;

	va_start(ap, fmt);
	zend_vspprintf(&msg, 0, fmt, ap);
	va_end(ap);
	if (!EG(exception)) zend_type_error("Buffers\\Compiled: %s (line %u)", msg, c->line);
	efree(msg);
	return s_bad();
}

#define FAILED (EG(exception) != NULL)

/* ---- emitting ---- */

static cc_ins *emit(comp *c, int op) { return cc_emit(c->f, c->cur, op); }

static int reg(comp *c, int dt) { return cc_reg(c->f, dt); }

/* a register holding v (a constant is loaded) */
static int materialize(comp *c, sval v)
{
	cc_ins *i;

	if (v.r >= 0) return v.r;
	i = emit(c, O_CONST);
	i->r = reg(c, nat(v.dt));
	i->k = v.k;
	return i->r;
}

/* env slot as a register of dtype dt, loaded once in block 0 */
static int env_reg(comp *c, int slot, int dt)
{
	if (c->envreg[slot] < 0) {
		cc_ins *i = cc_emit(c->f, 0, O_ENV);

		i->r = reg(c, dt);
		i->k.i = (uint64_t)slot;
		c->envreg[slot] = i->r;
	}
	return c->envreg[slot];
}

static void terminate(comp *c, int term)
{
	c->f->blk[c->cur].term = (uint8_t)term;
}

/* the code from here on runs only if ok (a Bool register) is true; otherwise the call fails with err */
static void check(comp *c, int ok, int err, int ea, int eb, int eaux)
{
	int cont = cc_block_new(c->f), fail = cc_block_new(c->f);
	cc_block *b = &c->f->blk[c->cur];

	b->term = T_BR;
	b->cond = ok;
	b->e[0].to = cont;
	b->e[1].to = fail;
	b = &c->f->blk[fail];
	b->term = T_ERR;
	b->err = err;
	b->ea = ea;
	b->eb = eb;
	b->eaux = eaux;
	c->cur = cont;
}

/* the call fails here, always: the rest of the block is not reached */
static sval fail_now(comp *c, int err, sval a, sval b, int eaux)
{
	int ea = a.t == V_NUM ? materialize(c, a) : -1, eb = b.t == V_NUM ? materialize(c, b) : -1;
	cc_block *bl = &c->f->blk[c->cur];

	bl->term = T_ERR;
	bl->err = err;
	bl->ea = ea;
	bl->eb = eb;
	bl->eaux = eaux;
	c->cur = -1;
	return s_bad();
}

#define DEAD (c->cur < 0)

static int cmp_reg(comp *c, int which, int dt, int a, int b)
{
	cc_ins *i = emit(c, O_CMP);

	i->sub = (uint8_t)which;
	i->adt = (int16_t)dt;
	i->a = a;
	i->b = b;
	i->r = reg(c, BUF_T_BOOL);
	return i->r;
}

static int const_reg(comp *c, int dt, uint64_t bits)
{
	cc_ins *i = emit(c, O_CONST);

	i->r = reg(c, dt);
	i->k.i = bits;
	return i->r;
}

static int and_reg(comp *c, int a, int b)
{
	cc_ins *i = emit(c, O_IBIN);

	i->sub = ZEND_BW_AND;
	i->dt = BUF_T_BOOL;
	i->a = a;
	i->b = b;
	i->r = reg(c, BUF_T_BOOL);
	return i->r;
}

/* ---- conversions ---- */

static bool int_fits(int64_t l, int dt)
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

/* v (a register of dtype from) converted to dt */
static sval conv(comp *c, sval v, int from, int to)
{
	cc_ins *i;

	if (from == to) { v.dt = (int16_t)to; return v; }
	if (v.r < 0) return s_const(to, cc_conv(from, to, v.k));
	i = emit(c, O_CONV);
	i->adt = (int16_t)from;
	i->dt = (int16_t)to;
	i->a = v.r;
	i->r = reg(c, to);
	return s_num(to, i->r);
}

/* a numeric value as a value of the concrete dtype dt, as an operand meets it: a weak int must fit an integer dtype
 * (ValueError when it runs), a weak float takes only float dtypes, a bool only Bool */
static sval to_dt(comp *c, sval v, int dt)
{
	if (v.t != V_NUM) return v.t == V_BAD ? v : type_err(c, "this value is not a number");
	if (!is_weak(v.dt)) return conv(c, v, v.dt, dt);
	switch (v.dt) {
	case BUFFERS_SCALAR_BOOL:
		if (dt != BUF_T_BOOL) return type_err(c, "a bool combines only with Bool, not %s", tname(dt));
		return v.r >= 0 ? s_num(dt, v.r) : s_const(dt, v.k);
	case BUFFERS_SCALAR_FLOAT:
		if (!cc_float(dt)) return type_err(c, "a float combines only with float dtypes, not %s", tname(dt));
		return conv(c, v, BUF_T_FLOAT64, dt);
	}
	/* a PHP int */
	if (dt == BUF_T_BOOL) return type_err(c, "Bool combines only with Bool, not int");
	if (cc_int(dt) && dt != CC_PHPINT && dt != BUF_T_INT64) {
		if (v.r < 0) {
			if (!int_fits(v.k.s, dt)) return fail_now(c, CC_E_FIT, v, s_bad(), dt);
		} else {
			/* lo <= v <= hi in PHP's int */
			int64_t lo, hi;

			switch (dt) {
			case BUF_T_INT8: lo = INT8_MIN; hi = INT8_MAX; break;
			case BUF_T_INT16: lo = INT16_MIN; hi = INT16_MAX; break;
			case BUF_T_INT32: lo = INT32_MIN; hi = INT32_MAX; break;
			case BUF_T_UINT8: lo = 0; hi = UINT8_MAX; break;
			case BUF_T_UINT16: lo = 0; hi = UINT16_MAX; break;
			case BUF_T_UINT32: lo = 0; hi = (int64_t)UINT32_MAX; break;
			default: lo = 0; hi = ZEND_LONG_MAX; break;   /* UInt64 */
			}
			if (CC_PHPINT == BUF_T_INT32 && hi > INT32_MAX) hi = INT32_MAX;
			{
				int ge = cmp_reg(c, CC_GE, CC_PHPINT, v.r, const_reg(c, CC_PHPINT, (uint64_t)lo));
				int le = cmp_reg(c, CC_LE, CC_PHPINT, v.r, const_reg(c, CC_PHPINT, (uint64_t)hi));

				check(c, and_reg(c, ge, le), CC_E_FIT, v.r, -1, dt);
			}
		}
	}
	return conv(c, v, CC_PHPINT, dt);
}

/* ---- operators ---- */

static const char *op_symbol(int op)
{
	switch (op) {
	case ZEND_ADD: return "+"; case ZEND_SUB: return "-"; case ZEND_MUL: return "*"; case ZEND_DIV: return "/";
	case ZEND_MOD: return "%"; case ZEND_POW: return "**"; case ZEND_SL: return "<<"; case ZEND_SR: return ">>";
	case ZEND_BW_AND: return "&"; case ZEND_BW_OR: return "|"; case ZEND_BW_XOR: return "^"; case ZEND_BW_NOT: return "~";
	}
	return "?";
}

static bool is_bitop(int op) { return op == ZEND_BW_AND || op == ZEND_BW_OR || op == ZEND_BW_XOR; }
static bool is_shift(int op) { return op == ZEND_SL || op == ZEND_SR; }

/* the result type of a op b: a dtype, a weak type, or -1 */
static int result_type(int op, int da, int db)
{
	bool wa = is_weak(da), wb = is_weak(db);

	if (wa && wb) {
		if (da == BUFFERS_SCALAR_BOOL || db == BUFFERS_SCALAR_BOOL) {
			return da == db && is_bitop(op) ? BUFFERS_SCALAR_BOOL : -1;
		}
		if (is_bitop(op) || is_shift(op)) return da == BUFFERS_SCALAR_INT && db == BUFFERS_SCALAR_INT ? BUFFERS_SCALAR_INT : -1;
		if (op == ZEND_DIV || da == BUFFERS_SCALAR_FLOAT || db == BUFFERS_SCALAR_FLOAT) return BUFFERS_SCALAR_FLOAT;
		return BUFFERS_SCALAR_INT;
	}
	return buf_dtype_promote(da, db, (uint8_t)op);
}

/* a op b for numbers; folded when both are constants */
static sval bin(comp *c, int op, sval a, sval b)
{
	int rt, cdt;
	cc_ins *i;

	if (a.t == V_BAD || b.t == V_BAD) return s_bad();
	if (a.t != V_NUM || b.t != V_NUM) return unsupported(c, "%s on this value", op_symbol(op));
	rt = result_type(op, a.dt, b.dt);
	if (rt == -1) {
		return type_err(c, "Unsupported operand types for %s: %s and %s", op_symbol(op), tname(a.dt), tname(b.dt));
	}
	cdt = nat(rt);
	a = is_weak(a.dt) && is_weak(rt) ? conv(c, a, nat(a.dt), cdt) : to_dt(c, a, cdt);
	if (FAILED || DEAD) return s_bad();
	b = is_weak(b.dt) && is_weak(rt) ? conv(c, b, nat(b.dt), cdt) : to_dt(c, b, cdt);
	if (FAILED || DEAD) return s_bad();
	if (cc_mt(cdt) == CC_MI) {
		bool sgn = cc_signed(cdt);

		/* the checks of % ** << >> (on constants: now) */
		if (op == ZEND_MOD || ((op == ZEND_POW || is_shift(op)) && sgn)) {
			int err = op == ZEND_MOD ? CC_E_MOD0 : op == ZEND_POW ? CC_E_POWNEG : CC_E_SHIFT;

			if (b.r < 0) {
				if (op == ZEND_MOD ? b.k.i == 0 : b.k.s < 0) return fail_now(c, err, s_bad(), s_bad(), 0);
			} else {
				int ok = op == ZEND_MOD ? cmp_reg(c, CC_NE, cdt, b.r, const_reg(c, cdt, 0)) : cmp_reg(c, CC_GE, cdt, b.r, const_reg(c, cdt, 0));

				check(c, ok, err, -1, -1, 0);
			}
		}
		if (a.r < 0 && b.r < 0) {
			cc_val k;

			k.i = cc_ibin(op, cdt, a.k.i, b.k.i);
			return s_const(rt, k);
		}
	} else {
		if (a.r < 0 && b.r < 0) {
			cc_val k;

			if (cc_mt(cdt) == CC_MD) k.d = cc_dbin(op, a.k.d, b.k.d);
			else k.f = cc_fbin(op, cdt, a.k.f, b.k.f);
			return s_const(rt, k);
		}
	}
	{
		int ra = materialize(c, a), rb = materialize(c, b);

		i = emit(c, cc_mt(cdt) == CC_MI ? O_IBIN : O_FBIN);
		i->sub = (uint8_t)op;
		i->dt = (int16_t)cdt;
		i->a = ra;
		i->b = rb;
		i->r = reg(c, cdt);
		return s_num(rt, i->r);
	}
}

/* the type two values take where they meet (comparisons, min(), max(), joins of paths) */
static int common(comp *c, int da, int db, bool report)
{
	int rt;

	if (da == db) return da;
	if ((da == BUF_T_BOOL || da == BUFFERS_SCALAR_BOOL) && (db == BUF_T_BOOL || db == BUFFERS_SCALAR_BOOL)) return BUF_T_BOOL;
	if (da == BUF_T_BOOL || da == BUFFERS_SCALAR_BOOL || db == BUF_T_BOOL || db == BUFFERS_SCALAR_BOOL) {
		if (report) type_err(c, "%s and %s have no common dtype", tname(da), tname(db));
		return -1;
	}
	if (is_weak(da) && is_weak(db)) return BUFFERS_SCALAR_FLOAT;   /* int and float */
	rt = buf_dtype_promote(da, db, ZEND_ADD);
	if (rt < 0 && report) type_err(c, "%s and %s have no common dtype", tname(da), tname(db));
	return rt;
}

/* v as a value of type t (from common()) */
static sval to_type(comp *c, sval v, int t)
{
	if (v.dt == t) return v;
	if (is_weak(t)) {   /* a weak int as a weak float */
		sval r = conv(c, v, nat(v.dt), nat(t));

		r.dt = (int16_t)t;
		return r;
	}
	return to_dt(c, v, t);
}

static sval cmp(comp *c, int which, sval a, sval b)
{
	int t, r;

	if (a.t == V_BAD || b.t == V_BAD) return s_bad();
	if (a.t != V_NUM || b.t != V_NUM) return unsupported(c, "comparing this value");
	t = common(c, a.dt, b.dt, true);
	if (t == -1) return s_bad();
	a = to_type(c, a, t);
	if (FAILED || DEAD) return s_bad();
	b = to_type(c, b, t);
	if (FAILED || DEAD) return s_bad();
	if (a.r < 0 && b.r < 0) {
		cc_val k;

		k.i = cc_cmp(which, nat(t), a.k, b.k);
		return s_const(is_weak(t) ? BUFFERS_SCALAR_BOOL : BUF_T_BOOL, k);
	}
	r = cmp_reg(c, which, nat(t), materialize(c, a), materialize(c, b));
	return s_num(BUF_T_BOOL, r);
}

static sval tobool(comp *c, sval v)
{
	int dt;

	if (v.t == V_BAD) return v;
	if (v.t != V_NUM) return unsupported(c, "this value as a condition");
	if (v.dt == BUF_T_BOOL || v.dt == BUFFERS_SCALAR_BOOL) return v;
	dt = nat(v.dt);
	if (v.r < 0) {
		cc_val z, k;

		z.i = 0;
		if (cc_mt(dt) == CC_MF) z.f = 0;
		if (cc_mt(dt) == CC_MD) z.d = 0;
		k.i = cc_cmp(CC_NE, dt, v.k, z);
		return s_const(is_weak(v.dt) ? BUFFERS_SCALAR_BOOL : BUF_T_BOOL, k);
	}
	{
		cc_val z;
		int zr;
		cc_ins *i;

		z.i = 0;
		i = emit(c, O_CONST);
		i->r = zr = reg(c, dt);
		i->k = z;
		if (cc_mt(dt) == CC_MF) i->k.f = 0;
		if (cc_mt(dt) == CC_MD) i->k.d = 0;
		return s_num(is_weak(v.dt) ? BUFFERS_SCALAR_BOOL : BUF_T_BOOL, cmp_reg(c, CC_NE, dt, v.r, zr));
	}
}

static sval lnot(comp *c, sval v)
{
	cc_val one;

	v = tobool(c, v);
	if (v.t != V_NUM) return v;
	one.i = 1;
	return bin(c, ZEND_BW_XOR, v, s_const(v.dt, one));
}

/* ---- tensors ---- */

/* the element of binding bi at byte offset off (a register, -1: 0): its address */
static int elem_addr(comp *c, int bi, int off)
{
	binding *b = &c->bind[bi];
	int base = env_reg(c, b->env, CC_DT_PTR);
	cc_ins *i;

	if (off < 0) return base;
	i = emit(c, O_PADD);   /* (env_reg() emits into block 0) */
	i->a = base;
	i->b = off;
	i->r = reg(c, CC_DT_PTR);
	return i->r;
}

/* $view[$ix]: the offset of the next axis, after checking the index */
static sval index_view(comp *c, sval view, sval ix, bool lvalue)
{
	binding *b = &c->bind[view.n];
	int axis = view.a, len, stride, off;
	cc_ins *i;

	if (ix.t == V_BAD) return ix;
	if (ix.t != V_NUM || !(ix.dt == BUFFERS_SCALAR_INT || cc_int(ix.dt))) {
		return type_err(c, "a tensor index must be an integer, not %s", ix.t == V_NUM ? tname(ix.dt) : "this value");
	}
	if (axis >= b->ndim) return unsupported(c, "indexing a tensor element");
	if (!is_weak(ix.dt)) ix = conv(c, ix, ix.dt, CC_PHPINT);
	ix.dt = BUFFERS_SCALAR_INT;
	len = env_reg(c, b->env + 1 + axis, CC_PHPINT);
	stride = env_reg(c, b->env + 1 + b->ndim + axis, BUF_T_INT64);
	/* 0 <= ix < len (unsigned) */
	{
		int ixr = materialize(c, ix);
		cc_ins *u;
		int ok;

		u = emit(c, O_CMP);   /* unsigned compare: the index as UInt64 */
		u->sub = CC_LT;
		u->adt = CC_PHPINT == BUF_T_INT64 ? BUF_T_UINT64 : BUF_T_UINT32;
		u->a = ixr;
		u->b = len;
		u->r = ok = reg(c, BUF_T_BOOL);
		check(c, ok, CC_E_RANGE, ixr, len, 0);
		/* off += ix * stride */
		if (CC_PHPINT != BUF_T_INT64) {
			i = emit(c, O_CONV);
			i->adt = CC_PHPINT;
			i->dt = BUF_T_INT64;
			i->a = ixr;
			i->r = reg(c, BUF_T_INT64);
			ixr = i->r;
		}
		i = emit(c, O_IBIN);
		i->sub = ZEND_MUL;
		i->dt = BUF_T_INT64;
		i->a = ixr;
		i->b = stride;
		i->r = reg(c, BUF_T_INT64);
		off = i->r;
		if (view.r >= 0) {
			i = emit(c, O_IBIN);
			i->sub = ZEND_ADD;
			i->dt = BUF_T_INT64;
			i->a = view.r;
			i->b = off;
			i->r = reg(c, BUF_T_INT64);
			off = i->r;
		}
	}
	{
		sval r;

		memset(&r, 0, sizeof r);
		r.t = axis + 1 == b->ndim ? (lvalue ? V_ELEM : V_NUM) : V_VIEW;
		r.n = view.n;
		r.a = axis + 1;
		r.r = off;
		if (r.t == V_NUM) {
			int addr = elem_addr(c, view.n, off);

			i = emit(c, O_LOAD);
			i->dt = (int16_t)b->dt;
			i->a = addr;
			i->r = reg(c, b->dt);
			if (c->k->src[b->src].str) {   /* a one-byte string */
				r = s_num(b->dt, i->r);
				r.t = V_CHAR;
				return r;
			}
			return s_num(b->dt, i->r);
		}
		return r;
	}
}

static void store_elem(comp *c, sval el, sval v)
{
	binding *b = &c->bind[el.n];
	cc_ins *i;

	if (v.t != V_NUM) { unsupported(c, "storing this value in a tensor"); return; }
	if (c->k->src[b->src].arr) { type_err(c, "writing into a PHP array: it is read as a copy; use a Buffers\\Tensor"); return; }
	if (c->k->src[b->src].str) { type_err(c, "writing into a string: strings are read-only"); return; }
	/* as convert() converts */
	v = conv(c, v, nat(v.dt), b->dt);
	c->k->src[b->src].written = 1;
	{
		int addr = elem_addr(c, el.n, el.r), val = materialize(c, v);

		i = emit(c, O_STORE);
		i->dt = (int16_t)b->dt;
		i->a = addr;
		i->b = val;
	}
}

static sval load_elem(comp *c, sval el)
{
	binding *b = &c->bind[el.n];
	int addr = elem_addr(c, el.n, el.r);
	cc_ins *i = emit(c, O_LOAD);

	i->dt = (int16_t)b->dt;
	i->a = addr;
	i->r = reg(c, b->dt);
	return s_num(b->dt, i->r);
}

/* ---- the environment: arguments, captured variables, properties ---- */

static int add_src(comp *c, int kind, uint32_t index)
{
	cc_kernel *k = c->k;

	k->src = erealloc(k->src, sizeof(cc_src) * (size_t)(k->nsrc + 1));
	memset(&k->src[k->nsrc], 0, sizeof(cc_src));
	k->src[k->nsrc].kind = (uint8_t)kind;
	k->src[k->nsrc].index = index;
	return k->nsrc++;
}

/* a PHP value of the environment as a value of the code: weak scalars (read at every call), tensors */
static sval env_value(comp *c, int kind, uint32_t index, zval *z, const char *what)
{
	int si;
	cc_src *s;

	ZVAL_DEREF(z);
	switch (Z_TYPE_P(z)) {
	case IS_LONG: case IS_DOUBLE: case IS_TRUE: case IS_FALSE: {
		int t = Z_TYPE_P(z) == IS_LONG ? BUFFERS_SCALAR_INT : Z_TYPE_P(z) == IS_DOUBLE ? BUFFERS_SCALAR_FLOAT : BUFFERS_SCALAR_BOOL;

		if (kind == SRC_ARG) {
			cc_ins *i = cc_emit(c->f, 0, O_ARG);

			i->r = reg(c, nat(t));
			i->k.i = index;
			si = add_src(c, kind, index);
			c->k->src[si].dt = (int16_t)t;
			c->k->src[si].env = -1;
			return s_num(t, i->r);
		}
		si = add_src(c, kind, index);
		s = &c->k->src[si];
		s->dt = (int16_t)t;
		s->env = c->k->nenv++;
		return s_num(t, env_reg(c, s->env, nat(t)));
	}
	case IS_NULL: {
		sval r = s_bad();

		si = add_src(c, kind, index);
		c->k->src[si].dt = CC_T_NULL;
		c->k->src[si].env = -1;
		r.t = V_NULL;
		return r;
	}
	case IS_STRING: {   /* its bytes, read-only */
		sval r;

		if (c->nbind == 64) return unsupported(c, "more than 64 tensors and strings");
		si = add_src(c, kind, index);
		s = &c->k->src[si];
		s->tensor = 1;
		s->str = 1;
		s->dt = BUF_T_UINT8;
		s->ndim = 1;
		s->env = c->k->nenv;
		c->k->nenv += 3;
		if (c->k->nenv > 256) return unsupported(c, "this many tensor axes");
		c->bind[c->nbind].src = si;
		c->bind[c->nbind].dt = BUF_T_UINT8;
		c->bind[c->nbind].ndim = 1;
		c->bind[c->nbind].env = s->env;
		memset(&r, 0, sizeof r);
		r.t = V_VIEW;
		r.n = c->nbind++;
		r.r = -1;
		return r;
	}
	case IS_ARRAY: case IS_OBJECT: {
		zval t;
		buf_view *v;
		bool arr = Z_TYPE_P(z) == IS_ARRAY;

		ZVAL_UNDEF(&t);
		if (arr) {
			zend_call_method_with_1_params(NULL, buf_ce_tensor, NULL, "fromarray", &t, z);
			if (FAILED) return s_bad();
		} else if (Z_OBJCE_P(z) == buf_ce_tensor) {
			ZVAL_COPY(&t, z);
		} else if (instanceof_function(Z_OBJCE_P(z), buf_ce_tensor_agg)) {
			zend_call_method_with_0_params(Z_OBJ_P(z), Z_OBJCE_P(z), NULL, "gettensor", &t);
			if (FAILED) return s_bad();
			if (Z_TYPE(t) != IS_OBJECT || Z_OBJCE(t) != buf_ce_tensor) {
				zval_ptr_dtor(&t);
				return type_err(c, "%s::getTensor() must return a Buffers\\Tensor", ZSTR_VAL(Z_OBJCE_P(z)->name));
			}
		} else {
			return unsupported(c, "%s of class %s", what, ZSTR_VAL(Z_OBJCE_P(z)->name));
		}
		v = buf_view_from(Z_OBJ(t));
		{
			int dt = v->dt, ndim = v->ndim;
			bool ok = cc_arith(dt);

			zval_ptr_dtor(&t);
			if (!ok) return type_err(c, "%s has dtype %s, which has no arithmetic", what, buf_dtypes[dt].name);
			if (c->nbind == 64) return unsupported(c, "more than 64 tensors");
			si = add_src(c, kind, index);
			s = &c->k->src[si];
			s->tensor = 1;
			s->arr = arr;
			s->dt = (int16_t)dt;
			s->ndim = (uint8_t)ndim;
			s->env = c->k->nenv;
			c->k->nenv += 1 + 2 * ndim;
			if (c->k->nenv > 256) return unsupported(c, "this many tensor axes");
			c->bind[c->nbind].src = si;
			c->bind[c->nbind].dt = dt;
			c->bind[c->nbind].ndim = ndim;
			c->bind[c->nbind].env = s->env;
			{
				sval r;

				memset(&r, 0, sizeof r);
				r.t = V_VIEW;
				r.n = c->nbind++;
				r.r = -1;
				if (ndim == 0) return load_elem(c, (sval){V_ELEM, 0, -1, r.n, 0, {0}});
				return r;
			}
		}
	}
	}
	return unsupported(c, "%s of type %s", what, zend_zval_type_name(z));
}

/* ---- lists ---- */

static int sv_add(comp *c, sval v)
{
	if (c->nsv == c->csv) {
		c->csv = c->csv ? c->csv * 2 : 64;
		c->sv = erealloc(c->sv, sizeof(sval) * (size_t)c->csv);
	}
	c->sv[c->nsv] = v;
	return c->nsv++;
}

/* ---- states and joins ---- */

static pstate *st_new(comp *c)
{
	pstate *s = ecalloc(1, sizeof(pstate) + sizeof(sval) * (size_t)c->ncells);

	for (int i = 0; i < c->ncells; i++) s->cell[i].r = -1;
	return s;
}

static pstate *st_copy(comp *c, const pstate *s)
{
	pstate *d = emalloc(sizeof(pstate) + sizeof(sval) * (size_t)c->ncells);

	memcpy(d, s, sizeof(pstate) + sizeof(sval) * (size_t)c->ncells);
	return d;
}

static bool sval_eq(comp *c, const sval *a, const sval *b)
{
	if (a->t != b->t) return false;
	switch (a->t) {
	case V_UNDEF: case V_BAD: case V_NULL: case V_THIS: return true;
	case V_NUM: return a->dt == b->dt && a->r == b->r && (a->r >= 0 || a->k.i == b->k.i);
	case V_VIEW: case V_ELEM: case V_SHAPE: return a->n == b->n && a->a == b->a && a->r == b->r;
	case V_LIST:
		if (a->a != b->a) return false;
		if (a->n == b->n) return true;
		for (int i = 0; i < a->a; i++) {
			if (!sval_eq(c, &c->sv[a->n + i], &c->sv[b->n + i])) return false;
		}
		return true;
	}
	return false;
}

/* the type of the join of numeric values (common() without errors): -1 if they cannot meet */
static int join_type(comp *c, const sval *const *v, int n)
{
	int t = v[0]->dt;

	for (int i = 1; i < n && t != -1; i++) t = common(c, t, v[i]->dt, false);
	return t;
}

/* parameters of a join: each is a register of the target block, and how every incoming value is passed to it */
typedef struct { int reg, kind, t; } jparam;   /* kind 0: a number of type t; 1: a byte offset (Int64) */

typedef struct {
	jparam *p;
	int np, cp;
} jplan;

static int plan_add(jplan *pl, int reg, int kind, int t)
{
	if (pl->np == pl->cp) {
		pl->cp = pl->cp ? pl->cp * 2 : 16;
		pl->p = erealloc(pl->p, sizeof(jparam) * (size_t)pl->cp);
	}
	pl->p[pl->np].reg = reg;
	pl->p[pl->np].kind = kind;
	pl->p[pl->np].t = t;
	return pl->np++;
}

/* the value where n values meet (v[0..n)), adding the parameters it needs to the plan */
static sval join_one(comp *c, jplan *pl, const sval *const *v, int n)
{
	bool same = true;
	sval r;

	for (int i = 1; i < n; i++) {
		if (!sval_eq(c, v[0], v[i])) { same = false; break; }
	}
	if (same) return *v[0];
	for (int i = 0; i < n; i++) {
		if (v[i]->t == V_UNDEF) { r = s_bad(); r.t = V_UNDEF; return r; }
		if (v[i]->t != v[0]->t) return s_bad();
	}
	switch (v[0]->t) {
	case V_NUM: {
		int t = join_type(c, v, n);

		if (t == -1) return s_bad();
		r = s_num(t, cc_reg(c->f, nat(t)));
		plan_add(pl, r.r, 0, t);
		return r;
	}
	case V_VIEW: case V_ELEM: case V_SHAPE:
		for (int i = 1; i < n; i++) {
			if (v[i]->n != v[0]->n || v[i]->a != v[0]->a) return s_bad();
		}
		r = *v[0];
		r.r = cc_reg(c->f, BUF_T_INT64);
		plan_add(pl, r.r, 1, BUF_T_INT64);
		return r;
	case V_LIST: {
		const sval **e = emalloc(sizeof(sval *) * (size_t)n);
		int start;

		for (int i = 1; i < n; i++) {
			if (v[i]->a != v[0]->a) { efree(e); return s_bad(); }
		}
		r = *v[0];
		start = c->nsv;
		for (int k = 0; k < r.a; k++) sv_add(c, s_bad());
		for (int k = 0; k < r.a; k++) {
			sval x;

			for (int i = 0; i < n; i++) e[i] = &c->sv[v[i]->n + k];
			x = join_one(c, pl, e, n);
			c->sv[start + k] = x;
		}
		efree(e);
		r.n = start;
		return r;
	}
	}
	return s_bad();
}

/* the arguments one incoming value passes to the plan's parameters (in the plan's order), emitted in c->cur */
static void join_args(comp *c, const sval *joined, const sval *in, int *args, int *na)
{
	if (sval_eq(c, joined, in)) return;
	switch (joined->t) {
	case V_NUM: {
		sval x = to_type(c, *in, joined->dt);

		args[(*na)++] = DEAD || FAILED ? -1 : materialize(c, x);
		return;
	}
	case V_VIEW: case V_ELEM: case V_SHAPE:
		args[(*na)++] = in->r >= 0 ? in->r : const_reg(c, BUF_T_INT64, 0);
		return;
	case V_LIST:
		for (int k = 0; k < joined->a; k++) join_args(c, &c->sv[joined->n + k], &c->sv[in->n + k], args, na);
		return;
	}
}

/* a join point: a block reached by several edges */
typedef struct {
	incoming *in;
	int nin, cin;
	int target;          /* its block, once made */
	pstate *merged;
	int nvals;           /* values compared: the cells (blocks), or 1 (the return of a function) */
	bool header;
	int hdr;
	jplan plan;
	bool undefined;      /* the frames differ */
} joinpt;

static void add_incoming(joinpt *j, int blk, int e, pstate *s)
{
	if (j->nin == j->cin) {
		j->cin = j->cin ? j->cin * 2 : 4;
		j->in = erealloc(j->in, sizeof(incoming) * (size_t)j->cin);
	}
	j->in[j->nin].blk = blk;
	j->in[j->nin].e = e;
	j->in[j->nin].s = s;
	j->nin++;
}

/* the edge e of block blk passes its values to the join's block: through an edge block that converts them */
static void join_connect(comp *c, joinpt *j, int blk, int e, const pstate *s, bool back)
{
	int saved = c->cur, eb = cc_block_new(c->f), *args = emalloc(sizeof(int) * (size_t)(j->plan.np + 1)), na = 0;
	cc_block *b;

	c->f->blk[blk].e[e].to = eb;
	c->cur = eb;
	if (back) {
		/* a loop's back edge: PHP's interrupts (the time limit) */
		int q;

		emit(c, O_INTR);
		q = reg(c, BUF_T_BOOL);
		emit(c, O_ERRQ)->r = q;
		{
			int one = const_reg(c, BUF_T_BOOL, 1);
			cc_ins *n = emit(c, O_IBIN);

			n->sub = ZEND_BW_XOR;
			n->dt = BUF_T_BOOL;
			n->a = q;
			n->b = one;
			n->r = reg(c, BUF_T_BOOL);
			q = n->r;
		}
		{
			int cont = cc_block_new(c->f), prop = cc_block_new(c->f);

			b = &c->f->blk[c->cur];
			b->term = T_BR;
			b->cond = q;
			b->e[0].to = cont;
			b->e[1].to = prop;
			c->f->blk[prop].term = T_PROP;
			c->cur = cont;
		}
	}
	for (int v = 0; v < j->nvals && !DEAD && !FAILED; v++) join_args(c, &j->merged->cell[v], &s->cell[v], args, &na);
	if (!DEAD && !FAILED) {
		b = &c->f->blk[c->cur];
		b->term = T_JMP;
		b->e[0].to = j->target;
		b->e[0].nargs = na;
		b->e[0].args = c->f->npool;
		for (int k = 0; k < na; k++) cc_pool_add(c->f, args[k]);
	}
	efree(args);
	c->cur = saved;
}

/* makes the join's block and its state from the edges that reached it (all of them, or the forward edges of a loop
 * header, whose carried values become parameters as the earlier rounds found) */
static void make_join(comp *c, joinpt *j)
{
	const sval **v = emalloc(sizeof(sval *) * (size_t)(j->nin + 1));
	hdrinfo *h = j->header ? &c->hdr[j->hdr] : NULL;

	j->target = cc_block_new(c->f);
	j->merged = st_copy(c, j->in[0].s);
	for (int i = 1; i < j->nin; i++) {
		if (j->in[i].s->depth != j->merged->depth) j->undefined = true;
	}
	for (int x = 0; x < j->nvals; x++) {
		for (int i = 0; i < j->nin; i++) v[i] = &j->in[i].s->cell[x];
		if (h && h->carried[x] == 1 && j->in[0].s->cell[x].t != V_UNDEF) {
			/* a loop-carried value: a parameter of the type the rounds found, joined with the entry values */
			sval t = h->tmpl[x], r;
			bool kinds = true;

			for (int i = 0; i < j->nin; i++) {
				const sval *e = v[i];

				if (t.t == V_NUM ? e->t != V_NUM : (e->t != t.t || e->n != t.n || e->a != t.a)) kinds = false;
			}
			if (!kinds) {
				j->merged->cell[x] = s_bad();
				continue;
			}
			if (t.t == V_NUM) {
				int jt = join_type(c, v, j->nin);

				jt = jt == -1 ? -1 : common(c, jt, t.dt, false);
				if (jt == -1) { r = s_bad(); }
				else {
					if (jt != t.dt) { h->tmpl[x].dt = (int16_t)jt; c->redo = true; }
					r = s_num(jt, cc_reg(c->f, nat(jt)));
					plan_add(&j->plan, r.r, 0, jt);
				}
			} else {
				r = t;
				r.r = cc_reg(c->f, BUF_T_INT64);
				plan_add(&j->plan, r.r, 1, BUF_T_INT64);
			}
			j->merged->cell[x] = r;
			continue;
		}
		j->merged->cell[x] = join_one(c, &j->plan, v, j->nin);
	}
	efree(v);
	{
		cc_block *b = &c->f->blk[j->target];

		b->nparams = j->plan.np;
		b->params = c->f->npool;
		for (int k = 0; k < j->plan.np; k++) cc_pool_add(c->f, j->plan.p[k].reg);
	}
	for (int i = 0; i < j->nin && !FAILED; i++) join_connect(c, j, j->in[i].blk, j->in[i].e, j->in[i].s, false);
}

/* a back edge reaches a loop header that is already made: the carried values must be what the header expects */
static void back_edge(comp *c, joinpt *j, int blk, int e, const pstate *s)
{
	hdrinfo *h = &c->hdr[j->hdr];

	for (int x = 0; x < j->nvals; x++) {
		const sval *in = &s->cell[x], *m = &j->merged->cell[x];

		if (sval_eq(c, m, in) || h->carried[x] == 2) continue;
		if (!h->carried[x]) {
			/* changed in the loop: carried from the next round on */
			if (m->t == V_NUM && in->t == V_NUM) {
				int t = common(c, m->dt, in->dt, false);

				if (t != -1) {
					h->carried[x] = 1;
					h->tmpl[x] = s_num(t, -1);
					c->redo = true;
				}
			} else if ((m->t == V_VIEW || m->t == V_ELEM) && in->t == m->t && in->n == m->n && in->a == m->a) {
				h->carried[x] = 1;
				h->tmpl[x] = *m;
				c->redo = true;
			}
			/* otherwise the value cannot be carried: an error if it is read (BAD from the next round on) */
			if (!h->carried[x]) {
				h->carried[x] = 2;
				h->tmpl[x] = s_bad();
				c->redo = true;
			}
			continue;
		}
		if ((m->t == V_NUM) != (in->t == V_NUM) || (m->t != V_NUM && (in->t != m->t || in->n != m->n || in->a != m->a))) {
			h->carried[x] = 2;
			h->tmpl[x] = s_bad();
			c->redo = true;
			continue;
		}
		if (m->t == V_NUM && in->t == V_NUM) {
			int t = common(c, m->dt, in->dt, false);

			if (t != m->dt) {
				h->tmpl[x] = s_num(t == -1 ? m->dt : t, -1);
				if (t == -1) { h->carried[x] = 2; h->tmpl[x] = s_bad(); }
				c->redo = true;
			}
		}
	}
	if (!c->redo) join_connect(c, j, blk, e, s, true);
}

/* ---- reading operands ---- */

static sval *slot(comp *c, pstate *s, uint32_t var) { (void)c; return &s->cell[EX_VAR_TO_NUM(var)]; }

static int sv_add(comp *c, sval v);

static sval const_val(comp *c, zval *z)
{
	cc_val k;

	switch (Z_TYPE_P(z)) {
	case IS_LONG: k.s = Z_LVAL_P(z); return s_const(BUFFERS_SCALAR_INT, k);
	case IS_DOUBLE: k.d = Z_DVAL_P(z); return s_const(BUFFERS_SCALAR_FLOAT, k);
	case IS_TRUE: case IS_FALSE: k.i = Z_TYPE_P(z) == IS_TRUE; return s_const(BUFFERS_SCALAR_BOOL, k);
	case IS_NULL: { sval r = s_bad(); r.t = V_NULL; return r; }
	case IS_ARRAY: {   /* a literal list of numbers */
		HashTable *ht = Z_ARRVAL_P(z);
		sval r = s_bad();
		zval *e;
		zend_ulong i = 0, key;
		zend_string *sk;
		int start;

		if (zend_hash_num_elements(ht) > CC_MAX_RET) return unsupported(c, "an array of more than %d elements", CC_MAX_RET);
		start = c->nsv;
		ZEND_HASH_FOREACH_KEY_VAL(ht, key, sk, e) {
			sval x;

			if (sk || key != i++) return unsupported(c, "an array with keys");
			ZVAL_DEREF(e);
			if (Z_TYPE_P(e) == IS_ARRAY) return unsupported(c, "a nested array literal");
			x = const_val(c, e);
			if (x.t != V_NUM) return s_bad();
			sv_add(c, x);
		} ZEND_HASH_FOREACH_END();
		r.t = V_LIST;
		r.n = start;
		r.a = (int32_t)i;
		return r;
	}
	}
	return unsupported(c, "a constant of type %s", zend_zval_type_name(z));
}

static sval operand(comp *c, pstate *s, const zend_op *op, zend_uchar type, znode_op node)
{
	if (type == IS_CONST) return const_val(c, RT_CONSTANT(op, node));
	if (type == IS_UNUSED) return s_bad();
	{
		sval v = *slot(c, s, node.var);

		if (v.t == V_UNDEF) {
			if (type == IS_CV) return unsupported(c, "reading the undefined variable $%s", ZSTR_VAL(c->oa->vars[EX_VAR_TO_NUM(node.var)]));
			return unsupported(c, "this value");
		}
		if (v.t == V_BAD) {
			if (type == IS_CV) {
				return type_err(c, "the variable $%s has values of types that cannot be combined where paths meet", ZSTR_VAL(c->oa->vars[EX_VAR_TO_NUM(node.var)]));
			}
			return type_err(c, "a value that differs between the paths that reach it has types that cannot be combined");
		}
		return v;
	}
}

static sval num_operand(comp *c, pstate *s, const zend_op *op, zend_uchar type, znode_op node)
{
	sval v = operand(c, s, op, type, node);

	if (v.t == V_BAD || FAILED) return s_bad();
	if (v.t == V_ELEM) return load_elem(c, v);
	if (v.t != V_NUM) return unsupported(c, "this value (a number is needed)");
	return v;
}

static void set_result(comp *c, pstate *s, const zend_op *op, sval v)
{
	if (op->result_type & (IS_TMP_VAR | IS_VAR | IS_CV)) *slot(c, s, op->result.var) = v;
}

/* $list[$ix]: a literal index takes the element; any other integer is checked and selects it */
static sval list_index(comp *c, sval list, sval ix)
{
	const sval **v;
	int t, len = list.a;
	sval r;

	if (ix.t != V_NUM || !(ix.dt == BUFFERS_SCALAR_INT || cc_int(ix.dt))) return type_err(c, "an array index must be an integer");
	if (ix.r < 0) {
		int64_t k = cc_signed(nat(ix.dt)) ? ix.k.s : (int64_t)ix.k.i;

		if (k < 0 || k >= len) return fail_now(c, CC_E_RANGE, ix, s_int(len), 0);
		return c->sv[list.n + k];
	}
	if (!len) return fail_now(c, CC_E_RANGE, ix, s_int(0), 0);
	v = emalloc(sizeof(sval *) * (size_t)len);
	for (int k = 0; k < len; k++) {
		v[k] = &c->sv[list.n + k];
		if (v[k]->t != V_NUM) { efree(v); return unsupported(c, "indexing this array with a variable"); }
	}
	t = join_type(c, v, len);
	efree(v);
	if (t == -1) return type_err(c, "the elements of this array have types that cannot be combined");
	if (!is_weak(ix.dt)) ix = conv(c, ix, ix.dt, CC_PHPINT);
	{
		int ixr = materialize(c, ix), lenr = const_reg(c, CC_PHPINT, (uint64_t)len), ok;
		cc_ins *u = emit(c, O_CMP);

		u->sub = CC_LT;
		u->adt = CC_PHPINT == BUF_T_INT64 ? BUF_T_UINT64 : BUF_T_UINT32;
		u->a = ixr;
		u->b = lenr;
		u->r = ok = reg(c, BUF_T_BOOL);
		check(c, ok, CC_E_RANGE, ixr, lenr, 0);
		r = to_type(c, c->sv[list.n + len - 1], t);
		for (int k = len - 2; k >= 0 && !FAILED; k--) {
			sval e = to_type(c, c->sv[list.n + k], t);
			int eq = cmp_reg(c, CC_EQ, CC_PHPINT, ixr, const_reg(c, CC_PHPINT, (uint64_t)k)), re = materialize(c, e), rr = materialize(c, r);
			cc_ins *i = emit(c, O_SEL);

			i->a = eq;
			i->b = re;
			i->c = rr;
			i->r = reg(c, nat(t));
			r = s_num(t, i->r);
		}
	}
	return r;
}

/* ---- calls ---- */

static int fn_id(zend_string *lcname)
{
	for (int i = 0; i < FN_COUNT; i++) {
		if (fn_names[i][0] && zend_string_equals_cstr(lcname, fn_names[i], strlen(fn_names[i]))) return i;
	}
	return -1;
}

static zend_function *find_function(zval *name)
{
	return Z_TYPE_P(name) == IS_STRING ? zend_hash_find_ptr(EG(function_table), Z_STR_P(name)) : NULL;
}

static int user_fid(comp *c, const zend_function *f)
{
	for (int i = 0; i < c->nufn; i++) {
		if (c->ufn[i] == f) return FN_COUNT + i;
	}
	if (c->nufn == 64) { unsupported(c, "calls to more than 64 different functions"); return -1; }
	c->ufn[c->nufn] = f;
	return FN_COUNT + c->nufn++;
}

static int resolve_static_call(comp *c, const zend_op *op)
{
	zend_class_entry *ce = NULL, *scope = c->fn->common.scope;
	zend_function *f;
	zval *mname;

	if (op->op1_type == IS_CONST) {
		zval *cn = RT_CONSTANT(op, op->op1);

		ce = zend_lookup_class_ex(Z_STR_P(cn), Z_STR_P(cn + 1), ZEND_FETCH_CLASS_NO_AUTOLOAD);
		if (!ce) { unsupported(c, "a call to a method of the class %s, which is not loaded", Z_STRVAL_P(cn)); return -1; }
	} else if (op->op1_type == IS_UNUSED) {
		switch (op->op1.num & ZEND_FETCH_CLASS_MASK) {
		case ZEND_FETCH_CLASS_SELF: case ZEND_FETCH_CLASS_STATIC: ce = scope; break;
		default: unsupported(c, "parent:: calls"); return -1;
		}
		if (!ce) { unsupported(c, "self:: outside a class"); return -1; }
	} else {
		unsupported(c, "a call to a method of a class named at run time");
		return -1;
	}
	if (op->op2_type != IS_CONST) { unsupported(c, "a method named at run time"); return -1; }
	mname = RT_CONSTANT(op, op->op2);
	f = zend_hash_find_ptr(&ce->function_table, Z_STR_P(mname + 1));
	if (!f) { unsupported(c, "the undefined method %s::%s()", ZSTR_VAL(ce->name), Z_STRVAL_P(mname)); return -1; }
	if (!(f->common.fn_flags & ZEND_ACC_STATIC) || f->type != ZEND_USER_FUNCTION) {
		unsupported(c, "the method %s::%s() (only static methods written in PHP)", ZSTR_VAL(ce->name), Z_STRVAL_P(mname));
		return -1;
	}
	if ((op->op1.num & ZEND_FETCH_CLASS_MASK) == ZEND_FETCH_CLASS_STATIC && op->op1_type == IS_UNUSED
		&& !(ce->ce_flags & ZEND_ACC_FINAL) && !(f->common.fn_flags & (ZEND_ACC_PRIVATE | ZEND_ACC_FINAL))) {
		unsupported(c, "static::%s(), which a subclass could override (use self::)", Z_STRVAL_P(mname));
		return -1;
	}
	if ((f->common.fn_flags & ZEND_ACC_PRIVATE) && f->common.scope != scope) {
		unsupported(c, "the private method %s::%s() from outside its class", ZSTR_VAL(ce->name), Z_STRVAL_P(mname));
		return -1;
	}
	if ((f->common.fn_flags & ZEND_ACC_PROTECTED) && (!scope || !instanceof_function(scope, f->common.scope))) {
		unsupported(c, "the protected method %s::%s() from outside its class", ZSTR_VAL(ce->name), Z_STRVAL_P(mname));
		return -1;
	}
	return user_fid(c, f);
}

static int resolve_call(comp *c, const zend_op *op)
{
	zval *name = RT_CONSTANT(op, op->op2);
	zend_function *f;

	switch (op->opcode) {
	case ZEND_INIT_FCALL: f = find_function(name); break;
	case ZEND_INIT_FCALL_BY_NAME: f = find_function(name + 1); break;
	case ZEND_INIT_NS_FCALL_BY_NAME:
		f = find_function(name + 1);
		if (!f) f = find_function(name + 2);
		break;
	default: unsupported(c, "this kind of call"); return -1;
	}
	if (f && f->type == ZEND_USER_FUNCTION) return user_fid(c, f);
	if (!f || f->type != ZEND_INTERNAL_FUNCTION || fn_id(f->common.function_name) < 0) {
		const char *n = f ? ZSTR_VAL(f->common.function_name) : Z_TYPE_P(name) == IS_STRING ? Z_STRVAL_P(name) : "?";

		unsupported(c, "the %sfunction %s()", strchr(n, '\\') ? "namespaced " : "", n);
		return -1;
	}
	return fn_id(f->common.function_name);
}

static sval count_of(comp *c, sval a)
{
	if (a.t == V_LIST) return s_int(a.a);
	if (a.t == V_VIEW && a.a < c->bind[a.n].ndim) return s_num(BUFFERS_SCALAR_INT, env_reg(c, c->bind[a.n].env + 1 + a.a, CC_PHPINT));
	return unsupported(c, "count() of this value");
}

/* a value of a float dtype for a math function: an int (weak or not) as a float in Float64, a float dtype as it is */
static sval math_arg(comp *c, sval x, int *dt)
{
	if (x.t == V_ELEM) x = load_elem(c, x);
	if (x.t != V_NUM) return unsupported(c, "this argument of a math function");
	if (x.dt == BUF_T_BOOL || x.dt == BUFFERS_SCALAR_BOOL) return type_err(c, "a math function of a %s", tname(x.dt));
	if (cc_float(x.dt) || x.dt == BUFFERS_SCALAR_FLOAT) { *dt = x.dt; return x; }
	{
		sval r = conv(c, x, nat(x.dt), BUF_T_FLOAT64);

		r.dt = is_weak(x.dt) ? BUFFERS_SCALAR_FLOAT : BUF_T_FLOAT64;
		*dt = r.dt;
		return r;
	}
}

/* sin() ... rad2deg(), log(), atan2(), hypot(), fdiv(): computed in double, as PHP's own functions compute them, and
 * rounded once to a float dtype of the argument (Float32, Float16, BFloat16) */
static sval math_call(comp *c, int fid, sval *args, int nargs)
{
	int ta, tb, t, cdt, sub;
	sval a, b;
	cc_ins *i;

	if (fid < FN_LOG || (fid == FN_LOG && nargs == 1)) {
		static const uint8_t m1[] = {CC_SIN, CC_COS, CC_TAN, CC_ASIN, CC_ACOS, CC_ATAN, CC_SINH, CC_COSH, CC_TANH, CC_ASINH,
			CC_ACOSH, CC_ATANH, CC_EXP, CC_EXPM1, CC_LOG10, CC_LOG2, CC_LOG1P, CC_DEG2RAD, CC_RAD2DEG};

		if (nargs != 1) return unsupported(c, "%s() with %d arguments", fn_names[fid], nargs);
		a = math_arg(c, args[0], &ta);
		if (FAILED) return s_bad();
		cdt = nat(ta);
		sub = fid == FN_LOG ? CC_LOG : m1[fid - FN_SIN];
		if (a.r < 0) return s_const(ta, cc_math1(sub, cdt, a.k));
		i = emit(c, O_MATH1);
		i->sub = (uint8_t)sub;
		i->dt = (int16_t)cdt;
		i->a = a.r;
		i->r = reg(c, cdt);
		return s_num(ta, i->r);
	}
	if (nargs != 2) return unsupported(c, "%s() with %d arguments", fn_names[fid], nargs);
	a = math_arg(c, args[0], &ta);
	b = FAILED ? s_bad() : math_arg(c, args[1], &tb);
	if (FAILED) return s_bad();
	t = common(c, ta, tb, true);
	if (t == -1) return s_bad();
	a = to_type(c, a, t);
	b = to_type(c, b, t);
	if (FAILED || DEAD) return s_bad();
	cdt = nat(t);
	sub = fid == FN_ATAN2 ? CC_ATAN2 : fid == FN_HYPOT ? CC_HYPOT : fid == FN_FDIV ? CC_FDIV : CC_LOGB;
	if (sub == CC_LOGB) {   /* a base that is not greater than 0: ValueError */
		if (b.r < 0) {
			double bv = cc_mt(cdt) == CC_MD ? b.k.d : (double)b.k.f;

			if (!(bv > 0.0)) return fail_now(c, CC_E_LOGBASE, s_bad(), s_bad(), 0);
		} else {
			int z = const_reg(c, cdt, 0);   /* 0.0, as float or double */

			check(c, cmp_reg(c, CC_GT, cdt, b.r, z), CC_E_LOGBASE, -1, -1, 0);
		}
	}
	if (a.r < 0 && b.r < 0) return s_const(t, cc_math2(sub, cdt, a.k, b.k));
	{
		int ra = materialize(c, a), rb = materialize(c, b);

		i = emit(c, O_MATH2);
		i->sub = (uint8_t)sub;
		i->dt = (int16_t)cdt;
		i->a = ra;
		i->b = rb;
		i->r = reg(c, cdt);
		return s_num(t, i->r);
	}
}

static sval call_builtin(comp *c, int fid, sval *args, int nargs)
{
	sval x;

	if (fid == FN_CNT) return nargs == 1 ? count_of(c, args[0]) : unsupported(c, "count() with %d arguments", nargs);
	if (fid == FN_ORD) {
		if (nargs != 1 || args[0].t != V_CHAR) return unsupported(c, "ord() of anything but a byte of a string ($s[$i])");
		{
			sval r = conv(c, s_num(BUF_T_UINT8, args[0].r), BUF_T_UINT8, CC_PHPINT);

			r.dt = BUFFERS_SCALAR_INT;
			return r;
		}
	}
	if (fid == FN_PI) {
		cc_val k;

		if (nargs) return unsupported(c, "pi() with arguments");
		k.d = M_PI;
		return s_const(BUFFERS_SCALAR_FLOAT, k);
	}
	if (fid == FN_POW) {
		if (nargs != 2) return unsupported(c, "pow() with %d arguments", nargs);
		return bin(c, ZEND_POW, args[0].t == V_ELEM ? load_elem(c, args[0]) : args[0], args[1].t == V_ELEM ? load_elem(c, args[1]) : args[1]);
	}
	if (fid >= FN_SIN && fid <= FN_FDIV) return math_call(c, fid, args, nargs);
	if (fid == FN_STRLEN) {
		if (nargs != 1 || args[0].t != V_VIEW || !c->k->src[c->bind[args[0].n].src].str) return unsupported(c, "strlen() of anything but a string");
		return s_num(BUFFERS_SCALAR_INT, env_reg(c, c->bind[args[0].n].env + 1, CC_PHPINT));
	}

	for (int i = 0; i < nargs; i++) {
		if (args[i].t == V_ELEM) args[i] = load_elem(c, args[i]);
		if (args[i].t != V_NUM) return args[i].t == V_BAD ? s_bad() : unsupported(c, "this argument of %s()", fn_names[fid]);
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
	x = args[0];
	switch (fid) {
	case FN_ABS: case FN_FLOOR: case FN_CEIL: case FN_ROUND: case FN_SQRT: case FN_ISNAN: case FN_ISFINITE: case FN_ISINF: {
		int dt = nat(x.dt);

		if (dt == BUF_T_BOOL) return type_err(c, "%s() of a %s", fn_names[fid], tname(x.dt));
		if (cc_mt(dt) == CC_MI) {
			cc_val k;

			switch (fid) {
			case FN_FLOOR: case FN_CEIL: case FN_ROUND: return x;   /* integers keep their dtype and value */
			case FN_ABS: {
				sval neg, lt;

				if (!cc_signed(dt)) return x;
				neg = bin(c, ZEND_SUB, s_const(x.dt, (cc_val){0}), x);
				lt = cmp(c, CC_LT, x, s_const(x.dt, (cc_val){0}));
				if (FAILED) return s_bad();
				if (lt.r < 0) return lt.k.i ? neg : x;
				{
					int rn = materialize(c, neg), rx = materialize(c, x);
					cc_ins *i = emit(c, O_SEL);

					i->a = lt.r;
					i->b = rn;
					i->c = rx;
					i->r = reg(c, dt);
					return s_num(x.dt, i->r);
				}
			}
			case FN_SQRT:
				x = conv(c, x, dt, BUF_T_FLOAT64);
				x.dt = is_weak(args[0].dt) ? BUFFERS_SCALAR_FLOAT : BUF_T_FLOAT64;
				dt = BUF_T_FLOAT64;
				break;
			default:
				k.i = fid == FN_ISFINITE;
				return s_const(BUFFERS_SCALAR_BOOL, k);
			}
		}
		if (fid == FN_ISNAN || fid == FN_ISFINITE || fid == FN_ISINF) {
			sval d = bin(c, ZEND_SUB, x, x), r;   /* x - x: 0 when finite, NaN otherwise */

			if (FAILED) return s_bad();
			if (fid == FN_ISNAN) return cmp(c, CC_NE, x, x);
			r = cmp(c, CC_EQ, d, d);
			if (fid == FN_ISFINITE) return r;
			return bin(c, ZEND_BW_AND, cmp(c, CC_EQ, x, x), lnot(c, r));
		}
		{
			static const int m[] = {CC_FABS, CC_FLOOR, CC_CEIL, CC_ROUND, CC_SQRT};

			if (x.r < 0) return s_const(x.dt, cc_math1(m[fid], dt, x.k));
			{
				cc_ins *i = emit(c, O_MATH1);

				i->sub = (uint8_t)m[fid];
				i->dt = (int16_t)dt;
				i->a = x.r;
				i->r = reg(c, dt);
				return s_num(x.dt, i->r);
			}
		}
	}
	case FN_MIN: case FN_MAX:
		for (int k = 1; k < nargs; k++) {
			sval y = args[k], p;
			int t = common(c, x.dt, y.dt, true);

			if (t == -1) return s_bad();
			x = to_type(c, x, t);
			y = to_type(c, y, t);
			if (FAILED || DEAD) return s_bad();
			/* min: the later argument when it is smaller; max: when it is greater or they are unordered */
			p = fid == FN_MAX ? lnot(c, cmp(c, CC_LE, y, x)) : cmp(c, CC_LT, y, x);
			if (FAILED) return s_bad();
			if (p.r < 0) { x = p.k.i ? y : x; continue; }
			{
				int ry = materialize(c, y), rx = materialize(c, x);
				cc_ins *i = emit(c, O_SEL);

				i->a = p.r;
				i->b = ry;
				i->c = rx;
				i->r = reg(c, nat(t));
				x = s_num(t, i->r);
			}
		}
		return x;
	case FN_INTDIV: {
		sval y = args[1];
		int t = result_type(ZEND_BW_AND, x.dt, y.dt);   /* integers only, as the bitwise operators */
		int cdt;
		cc_ins *i;

		if (t == -1 || t == BUF_T_BOOL || t == BUFFERS_SCALAR_BOOL) return type_err(c, "intdiv() takes integers, not %s and %s", tname(x.dt), tname(y.dt));
		cdt = nat(t);
		x = is_weak(t) ? x : to_dt(c, x, cdt);
		y = is_weak(t) ? y : to_dt(c, y, cdt);
		if (FAILED || DEAD) return s_bad();
		if (y.r < 0) {
			if (y.k.i == 0) return fail_now(c, CC_E_DIV0, s_bad(), s_bad(), 0);
			if (x.r < 0) { cc_val k; k.i = cc_ibin(ZEND_DIV, cdt, x.k.i, y.k.i); return s_const(t, k); }
		} else {
			check(c, cmp_reg(c, CC_NE, cdt, y.r, const_reg(c, cdt, 0)), CC_E_DIV0, -1, -1, 0);
		}
		{
			int rx = materialize(c, x), ry = materialize(c, y);

			i = emit(c, O_IBIN);
			i->sub = ZEND_DIV;
			i->dt = (int16_t)cdt;
			i->a = rx;
			i->b = ry;
			i->r = reg(c, cdt);
			return s_num(t, i->r);
		}
	}
	case FN_FMOD:
		return bin(c, ZEND_MOD, result_type(ZEND_DIV, x.dt, args[1].dt) == BUFFERS_SCALAR_FLOAT && x.dt == BUFFERS_SCALAR_INT ? conv(c, x, CC_PHPINT, BUF_T_FLOAT64) : x,
			args[1]);
	}
	return s_bad();
}

/* whether a declared type names Buffers\Tensor */
static bool type_has_tensor(zend_type t)
{
	const zend_type *e;

	ZEND_TYPE_FOREACH(t, e) {
		if (ZEND_TYPE_HAS_NAME(*e) && zend_string_equals_literal_ci(ZEND_TYPE_NAME(*e), "Buffers\\Tensor")) return true;
	} ZEND_TYPE_FOREACH_END();
	return false;
}

/* an argument (or a returned value) checked against, and converted to, a declared type, as PHP does */
static sval coerce_param(comp *c, const zend_arg_info *ai, sval v)
{
	uint32_t mask;
	const char *name = ai->name ? ZSTR_VAL(ai->name) : "(return)";

	if (!ZEND_TYPE_IS_SET(ai->type) || v.t == V_BAD) return v;
	if (v.t == V_ELEM) v = load_elem(c, v);
	mask = ZEND_TYPE_PURE_MASK(ai->type);
	if ((mask & MAY_BE_ANY) == MAY_BE_ANY) return v;   /* mixed */
	switch (v.t) {
	case V_NULL:
		if (mask & MAY_BE_NULL) return v;
		return type_err(c, "parameter $%s does not accept null", name);
	case V_VIEW:
		if (c->k->src[c->bind[v.n].src].str && v.a == 0) {
			if (mask & MAY_BE_STRING) return v;
			return type_err(c, "parameter $%s does not accept a string", name);
		}
		if (type_has_tensor(ai->type) || (mask & MAY_BE_OBJECT) || (c->k->src[c->bind[v.n].src].arr && (mask & MAY_BE_ARRAY))) return v;
		return type_err(c, "parameter $%s does not accept a tensor", name);
	case V_LIST:
		if (mask & MAY_BE_ARRAY) return v;
		return type_err(c, "parameter $%s does not take an array", name);
	case V_NUM:
		break;
	default:
		return type_err(c, "parameter $%s does not take this value", name);
	}
	if (v.dt == BUFFERS_SCALAR_INT || cc_int(v.dt)) {
		if (mask & MAY_BE_LONG) return v;
		if (mask & MAY_BE_DOUBLE) {   /* an int for a float parameter becomes a float */
			sval r = conv(c, v, nat(v.dt), BUF_T_FLOAT64);

			r.dt = is_weak(v.dt) ? BUFFERS_SCALAR_FLOAT : BUF_T_FLOAT64;
			return r;
		}
	} else if (v.dt == BUFFERS_SCALAR_FLOAT || cc_float(v.dt)) {
		if (mask & MAY_BE_DOUBLE) return v;
	} else if (mask & (MAY_BE_BOOL | MAY_BE_TRUE | MAY_BE_FALSE)) {
		return v;
	}
	return type_err(c, "parameter $%s does not accept %s", name, tname(v.dt));
}

/* ---- properties and constants ---- */

static sval this_prop(comp *c, const zend_op *op)
{
	zend_string *name;
	zend_property_info *pi;
	const zend_class_entry *saved;
	zval *z;

	if (!c->this_obj) return unsupported(c, "$this outside an object");
	if (op->op2_type != IS_CONST) return unsupported(c, "a property named at run time");
	name = Z_STR_P(RT_CONSTANT(op, op->op2));
	saved = EG(fake_scope);
	EG(fake_scope) = c->scope;
	pi = zend_get_property_info(c->this_obj->ce, name, 1);
	EG(fake_scope) = (zend_class_entry *)saved;
	if (!pi || pi == ZEND_WRONG_PROPERTY_INFO || (pi->flags & ZEND_ACC_STATIC)) {
		return unsupported(c, "the property $%s (only declared properties the closure can read)", ZSTR_VAL(name));
	}
#if PHP_VERSION_ID >= 80400
	if (pi->hooks) return unsupported(c, "the property $%s, which has hooks", ZSTR_VAL(name));
#endif
	z = OBJ_PROP(c->this_obj, pi->offset);
	if (Z_TYPE_P(z) == IS_UNDEF) {
		zend_throw_error(NULL, "Typed property %s::$%s must not be accessed before initialization", ZSTR_VAL(pi->ce->name), ZSTR_VAL(name));
		return s_bad();
	}
	return env_value(c, SRC_PROP, pi->offset, z, "this property");
}

static sval fetch_constant(comp *c, const zend_op *op)
{
	zval *key = RT_CONSTANT(op, op->op2) + 1, *z;
	zend_constant *k;

	z = zend_hash_find(EG(zend_constants), Z_STR_P(key));
	if (!z && (op->op1.num & IS_CONSTANT_UNQUALIFIED_IN_NAMESPACE)) z = zend_hash_find(EG(zend_constants), Z_STR_P(key + 1));
	if (!z) return unsupported(c, "the undefined constant %s", Z_STRVAL_P(RT_CONSTANT(op, op->op2)));
	k = Z_PTR_P(z);
	return const_val(c, &k->value);
}

static sval fetch_class_constant(comp *c, const zend_op *op)
{
	zend_string *cls;
	zval *z;

	if (op->op2_type != IS_CONST) return unsupported(c, "a class constant named at run time");
	if (op->op1_type == IS_CONST) {
		cls = Z_STR_P(RT_CONSTANT(op, op->op1));
	} else if (op->op1_type == IS_UNUSED && (op->op1.num & ZEND_FETCH_CLASS_MASK) == ZEND_FETCH_CLASS_SELF && c->fn->common.scope) {
		cls = c->fn->common.scope->name;
	} else {
		return unsupported(c, "this class constant");
	}
	z = zend_get_class_constant_ex(cls, Z_STR_P(RT_CONSTANT(op, op->op2)), c->fn->common.scope, ZEND_FETCH_CLASS_SILENT);
	if (!z) return unsupported(c, "the class constant %s::%s", ZSTR_VAL(cls), Z_STRVAL_P(RT_CONSTANT(op, op->op2)));
	ZVAL_DEREF(z);
	return const_val(c, z);
}

/* ---- functions ---- */

static bool run_func(comp *c, const zend_function *fn, const sval *args, int nargs, sval *ret);

static void st_free_all(incoming *in, int n)
{
	for (int i = 0; i < n; i++) efree(in[i].s);
}

static int jump_block(const zend_cfg *cfg, const zend_op_array *oa, const zend_op *op, znode_op node)
{
	return (int)cfg->map[OP_JMP_ADDR(op, node) - oa->opcodes];
}

/* a binary operator of an opcode on two operands, or of a compound assignment */
static bool binary_opcode(int opc)
{
	switch (opc) {
	case ZEND_ADD: case ZEND_SUB: case ZEND_MUL: case ZEND_DIV: case ZEND_MOD: case ZEND_POW:
	case ZEND_SL: case ZEND_SR: case ZEND_BW_AND: case ZEND_BW_OR: case ZEND_BW_XOR:
		return true;
	}
	return false;
}

/* compiles fn's opcodes into c->f from c->cur on; args: the inlined call's arguments (NULL: the closure itself). The value
 * returned is in *ret and c->cur is where the code goes on (-1: never). false after throwing. */
static bool run_func(comp *c, const zend_function *fn, const sval *args, int nargs, sval *ret)
{
	const zend_op_array *oa = &fn->op_array;
	zend_arena *arena = zend_arena_create(64 * 1024);
	zend_cfg cfg;
	int *rpo = NULL, nrpo = 0, *state = NULL, *stack = NULL, *order = NULL;
	joinpt *J = NULL, exitj;
	pstate *s = NULL;   /* the state of the block being read */
	bool ok = false;
	/* the caller's context */
	const zend_op_array *s_oa = c->oa;
	const zend_function *s_fn = c->fn;
	int s_nslots = c->nslots, s_ncells = c->ncells;
	uint32_t s_line = c->line;

	memset(&exitj, 0, sizeof exitj);
	*ret = s_bad();
	if (args) {
		for (int i = 0; i < c->depth; i++) {
			if (c->chain[i] == fn) { unsupported(c, "the recursive call of %s()", ZSTR_VAL(fn->common.function_name)); goto out; }
		}
		if (c->depth == MAX_INLINE) { unsupported(c, "calls nested more than %d deep", MAX_INLINE); goto out; }
		if ((uint32_t)nargs > oa->num_args) { unsupported(c, "more arguments than %s() takes", ZSTR_VAL(fn->common.function_name)); goto out; }
	}
	c->chain[c->depth++] = fn;
	c->oa = oa;
	c->fn = fn;
	c->line = oa->line_start;
	c->nslots = (int)(oa->last_var + oa->T);
	c->ncells = c->nslots + FR_DEPTH * FR_ARGS;
	if (fn->common.fn_flags & (ZEND_ACC_VARIADIC | ZEND_ACC_GENERATOR | ZEND_ACC_RETURN_REFERENCE)) {
		unsupported(c, fn->common.fn_flags & ZEND_ACC_GENERATOR ? "a generator" : fn->common.fn_flags & ZEND_ACC_VARIADIC ? "a variadic parameter" : "returning by reference");
		goto out;
	}
	if (oa->last_try_catch) { unsupported(c, "try/catch"); goto out; }
	for (uint32_t i = 0; i < oa->num_args; i++) {
		if (ZEND_ARG_SEND_MODE(&oa->arg_info[i])) { unsupported(c, "the by-reference parameter $%s", ZSTR_VAL(oa->arg_info[i].name)); goto out; }
	}

	/* blocks, and their reverse post-order */
	memset(&cfg, 0, sizeof cfg);
	zend_build_cfg(&arena, oa, 0, &cfg);
	zend_cfg_build_predecessors(&arena, &cfg);
	zend_cfg_compute_dominators_tree(oa, &cfg);
	zend_cfg_identify_loops(oa, &cfg);
	for (int b = 0; b < cfg.blocks_count; b++) {
		if (cfg.blocks[b].flags & ZEND_BB_IRREDUCIBLE_LOOP) { unsupported(c, "a jump into a loop"); goto out; }
	}
	rpo = emalloc(sizeof(int) * (size_t)cfg.blocks_count);
	order = emalloc(sizeof(int) * (size_t)cfg.blocks_count);
	state = ecalloc((size_t)cfg.blocks_count, sizeof(int));
	stack = emalloc(sizeof(int) * (size_t)cfg.blocks_count * 2);
	{
		/* iterative depth-first search: post-order, reversed */
		int sp = 0, npost = 0;

		stack[sp++] = 0;
		state[0] = 1;
		while (sp) {
			int b = stack[sp - 1], pushed = 0;
			const zend_basic_block *bb = &cfg.blocks[b];

			for (int k = bb->successors_count - 1; k >= 0; k--) {
				int s = bb->successors[k];

				if (s >= 0 && !state[s]) { state[s] = 1; stack[sp++] = s; pushed = 1; break; }
			}
			if (!pushed) { order[npost++] = b; sp--; }
		}
		for (int i = 0; i < npost; i++) rpo[i] = order[npost - 1 - i];
		nrpo = npost;
		for (int i = 0; i < nrpo; i++) order[rpo[i]] = i;   /* order: position in the reverse post-order */
	}
	J = ecalloc((size_t)cfg.blocks_count, sizeof(joinpt));
	for (int b = 0; b < cfg.blocks_count; b++) {
		J[b].target = -1;
		J[b].nvals = c->nslots;
		J[b].header = (cfg.blocks[b].flags & ZEND_BB_LOOP_HEADER) != 0;
	}
	exitj.target = -1;
	exitj.nvals = 1;

	for (int ri = 0; ri < nrpo; ri++) {
		int zb = rpo[ri];
		const zend_basic_block *bb = &cfg.blocks[zb];
		joinpt *j = &J[zb];
		bool ended = false;

		if (zb == 0) {
			/* the entry: an edge from where the code is (the start, or the caller) */
			c->f->blk[c->cur].term = T_JMP;
			add_incoming(j, c->cur, 0, st_new(c));
		}
		{
			if (!j->nin) continue;   /* not reached */
			if (j->header) {
				hdrinfo *h;

				j->hdr = c->hvisit++;
				if (j->hdr >= c->nhdr) {
					if (c->nhdr == c->chdr) {
						c->chdr = c->chdr ? c->chdr * 2 : 8;
						c->hdr = erealloc(c->hdr, sizeof(hdrinfo) * (size_t)c->chdr);
					}
					h = &c->hdr[c->nhdr++];
					h->nvals = j->nvals;
					h->carried = ecalloc((size_t)j->nvals, 1);
					h->tmpl = ecalloc((size_t)j->nvals, sizeof(sval));
				}
				if (c->hdr[j->hdr].nvals != j->nvals) { unsupported(c, "this loop"); goto out; }
				for (int x = 0; x < j->nvals; x++) {
					if (c->hdr[j->hdr].carried[x] == 2) j->in[0].s->cell[x] = s_bad();
				}
			}
			make_join(c, j);
			if (FAILED) goto out;
			c->cur = j->target;
			s = st_copy(c, j->merged);
		}
		for (uint32_t at = bb->start; at < bb->start + bb->len && !ended; at++) {
			const zend_op *op = &oa->opcodes[at];

			c->line = op->lineno;
			if (DEAD) break;
			switch (op->opcode) {
			case ZEND_NOP: case ZEND_EXT_STMT: case ZEND_EXT_NOP: case ZEND_EXT_FCALL_BEGIN: case ZEND_EXT_FCALL_END:
			case ZEND_FREE: case ZEND_CHECK_VAR: case ZEND_CHECK_FUNC_ARG:
				break;
			case ZEND_RECV: case ZEND_RECV_INIT: {
				uint32_t i = op->op1.num - 1;
				sval v;

				if (!args) {   /* the closure's own parameters: this call's arguments */
					if (i < c->nargs) {
						v = env_value(c, SRC_ARG, i, &c->args[i], "this argument");
					} else if (op->opcode == ZEND_RECV_INIT) {
						v = const_val(c, RT_CONSTANT(op, op->op2));
					} else {
						zend_argument_count_error("Too few arguments to function {closure}(), %u passed and at least %u expected", c->nargs, oa->required_num_args);
						goto out;
					}
					if (FAILED) goto out;
				} else if ((int)i < nargs) {
					v = args[i];
				} else if (op->opcode == ZEND_RECV_INIT) {
					v = const_val(c, RT_CONSTANT(op, op->op2));
				} else {
					unsupported(c, "a call of %s() without its argument $%s", ZSTR_VAL(fn->common.function_name), ZSTR_VAL(oa->arg_info[i].name));
					goto out;
				}
				v = coerce_param(c, &oa->arg_info[i], v);
				if (FAILED) goto out;
				set_result(c, s, op, v);
				break;
			}
			case ZEND_BIND_STATIC: {
				HashTable *ht;
				zval *z;
				const char *name = ZSTR_VAL(oa->vars[EX_VAR_TO_NUM(op->op1.var)]);
				uint32_t off = op->extended_value & ~(ZEND_BIND_REF | ZEND_BIND_IMPLICIT | ZEND_BIND_EXPLICIT);

				if (args || fn != c->top) { unsupported(c, "static variables"); goto out; }
				ht = ZEND_MAP_PTR_GET(fn->op_array.static_variables_ptr);
				if (!ht) ht = fn->op_array.static_variables;
				if (!ht || (op->extended_value & ZEND_BIND_REF)) { unsupported(c, "the by-reference capture of $%s", name); goto out; }
				z = (zval *)((char *)ht->arData + off);
				if (Z_ISREF_P(z)) { unsupported(c, "the by-reference capture of $%s", name); goto out; }
				*slot(c, s, op->op1.var) = env_value(c, SRC_CAPTURE, off, z, "the captured variable");
				if (FAILED) goto out;
				break;
			}
			case ZEND_QM_ASSIGN: case ZEND_VERIFY_RETURN_TYPE:
				if (op->op1_type != IS_UNUSED) {
					sval v = operand(c, s, op, op->op1_type, op->op1);

					if (FAILED) goto out;
					if (op->opcode == ZEND_VERIFY_RETURN_TYPE && (fn->common.fn_flags & ZEND_ACC_HAS_RETURN_TYPE)) {
						v = coerce_param(c, (const zend_arg_info *)oa->arg_info - 1, v);
						if (FAILED) goto out;
					}
					set_result(c, s, op, v);
				}
				break;
			case ZEND_ADD: case ZEND_SUB: case ZEND_MUL: case ZEND_DIV: case ZEND_MOD: case ZEND_POW:
			case ZEND_SL: case ZEND_SR: case ZEND_BW_AND: case ZEND_BW_OR: case ZEND_BW_XOR: {
				sval a = num_operand(c, s, op, op->op1_type, op->op1), b = FAILED ? s_bad() : num_operand(c, s, op, op->op2_type, op->op2);

				if (FAILED) goto out;
				set_result(c, s, op, bin(c, op->opcode, a, b));
				if (FAILED) goto out;
				break;
			}
			case ZEND_BW_NOT: {
				sval a = num_operand(c, s, op, op->op1_type, op->op1), r;
				int rt;

				if (FAILED) goto out;
				if (is_weak(a.dt)) {
					if (a.dt != BUFFERS_SCALAR_INT) { type_err(c, "Cannot perform bitwise not on %s", tname(a.dt)); goto out; }
					rt = a.dt;
				} else if ((rt = buf_dtype_promote(a.dt, -1, ZEND_BW_NOT)) < 0) {
					type_err(c, "Unsupported operand dtype for ~: %s", tname(a.dt));
					goto out;
				}
				if (a.r < 0) {
					cc_val k;

					k.i = cc_norm(~a.k.i, nat(rt));
					r = s_const(rt, k);
				} else {
					cc_ins *i = emit(c, O_BWNOT);

					i->dt = (int16_t)nat(rt);
					i->a = a.r;
					i->r = reg(c, nat(rt));
					r = s_num(rt, i->r);
				}
				set_result(c, s, op, r);
				break;
			}
			case ZEND_BOOL_NOT: case ZEND_BOOL: {
				sval a = num_operand(c, s, op, op->op1_type, op->op1);

				if (FAILED) goto out;
				set_result(c, s, op, op->opcode == ZEND_BOOL ? tobool(c, a) : lnot(c, a));
				if (FAILED) goto out;
				break;
			}
			case ZEND_BOOL_XOR: {
				sval a = tobool(c, num_operand(c, s, op, op->op1_type, op->op1)), b = FAILED ? s_bad() : tobool(c, num_operand(c, s, op, op->op2_type, op->op2));

				if (FAILED) goto out;
				set_result(c, s, op, bin(c, ZEND_BW_XOR, a, b));
				if (FAILED) goto out;
				break;
			}
			case ZEND_TYPE_CHECK: {
				/* is_int(), $x === null ...: the type of every value is known here */
				sval a = operand(c, s, op, op->op1_type, op->op1);
				uint32_t t;

				if (FAILED) goto out;
				switch (a.t) {
				case V_NULL: t = MAY_BE_NULL; break;
				case V_NUM:
					t = a.dt == BUFFERS_SCALAR_BOOL || a.dt == BUF_T_BOOL ? MAY_BE_BOOL
						: a.dt == BUFFERS_SCALAR_FLOAT || cc_float(a.dt) ? MAY_BE_DOUBLE : MAY_BE_LONG;
					break;
				case V_ELEM: t = cc_float(c->bind[a.n].dt) ? MAY_BE_DOUBLE : c->bind[a.n].dt == BUF_T_BOOL ? MAY_BE_BOOL : MAY_BE_LONG; break;
				case V_LIST: t = MAY_BE_ARRAY; break;
				case V_CHAR: t = MAY_BE_STRING; break;
				case V_VIEW: t = c->k->src[c->bind[a.n].src].str && a.a == 0 ? MAY_BE_STRING : MAY_BE_OBJECT; break;
				default: unsupported(c, "this type check"); goto out;
				}
				{
					cc_val k;

					/* a bool value: true and false are both MAY_BE_BOOL here (is_bool) */
					k.i = (op->extended_value & t) != 0 || (t == MAY_BE_BOOL && (op->extended_value & (MAY_BE_TRUE | MAY_BE_FALSE)) == (MAY_BE_TRUE | MAY_BE_FALSE));
					set_result(c, s, op, s_const(BUFFERS_SCALAR_BOOL, k));
				}
				break;
			}
			case ZEND_STRLEN: {
				sval a = operand(c, s, op, op->op1_type, op->op1);

				if (FAILED) goto out;
				set_result(c, s, op, call_builtin(c, FN_STRLEN, &a, 1));
				if (FAILED) goto out;
				break;
			}
			case ZEND_IS_EQUAL: case ZEND_IS_NOT_EQUAL: case ZEND_IS_IDENTICAL: case ZEND_IS_NOT_IDENTICAL:
			case ZEND_IS_SMALLER: case ZEND_IS_SMALLER_OR_EQUAL: {
				sval a, b;

				a = operand(c, s, op, op->op1_type, op->op1);
				b = FAILED ? s_bad() : operand(c, s, op, op->op2_type, op->op2);
				if (FAILED) goto out;
				if ((a.t == V_NULL || b.t == V_NULL) && (op->opcode == ZEND_IS_IDENTICAL || op->opcode == ZEND_IS_NOT_IDENTICAL)) {
					/* === null: known here */
					cc_val k;

					k.i = (a.t == V_NULL && b.t == V_NULL) == (op->opcode == ZEND_IS_IDENTICAL);
					set_result(c, s, op, s_const(BUFFERS_SCALAR_BOOL, k));
					break;
				}
				if (a.t == V_ELEM) a = load_elem(c, a);
				if (b.t == V_ELEM) b = load_elem(c, b);
				if (a.t != V_NUM || b.t != V_NUM) { unsupported(c, "comparing this value (numbers, and === null)"); goto out; }
				int which = op->opcode == ZEND_IS_SMALLER ? CC_LT : op->opcode == ZEND_IS_SMALLER_OR_EQUAL ? CC_LE
					: (op->opcode == ZEND_IS_EQUAL || op->opcode == ZEND_IS_IDENTICAL) ? CC_EQ : CC_NE;

				if (FAILED) goto out;
				set_result(c, s, op, cmp(c, which, a, b));
				if (FAILED) goto out;
				break;
			}
			case ZEND_CAST: {
				sval a = num_operand(c, s, op, op->op1_type, op->op1), r;

				if (FAILED) goto out;
				switch (op->extended_value) {
				case IS_LONG: case IS_DOUBLE: {
					int to = op->extended_value == IS_LONG ? CC_PHPINT : BUF_T_FLOAT64;

					r = conv(c, a, nat(a.dt), to);
					r.dt = is_weak(a.dt) ? (op->extended_value == IS_LONG ? BUFFERS_SCALAR_INT : BUFFERS_SCALAR_FLOAT) : to;
					break;
				}
				case _IS_BOOL: r = tobool(c, a); break;
				default: unsupported(c, "this cast"); goto out;
				}
				set_result(c, s, op, r);
				if (FAILED) goto out;
				break;
			}
			case ZEND_ASSIGN: {
				sval v;

				if (op->op1_type != IS_CV) { unsupported(c, "assigning to anything but a local variable"); goto out; }
				v = operand(c, s, op, op->op2_type, op->op2);
				if (FAILED) goto out;
				if (v.t == V_ELEM) v = load_elem(c, v);
				*slot(c, s, op->op1.var) = v;
				set_result(c, s, op, v);
				break;
			}
			case ZEND_ASSIGN_OP: case ZEND_PRE_INC: case ZEND_PRE_DEC: case ZEND_POST_INC: case ZEND_POST_DEC: {
				sval cur, rhs, v;
				int opc;
				sval *target = op->op1_type == IS_CV || op->op1_type == IS_VAR ? slot(c, s, op->op1.var) : NULL;
				bool elem = target && target->t == V_ELEM;
				sval el = elem ? *target : s_bad();

				if (!target || (op->op1_type == IS_VAR && !elem)) { unsupported(c, "assigning to anything but a local variable or a tensor element"); goto out; }
				cur = elem ? load_elem(c, el) : num_operand(c, s, op, op->op1_type, op->op1);
				if (FAILED) goto out;
				if (op->opcode == ZEND_ASSIGN_OP) {
					rhs = num_operand(c, s, op, op->op2_type, op->op2);
					opc = (int)op->extended_value;
					if (!binary_opcode(opc)) { unsupported(c, "this compound assignment"); goto out; }
				} else {
					rhs = s_int(1);
					opc = op->opcode == ZEND_PRE_INC || op->opcode == ZEND_POST_INC ? ZEND_ADD : ZEND_SUB;
				}
				if (FAILED) goto out;
				v = bin(c, opc, cur, rhs);
				if (FAILED) goto out;
				if (DEAD) { ended = true; break; }
				if (elem) {
					store_elem(c, el, v);
					if (FAILED) goto out;
				} else {
					*target = v;
				}
				set_result(c, s, op, op->opcode == ZEND_POST_INC || op->opcode == ZEND_POST_DEC ? cur : v);
				break;
			}
			case ZEND_FETCH_DIM_R: case ZEND_FETCH_LIST_R: case ZEND_FETCH_DIM_W: case ZEND_FETCH_DIM_RW: case ZEND_FETCH_DIM_FUNC_ARG: {
				/* FUNC_ARG: an argument of a call whose by-reference parameters are known only at run time; the functions
				 * compiled here take none */
				sval base = operand(c, s, op, op->op1_type, op->op1), r;
				bool lv = op->opcode == ZEND_FETCH_DIM_W || op->opcode == ZEND_FETCH_DIM_RW;

				if (FAILED) goto out;
				if (base.t == V_VIEW) {
					r = index_view(c, base, num_operand(c, s, op, op->op2_type, op->op2), lv);
				} else if (base.t == V_LIST && !lv) {
					sval ix = num_operand(c, s, op, op->op2_type, op->op2);

					if (FAILED) goto out;
					r = list_index(c, base, ix);
				} else if (base.t == V_SHAPE && !lv) {
					sval ix = num_operand(c, s, op, op->op2_type, op->op2);

					if (FAILED) goto out;
					if (ix.t != V_NUM || ix.r >= 0 || ix.dt != BUFFERS_SCALAR_INT || ix.k.s < 0 || ix.k.s >= c->bind[base.n].ndim - base.a) {
						unsupported(c, "this index of a shape (an integer literal within it)");
						goto out;
					}
					r = s_num(BUFFERS_SCALAR_INT, env_reg(c, c->bind[base.n].env + 1 + base.a + (int)ix.k.s, CC_PHPINT));
				} else {
					unsupported(c, lv ? "writing into this value" : "indexing this value");
					goto out;
				}
				if (FAILED) goto out;
				set_result(c, s, op, r);
				if (DEAD) ended = true;
				break;
			}
			case ZEND_ASSIGN_DIM: case ZEND_ASSIGN_DIM_OP: {
				const zend_op *data = op + 1;
				sval base = operand(c, s, op, op->op1_type, op->op1), el, v;

				if (FAILED) goto out;
				if (base.t != V_VIEW) { unsupported(c, "writing into this value (only tensor elements)"); goto out; }
				if (op->op2_type == IS_UNUSED) { unsupported(c, "appending to a tensor"); goto out; }
				el = index_view(c, base, num_operand(c, s, op, op->op2_type, op->op2), true);
				if (FAILED) goto out;
				if (DEAD) { ended = true; break; }
				if (el.t != V_ELEM) { unsupported(c, "assigning to a sub-view; index every axis down to one element"); goto out; }
				v = num_operand(c, s, data, data->op1_type, data->op1);
				if (FAILED) goto out;
				if (op->opcode == ZEND_ASSIGN_DIM_OP) {
					if (!binary_opcode((int)op->extended_value)) { unsupported(c, "this compound assignment"); goto out; }
					v = bin(c, (int)op->extended_value, load_elem(c, el), v);
					if (FAILED) goto out;
					if (DEAD) { ended = true; break; }
				}
				store_elem(c, el, v);
				if (FAILED) goto out;
				set_result(c, s, op, v);
				at++;   /* OP_DATA */
				break;
			}
			case ZEND_FETCH_OBJ_R: case ZEND_FETCH_OBJ_W: case ZEND_FETCH_OBJ_RW: case ZEND_FETCH_OBJ_FUNC_ARG: {
				sval r;

				sval base = op->op1_type == IS_UNUSED ? s_bad() : operand(c, s, op, op->op1_type, op->op1);

				if (FAILED) goto out;
				if (op->op1_type == IS_UNUSED || base.t == V_THIS) {   /* PHP 8.3 fetches $this first (FETCH_THIS) */
					r = this_prop(c, op);
					if (op->opcode != ZEND_FETCH_OBJ_R && op->opcode != ZEND_FETCH_OBJ_FUNC_ARG && r.t != V_VIEW && !FAILED) { unsupported(c, "writing to a property"); goto out; }
				} else {
					zval *pn;

					if (base.t != V_VIEW || op->op2_type != IS_CONST || (op->opcode != ZEND_FETCH_OBJ_R && op->opcode != ZEND_FETCH_OBJ_FUNC_ARG)) { unsupported(c, "this property"); goto out; }
					pn = RT_CONSTANT(op, op->op2);
					if (zend_string_equals_literal(Z_STR_P(pn), "shape")) {
						r = base;
						r.t = V_SHAPE;
					} else if (zend_string_equals_literal(Z_STR_P(pn), "ndim")) {
						r = s_int(c->bind[base.n].ndim - base.a);
					} else {
						unsupported(c, "the property $%s of a tensor (shape and ndim are)", Z_STRVAL_P(pn));
						goto out;
					}
				}
				if (FAILED) goto out;
				set_result(c, s, op, r);
				break;
			}
			case ZEND_COUNT: {
				sval a = operand(c, s, op, op->op1_type, op->op1), r;

				if (FAILED) goto out;
				r = count_of(c, a);
				if (FAILED) goto out;
				set_result(c, s, op, r);
				break;
			}
			case ZEND_FETCH_CONSTANT:
				set_result(c, s, op, fetch_constant(c, op));
				if (FAILED) goto out;
				break;
			case ZEND_FETCH_CLASS_CONSTANT:
				set_result(c, s, op, fetch_class_constant(c, op));
				if (FAILED) goto out;
				break;
			case ZEND_INIT_ARRAY: case ZEND_ADD_ARRAY_ELEMENT: {
				sval arr;

				if (op->op2_type != IS_UNUSED || (op->extended_value & ZEND_ARRAY_ELEMENT_REF)) { unsupported(c, "an array with keys or references"); goto out; }
				if (op->opcode == ZEND_INIT_ARRAY) {
					arr = s_bad();
					arr.t = V_LIST;
					arr.n = c->nsv;
					arr.a = 0;
				} else {
					int start = c->nsv;

					arr = *slot(c, s, op->result.var);
					if (arr.t != V_LIST) { unsupported(c, "this array"); goto out; }
					for (int i = 0; i < arr.a; i++) sv_add(c, c->sv[arr.n + i]);
					arr.n = start;
				}
				if (op->op1_type != IS_UNUSED) {
					sval v = num_operand(c, s, op, op->op1_type, op->op1);

					if (FAILED) goto out;
					if (arr.a >= CC_MAX_RET) { unsupported(c, "an array of more than %d elements", CC_MAX_RET); goto out; }
					if (op->opcode == ZEND_INIT_ARRAY) arr.n = c->nsv;
					sv_add(c, v);
					arr.a++;
				}
				set_result(c, s, op, arr);
				break;
			}
			case ZEND_INIT_FCALL: case ZEND_INIT_FCALL_BY_NAME: case ZEND_INIT_NS_FCALL_BY_NAME: case ZEND_INIT_STATIC_METHOD_CALL: {
				int fid = op->opcode == ZEND_INIT_STATIC_METHOD_CALL ? resolve_static_call(c, op) : resolve_call(c, op);

				if (fid < 0) goto out;
				if (s->depth == FR_DEPTH) { unsupported(c, "calls nested this deep"); goto out; }
				s->fid[s->depth] = fid;
				s->nargs[s->depth] = 0;
				for (int k = 0; k < FR_ARGS; k++) s->cell[c->nslots + s->depth * FR_ARGS + k].t = V_UNDEF;
				s->depth++;
				break;
			}
			case ZEND_SEND_VAL: case ZEND_SEND_VAR: case ZEND_SEND_VAL_EX: case ZEND_SEND_VAR_EX: case ZEND_SEND_VAR_NO_REF_EX:
			case ZEND_SEND_FUNC_ARG: case ZEND_SEND_VAR_NO_REF: {
				int pos;
				sval v;

				if (!s->depth || op->op2_type == IS_CONST) { unsupported(c, "this argument (named arguments are not supported)"); goto out; }
				pos = (int)op->op2.num - 1;
				if (pos < 0 || pos >= FR_ARGS) { unsupported(c, "more than %d arguments", FR_ARGS); goto out; }
				v = operand(c, s, op, op->op1_type, op->op1);
				if (FAILED) goto out;
				if (v.t == V_ELEM) v = load_elem(c, v);
				s->cell[c->nslots + (s->depth - 1) * FR_ARGS + pos] = v;
				if (pos + 1 > s->nargs[s->depth - 1]) s->nargs[s->depth - 1] = pos + 1;
				break;
			}
			case ZEND_DO_ICALL: case ZEND_DO_FCALL: case ZEND_DO_FCALL_BY_NAME: case ZEND_DO_UCALL: {
				sval av[FR_ARGS], r;
				int d;

				if (!s->depth) { unsupported(c, "this call"); goto out; }
				d = --s->depth;
				for (int k = 0; k < s->nargs[d]; k++) {
					av[k] = s->cell[c->nslots + d * FR_ARGS + k];
					if (av[k].t == V_UNDEF || av[k].t == V_BAD) { unsupported(c, "a missing argument"); goto out; }
				}
				if (s->fid[d] >= FN_COUNT) {
					if (!run_func(c, c->ufn[s->fid[d] - FN_COUNT], av, s->nargs[d], &r)) goto out;
					if (c->redo) { ok = true; goto out; }   /* another round: the rest does not matter */
				} else {
					r = call_builtin(c, s->fid[d], av, s->nargs[d]);
					if (FAILED) goto out;
				}
				set_result(c, s, op, r);
				if (DEAD) ended = true;
				break;
			}
#ifdef ZEND_FRAMELESS_ICALL_0
			case ZEND_FRAMELESS_ICALL_1: case ZEND_FRAMELESS_ICALL_2: case ZEND_FRAMELESS_ICALL_3: {
				zend_function *f = ZEND_FLF_FUNC(op);
				int fid = f && f->type == ZEND_INTERNAL_FUNCTION ? fn_id(f->common.function_name) : -1, na = op->opcode - ZEND_FRAMELESS_ICALL_0;
				sval av[3];

				if (fid < 0) { unsupported(c, "the function %s()", f ? ZSTR_VAL(f->common.function_name) : "?"); goto out; }
				if (na >= 1) av[0] = operand(c, s, op, op->op1_type, op->op1);
				if (na >= 2 && !FAILED) av[1] = operand(c, s, op, op->op2_type, op->op2);
				if (na >= 3 && !FAILED) av[2] = operand(c, s, op + 1, (op + 1)->op1_type, (op + 1)->op1);
				if (FAILED) goto out;
				set_result(c, s, op, call_builtin(c, fid, av, na));
				if (FAILED) goto out;
				if (na == 3) at++;
				if (DEAD) ended = true;
				break;
			}
			case ZEND_JMP_FRAMELESS: {
				/* in a namespace: a function of that name in the namespace takes the dynamic call, which rejects it */
				int to = jump_block(&cfg, oa, op, op->op2);

				if (!find_function(RT_CONSTANT(op, op->op1))) {
					cc_block *b = &c->f->blk[c->cur];

					b->term = T_JMP;
					if (J[to].target >= 0) { unsupported(c, "this jump"); goto out; }
					add_incoming(&J[to], c->cur, 0, s);
					s = NULL;
					ended = true;
				}
				break;
			}
#endif
			case ZEND_JMP: {
				int to = jump_block(&cfg, oa, op, op->op1);
				cc_block *b = &c->f->blk[c->cur];

				b->term = T_JMP;
				if (J[to].target >= 0) {
					back_edge(c, &J[to], c->cur, 0, s);
					efree(s);
				} else {
					add_incoming(&J[to], c->cur, 0, s);
				}
				s = NULL;
				ended = true;
				break;
			}
			case ZEND_JMPZ: case ZEND_JMPNZ: case ZEND_JMPZ_EX: case ZEND_JMPNZ_EX: case ZEND_JMP_SET: {
				sval cond = tobool(c, num_operand(c, s, op, op->op1_type, op->op1));
				int to = jump_block(&cfg, oa, op, op->op2), next = cfg.blocks[zb].successors[1];
				bool jz = op->opcode == ZEND_JMPZ || op->opcode == ZEND_JMPZ_EX;

				if (FAILED) goto out;
				if (op->opcode == ZEND_JMPZ_EX || op->opcode == ZEND_JMPNZ_EX) set_result(c, s, op, cond);
				if (op->opcode == ZEND_JMP_SET) set_result(c, s, op, operand(c, s, op, op->op1_type, op->op1));
				if (cond.r < 0) {
					bool jump = (cond.k.i != 0) != jz;
					int dest = jump ? to : next;
					cc_block *b = &c->f->blk[c->cur];

					b->term = T_JMP;
					if (J[dest].target >= 0) { back_edge(c, &J[dest], c->cur, 0, s); efree(s); }
					else add_incoming(&J[dest], c->cur, 0, s);
				} else {
					cc_block *b = &c->f->blk[c->cur];
					pstate *s2 = st_copy(c, s);
					int dest[2] = { jz ? next : to, jz ? to : next };
					pstate *ss[2] = { s, s2 };
					int blk = c->cur;

					b->term = T_BR;
					b->cond = cond.r;
					for (int e = 0; e < 2; e++) {
						if (J[dest[e]].target >= 0) { back_edge(c, &J[dest[e]], blk, e, ss[e]); efree(ss[e]); }
						else add_incoming(&J[dest[e]], blk, e, ss[e]);
					}
				}
				s = NULL;
				ended = true;
				break;
			}
			case ZEND_RETURN: {
				sval v = operand(c, s, op, op->op1_type, op->op1);
				pstate *rs;

				if (FAILED) goto out;
				if (v.t == V_ELEM) v = load_elem(c, v);
				rs = ecalloc(1, sizeof(pstate) + sizeof(sval) * (size_t)c->ncells);
				rs->cell[0] = v;
				c->f->blk[c->cur].term = T_JMP;
				add_incoming(&exitj, c->cur, 0, rs);
				efree(s);
				s = NULL;
				ended = true;
				break;
			}
			case ZEND_ASSIGN_OBJ: case ZEND_ASSIGN_OBJ_OP: case ZEND_ASSIGN_STATIC_PROP: case ZEND_ASSIGN_STATIC_PROP_OP:
			case ZEND_PRE_INC_OBJ: case ZEND_PRE_DEC_OBJ: case ZEND_POST_INC_OBJ: case ZEND_POST_DEC_OBJ:
				unsupported(c, "writing to a property");
				goto out;
			case ZEND_FETCH_THIS:
				if (!c->this_obj) { unsupported(c, "$this outside an object"); goto out; }
				{
					sval t = s_bad();

					t.t = V_THIS;
					set_result(c, s, op, t);
				}
				break;
			case ZEND_INIT_METHOD_CALL:
				unsupported(c, "$this as a value and instance methods");
				goto out;
			default:
				unsupported(c, "the operation %s", zend_get_opcode_name(op->opcode) ? zend_get_opcode_name(op->opcode) + 5 : "?");
				goto out;
			}
			if (DEAD) ended = true;
		}
		if (!ended && !DEAD && s) {
			/* falls through to the next block */
			int next = bb->start + bb->len < oa->last ? (int)cfg.map[bb->start + bb->len] : -1;

			if (next < 0) { unsupported(c, "this end of a block"); goto out; }
			c->f->blk[c->cur].term = T_JMP;
			if (J[next].target >= 0) { back_edge(c, &J[next], c->cur, 0, s); efree(s); }
			else add_incoming(&J[next], c->cur, 0, s);
			s = NULL;
		}
		if (s) efree(s);
		s = NULL;
		if (FAILED) goto out;
		if (c->redo) { ok = true; goto out; }   /* another round will follow: the rest does not matter */
	}

	/* where the function returns */
	if (!exitj.nin) {
		c->cur = -1;
		*ret = s_bad();
	} else {
		/* the frames of the return states are unused */
		int sn = c->nslots, scn = c->ncells;

		c->nslots = 1;
		c->ncells = 1;
		for (int i = 0; i < exitj.nin; i++) exitj.in[i].s->depth = 0;
		make_join(c, &exitj);
		c->nslots = sn;
		c->ncells = scn;
		if (FAILED) goto out;
		c->cur = exitj.target;
		*ret = exitj.merged->cell[0];
		if (ret->t == V_BAD) {
			type_err(c, "the returns of %s give values of types that cannot be combined", args ? ZSTR_VAL(fn->common.function_name) : "the closure");
			goto out;
		}
	}
	ok = true;
out:
	if (s) efree(s);
	if (J) {
		for (int b = 0; b < cfg.blocks_count; b++) {
			st_free_all(J[b].in, J[b].nin);
			if (J[b].in) efree(J[b].in);
			if (J[b].merged) efree(J[b].merged);
			if (J[b].plan.p) efree(J[b].plan.p);
		}
		efree(J);
	}
	st_free_all(exitj.in, exitj.nin);
	if (exitj.in) efree(exitj.in);
	if (exitj.merged) efree(exitj.merged);
	if (exitj.plan.p) efree(exitj.plan.p);
	if (rpo) efree(rpo);
	if (order) efree(order);
	if (state) efree(state);
	if (stack) efree(stack);
	zend_arena_destroy(arena);
	c->depth--;
	c->oa = s_oa;
	c->fn = s_fn;
	c->nslots = s_nslots;
	c->ncells = s_ncells;
	c->line = s_line;
	return ok;
}

/* ---- the kernel ---- */

void cc_kernel_free(cc_kernel *k)
{
	if (!k) return;
	for (int i = 0; i < k->nfn; i++) cc_func_free(k->fns[i]);
	if (k->fns) efree(k->fns);
	if (k->src) efree(k->src);
	if (k->native && k->native_free) k->native_free(k->native);
	efree(k);
}

cc_kernel *cc_compile(zend_object *closure, zval *args, uint32_t nargs)
{
	const zend_function *fn = zend_get_closure_method_def(closure);
	zval *thisz;
	comp c;
	cc_kernel *k = NULL;

	memset(&c, 0, sizeof c);
	c.top = fn;
	c.closure = closure;
	{
		zval cz;

		ZVAL_OBJ(&cz, closure);
		thisz = zend_get_closure_this_ptr(&cz);
		c.this_obj = thisz && Z_TYPE_P(thisz) == IS_OBJECT ? Z_OBJ_P(thisz) : NULL;
	}
	c.scope = fn->common.scope;
	c.args = args;
	c.nargs = nargs;
	if (fn->type != ZEND_USER_FUNCTION) {
		zend_throw_exception_ex(zend_ce_value_error, 0, "Buffers\\Compiled: only closures written in PHP can be compiled");
		return NULL;
	}
	for (int round = 0;; round++) {
		sval ret;

		if (round == MAX_ROUNDS) {
			unsupported(&c, "a loop whose types do not settle");
			break;
		}
		k = ecalloc(1, sizeof *k);
		c.k = k;
		c.f = cc_func_new();
		c.nbind = 0;
		c.nsv = 0;
		c.hvisit = 0;
		c.redo = false;
		c.nufn = 0;
		c.depth = 0;
		for (int i = 0; i < 256; i++) c.envreg[i] = -1;
		c.cur = cc_block_new(c.f);
		if (!run_func(&c, fn, NULL, 0, &ret)) {
			cc_func_free(c.f);
			cc_kernel_free(k);
			k = NULL;
			break;
		}
		if (c.redo) {
			cc_func_free(c.f);
			cc_kernel_free(k);
			k = NULL;
			continue;
		}
		/* the returned value into ctx->rets */
		if (c.cur >= 0) {
			if (ret.t == V_NUM) {
				int rr = materialize(&c, ret);
				cc_ins *i = emit(&c, O_SETRET);

				i->a = rr;
				k->nret = 1;
				k->retdt[0] = ret.dt;
			} else if (ret.t == V_LIST) {
				k->ret_list = true;
				k->nret = ret.a;
				for (int e = 0; e < ret.a; e++) {
					sval x = c.sv[ret.n + e];
					cc_ins *i;

					if (x.t != V_NUM) { unsupported(&c, "returning an array of these values"); break; }
					{
						int rr = materialize(&c, x);

						i = emit(&c, O_SETRET);
						i->k.i = (uint64_t)e;
						i->a = rr;
					}
					k->retdt[e] = x.dt;
				}
			} else if (ret.t == V_VIEW) {
				unsupported(&c, c.k->src[c.bind[ret.n].src].str ? "returning a string" : "returning a tensor");
			} else if (ret.t != V_NULL) {
				unsupported(&c, "returning this value");
			}
			if (!FAILED) terminate(&c, T_RET);
		}
		if (FAILED) {
			cc_func_free(c.f);
			cc_kernel_free(k);
			k = NULL;
			break;
		}
		k->fns = emalloc(sizeof(cc_func *));
		k->fns[0] = c.f;
		k->nfn = 1;
		break;
	}
	for (int i = 0; i < c.nhdr; i++) {
		efree(c.hdr[i].carried);
		efree(c.hdr[i].tmpl);
	}
	if (c.hdr) efree(c.hdr);
	if (c.sv) efree(c.sv);
	if (k && getenv("BUFFERS_CC_DUMP")) cc_dump(k->fns[0]);
	return k;
}
