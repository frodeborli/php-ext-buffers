/* CIR: building functions, printing them (debugging), and the interpreter, which runs CIR on any CPU and is the
 * reference the native backend is tested against. */
#include "cir.h"
#include "zend_execute.h"

cc_func *cc_func_new(void)
{
	cc_func *f = ecalloc(1, sizeof *f);

	f->ret_dt = -1;
	return f;
}

void cc_func_free(cc_func *f)
{
	if (!f) return;
	for (int i = 0; i < f->nblk; i++) efree(f->blk[i].ins);
	efree(f->blk);
	efree(f->rdt);
	efree(f->pool);
	efree(f->pdt);
	efree(f);
}

int cc_block_new(cc_func *f)
{
	cc_block *b;

	if (f->nblk == f->cblk) {
		f->cblk = f->cblk ? f->cblk * 2 : 16;
		f->blk = erealloc(f->blk, sizeof(cc_block) * (size_t)f->cblk);
	}
	b = &f->blk[f->nblk];
	memset(b, 0, sizeof *b);
	b->cond = b->ea = b->eb = -1;
	b->e[0].to = b->e[1].to = -1;
	return f->nblk++;
}

int cc_reg(cc_func *f, int dt)
{
	if (f->nreg == f->creg) {
		f->creg = f->creg ? f->creg * 2 : 64;
		f->rdt = erealloc(f->rdt, sizeof(int16_t) * (size_t)f->creg);
	}
	f->rdt[f->nreg] = (int16_t)dt;
	return f->nreg++;
}

int cc_pool_add(cc_func *f, int32_t v)
{
	if (f->npool == f->cpool) {
		f->cpool = f->cpool ? f->cpool * 2 : 64;
		f->pool = erealloc(f->pool, sizeof(int32_t) * (size_t)f->cpool);
	}
	f->pool[f->npool] = v;
	return f->npool++;
}

cc_ins *cc_emit(cc_func *f, int b, int op)
{
	cc_block *bl = &f->blk[b];
	cc_ins *i;

	if (bl->nins == bl->cins) {
		bl->cins = bl->cins ? bl->cins * 2 : 8;
		bl->ins = erealloc(bl->ins, sizeof(cc_ins) * (size_t)bl->cins);
	}
	i = &bl->ins[bl->nins++];
	memset(i, 0, sizeof *i);
	i->op = (uint8_t)op;
	i->r = i->a = i->b = i->c = -1;
	i->dt = i->adt = -1;
	return i;
}

static const char *const op_names[O_COUNT_] = {
	"const", "arg", "env", "copy", "ibin", "fbin", "bwnot", "cmp", "sel", "conv", "math1", "load", "store", "padd", "call", "intr", "errq", "setret"
};

static const char *dtn(int dt)
{
	if (dt == CC_DT_PTR) return "ptr";
	if (dt < 0) return "-";
	return buf_dtypes[dt].name;
}

void cc_dump(const cc_func *f)
{
	for (int b = 0; b < f->nblk; b++) {
		const cc_block *bl = &f->blk[b];

		fprintf(stderr, "B%d(", b);
		for (int p = 0; p < bl->nparams; p++) fprintf(stderr, "%sr%d:%s", p ? ", " : "", f->pool[bl->params + p], dtn(f->rdt[f->pool[bl->params + p]]));
		fprintf(stderr, "):\n");
		for (int i = 0; i < bl->nins; i++) {
			const cc_ins *x = &bl->ins[i];

			fprintf(stderr, "  ");
			if (x->r >= 0) fprintf(stderr, "r%d:%s = ", x->r, dtn(f->rdt[x->r]));
			fprintf(stderr, "%s", op_names[x->op]);
			if (x->op == O_IBIN || x->op == O_FBIN || x->op == O_CMP || x->op == O_MATH1) fprintf(stderr, ".%d", x->sub);
			if (x->op == O_CONST) fprintf(stderr, " 0x%" PRIx64, x->k.i);
			if (x->op == O_ARG || x->op == O_ENV || x->op == O_CALL) fprintf(stderr, " #%" PRId64, x->k.s);
			if (x->a >= 0) fprintf(stderr, " r%d", x->a);
			if (x->b >= 0) fprintf(stderr, " r%d", x->b);
			if (x->c >= 0) fprintf(stderr, " r%d", x->c);
			for (int k = 0; k < x->nargs; k++) fprintf(stderr, " r%d", f->pool[x->args + k]);
			if (x->dt >= 0 || x->dt == CC_DT_PTR) fprintf(stderr, " [%s", dtn(x->dt));
			if (x->adt >= 0) fprintf(stderr, " <- %s", dtn(x->adt));
			if (x->dt >= 0 || x->dt == CC_DT_PTR) fprintf(stderr, "]");
			fprintf(stderr, "\n");
		}
		switch (bl->term) {
		case T_OPEN: fprintf(stderr, "  (open)\n"); break;
		case T_RET: fprintf(stderr, "  ret r%d\n", bl->cond); break;
		case T_ERR: fprintf(stderr, "  err %d r%d r%d\n", bl->err, bl->ea, bl->eb); break;
		case T_PROP: fprintf(stderr, "  prop\n"); break;
		default:
			for (int e = 0; e < (bl->term == T_BR ? 2 : 1); e++) {
				fprintf(stderr, "  %s B%d(", e ? "else" : bl->term == T_BR ? "br r" : "jmp", bl->e[e].to);
				if (!e && bl->term == T_BR) fprintf(stderr, "%d -> B%d(", bl->cond, bl->e[e].to);
				for (int k = 0; k < bl->e[e].nargs; k++) fprintf(stderr, "%sr%d", k ? ", " : "", f->pool[bl->e[e].args + k]);
				fprintf(stderr, ")\n");
			}
		}
	}
}

