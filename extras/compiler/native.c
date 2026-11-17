/* The native backend: CIR lowered to machine code with IR (extras/ir), the code generator of PHP's own JIT.
 *
 * Blocks are emitted in reverse post-order, so every forward edge into a block is made before the block: a block starts
 * with BEGIN (one edge) or MERGE (several), its parameters are PHIs. A loop header is a LOOP_BEGIN after the merge of
 * its forward edges; its back edges (merged first when there are several) close it with LOOP_END, and its PHIs get their
 * second operand then. Integer registers are 64-bit, sign- or zero-extended from their dtype as in the interpreter;
 * every operation gives the interpreter's bits (ops.h), with C helpers for the rare cases (Float16 rounding, integer
 * powers, conversions outside the fast range). */
#include "native.h"

#ifdef BUF_CC_NATIVE

#include "ir.h"
#include "ir_builder.h"
#include "zend_execute.h"

/* ---- helpers called by compiled code ---- */

static uint64_t rt_ipow(int64_t dt, uint64_t a, uint64_t b) { return cc_ibin(ZEND_POW, (int)dt, a, b); }
static float rt_round16(int64_t dt, float f) { return cc_round16(f, (int)dt); }
static float rt_powf(float a, float b) { return (float)pow((double)a, (double)b); }
static int64_t rt_d2i(double d) { return (int64_t)cc_f2i(d); }
static double rt_u2d(uint64_t v) { return (double)v; }
static float rt_i2f(int64_t to, uint64_t v, int64_t sgn) { return cc_round_to(buf_int_for_rounding(v, sgn != 0), (int)to); }
static float rt_d2f(int64_t to, double d) { return cc_round_to(d, (int)to); }
static float rt_ld16(int64_t dt, const uint8_t *p) { return cc_load((int)dt, p).f; }
static void rt_st16(int64_t dt, uint8_t *p, float f) { cc_val v; v.f = f; cc_store((int)dt, p, v); }
static double rt_floor(double d) { return floor(d); }
static double rt_ceil(double d) { return ceil(d); }
static double rt_round(double d) { return round(d); }
static double rt_sqrt(double d) { return sqrt(d); }
static double rt_fmod(double a, double b) { return fmod(a, b); }
static double rt_pow(double a, double b) { return pow(a, b); }
static float rt_floorf(float d) { return floorf(d); }
static float rt_ceilf(float d) { return ceilf(d); }
static float rt_roundf(float d) { return roundf(d); }
static float rt_sqrtf(float d) { return sqrtf(d); }
static float rt_fmodf(float a, float b) { return fmodf(a, b); }

static void rt_intr(cc_ctx *ctx)
{
	if (zend_atomic_bool_load_ex(&EG(vm_interrupt))) {
		zend_atomic_bool_store_ex(&EG(vm_interrupt), false);
		if (zend_atomic_bool_load_ex(&EG(timed_out))) zend_timeout();
		if (zend_interrupt_function) zend_interrupt_function(EG(current_execute_data));
		if (EG(exception)) ctx->err = CC_E_THROWN;
	}
}

/* ---- lowering ---- */

typedef struct { int from, e; } pedge;

typedef struct {
	ir_ctx *ctx;
	const cc_func *f;
	ir_ref *val;          /* per register */
	ir_ref cptr, env, args, rets;
	int *pos;             /* position in reverse post-order, -1: not reached */
	int *rpo, nrpo;
	pedge **preds;        /* per block */
	int *npreds;
	ir_ref *end;          /* per edge (block * 2 + e): its END */
	ir_ref **eargs;       /* per edge: the values of its arguments */
	ir_ref *loop;         /* per header block */
	ir_ref **phis;        /* per header: its PHIs */
	int *backleft;
	ir_ref **bends;       /* per header: the ENDs of back edges, when there are several */
	ir_ref ***bargs;
	int *nb;
} gen;

