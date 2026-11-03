/* Virtual, read-only properties for internal classes, backed by internal get hooks.
 *
 * Tensor declares `public ?DType $dtype { get; }` and friends. For an internal class to satisfy such
 * an interface property without storing anything per object, it declares the property as virtual
 * (no slot in the object) with an internal `get` hook, exactly as a PHP 8.4 class would with
 * `public int $ndim { get => ...; }`. The hook is an ordinary internal function that reads the
 * value out of the object when the property is read, so reading allocates only the value returned and
 * creating a view allocates nothing for its properties. Writes fail in the engine ("is read-only").
 * gen_stub.php cannot express hooks, so the property infos and hook functions are built here. */
#include "php_buffers.h"

typedef struct { zend_property_info *pi; zend_function *fn; } hook_rec;
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
		zend_function_entry fe[2] = {{0}, {0}};
		HashTable tmp;
		zend_function *fn = NULL;
		zend_property_info *pi;

		ZVAL_UNDEF(&undef);
		pi = zend_declare_typed_property(ce, name, &undef,
			ZEND_ACC_PUBLIC | ZEND_ACC_VIRTUAL | (iface ? ZEND_ACC_ABSTRACT : 0), NULL, d->type);

		snprintf(fname, sizeof fname, "$%s::get", d->name);
		fe[0].fname = fname;
		fe[0].handler = iface ? NULL : d->get;
		fe[0].arg_info = (const zend_internal_arg_info *)d->arginfo;
		fe[0].num_args = d->num_args;
		fe[0].flags = ZEND_ACC_PUBLIC | (iface ? ZEND_ACC_ABSTRACT : 0);
		zend_hash_init(&tmp, 1, NULL, NULL, 1);
		zend_register_functions(ce, fe, &tmp, MODULE_PERSISTENT);
		ZEND_HASH_FOREACH_PTR(&tmp, fn) { break; } ZEND_HASH_FOREACH_END();
		zend_hash_destroy(&tmp);
		ZEND_ASSERT(fn);
		if (nrecs == caprecs) {
			caprecs = caprecs ? caprecs * 2 : 16;
			recs = realloc(recs, sizeof(hook_rec) * caprecs);
		}

		pi->hooks = calloc(1, ZEND_PROPERTY_HOOK_STRUCT_SIZE);
		pi->hooks[ZEND_PROPERTY_HOOK_GET] = fn;
		fn->common.prop_info = pi;
		ce->num_hooked_props++;
		recs[nrecs].pi = pi;
		recs[nrecs++].fn = fn;
	}
}

/* The class destructor does not know about internal hooks: release them here. */
void buf_props_mshutdown(void)
{
	for (int i = 0; i < nrecs; i++) {
		zend_function *fn = recs[i].fn;

		if (fn->common.fn_flags & (ZEND_ACC_HAS_RETURN_TYPE | ZEND_ACC_HAS_TYPE_HINTS)) {
			free((void *)(fn->internal_function.arg_info - 1));
		}
		free(fn);
		free(recs[i].pi->hooks);
		recs[i].pi->hooks = NULL;
	}
	free(recs);
	recs = NULL;
	nrecs = caprecs = 0;
}