/* ---- the interpreter ---- */

void cc_interp(const cc_func *const *fns, int fi, cc_ctx *ctx, cc_val *ret)
{
	const cc_func *f = fns[fi];
	cc_val *R = emalloc(sizeof(cc_val) * (size_t)(f->nreg ? f->nreg : 1) * 2), *tmp = R + (f->nreg ? f->nreg : 1);
	int b = 0;

	if (++ctx->depth > CC_MAX_DEPTH) {
		ctx->err = CC_E_DEPTH;
		goto out;
	}
	for (;;) {
		const cc_block *bl = &f->blk[b];

		for (int i = 0; i < bl->nins; i++) {
			const cc_ins *x = &bl->ins[i];

			switch (x->op) {
			case O_CONST: R[x->r] = x->k; break;
			case O_ARG: R[x->r] = ctx->args[x->k.i]; break;
			case O_ENV: R[x->r] = ctx->env[x->k.i]; break;
			case O_COPY: R[x->r] = R[x->a]; break;
			case O_IBIN: R[x->r].i = cc_ibin(x->sub, x->dt, R[x->a].i, R[x->b].i); break;
			case O_FBIN:
				if (cc_mt(x->dt) == CC_MD) R[x->r].d = cc_dbin(x->sub, R[x->a].d, R[x->b].d);
				else R[x->r].f = cc_fbin(x->sub, x->dt, R[x->a].f, R[x->b].f);
				break;
			case O_BWNOT: R[x->r].i = cc_norm(~R[x->a].i, x->dt); break;
			case O_CMP: R[x->r].i = cc_cmp(x->sub, x->adt, R[x->a], R[x->b]); break;
			case O_SEL: R[x->r] = R[x->a].i ? R[x->b] : R[x->c]; break;
			case O_CONV: R[x->r] = cc_conv(x->adt, x->dt, R[x->a]); break;
			case O_MATH1: R[x->r] = cc_math1(x->sub, x->dt, R[x->a]); break;
			case O_LOAD: R[x->r] = cc_load(x->dt, R[x->a].p); break;
			case O_STORE: cc_store(x->dt, R[x->a].p, R[x->b]); break;
			case O_PADD: R[x->r].p = (uint8_t *)R[x->a].p + R[x->b].s; break;
			case O_CALL: {
				cc_val *args = emalloc(sizeof(cc_val) * (size_t)(x->nargs ? x->nargs : 1)), *saved = ctx->args, rv;

				for (int k = 0; k < x->nargs; k++) args[k] = R[f->pool[x->args + k]];
				ctx->args = args;
				rv.i = 0;
				cc_interp(fns, (int)x->k.i, ctx, &rv);
				ctx->args = saved;
				efree(args);
				if (x->r >= 0) R[x->r] = rv;
				break;
			}
			case O_INTR:
				if (UNEXPECTED(zend_atomic_bool_load_ex(&EG(vm_interrupt)))) {
					zend_atomic_bool_store_ex(&EG(vm_interrupt), false);
					if (zend_atomic_bool_load_ex(&EG(timed_out))) zend_timeout();
					if (zend_interrupt_function) zend_interrupt_function(EG(current_execute_data));
					if (EG(exception)) ctx->err = CC_E_THROWN;
				}
				break;
			case O_ERRQ: R[x->r].i = ctx->err != 0; break;
			case O_SETRET: ctx->rets[x->k.i] = R[x->a]; break;
			}
		}
		switch (bl->term) {
		case T_JMP: case T_BR: {
			const cc_edge *e = &bl->e[bl->term == T_BR && !R[bl->cond].i ? 1 : 0];
			const cc_block *to = &f->blk[e->to];

			/* the arguments are read before any parameter is written */
			for (int k = 0; k < e->nargs; k++) tmp[k] = R[f->pool[e->args + k]];
			for (int k = 0; k < e->nargs; k++) R[f->pool[to->params + k]] = tmp[k];
			b = e->to;
			continue;
		}
		case T_RET:
			if (bl->cond >= 0) *ret = R[bl->cond];
			goto out;
		case T_ERR:
			ctx->err = bl->err;
			ctx->eaux = bl->eaux;
			ctx->ea = bl->ea >= 0 ? R[bl->ea].s : 0;
			ctx->eb = bl->eb >= 0 ? R[bl->eb].s : 0;
			goto out;
		default:   /* T_PROP */
			goto out;
		}
	}
out:
	ctx->depth--;
	efree(R);
}