#undef _ir_CTX
#define _ir_CTX (g->ctx)

static ir_type itype(int dt)
{
	switch (cc_mt(dt)) {
	case CC_MF: return IR_FLOAT;
	case CC_MD: return IR_DOUBLE;
	case CC_MP: return IR_ADDR;
	}
	return IR_I64;
}

static ir_ref fn_addr(gen *g, void *fn, ir_str proto) { return ir_const_func_addr(g->ctx, (uintptr_t)fn, proto); }
#define P1(r, a) ir_proto_1(g->ctx, 0, r, a)
#define P2(r, a, b) ir_proto_2(g->ctx, 0, r, a, b)
#define P3(r, a, b, c) ir_proto_3(g->ctx, 0, r, a, b, c)

static ir_ref zext_bool(gen *g, ir_ref b) { return ir_ZEXT_I64(b); }

/* an integer brought to dt's width */
static ir_ref norm(gen *g, int dt, ir_ref v)
{
	int bits;

	if (dt == BUF_T_BOOL) return zext_bool(g, ir_NE(v, ir_CONST_I64(0)));
	bits = buf_dtypes[dt].bits;
	if (bits == 64) return v;
	if (cc_signed(dt)) {
		switch (bits) {
		case 8: return ir_SEXT_I64(ir_TRUNC_I8(v));
		case 16: return ir_SEXT_I64(ir_TRUNC_I16(v));
		default: return ir_SEXT_I64(ir_TRUNC_I32(v));
		}
	}
	return ir_AND_I64(v, ir_CONST_I64((int64_t)(((uint64_t)1 << bits) - 1)));
}

static ir_ref round16(gen *g, int dt, ir_ref f)
{
	if (dt != BUF_T_FLOAT16 && dt != BUF_T_BFLOAT16) return f;
	return ir_CALL_2(IR_FLOAT, fn_addr(g, rt_round16, P2(IR_FLOAT, IR_I64, IR_FLOAT)), ir_CONST_I64(dt), f);
}

static ir_ref ibin(gen *g, int op, int dt, ir_ref a, ir_ref b)
{
	bool sgn = cc_signed(dt);
	int bits = cc_bits(dt);

	switch (op) {
	case ZEND_ADD: return norm(g, dt, ir_ADD_I64(a, b));
	case ZEND_SUB: return norm(g, dt, ir_SUB_I64(a, b));
	case ZEND_MUL: return norm(g, dt, ir_MUL_I64(a, b));
	case ZEND_BW_AND: return ir_AND_I64(a, b);
	case ZEND_BW_OR: return ir_OR_I64(a, b);
	case ZEND_BW_XOR: return ir_XOR_I64(a, b);
	case ZEND_MOD: case ZEND_DIV:
		if (sgn) {
			/* -1: no division (the minimum by -1 traps on x86) */
			ir_ref m1 = ir_EQ(b, ir_CONST_I64(-1)), d = ir_COND(IR_I64, m1, ir_CONST_I64(1), b);

			if (op == ZEND_MOD) return ir_COND(IR_I64, m1, ir_CONST_I64(0), norm(g, dt, ir_MOD_I64(a, d)));
			return norm(g, dt, ir_COND(IR_I64, m1, ir_NEG_I64(a), ir_DIV_I64(a, d)));
		}
		return op == ZEND_MOD ? ir_MOD_U64(a, b) : ir_DIV_U64(a, b);
	case ZEND_POW:
		return ir_CALL_3(IR_I64, fn_addr(g, rt_ipow, P3(IR_I64, IR_I64, IR_I64, IR_I64)), ir_CONST_I64(dt), a, b);
	case ZEND_SL:
		return ir_COND(IR_I64, ir_UGE(b, ir_CONST_I64(bits)), ir_CONST_I64(0), norm(g, dt, ir_SHL_I64(a, ir_AND_I64(b, ir_CONST_I64(63)))));
	case ZEND_SR:
		if (sgn) return ir_SAR_I64(a, ir_COND(IR_I64, ir_UGT(b, ir_CONST_I64(63)), ir_CONST_I64(63), b));
		return ir_COND(IR_I64, ir_UGT(b, ir_CONST_I64(63)), ir_CONST_I64(0), ir_SHR_U64(a, ir_AND_I64(b, ir_CONST_I64(63))));
	}
	return ir_CONST_I64(0);
}

