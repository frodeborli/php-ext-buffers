/* Virtual, read-only properties for internal classes, backed by internal get hooks.
 *
 * TensorInterface declares `public ?DType $dtype { get; }` and friends. For an internal class to satisfy such
 * an interface property without storing anything per object, it declares the property as virtual
 * (no slot in the object) with an internal `get` hook, exactly as a PHP 8.4 class would with
 * `public int $ndim { get => ...; }`. The hook is an ordinary internal function that reads the
 * value out of the object when the property is read, so reading allocates only the value returned and
 * creating a view allocates nothing for its properties. Writes fail in the engine ("is read-only").
 * gen_stub.php cannot express hooks, so the property infos and hook functions are built here.
 *
 * PHP 8.3 has no property hooks (and no interface properties). There the same get (and set) functions are reached
 * through internal __get, __set, __isset and __unset methods that this file gives each class declaring properties:
 * the standard read_property handler calls them for a name the class does not declare, so it works for every class
 * whatever its object handlers (other extensions' classes too, through the C API) and subclasses inherit them. The
 * properties are not declared on 8.3: property_exists() and Reflection do not see them, and interfaces declare none. */
#include "php_buffers.h"
#include "zend_exceptions.h"

/* An internal function of class `ce` that is not a method of it (not in its function table). With a magic name
 * (__get ...) zend_register_functions() also makes it the class's magic method. */
static zend_function *make_function(zend_class_entry *ce, const char *fname, zif_handler handler,
	const zend_internal_arg_info *arginfo, uint32_t num_args, uint32_t flags)
{
	zend_function_entry fe[2] = {{0}, {0}};
	HashTable tmp;
	zend_function *fn = NULL;

	fe[0].fname = fname;
	fe[0].handler = handler;
	fe[0].arg_info = arginfo;
	fe[0].num_args = num_args;
	fe[0].flags = flags;
	zend_hash_init(&tmp, 1, NULL, NULL, 1);
	zend_register_functions(ce, fe, &tmp, MODULE_PERSISTENT);
	ZEND_HASH_FOREACH_PTR(&tmp, fn) { break; } ZEND_HASH_FOREACH_END();
	zend_hash_destroy(&tmp);
	ZEND_ASSERT(fn);
	return fn;
}

static void free_function(zend_function *fn)
{
	if (!fn) return;
	if (fn->common.fn_flags & (ZEND_ACC_HAS_RETURN_TYPE | ZEND_ACC_HAS_TYPE_HINTS)) {
		free((void *)(fn->internal_function.arg_info - 1));
	}
	free(fn);
}

#if PHP_VERSION_ID >= 80400

typedef struct { zend_property_info *pi; zend_function *fn[2]; } hook_rec;
static hook_rec *recs;
static int nrecs, caprecs;

void buf_declare_hooked_props(zend_class_entry *ce, const buf_prop_def *defs, int n)
{
	bool iface = ce->ce_flags & ZEND_ACC_INTERFACE;

	for (int i = 0; i < n; i++) {
		const buf_prop_def *d = &defs[i];
		zval undef;
		char fname[64];
		zend_string *name = zend_string_init_interned(d->name, strlen(d->name), 1);
		zend_property_info *pi;

		ZVAL_UNDEF(&undef);
		pi = zend_declare_typed_property(ce, name, &undef,
			ZEND_ACC_PUBLIC | ZEND_ACC_VIRTUAL | (iface ? ZEND_ACC_ABSTRACT : 0), NULL, d->type);

		if (nrecs == caprecs) {
			caprecs = caprecs ? caprecs * 2 : 16;
			recs = realloc(recs, sizeof(hook_rec) * caprecs);
		}
		pi->hooks = calloc(1, ZEND_PROPERTY_HOOK_STRUCT_SIZE);
		recs[nrecs].pi = pi;
		recs[nrecs].fn[0] = recs[nrecs].fn[1] = NULL;
		for (int k = 0; k < (d->set ? 2 : 1); k++) {
			bool is_set = k == 1;
			zend_function *fn;

			snprintf(fname, sizeof fname, "$%s::%s", d->name, is_set ? "set" : "get");
			fn = make_function(ce, fname, iface ? NULL : (is_set ? d->set : d->get), is_set ? d->set_arginfo : d->arginfo,
				is_set ? 1 : d->num_args, ZEND_ACC_PUBLIC | (iface ? ZEND_ACC_ABSTRACT : 0));
			pi->hooks[is_set ? ZEND_PROPERTY_HOOK_SET : ZEND_PROPERTY_HOOK_GET] = fn;
			fn->common.prop_info = pi;
			recs[nrecs].fn[k] = fn;
		}
		ce->num_hooked_props++;
		nrecs++;
	}
}

