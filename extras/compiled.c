/* Buffers\Compiled: a closure compiled to fast code (docs/extras.md).
 *
 * The constructor keeps the closure. A call compiles it for the types of its arguments and of the values it reads from
 * outside (captured variables, properties of $this): a specialization, kept for later calls whose values have the same
 * types. Each call reads those values again (their types are checked, their values passed), so a captured float that
 * changes does not compile the closure again. */
#include "../php_buffers.h"
#include "zend_closures.h"
#include "zend_exceptions.h"
#include "zend_interfaces.h"
#include "compiler/native.h"
#include "extras.h"

#define MAX_SPECS 16

typedef struct {
	zval closure;
	cc_kernel *spec[MAX_SPECS];
	uint32_t spec_nargs[MAX_SPECS];
	int nspec;
	zend_object std;
} buf_compiled;

static zend_class_entry *buf_ce_compiled;
static zend_object_handlers compiled_handlers;

static inline buf_compiled *compiled_from(zend_object *o) { return (buf_compiled *)((char *)o - XtOffsetOf(buf_compiled, std)); }

static zend_object *compiled_new(zend_class_entry *ce)
{
	buf_compiled *c = zend_object_alloc(sizeof(buf_compiled), ce);

	zend_object_std_init(&c->std, ce);
	object_properties_init(&c->std, ce);
	c->std.handlers = &compiled_handlers;
	ZVAL_UNDEF(&c->closure);
	return &c->std;
}

static void compiled_free(zend_object *o)
{
	buf_compiled *c = compiled_from(o);

	for (int i = 0; i < c->nspec; i++) cc_kernel_free(c->spec[i]);
	zval_ptr_dtor(&c->closure);
	zend_object_std_dtor(o);
}

static HashTable *compiled_gc(zend_object *o, zval **table, int *n)
{
	buf_compiled *c = compiled_from(o);

	*table = &c->closure;
	*n = 1;
	return zend_std_get_properties(o);
}

/* the value of a source for this call */
static zval *src_zval(buf_compiled *bc, const cc_src *s, zval *args)
{
	zend_object *cl = Z_OBJ(bc->closure);

	switch (s->kind) {
	case SRC_ARG: return &args[s->index];
	case SRC_CAPTURE: {
		const zend_function *fn = zend_get_closure_method_def(cl);
		HashTable *ht = ZEND_MAP_PTR_GET(fn->op_array.static_variables_ptr);

		if (!ht) ht = fn->op_array.static_variables;
		return (zval *)((char *)ht->arData + s->index);
	}
	default: {
		zval *t = zend_get_closure_this_ptr(&bc->closure);

		return OBJ_PROP(Z_OBJ_P(t), s->index);
	}
	}
}

/* the scalar type of a PHP value as the front end types it */
static int scalar_type(zval *z)
{
	switch (Z_TYPE_P(z)) {
	case IS_LONG: return BUFFERS_SCALAR_INT;
	case IS_DOUBLE: return BUFFERS_SCALAR_FLOAT;
	case IS_TRUE: case IS_FALSE: return BUFFERS_SCALAR_BOOL;
	}
	return 0;
}

/* the tensor a source holds for this call (a new reference in *hold), or NULL if it is not a tensor */
static buf_view *src_tensor(zval *z, bool arr, zval *hold)
{
	ZVAL_UNDEF(hold);
	if (arr) {
		if (Z_TYPE_P(z) != IS_ARRAY) return NULL;
		zend_call_method_with_1_params(NULL, buf_ce_tensor, NULL, "fromarray", hold, z);
		if (EG(exception)) { zend_clear_exception(); return NULL; }
	} else if (Z_TYPE_P(z) == IS_OBJECT && Z_OBJCE_P(z) == buf_ce_tensor) {
		ZVAL_COPY(hold, z);
	} else if (Z_TYPE_P(z) == IS_OBJECT && instanceof_function(Z_OBJCE_P(z), buf_ce_tensor_agg)) {
		zend_call_method_with_0_params(Z_OBJ_P(z), Z_OBJCE_P(z), NULL, "gettensor", hold);
		if (EG(exception)) return NULL;
		if (Z_TYPE_P(hold) != IS_OBJECT || Z_OBJCE_P(hold) != buf_ce_tensor) { zval_ptr_dtor(hold); ZVAL_UNDEF(hold); return NULL; }
	} else {
		return NULL;
	}
	return buf_view_from(Z_OBJ_P(hold));
}