static ir_ref fbin(gen *g, int op, int dt, ir_ref a, ir_ref b)
{
	if (cc_mt(dt) == CC_MD) {
		switch (op) {
		case ZEND_ADD: return ir_ADD_D(a, b);
		case ZEND_SUB: return ir_SUB_D(a, b);
		case ZEND_MUL: return ir_MUL_D(a, b);
		case ZEND_DIV: return ir_DIV_D(a, b);
		case ZEND_MOD: return ir_CALL_2(IR_DOUBLE, fn_addr(g, rt_fmod, P2(IR_DOUBLE, IR_DOUBLE, IR_DOUBLE)), a, b);
		default: return ir_CALL_2(IR_DOUBLE, fn_addr(g, rt_pow, P2(IR_DOUBLE, IR_DOUBLE, IR_DOUBLE)), a, b);
		}
	}
	switch (op) {
	case ZEND_ADD: return round16(g, dt, ir_ADD_F(a, b));
	case ZEND_SUB: return round16(g, dt, ir_SUB_F(a, b));
	case ZEND_MUL: return round16(g, dt, ir_MUL_F(a, b));
	case ZEND_DIV: return round16(g, dt, ir_DIV_F(a, b));
	case ZEND_MOD: return round16(g, dt, ir_CALL_2(IR_FLOAT, fn_addr(g, rt_fmodf, P2(IR_FLOAT, IR_FLOAT, IR_FLOAT)), a, b));
	default: return round16(g, dt, ir_CALL_2(IR_FLOAT, fn_addr(g, rt_powf, P2(IR_FLOAT, IR_FLOAT, IR_FLOAT)), a, b));
	}
}

static ir_ref cmp(gen *g, int which, int dt, ir_ref a, ir_ref b)
{
	bool u = cc_mt(dt) == CC_MI && !cc_signed(dt);
	ir_ref r;

	switch (which) {
	case CC_EQ: r = ir_EQ(a, b); break;
	case CC_NE: r = ir_NE(a, b); break;
	case CC_LT: r = u ? ir_ULT(a, b) : ir_LT(a, b); break;
	case CC_LE: r = u ? ir_ULE(a, b) : ir_LE(a, b); break;
	case CC_GT: r = u ? ir_UGT(a, b) : ir_GT(a, b); break;
	default: r = u ? ir_UGE(a, b) : ir_GE(a, b); break;
	}
	return zext_bool(g, r);
}

/* (int) of a double, PHP's rule, wrapped to dt */
static ir_ref d2i(gen *g, int dt, ir_ref d)
{
	ir_ref in = ir_AND_B(ir_GT(d, ir_CONST_DOUBLE(-9.2e18)), ir_LT(d, ir_CONST_DOUBLE(9.2e18)));
	ir_ref iff = ir_IF(in), e1, e2, v1, v2;

	ir_IF_TRUE(iff);
	v1 = ir_FP2I64(d);
	e1 = ir_END();
	ir_IF_FALSE_cold(iff);
	v2 = ir_CALL_1(IR_I64, fn_addr(g, rt_d2i, P1(IR_I64, IR_DOUBLE)), d);
	e2 = ir_END();
	ir_MERGE_2(e1, e2);
	return norm(g, dt, ir_PHI_2(IR_I64, v1, v2));
}