/* The class destructor does not know about internal hooks: release them here. */
void buf_props_mshutdown(void)
{
	for (int i = 0; i < nrecs; i++) {
		for (int k = 0; k < 2; k++) {
			free_function(recs[i].fn[k]);
		}
		free(recs[i].pi->hooks);
		recs[i].pi->hooks = NULL;
	}
	free(recs);
	recs = NULL;
	nrecs = caprecs = 0;
}

#else /* PHP 8.3 */

typedef struct { zend_function *get, *set; } prop_rec;

/* class (by pointer) -> HashTable of name -> prop_rec*, persistent; and every function made, to free them */
static HashTable class_props;
static bool class_props_inited;
static zend_function **fns;
static int nfns, capfns;

static void keep_function(zend_function *fn)
{
	if (nfns == capfns) {
		capfns = capfns ? capfns * 2 : 32;
		fns = realloc(fns, sizeof(*fns) * capfns);
	}
	fns[nfns++] = fn;
}

static void free_props_table(zval *zv)
{
	HashTable *t = Z_PTR_P(zv);
	prop_rec *p;

	ZEND_HASH_FOREACH_PTR(t, p) { free(p); } ZEND_HASH_FOREACH_END();
	zend_hash_destroy(t);
	free(t);
}

/* The property `name` of the object's class or of a parent class, or NULL */
static prop_rec *find_prop(zend_class_entry *ce, zend_string *name)
{
	for (; ce; ce = ce->parent) {
		HashTable *t = zend_hash_index_find_ptr(&class_props, (zend_ulong)(uintptr_t)ce);
		prop_rec *p;

		if (t && (p = zend_hash_find_ptr(t, name))) {
			return p;
		}
	}
	return NULL;
}

static ZEND_FUNCTION(buf_magic_get)
{
	zend_string *name;
	zend_object *obj = Z_OBJ_P(ZEND_THIS);
	prop_rec *p;

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_STR(name)
	ZEND_PARSE_PARAMETERS_END();
	p = find_prop(obj->ce, name);
	if (!p) {
		zend_error(E_WARNING, "Undefined property: %s::$%s", ZSTR_VAL(obj->ce->name), ZSTR_VAL(name));
		RETURN_NULL();
	}
	zend_call_known_instance_method(p->get, obj, return_value, 0, NULL);
}

static ZEND_FUNCTION(buf_magic_set)
{
	zend_string *name;
	zval *value;
	zend_object *obj = Z_OBJ_P(ZEND_THIS);
	prop_rec *p;

	ZEND_PARSE_PARAMETERS_START(2, 2)
		Z_PARAM_STR(name)
		Z_PARAM_ZVAL(value)
	ZEND_PARSE_PARAMETERS_END();
	p = find_prop(obj->ce, name);
	if (!p) {
		zend_throw_error(NULL, "Cannot create dynamic property %s::$%s", ZSTR_VAL(obj->ce->name), ZSTR_VAL(name));
	} else if (!p->set) {
		zend_throw_error(NULL, "Property %s::$%s is read-only", ZSTR_VAL(obj->ce->name), ZSTR_VAL(name));
	} else {
		zval rv;

		ZVAL_UNDEF(&rv);
		zend_call_known_instance_method_with_1_params(p->set, obj, &rv, value);
		zval_ptr_dtor(&rv);
	}
}

