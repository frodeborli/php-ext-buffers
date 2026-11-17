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
#include "compiler/zarr.h"
#include "extras.h"

#define MAX_SPECS 16

typedef struct {
	zval closure;
	zend_function *call;   /* what a call of the object runs (call_fn()) */
	zend_object std;
} buf_compiled;

/* Compiled code, shared by every Buffers\Compiled of the same function for the rest of the request (a closure made
 * again, or a method of another object of the same class, finds it): per function code, class of the bound $this (its
 * properties' layout) and number of arguments, one kernel per combination of the types of the values it reads. */
typedef struct {
	const zend_op *code;
	const zend_class_entry *ce;
	uint32_t npass;
	cc_kernel *spec[MAX_SPECS];
	int nspec;
	zval keep;   /* a closure of the same code, without $this: the code stays, so its address is not reused */
} cc_entry;

ZEND_TLS cc_entry *cc_cache;
ZEND_TLS int cc_ncache, cc_ccache;

static cc_entry *cache_entry(const zend_function *fn, const zend_class_entry *ce, uint32_t npass)
{
	cc_entry *e;

	for (int i = 0; i < cc_ncache; i++) {
		e = &cc_cache[i];
		if (e->code == fn->op_array.opcodes && e->ce == ce && e->npass == npass) return e;
	}
	if (cc_ncache == cc_ccache) {
		cc_ccache = cc_ccache ? cc_ccache * 2 : 16;
		cc_cache = erealloc(cc_cache, sizeof(cc_entry) * (size_t)cc_ccache);
	}
	e = &cc_cache[cc_ncache++];
	memset(e, 0, sizeof *e);
	e->code = fn->op_array.opcodes;
	e->ce = ce;
	e->npass = npass;
	ZVAL_UNDEF(&e->keep);
	if (fn->common.fn_flags & ZEND_ACC_CLOSURE) {
		zend_create_closure(&e->keep, (zend_function *)fn, fn->common.scope, fn->common.scope, NULL);
	}
	return e;
}

/* What $compiled(...) and ->__invoke() run: __invoke, with the closure's parameters as references where the closure takes
 * them by reference, then any others by value. One per function code for the rest of the request, not part of the
 * object: the engine reads the function after a call has released the object. */
typedef struct cc_callfn {
	const zend_op *code;
	zend_function fn;
	zend_internal_arg_info *ai;
	zval keep;   /* (as cc_entry.keep) */
	struct cc_callfn *next;
} cc_callfn;

ZEND_TLS cc_callfn *cc_calls;

ZEND_METHOD(Buffers_Compiled, __invoke);
static zend_class_entry *buf_ce_compiled;

static zend_function *call_fn(const zend_function *f)
{
	cc_callfn *c;
	uint32_t n = f->common.num_args;
	zend_internal_function *cf;

	for (c = cc_calls; c; c = c->next) {
		if (c->code == f->op_array.opcodes) return &c->fn;
	}
	c = ecalloc(1, sizeof *c);
	c->code = f->op_array.opcodes;
	ZVAL_UNDEF(&c->keep);
	if (f->common.fn_flags & ZEND_ACC_CLOSURE) zend_create_closure(&c->keep, (zend_function *)f, f->common.scope, f->common.scope, NULL);
	c->ai = ecalloc(n + 2, sizeof *c->ai);
	c->ai[0].type = (zend_type)ZEND_TYPE_INIT_CODE(IS_MIXED, 0, 0);   /* (the return) */
	for (uint32_t i = 0; i < n; i++) {
		uint32_t mode = ZEND_ARG_SEND_MODE(&f->common.arg_info[i]);

		c->ai[1 + i].name = ZSTR_VAL(f->common.arg_info[i].name);
		c->ai[1 + i].type = (zend_type)ZEND_TYPE_INIT_CODE(IS_MIXED, 0, _ZEND_ARG_INFO_FLAGS(mode, 0, 0));
		if (mode && i < MAX_ARG_FLAG_NUM) ZEND_SET_ARG_FLAG(&c->fn, i + 1, mode);
	}
	c->ai[1 + n].name = "args";
	c->ai[1 + n].type = (zend_type)ZEND_TYPE_INIT_CODE(IS_MIXED, 0, _ZEND_ARG_INFO_FLAGS(0, 1, 0));
	cf = &c->fn.internal_function;
	cf->type = ZEND_INTERNAL_FUNCTION;
	cf->fn_flags = ZEND_ACC_PUBLIC | ZEND_ACC_VARIADIC;
	cf->function_name = ZSTR_KNOWN(ZEND_STR_MAGIC_INVOKE);
	cf->scope = buf_ce_compiled;
	cf->num_args = n;
	cf->required_num_args = 0;
	cf->arg_info = c->ai + 1;
	cf->handler = zim_Buffers_Compiled___invoke;
	c->next = cc_calls;
	cc_calls = c;
	return &c->fn;
}