/* fills env and args from this call's values if they have the kernel's types; false if they do not (or after throwing) */
static bool cc_bind(buf_compiled *bc, cc_kernel *k, zval *args, cc_val *env, cc_val *argv, zval *holds, bool *thrown)
{
	*thrown = false;
	for (int i = 0; i < k->nsrc; i++) {
		const cc_src *s = &k->src[i];
		zval *z = src_zval(bc, s, args);

		ZVAL_UNDEF(&holds[i]);
		ZVAL_DEREF(z);
		if (!s->tensor) {
			cc_val v;

			if (s->dt == CC_T_NULL) {
				if (Z_TYPE_P(z) != IS_NULL) return false;
				continue;
			}
			if (scalar_type(z) != s->dt) return false;
			switch (Z_TYPE_P(z)) {
			case IS_LONG: v.s = Z_LVAL_P(z); break;
			case IS_DOUBLE: v.d = Z_DVAL_P(z); break;
			default: v.i = Z_TYPE_P(z) == IS_TRUE;
			}
			if (s->kind == SRC_ARG) argv[s->index] = v;
			else env[s->env] = v;
			continue;
		}
		if (s->str) {   /* the string's bytes (held by its zval for the call) */
			if (Z_TYPE_P(z) != IS_STRING) return false;
			env[s->env].p = (uint8_t *)Z_STRVAL_P(z);
			env[s->env + 1].s = (int64_t)Z_STRLEN_P(z);
			env[s->env + 2].s = 1;
			continue;
		}
		{
			buf_view *v = src_tensor(z, s->arr, &holds[i]);

			if (EG(exception)) { *thrown = true; return false; }
			if (!v || v->dt != s->dt || v->ndim != s->ndim) return false;
			if (v->slow) {
				zend_throw_error(NULL, "Buffers\\Compiled: a tensor in memory that is not host memory (move it with to())");
				*thrown = true;
				return false;
			}
			if (s->written && !v->writable) {
				zend_throw_error(NULL, "Cannot write to a read-only Tensor");
				*thrown = true;
				return false;
			}
			env[s->env].p = v->data;
			for (int d = 0; d < v->ndim; d++) {
				env[s->env + 1 + d].s = v->shape[d];
				env[s->env + 1 + v->ndim + d].s = v->sbytes[d];
			}
		}
	}
	return true;
}

static void cc_raise(const cc_ctx *ctx)
{
	switch (ctx->err) {
	case CC_E_MOD0: zend_throw_exception(zend_ce_division_by_zero_error, "Modulo by zero", 0); break;
	case CC_E_DIV0: zend_throw_exception(zend_ce_division_by_zero_error, "Division by zero", 0); break;
	case CC_E_SHIFT: zend_throw_exception(zend_ce_arithmetic_error, "Bit shift by negative number", 0); break;
	case CC_E_POWNEG: zend_value_error("Negative exponent in an integer power; convert() to a float dtype first"); break;
	case CC_E_FIT: zend_value_error("Operand %" PRId64 " does not fit the dtype %s of the other operand", ctx->ea, buf_dtypes[ctx->eaux].name); break;
	case CC_E_RANGE: zend_value_error("Index %" PRId64 " is out of range for an axis of length %" PRId64, ctx->ea, ctx->eb); break;
	case CC_E_DEPTH: zend_throw_error(NULL, "Maximum call depth of %d reached. Infinite recursion?", CC_MAX_DEPTH); break;
	default: break;   /* CC_E_THROWN: already set */
	}
}

static void ret_zval(int dt, cc_val v, zval *z)
{
	switch (dt) {
	case BUFFERS_SCALAR_INT: ZVAL_LONG(z, (zend_long)v.s); return;
	case BUFFERS_SCALAR_FLOAT: case BUF_T_FLOAT64: ZVAL_DOUBLE(z, v.d); return;
	case BUFFERS_SCALAR_BOOL: case BUF_T_BOOL: ZVAL_BOOL(z, v.i != 0); return;
	case BUF_T_FLOAT32: case BUF_T_FLOAT16: case BUF_T_BFLOAT16: ZVAL_DOUBLE(z, (double)v.f); return;
	}
	ZVAL_LONG(z, (zend_long)v.s);
}

ZEND_METHOD(Buffers_Compiled, __construct)
{
	zval *fn;
	buf_compiled *bc = compiled_from(Z_OBJ_P(ZEND_THIS));

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_OBJECT_OF_CLASS(fn, zend_ce_closure)
	ZEND_PARSE_PARAMETERS_END();
	if (Z_TYPE(bc->closure) != IS_UNDEF) {
		zend_throw_error(NULL, "Buffers\\Compiled is already constructed");
		RETURN_THROWS();
	}
	if (zend_get_closure_method_def(Z_OBJ_P(fn))->type != ZEND_USER_FUNCTION) {
		zend_value_error("Buffers\\Compiled::__construct(): only closures written in PHP can be compiled");
		RETURN_THROWS();
	}
	ZVAL_COPY(&bc->closure, fn);
}