static ir_ref conv(gen *g, int from, int to, ir_ref v)
{
	int fm = cc_mt(from), tm = cc_mt(to);

	if (to == BUF_T_BOOL) {
		switch (fm) {
		case CC_MD: return zext_bool(g, ir_NE(v, ir_CONST_DOUBLE(0)));
		case CC_MF: return zext_bool(g, ir_NE(v, ir_CONST_FLOAT(0)));
		default: return zext_bool(g, ir_NE(v, ir_CONST_I64(0)));
		}
	}
	if (fm == CC_MI) {
		bool sgn = cc_signed(from);

		switch (tm) {
		case CC_MI: return norm(g, to, v);
		case CC_MD:
			if (!sgn && from == BUF_T_UINT64) return ir_CALL_1(IR_DOUBLE, fn_addr(g, rt_u2d, P1(IR_DOUBLE, IR_U64)), v);
			return ir_INT2D(v);
		default:
			if (to == BUF_T_FLOAT32 && from != BUF_T_INT64 && from != BUF_T_UINT64) return ir_INT2F(v);
			return ir_CALL_3(IR_FLOAT, fn_addr(g, rt_i2f, P3(IR_FLOAT, IR_I64, IR_I64, IR_I64)), ir_CONST_I64(to), v, ir_CONST_I64(sgn));
		}
	}
	if (fm == CC_MF) {
		switch (tm) {
		case CC_MD: return ir_F2D(v);
		case CC_MF: return round16(g, to, v);
		default: return d2i(g, to, ir_F2D(v));
		}
	}
	switch (tm) {
	case CC_MD: return v;
	case CC_MF:
		if (to == BUF_T_FLOAT32) return ir_D2F(v);
		return ir_CALL_2(IR_FLOAT, fn_addr(g, rt_d2f, P2(IR_FLOAT, IR_I64, IR_DOUBLE)), ir_CONST_I64(to), v);
	default: return d2i(g, to, v);
	}
}

static ir_ref math1(gen *g, int fn, int dt, ir_ref a)
{
	if (cc_mt(dt) == CC_MD) {
		void *f = fn == CC_FLOOR ? (void *)rt_floor : fn == CC_CEIL ? (void *)rt_ceil : fn == CC_ROUND ? (void *)rt_round : (void *)rt_sqrt;

		if (fn == CC_FABS) return ir_ABS_D(a);
		return ir_CALL_1(IR_DOUBLE, fn_addr(g, f, P1(IR_DOUBLE, IR_DOUBLE)), a);
	}
	if (fn == CC_FABS) return ir_ABS_F(a);
	{
		void *f = fn == CC_FLOOR ? (void *)rt_floorf : fn == CC_CEIL ? (void *)rt_ceilf : fn == CC_ROUND ? (void *)rt_roundf : (void *)rt_sqrtf;
		ir_ref r = ir_CALL_1(IR_FLOAT, fn_addr(g, f, P1(IR_FLOAT, IR_FLOAT)), a);

		return fn == CC_SQRT ? round16(g, dt, r) : r;
	}
}

static ir_ref load(gen *g, int dt, ir_ref p)
{
	switch (dt) {
	case BUF_T_BOOL: return zext_bool(g, ir_NE(ir_LOAD_U8(p), ir_CONST_U8(0)));
	case BUF_T_INT8: return ir_SEXT_I64(ir_LOAD_I8(p));
	case BUF_T_UINT8: return ir_ZEXT_I64(ir_LOAD_U8(p));
	case BUF_T_INT16: return ir_SEXT_I64(ir_LOAD_I16(p));
	case BUF_T_UINT16: return ir_ZEXT_I64(ir_LOAD_U16(p));
	case BUF_T_INT32: return ir_SEXT_I64(ir_LOAD_I32(p));
	case BUF_T_UINT32: return ir_ZEXT_I64(ir_LOAD_U32(p));
	case BUF_T_INT64: case BUF_T_UINT64: return ir_LOAD_I64(p);
	case BUF_T_FLOAT32: return ir_LOAD_F(p);
	case BUF_T_FLOAT64: return ir_LOAD_D(p);
	}
	return ir_CALL_2(IR_FLOAT, fn_addr(g, rt_ld16, P2(IR_FLOAT, IR_I64, IR_ADDR)), ir_CONST_I64(dt), p);
}