void buf_extras_rshutdown(void)
{
	while (cc_calls) {
		cc_callfn *c = cc_calls;

		cc_calls = c->next;
		efree(c->ai);
		zval_ptr_dtor(&c->keep);
		efree(c);
	}
	for (int i = 0; i < cc_ncache; i++) {
		for (int k = 0; k < cc_cache[i].nspec; k++) cc_kernel_free(cc_cache[i].spec[k]);
		zval_ptr_dtor(&cc_cache[i].keep);
	}
	if (cc_cache) efree(cc_cache);
	cc_cache = NULL;
	cc_ncache = cc_ccache = 0;
}

static zend_object_handlers compiled_handlers;

static inline buf_compiled *compiled_from(zend_object *o) { return (buf_compiled *)((char *)o - XtOffsetOf(buf_compiled, std)); }

static zend_object *compiled_new(zend_class_entry *ce)
{
	buf_compiled *c = zend_object_alloc(sizeof(buf_compiled), ce);

	zend_object_std_init(&c->std, ce);
	object_properties_init(&c->std, ce);
	c->std.handlers = &compiled_handlers;
	ZVAL_UNDEF(&c->closure);
	c->call = NULL;
	return &c->std;
}

static void compiled_free(zend_object *o)
{
	buf_compiled *c = compiled_from(o);

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
static buf_view *src_tensor(zval *z, zval *hold)
{
	ZVAL_UNDEF(hold);
	if (Z_TYPE_P(z) == IS_OBJECT && Z_OBJCE_P(z) == buf_ce_tensor) {
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

/* a PHP array of a source (z: where it is) for this call: checked against what the kernel reads, and separated first if
 * the code writes into it. Its zvals are read in place */
static bool bind_array(const cc_src *s, zval *z, cc_val *env, zval *hold, zval **where)
{
	int64_t shape[ZA_MAX_NDIM];
	unsigned seen = 0;
	int r, ak;

	if (s->written) {
		if (s->byref || s->kind == SRC_PROP) {
			ZVAL_DEREF(z);   /* the array itself */
		} else if (s->kind == SRC_ARG && !Z_ISREF_P(z)) {
			/* the call's own copy */
		} else {   /* a capture by value, or an argument by value given as a reference: a copy for this call */
			ZVAL_DEREF(z);
			ZVAL_COPY(hold, z);
			z = hold;
		}
		r = za_check(z, 0, s->ndim, ZA_WRITE, shape, &seen);
	} else {
		ZVAL_DEREF(z);
		r = za_check(z, 0, s->ndim, ZA_READ, shape, &seen);
		if (r == 1) {   /* a list that is not packed: a packed copy for this call */
			ZVAL_COPY(hold, z);
			z = hold;
			seen = 0;
			r = za_check(z, 0, s->ndim, ZA_WRITE, shape, &seen);
		}
	}
	if (r != 0 || (ak = za_kind(seen)) < 0) return false;
	*where = z;
	if (ak != AK_EMPTY && ak != s->ak && !(s->ak == AK_NUM && (ak == AK_INT || ak == AK_FLOAT))) return false;
	{
		zval *a = Z_ARRVAL_P(z)->nNumUsed ? Z_ARRVAL_P(z)->arPacked : NULL;

		env[s->env].p = (uint8_t *)a;
		for (int d = 0; d < s->ndim; d++) {
			env[s->env + 1 + d].s = shape[d];
			env[s->env + 1 + s->ndim + d].s = (int64_t)sizeof(zval);
		}
		env[s->env + 1 + 2 * s->ndim].s = (int64_t)(uintptr_t)a;
	}
	return true;
}

/* fills env and args from this call's values if they have the kernel's types; false if they do not (or after throwing) */
static bool cc_bind(buf_compiled *bc, cc_kernel *k, zval *args, cc_val *env, cc_val *argv, zval *holds, zval **where, bool *thrown)
{
	*thrown = false;
	for (int i = 0; i < k->nsrc; i++) {
		const cc_src *s = &k->src[i];
		zval *z = src_zval(bc, s, args);

		ZVAL_UNDEF(&holds[i]);
		if (s->arr) {
			if (!bind_array(s, z, env, &holds[i], &where[i])) return false;
			continue;
		}
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
			buf_view *v = src_tensor(z, &holds[i]);

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
	case CC_E_LOGBASE: zend_value_error("log(): Argument #2 ($base) must be greater than 0"); break;
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

static zend_result compiled_get_closure(zend_object *obj, zend_class_entry **ce_ptr, zend_function **fptr_ptr, zend_object **obj_ptr, bool check_only)
{
	buf_compiled *bc = compiled_from(obj);

	if (Z_TYPE(bc->closure) == IS_UNDEF) return zend_std_get_closure(obj, ce_ptr, fptr_ptr, obj_ptr, check_only);
	*ce_ptr = obj->ce;
	*fptr_ptr = bc->call;
	*obj_ptr = obj;
	return SUCCESS;
}

/* ->__invoke() too takes the closure's by-reference parameters */
static zend_function *compiled_get_method(zend_object **obj, zend_string *name, const zval *key)
{
	buf_compiled *bc = compiled_from(*obj);

	if (Z_TYPE(bc->closure) != IS_UNDEF && zend_string_equals_literal_ci(name, "__invoke")) return bc->call;
	return zend_std_get_method(obj, name, key);
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
	bc->call = call_fn(zend_get_closure_method_def(Z_OBJ_P(fn)));
}

ZEND_METHOD(Buffers_Compiled, __invoke)
{
	zval *args = NULL, *holds = NULL;
	uint32_t nargs = 0, npass;
	buf_compiled *bc = compiled_from(Z_OBJ_P(ZEND_THIS));
	cc_kernel *k = NULL;
	cc_val *env = NULL, *argv = NULL, rets[CC_MAX_RET];
	zval **where = NULL;
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
	{
		zval *thisz = zend_get_closure_this_ptr(&bc->closure);
		cc_entry *e = cache_entry(fn, thisz && Z_TYPE_P(thisz) == IS_OBJECT ? Z_OBJCE_P(thisz) : NULL, npass);

		for (int i = 0; i < e->nspec; i++) {
			cc_kernel *c = e->spec[i];

			env = ecalloc((size_t)c->nenv + 1, sizeof(cc_val));
			holds = ecalloc((size_t)c->nsrc + 1, sizeof(zval));
			where = ecalloc((size_t)c->nsrc + 1, sizeof(zval *));
			if (cc_bind(bc, c, args, env, argv, holds, where, &thrown)) {
				k = c;
				break;
			}
			for (int h = 0; h < c->nsrc; h++) zval_ptr_dtor(&holds[h]);
			efree(holds);
			efree(env);
			efree(where);
			holds = NULL;
			env = NULL;
			where = NULL;
			if (thrown) goto done;
		}
		if (!k) {
			k = cc_compile(Z_OBJ(bc->closure), args, npass);
			if (!k) goto done;
#ifdef BUF_CC_NATIVE
			if (!getenv("BUFFERS_CC_INTERP")) cc_native_compile(k);   /* test hook: the interpreter, to compare the two */
#endif
			e = cache_entry(fn, thisz && Z_TYPE_P(thisz) == IS_OBJECT ? Z_OBJCE_P(thisz) : NULL, npass);   /* (compiling may call PHP code) */
			if (e->nspec == MAX_SPECS) {   /* the oldest goes */
				cc_kernel_free(e->spec[0]);
				memmove(e->spec, e->spec + 1, sizeof(cc_kernel *) * (MAX_SPECS - 1));
				e->nspec--;
			}
			e->spec[e->nspec++] = k;
			env = ecalloc((size_t)k->nenv + 1, sizeof(cc_val));
			holds = ecalloc((size_t)k->nsrc + 1, sizeof(zval));
			where = ecalloc((size_t)k->nsrc + 1, sizeof(zval *));
			if (!cc_bind(bc, k, args, env, argv, holds, where, &thrown)) {
				if (!thrown) zend_throw_error(NULL, "Buffers\\Compiled: the values changed while compiling");
				goto done;
			}
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
	} else if (k->ret_src) {
		ZVAL_COPY(return_value, where[k->ret_src - 1]);
	}
done:
	if (holds) {
		for (int s = 0; s < k->nsrc; s++) zval_ptr_dtor(&holds[s]);
		efree(holds);
	}
	if (env) efree(env);
	if (where) efree(where);
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
	compiled_handlers.get_closure = compiled_get_closure;
	compiled_handlers.get_method = compiled_get_method;
}