static ZEND_FUNCTION(buf_magic_isset)
{
	zend_string *name;
	zend_object *obj = Z_OBJ_P(ZEND_THIS);
	prop_rec *p;
	zval rv;

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_STR(name)
	ZEND_PARSE_PARAMETERS_END();
	p = find_prop(obj->ce, name);
	if (!p) {
		RETURN_FALSE;
	}
	ZVAL_UNDEF(&rv);
	zend_call_known_instance_method(p->get, obj, &rv, 0, NULL);
	if (EG(exception)) {
		/* isset() never throws for a property that exists but has no value (`$value` of a view with axes) */
		zend_clear_exception();
		zval_ptr_dtor(&rv);
		RETURN_FALSE;
	}
	RETVAL_BOOL(Z_TYPE(rv) != IS_NULL && Z_TYPE(rv) != IS_UNDEF);
	zval_ptr_dtor(&rv);
}

static ZEND_FUNCTION(buf_magic_unset)
{
	zend_string *name;
	zend_object *obj = Z_OBJ_P(ZEND_THIS);

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_STR(name)
	ZEND_PARSE_PARAMETERS_END();
	if (find_prop(obj->ce, name)) {
		zend_throw_error(NULL, "Cannot unset property %s::$%s", ZSTR_VAL(obj->ce->name), ZSTR_VAL(name));
	}
}

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(ai_magic_get, 0, 1, IS_MIXED, 0)
	ZEND_ARG_TYPE_INFO(0, name, IS_STRING, 0)
ZEND_END_ARG_INFO()
ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(ai_magic_set, 0, 2, IS_VOID, 0)
	ZEND_ARG_TYPE_INFO(0, name, IS_STRING, 0)
	ZEND_ARG_TYPE_INFO(0, value, IS_MIXED, 0)
ZEND_END_ARG_INFO()
ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(ai_magic_isset, 0, 1, _IS_BOOL, 0)
	ZEND_ARG_TYPE_INFO(0, name, IS_STRING, 0)
ZEND_END_ARG_INFO()
ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(ai_magic_unset, 0, 1, IS_VOID, 0)
	ZEND_ARG_TYPE_INFO(0, name, IS_STRING, 0)
ZEND_END_ARG_INFO()

void buf_declare_hooked_props(zend_class_entry *ce, const buf_prop_def *defs, int n)
{
	HashTable *t;

	if (ce->ce_flags & ZEND_ACC_INTERFACE) {
		return;   /* no interface properties before 8.4: the implementing classes declare theirs */
	}
	if (!class_props_inited) {
		zend_hash_init(&class_props, 8, NULL, free_props_table, 1);
		class_props_inited = true;
	}
	t = zend_hash_index_find_ptr(&class_props, (zend_ulong)(uintptr_t)ce);
	if (!t) {
		t = malloc(sizeof(HashTable));
		zend_hash_init(t, 8, NULL, NULL, 1);
		zend_hash_index_add_new_ptr(&class_props, (zend_ulong)(uintptr_t)ce, t);
		keep_function(make_function(ce, "__get", ZEND_FN(buf_magic_get), ai_magic_get, 1, ZEND_ACC_PUBLIC));
		keep_function(make_function(ce, "__set", ZEND_FN(buf_magic_set), ai_magic_set, 2, ZEND_ACC_PUBLIC));
		keep_function(make_function(ce, "__isset", ZEND_FN(buf_magic_isset), ai_magic_isset, 1, ZEND_ACC_PUBLIC));
		keep_function(make_function(ce, "__unset", ZEND_FN(buf_magic_unset), ai_magic_unset, 1, ZEND_ACC_PUBLIC));
	}
	for (int i = 0; i < n; i++) {
		const buf_prop_def *d = &defs[i];
		char fname[64];
		prop_rec *p = calloc(1, sizeof *p);

		snprintf(fname, sizeof fname, "$%s::get", d->name);
		p->get = make_function(ce, fname, d->get, d->arginfo, d->num_args, ZEND_ACC_PUBLIC);
		keep_function(p->get);
		if (d->set) {
			snprintf(fname, sizeof fname, "$%s::set", d->name);
			p->set = make_function(ce, fname, d->set, d->set_arginfo, 1, ZEND_ACC_PUBLIC);
			keep_function(p->set);
		}
		zend_hash_str_update_ptr(t, d->name, strlen(d->name), p);
	}
}

void buf_props_mshutdown(void)
{
	if (class_props_inited) {
		zend_hash_destroy(&class_props);
		class_props_inited = false;
	}
	for (int i = 0; i < nfns; i++) {
		free_function(fns[i]);
	}
	free(fns);
	fns = NULL;
	nfns = capfns = 0;
}

#endif