static void store(gen *g, int dt, ir_ref p, ir_ref v)
{
	switch (dt) {
	case BUF_T_BOOL: case BUF_T_INT8: case BUF_T_UINT8: ir_STORE(p, ir_TRUNC_U8(v)); return;
	case BUF_T_INT16: case BUF_T_UINT16: ir_STORE(p, ir_TRUNC_U16(v)); return;
	case BUF_T_INT32: case BUF_T_UINT32: ir_STORE(p, ir_TRUNC_U32(v)); return;
	case BUF_T_INT64: case BUF_T_UINT64: case BUF_T_FLOAT32: case BUF_T_FLOAT64: ir_STORE(p, v); return;
	}
	ir_CALL_3(IR_VOID, fn_addr(g, rt_st16, P3(IR_VOID, IR_I64, IR_ADDR, IR_FLOAT)), ir_CONST_I64(dt), p, v);
}

static ir_ref slot_addr(gen *g, ir_ref base, int64_t k) { return ir_ADD_OFFSET(base, (uintptr_t)(k * (int64_t)sizeof(cc_val))); }

static void lower(gen *g, const cc_ins *x)
{
	const cc_func *f = g->f;
	ir_ref *V = g->val, r = IR_UNUSED;
	int dt = x->dt;

	switch (x->op) {
	case O_CONST:
		switch (cc_mt(f->rdt[x->r])) {
		case CC_MF: r = ir_CONST_FLOAT(x->k.f); break;
		case CC_MD: r = ir_CONST_DOUBLE(x->k.d); break;
		case CC_MP: r = ir_CONST_ADDR(x->k.p); break;
		default: r = ir_CONST_I64(x->k.s);
		}
		break;
	case O_ARG: r = ir_LOAD(itype(f->rdt[x->r]), slot_addr(g, g->args, x->k.s)); break;
	case O_ENV: r = ir_LOAD(itype(f->rdt[x->r]), slot_addr(g, g->env, x->k.s)); break;
	case O_COPY: r = V[x->a]; break;
	case O_IBIN: r = ibin(g, x->sub, dt, V[x->a], V[x->b]); break;
	case O_FBIN: r = fbin(g, x->sub, dt, V[x->a], V[x->b]); break;
	case O_BWNOT: r = norm(g, dt, ir_NOT_I64(V[x->a])); break;
	case O_CMP: r = cmp(g, x->sub, x->adt, V[x->a], V[x->b]); break;
	case O_SEL: r = ir_COND(itype(f->rdt[x->r]), ir_NE(V[x->a], ir_CONST_I64(0)), V[x->b], V[x->c]); break;
	case O_CONV: r = conv(g, x->adt, dt, V[x->a]); break;
	case O_MATH1: r = math1(g, x->sub, dt, V[x->a]); break;
	case O_LOAD: r = load(g, dt, V[x->a]); break;
	case O_STORE: store(g, dt, V[x->a], V[x->b]); break;
	case O_PADD: r = ir_ADD_A(V[x->a], V[x->b]); break;
	case O_INTR: {
		ir_ref iff = ir_IF(ir_LOAD_U8(ir_CONST_ADDR(&EG(vm_interrupt))));

		ir_IF_TRUE_cold(iff);
		ir_CALL_1(IR_VOID, fn_addr(g, rt_intr, P1(IR_VOID, IR_ADDR)), g->cptr);
		ir_MERGE_WITH_EMPTY_FALSE(iff);
		break;
	}
	case O_ERRQ: r = zext_bool(g, ir_NE(ir_LOAD_I32(ir_ADD_OFFSET(g->cptr, offsetof(cc_ctx, err))), ir_CONST_I32(0))); break;
	case O_SETRET: ir_STORE(slot_addr(g, g->rets, x->k.s), V[x->a]); break;
	default: break;   /* O_CALL: not made by the front end yet */
	}
	if (x->r >= 0) V[x->r] = r;
}

