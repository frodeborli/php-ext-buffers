/* CIR: the compiler's intermediate representation, between the front end (PHP opcodes, front.c) and the backends
 * (interp.c, and the native backend).
 *
 * A function is a list of blocks of instructions on typed registers. A register has one dtype (ops.h), set when it is
 * made; instructions are total operations (an operation that can fail is preceded by an explicit check that branches to
 * a block ending in T_ERR). Blocks take parameters (the values of variables that differ between the paths that reach
 * them), passed by the edges that lead to them. Block 0 is the entry. */
#ifndef BUF_CC_CIR_H
#define BUF_CC_CIR_H

#include "ops.h"

enum {
	O_CONST,     /* r = k */
	O_ARG,       /* r = argument k.i of the function */
	O_ENV,       /* r = the call's environment slot k.i (ctx->env): tensor addresses, shapes, strides, captured values */
	O_COPY,      /* r = a */
	O_IBIN,      /* r = a sub b, integers (and Bool for & | ^) of dtype dt: ops.h cc_ibin() */
	O_FBIN,      /* r = a sub b, floats of dtype dt: cc_fbin() / cc_dbin() */
	O_BWNOT,     /* r = ~a, wrapped to dt */
	O_CMP,       /* r (Bool) = a sub b, operands of dtype adt (sub: CC_EQ ...) */
	O_SEL,       /* r = a ? b : c (a Bool) */
	O_CONV,      /* r = a converted from adt to dt: cc_conv() */
	O_MATH1,     /* r = sub(a) for a float dt: cc_math1() */
	O_LOAD,      /* r = the element of dtype dt at address a */
	O_STORE,     /* the element of dtype dt at address a = b */
	O_PADD,      /* r (an address) = a + b (Int64 bytes) */
	O_CALL,      /* r = function k.i (args: the function's pool, args .. args + nargs); may set ctx->err */
	O_INTR,      /* a loop's back edge: PHP's interrupts (time limit, signals); may set ctx->err */
	O_ERRQ,      /* r (Bool) = ctx->err != 0 */
	O_SETRET,    /* ctx->rets[k.i] = a */
	O_COUNT_
};

/* terminators */
enum {
	T_OPEN,      /* not terminated yet */
	T_JMP,       /* to e[0] */
	T_BR,        /* to e[0] if cond, else e[1] */
	T_RET,       /* return cond (a register; -1: nothing) */
	T_ERR,       /* the call fails: error err, payload registers ea, eb (-1: none), eaux */
	T_PROP       /* the call fails with the error a call or an interrupt already set */
};

typedef struct {
	uint8_t op, sub;
	int16_t dt, adt;
	int32_t r, a, b, c;
	int32_t args, nargs;
	cc_val k;
} cc_ins;

typedef struct { int32_t to, args, nargs; } cc_edge;

typedef struct {
	cc_ins *ins;
	int32_t nins, cins;
	int32_t params, nparams;   /* registers, in the pool */
	uint8_t term;
	int32_t cond;
	cc_edge e[2];
	int32_t err, ea, eb, eaux;
} cc_block;

typedef struct {
	cc_block *blk;
	int32_t nblk, cblk;
	int16_t *rdt;
	int32_t nreg, creg;
	int32_t *pool;
	int32_t npool, cpool;
	int16_t *pdt;              /* parameter dtypes */
	int32_t nparam;
	int16_t ret_dt;            /* -1: no value */
} cc_func;

/* errors of compiled code (ctx->err) */
enum {
	CC_E_NONE,
	CC_E_MOD0,      /* DivisionByZeroError: Modulo by zero */
	CC_E_DIV0,      /* DivisionByZeroError: Division by zero (intdiv) */
	CC_E_SHIFT,     /* ArithmeticError: Bit shift by negative number */
	CC_E_POWNEG,    /* ValueError: negative exponent of an integer power */
	CC_E_FIT,       /* ValueError: the PHP int ea does not fit the dtype eaux */
	CC_E_RANGE,     /* ValueError: index ea out of range for an axis of length eb */
	CC_E_DEPTH,     /* Error: calls nested too deep */
	CC_E_THROWN     /* an exception is already set (an interrupt) */
};

/* the state of one call of compiled code */
typedef struct {
	int32_t err, eaux;
	int64_t ea, eb;
	int32_t depth;
	cc_val *env;
	cc_val *args;
	cc_val *rets;
} cc_ctx;

#define CC_MAX_DEPTH 10000

/* building */
cc_func *cc_func_new(void);
void cc_func_free(cc_func *f);
int cc_block_new(cc_func *f);
int cc_reg(cc_func *f, int dt);
int cc_pool_add(cc_func *f, int32_t v);
cc_ins *cc_emit(cc_func *f, int b, int op);
void cc_dump(const cc_func *f);

/* running (interp.c) */
void cc_interp(const cc_func *const *fns, int fi, cc_ctx *ctx, cc_val *ret);

#endif