ZEND_METHOD(Buffers_Compiled, __invoke)
{
	zval *args = NULL, *holds = NULL;
	uint32_t nargs = 0, npass;
	buf_compiled *bc = compiled_from(Z_OBJ_P(ZEND_THIS));
	cc_kernel *k = NULL;
	cc_val *env = NULL, *argv = NULL, rets[CC_MAX_RET];
	cc_ctx ctx;
	bool thrown = false;
	const zend_function *fn;

	ZEND_PARSE_PARAMETERS_START(0, -1)
		Z_PARAM_VARIADIC('*', args, nargs)
	ZEND_PARSE_PARAMETERS_END();
	if (Z_TYPE(bc->closure) == IS_UNDEF) {
		zend_throw_error(NULL, "Buffers\\Compiled is not constructed");
		RETURN_THROWS();
	}
	fn = zend_get_closure_method_def(Z_OBJ(bc->closure));
	npass = nargs < fn->common.num_args ? nargs : fn->common.num_args;
	argv = ecalloc(npass + 1, sizeof(cc_val));
	/* a specialization for the types of this call's values */
	for (int i = 0; i < bc->nspec && !k; i++) {
		cc_kernel *c = bc->spec[i];

		if (bc->spec_nargs[i] != npass) continue;
		env = ecalloc((size_t)c->nenv + 1, sizeof(cc_val));
		holds = ecalloc((size_t)c->nsrc + 1, sizeof(zval));
		if (cc_bind(bc, c, args, env, argv, holds, &thrown)) {
			k = c;
			break;
		}
		for (int s = 0; s < c->nsrc; s++) zval_ptr_dtor(&holds[s]);
		efree(holds);
		efree(env);
		holds = NULL;
		env = NULL;
		if (thrown) goto done;
	}
	if (!k) {
		k = cc_compile(Z_OBJ(bc->closure), args, npass);
		if (!k) goto done;
#ifdef BUF_CC_NATIVE
		if (!getenv("BUFFERS_CC_INTERP")) cc_native_compile(k);   /* test hook: the interpreter, to compare the two */
#endif
		if (bc->nspec == MAX_SPECS) {
			cc_kernel_free(bc->spec[0]);
			memmove(bc->spec, bc->spec + 1, sizeof(cc_kernel *) * (MAX_SPECS - 1));
			memmove(bc->spec_nargs, bc->spec_nargs + 1, sizeof(uint32_t) * (MAX_SPECS - 1));
			bc->nspec--;
		}
		bc->spec[bc->nspec] = k;
		bc->spec_nargs[bc->nspec++] = npass;
		env = ecalloc((size_t)k->nenv + 1, sizeof(cc_val));
		holds = ecalloc((size_t)k->nsrc + 1, sizeof(zval));
		if (!cc_bind(bc, k, args, env, argv, holds, &thrown)) {
			if (!thrown) zend_throw_error(NULL, "Buffers\\Compiled: the values changed while compiling");
			goto done;
		}
	}
	memset(&ctx, 0, sizeof ctx);
	ctx.env = env;
	ctx.args = argv;
	ctx.rets = rets;
	memset(rets, 0, sizeof rets);
#ifdef BUF_CC_NATIVE
	if (k->native) cc_native_run(k, &ctx);
	else
#endif
	cc_interp((const cc_func *const *)k->fns, 0, &ctx, NULL);
	if (ctx.err) {
		cc_raise(&ctx);
		goto done;
	}
	if (k->ret_list) {
		array_init_size(return_value, (uint32_t)k->nret);
		for (int i = 0; i < k->nret; i++) {
			zval z;

			ret_zval(k->retdt[i], rets[i], &z);
			add_next_index_zval(return_value, &z);
		}
	} else if (k->nret) {
		ret_zval(k->retdt[0], rets[0], return_value);
	}
done:
	if (holds) {
		for (int s = 0; s < k->nsrc; s++) zval_ptr_dtor(&holds[s]);
		efree(holds);
	}
	if (env) efree(env);
	efree(argv);
}

ZEND_BEGIN_ARG_INFO_EX(arginfo_compiled_construct, 0, 0, 1)
	ZEND_ARG_OBJ_INFO(0, fn, Closure, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_compiled_invoke, 0, 0, IS_MIXED, 0)
	ZEND_ARG_VARIADIC_TYPE_INFO(0, args, IS_MIXED, 0)
ZEND_END_ARG_INFO()

static const zend_function_entry compiled_methods[] = {
	ZEND_ME(Buffers_Compiled, __construct, arginfo_compiled_construct, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_Compiled, __invoke, arginfo_compiled_invoke, ZEND_ACC_PUBLIC)
	ZEND_FE_END
};

void buf_extras_minit(void)
{
	zend_class_entry ce;

	INIT_NS_CLASS_ENTRY(ce, "Buffers", "Compiled", compiled_methods);
	buf_ce_compiled = zend_register_internal_class_ex(&ce, NULL);
	buf_ce_compiled->ce_flags |= ZEND_ACC_FINAL | ZEND_ACC_NO_DYNAMIC_PROPERTIES | ZEND_ACC_NOT_SERIALIZABLE;
	buf_ce_compiled->create_object = compiled_new;
	memcpy(&compiled_handlers, zend_get_std_object_handlers(), sizeof compiled_handlers);
	compiled_handlers.offset = XtOffsetOf(buf_compiled, std);
	compiled_handlers.free_obj = compiled_free;
	compiled_handlers.get_gc = compiled_gc;
	compiled_handlers.clone_obj = NULL;
}