/* the values an edge passes */
static void edge_args(gen *g, int b, int e)
{
	const cc_edge *ed = &g->f->blk[b].e[e];

	g->eargs[b * 2 + e] = emalloc(sizeof(ir_ref) * (size_t)(ed->nargs + 1));
	for (int k = 0; k < ed->nargs; k++) g->eargs[b * 2 + e][k] = g->val[g->f->pool[ed->args + k]];
}

/* the edge e of block b, at the current control: an END, or the end of a loop */
static void close_edge(gen *g, int b, int e)
{
	const cc_func *f = g->f;
	int to = f->blk[b].e[e].to, np = f->blk[to].nparams;

	edge_args(g, b, e);
	if (g->pos[to] > g->pos[b]) {
		g->end[b * 2 + e] = ir_END();
		return;
	}
	/* a back edge */
	if (g->backleft[to] == 1 && !g->nb[to]) {
		ir_MERGE_SET_OP(g->loop[to], 2, ir_LOOP_END());
		for (int k = 0; k < np; k++) ir_PHI_SET_OP(g->phis[to][k], 2, g->eargs[b * 2 + e][k]);
		g->backleft[to] = 0;
		return;
	}
	g->bends[to][g->nb[to]] = ir_END();
	g->bargs[to][g->nb[to]] = g->eargs[b * 2 + e];
	g->nb[to]++;
	if (--g->backleft[to] == 0) {
		int n = g->nb[to];
		ir_ref *vals = emalloc(sizeof(ir_ref) * (size_t)n), *phv = emalloc(sizeof(ir_ref) * (size_t)(np + 1));

		ir_MERGE_N(n, g->bends[to]);
		for (int k = 0; k < np; k++) {
			for (int i = 0; i < n; i++) vals[i] = g->bargs[to][i][k];
			phv[k] = ir_PHI_N(itype(f->rdt[f->pool[f->blk[to].params + k]]), n, vals);
		}
		ir_MERGE_SET_OP(g->loop[to], 2, ir_LOOP_END());
		for (int k = 0; k < np; k++) ir_PHI_SET_OP(g->phis[to][k], 2, phv[k]);
		efree(vals);
		efree(phv);
	}
}

static void *build(gen *g, size_t *size)
{
	const cc_func *f = g->f;
	int nb = f->nblk;
	int *stack = emalloc(sizeof(int) * (size_t)nb * 2 + 2), *state = ecalloc((size_t)nb, sizeof(int)), sp = 0, npost = 0;

	/* reverse post-order, and the edges into each block */
	g->pos = emalloc(sizeof(int) * (size_t)nb);
	g->rpo = emalloc(sizeof(int) * (size_t)nb);
	stack[sp++] = 0;
	state[0] = 1;
	while (sp) {
		int b = stack[sp - 1], pushed = 0;
		const cc_block *bl = &f->blk[b];
		int ne = bl->term == T_BR ? 2 : bl->term == T_JMP ? 1 : 0;

		for (int e = 0; e < ne; e++) {
			int s = bl->e[e].to;

			if (!state[s]) { state[s] = 1; stack[sp++] = s; pushed = 1; break; }
		}
		if (!pushed) { g->rpo[npost++] = b; sp--; }
	}
	g->nrpo = npost;
	for (int i = 0; i < npost / 2; i++) { int t = g->rpo[i]; g->rpo[i] = g->rpo[npost - 1 - i]; g->rpo[npost - 1 - i] = t; }
	for (int b = 0; b < nb; b++) g->pos[b] = -1;
	for (int i = 0; i < npost; i++) g->pos[g->rpo[i]] = i;
	g->preds = ecalloc((size_t)nb, sizeof(pedge *));
	g->npreds = ecalloc((size_t)nb, sizeof(int));
	g->backleft = ecalloc((size_t)nb, sizeof(int));
	g->nb = ecalloc((size_t)nb, sizeof(int));
	g->loop = ecalloc((size_t)nb, sizeof(ir_ref));
	g->phis = ecalloc((size_t)nb, sizeof(ir_ref *));
	g->bends = ecalloc((size_t)nb, sizeof(ir_ref *));
	g->bargs = ecalloc((size_t)nb, sizeof(ir_ref **));
	g->end = ecalloc((size_t)nb * 2, sizeof(ir_ref));
	g->eargs = ecalloc((size_t)nb * 2, sizeof(ir_ref *));
	g->val = ecalloc((size_t)(f->nreg + 1), sizeof(ir_ref));
	for (int b = 0; b < nb; b++) {
		const cc_block *bl = &f->blk[b];
		int ne = bl->term == T_BR ? 2 : bl->term == T_JMP ? 1 : 0;

		if (g->pos[b] < 0) continue;
		for (int e = 0; e < ne; e++) {
			int s = bl->e[e].to;

			g->preds[s] = erealloc(g->preds[s], sizeof(pedge) * (size_t)(g->npreds[s] + 1));
			g->preds[s][g->npreds[s]].from = b;
			g->preds[s][g->npreds[s]].e = e;
			g->npreds[s]++;
			if (g->pos[s] <= g->pos[b]) g->backleft[s]++;
		}
	}
	for (int b = 0; b < nb; b++) {
		if (g->backleft[b]) {
			g->bends[b] = emalloc(sizeof(ir_ref) * (size_t)g->backleft[b]);
			g->bargs[b] = emalloc(sizeof(ir_ref *) * (size_t)g->backleft[b]);
		}
	}

	for (int i = 0; i < g->nrpo; i++) {
		int b = g->rpo[i];
		const cc_block *bl = &f->blk[b];
		int nf = 0;
		ir_ref *ends = emalloc(sizeof(ir_ref) * (size_t)(g->npreds[b] + 1));
		const pedge **fp = emalloc(sizeof(pedge *) * (size_t)(g->npreds[b] + 1));

		for (int k = 0; k < g->npreds[b]; k++) {
			const pedge *p = &g->preds[b][k];

			if (g->pos[p->from] < g->pos[b]) { fp[nf] = p; ends[nf++] = g->end[p->from * 2 + p->e]; }
		}
		if (b == 0) {
			ir_START();
			g->cptr = ir_PARAM(IR_ADDR, "ctx", 1);
			g->env = ir_LOAD_A(ir_ADD_OFFSET(g->cptr, offsetof(cc_ctx, env)));
			g->args = ir_LOAD_A(ir_ADD_OFFSET(g->cptr, offsetof(cc_ctx, args)));
			g->rets = ir_LOAD_A(ir_ADD_OFFSET(g->cptr, offsetof(cc_ctx, rets)));
		} else if (nf == 1) {
			ir_BEGIN(ends[0]);
		} else {
			ir_MERGE_N(nf, ends);
		}
		/* parameters */
		if (bl->nparams) {
			ir_ref *vals = emalloc(sizeof(ir_ref) * (size_t)(nf + 1));

			for (int k = 0; k < bl->nparams; k++) {
				int r = f->pool[bl->params + k];

				for (int j = 0; j < nf; j++) vals[j] = g->eargs[fp[j]->from * 2 + fp[j]->e][k];
				g->val[r] = nf == 1 ? vals[0] : ir_PHI_N(itype(f->rdt[r]), nf, vals);
			}
			efree(vals);
		}
		if (g->backleft[b]) {
			g->loop[b] = ir_LOOP_BEGIN(ir_END());
			g->phis[b] = emalloc(sizeof(ir_ref) * (size_t)(bl->nparams + 1));
			for (int k = 0; k < bl->nparams; k++) {
				int r = f->pool[bl->params + k];

				g->phis[b][k] = ir_PHI_2(itype(f->rdt[r]), g->val[r], IR_UNUSED);
				g->val[r] = g->phis[b][k];
			}
		}
		efree(ends);
		efree(fp);
		for (int k = 0; k < bl->nins; k++) lower(g, &bl->ins[k]);
		switch (bl->term) {
		case T_JMP:
			close_edge(g, b, 0);
			break;
		case T_BR: {
			ir_ref iff = ir_IF(ir_NE(g->val[bl->cond], ir_CONST_I64(0)));

			ir_IF_TRUE(iff);
			close_edge(g, b, 0);
			ir_IF_FALSE(iff);
			close_edge(g, b, 1);
			break;
		}
		case T_ERR:
			ir_STORE(ir_ADD_OFFSET(g->cptr, offsetof(cc_ctx, err)), ir_CONST_I32(bl->err));
			ir_STORE(ir_ADD_OFFSET(g->cptr, offsetof(cc_ctx, eaux)), ir_CONST_I32(bl->eaux));
			if (bl->ea >= 0) ir_STORE(ir_ADD_OFFSET(g->cptr, offsetof(cc_ctx, ea)), g->val[bl->ea]);
			if (bl->eb >= 0) ir_STORE(ir_ADD_OFFSET(g->cptr, offsetof(cc_ctx, eb)), g->val[bl->eb]);
			ir_RETURN(IR_UNUSED);
			break;
		default:   /* T_RET (the top function returns nothing: its values are in ctx->rets), T_PROP */
			ir_RETURN(IR_UNUSED);
		}
	}
	efree(stack);
	efree(state);
	return ir_jit_compile(g->ctx, 2, size);
}

static void gen_free(gen *g)
{
	int nb = g->f->nblk;

	for (int b = 0; b < nb; b++) {
		if (g->preds) efree(g->preds[b]);
		if (g->phis && g->phis[b]) efree(g->phis[b]);
		if (g->bends && g->bends[b]) efree(g->bends[b]);
		if (g->bargs && g->bargs[b]) efree(g->bargs[b]);
	}
	for (int e = 0; g->eargs && e < nb * 2; e++) {
		if (g->eargs[e]) efree(g->eargs[e]);
	}
	efree(g->preds); efree(g->npreds); efree(g->backleft); efree(g->nb); efree(g->loop); efree(g->phis); efree(g->bends);
	efree(g->bargs); efree(g->end); efree(g->eargs); efree(g->val); efree(g->pos); efree(g->rpo);
}

typedef struct { void *code; size_t size; } native_code;

static void native_free(void *p)
{
	native_code *n = p;

	ir_mem_unmap(n->code, n->size);
	efree(n);
}

bool cc_native_compile(cc_kernel *k)
{
	ir_ctx ctx;
	gen g;
	size_t size = 0;
	void *code;

	if (k->nfn != 1) return false;
	memset(&g, 0, sizeof g);
	ir_init(&ctx, IR_FUNCTION | IR_OPT_FOLDING, IR_CONSTS_LIMIT_MIN, IR_INSNS_LIMIT_MIN);
	ctx.ret_type = IR_VOID;
	g.ctx = &ctx;
	g.f = k->fns[0];
	code = build(&g, &size);
	gen_free(&g);
	ir_free(&ctx);
	if (!code) return false;
	{
		native_code *n = emalloc(sizeof *n);

		n->code = code;
		n->size = size;
		k->native = n;
		k->native_free = native_free;
	}
	return true;
}

void cc_native_run(const cc_kernel *k, cc_ctx *ctx)
{
	((void (*)(cc_ctx *))((native_code *)k->native)->code)(ctx);
}

#endif
