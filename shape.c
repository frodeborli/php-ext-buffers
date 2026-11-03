/* Shape: the dimensions of a tensor, each an int or a 0-d Tensor (a symbolic dimension, for
 * extensions that build deferred computations). Immutable. */
#include "php_buffers.h"
#include "zend_exceptions.h"
#include "zend_interfaces.h"
#include "buffers_arginfo.h"

zend_class_entry *buf_ce_shape;
static zend_object_handlers shape_handlers;

static zend_object *shape_create(zend_class_entry *ce)
{
	buf_shape *s = zend_object_alloc(sizeof(buf_shape), ce);

	zend_object_std_init(&s->std, ce);
	s->std.handlers = &shape_handlers;
	s->n = 0;
	s->dims = NULL;
	return &s->std;
}

static void shape_free(zend_object *o)
{
	buf_shape *s = buf_shape_from(o);

	for (uint32_t i = 0; i < s->n; i++) {
		zval_ptr_dtor(&s->dims[i]);
	}
	if (s->dims) efree(s->dims);
	zend_object_std_dtor(o);
}


/* validates one dimension; `pos` is for the message. false after throwing */
static bool dim_ok(zval *z, uint32_t pos)
{
	zval rv, *nd;

	ZVAL_DEREF(z);
	if (Z_TYPE_P(z) == IS_LONG) {
		if (Z_LVAL_P(z) < 0) {
			zend_value_error("Shape dimension %u must not be negative, " ZEND_LONG_FMT " given", pos, Z_LVAL_P(z));
			return false;
		}
		return true;
	}
	if (Z_TYPE_P(z) != IS_OBJECT || !instanceof_function(Z_OBJCE_P(z), buf_ce_tensor)) {
		zend_type_error("Shape dimension %u must be of type int|Buffers\\Tensor, %s given", pos, zend_zval_type_name(z));
		return false;
	}
	nd = zend_read_property(Z_OBJCE_P(z), Z_OBJ_P(z), "ndim", sizeof("ndim") - 1, 0, &rv);
	if (EG(exception)) return false;
	if (Z_TYPE_P(nd) != IS_LONG || Z_LVAL_P(nd) != 0) {
		zend_value_error("Shape dimension %u must be a 0-d Tensor", pos);
		return false;
	}
	return true;
}

static void shape_fill(buf_shape *s, uint32_t n, const zval *src)
{
	s->dims = n ? emalloc(sizeof(zval) * n) : NULL;
	for (uint32_t i = 0; i < n; i++) {
		zval *e = (zval *)src + i;

		ZVAL_DEREF(e);
		ZVAL_COPY(&s->dims[i], e);
	}
	s->n = n;
}

zend_object *buffers_shape_create(uint32_t n, const zval *dims)
{
	zend_object *o;

	for (uint32_t i = 0; i < n; i++) {
		zval tmp;
		ZVAL_COPY_VALUE(&tmp, &dims[i]);
		if (!dim_ok(&tmp, i)) return NULL;
	}
	o = shape_create(buf_ce_shape);
	shape_fill(buf_shape_from(o), n, dims);
	return o;
}

const zval *buffers_shape_dims(zend_object *shape, uint32_t *n)
{
	buf_shape *s = buf_shape_from(shape);

	*n = s->n;
	return s->dims;
}

zend_object *buf_shape_new_ints(int n, const int64_t *dims)
{
	zend_object *o = shape_create(buf_ce_shape);
	buf_shape *s = buf_shape_from(o);

	s->dims = n ? emalloc(sizeof(zval) * n) : NULL;
	for (int i = 0; i < n; i++) ZVAL_LONG(&s->dims[i], dims[i]);
	s->n = n;
	return o;
}

#define SELF buf_shape_from(Z_OBJ_P(ZEND_THIS))

PHP_METHOD(Buffers_Shape, __construct)
{
	HashTable *ht;
	buf_shape *s = SELF;
	zval *z, *vals;
	uint32_t n, i = 0;

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_ARRAY_HT(ht)
	ZEND_PARSE_PARAMETERS_END();

	if (s->dims || s->n) {
		zend_throw_error(NULL, "Shape is already constructed");
		RETURN_THROWS();
	}
	if (!zend_array_is_list(ht)) {
		zend_argument_value_error(1, "must be a list");
		RETURN_THROWS();
	}
	n = zend_hash_num_elements(ht);
	vals = safe_emalloc(n, sizeof(zval), 0);
	ZEND_HASH_FOREACH_VAL(ht, z) {
		ZVAL_COPY_VALUE(&vals[i], z);
		if (!dim_ok(&vals[i], i)) {
			efree(vals);
			RETURN_THROWS();
		}
		i++;
	} ZEND_HASH_FOREACH_END();
	shape_fill(s, n, vals);
	efree(vals);
}

static bool shape_concrete(const buf_shape *s)
{
	for (uint32_t i = 0; i < s->n; i++) {
		if (Z_TYPE(s->dims[i]) != IS_LONG) return false;
	}
	return true;
}

PHP_METHOD(Buffers_Shape, isConcrete) { ZEND_PARSE_PARAMETERS_NONE(); RETURN_BOOL(shape_concrete(SELF)); }
PHP_METHOD(Buffers_Shape, count) { ZEND_PARSE_PARAMETERS_NONE(); RETURN_LONG(SELF->n); }

PHP_METHOD(Buffers_Shape, size)
{
	buf_shape *s = SELF;
	int64_t p = 1;

	ZEND_PARSE_PARAMETERS_NONE();
	if (!shape_concrete(s)) {
		zend_throw_error(NULL, "Cannot compute the size of a shape with symbolic dimensions: no extension that builds deferred computations is loaded");
		RETURN_THROWS();
	}
	for (uint32_t i = 0; i < s->n; i++) {
		if (buf_mul_ovf(p, Z_LVAL(s->dims[i]), &p)) {
			zend_value_error("The size of the shape exceeds PHP_INT_MAX");
			RETURN_THROWS();
		}
	}
	RETURN_LONG(p);
}

PHP_METHOD(Buffers_Shape, toArray)
{
	buf_shape *s = SELF;

	ZEND_PARSE_PARAMETERS_NONE();
	array_init_size(return_value, s->n);
	for (uint32_t i = 0; i < s->n; i++) {
		Z_TRY_ADDREF(s->dims[i]);
		zend_hash_next_index_insert_new(Z_ARRVAL_P(return_value), &s->dims[i]);
	}
}

/* ---- ArrayAccess handlers ---------------------------------------------------------------- */

static zval *shape_read(zend_object *o, zval *off, int type, zval *rv)
{
	buf_shape *s = buf_shape_from(o);
	zend_long i;

	if (!off || Z_TYPE_P(off) != IS_LONG) {
		if (type == BP_VAR_IS) { ZVAL_NULL(rv); return rv; }
		if (!off) zend_throw_error(NULL, "Cannot append to a Shape");
		else zend_type_error("Shape offset must be of type int, %s given", zend_zval_type_name(off));
		return NULL;
	}
	i = Z_LVAL_P(off);
	if (i < 0) i += (zend_long)s->n;
	if (UNEXPECTED(i < 0 || i >= (zend_long)s->n)) {
		if (type == BP_VAR_IS) { ZVAL_NULL(rv); return rv; }
		zend_value_error("Index " ZEND_LONG_FMT " is out of range for a shape of %u dimensions", Z_LVAL_P(off), s->n);
		return NULL;
	}
	ZVAL_COPY(rv, &s->dims[i]);
	return rv;
}

static void shape_write(zend_object *o, zval *off, zval *val)
{
	zend_throw_error(NULL, "Cannot modify a Shape: it is immutable");
}

static int shape_has(zend_object *o, zval *off, int check_empty)
{
	buf_shape *s = buf_shape_from(o);
	zend_long i;

	ZVAL_DEREF(off);
	if (Z_TYPE_P(off) != IS_LONG) return 0;
	i = Z_LVAL_P(off);
	if (i < 0) i += (zend_long)s->n;
	if (i < 0 || i >= (zend_long)s->n) return 0;
	return check_empty ? zend_is_true(&s->dims[i]) : 1;
}

static void shape_unset(zend_object *o, zval *off)
{
	zend_throw_error(NULL, "Cannot modify a Shape: it is immutable");
}

static zend_result shape_count(zend_object *o, zend_long *count)
{
	*count = buf_shape_from(o)->n;
	return SUCCESS;
}

PHP_METHOD(Buffers_Shape, offsetExists)
{
	zval *off;

	ZEND_PARSE_PARAMETERS_START(1, 1) Z_PARAM_ZVAL(off) ZEND_PARSE_PARAMETERS_END();
	RETURN_BOOL(shape_has(Z_OBJ_P(ZEND_THIS), off, 0));
}

PHP_METHOD(Buffers_Shape, offsetGet)
{
	zval *off;

	ZEND_PARSE_PARAMETERS_START(1, 1) Z_PARAM_ZVAL(off) ZEND_PARSE_PARAMETERS_END();
	if (!shape_read(Z_OBJ_P(ZEND_THIS), off, BP_VAR_R, return_value)) RETURN_THROWS();
}

PHP_METHOD(Buffers_Shape, offsetSet)
{
	zval *off, *val;

	ZEND_PARSE_PARAMETERS_START(2, 2) Z_PARAM_ZVAL(off) Z_PARAM_ZVAL(val) ZEND_PARSE_PARAMETERS_END();
	shape_write(Z_OBJ_P(ZEND_THIS), off, val);
}

PHP_METHOD(Buffers_Shape, offsetUnset)
{
	zval *off;

	ZEND_PARSE_PARAMETERS_START(1, 1) Z_PARAM_ZVAL(off) ZEND_PARSE_PARAMETERS_END();
	shape_unset(Z_OBJ_P(ZEND_THIS), off);
}

/* ---- iteration ------------------------------------------------------------------------------ */

typedef struct { zend_object_iterator it; uint32_t idx; zval cur; } shape_iter;

static void iter_dtor(zend_object_iterator *it)
{
	zval_ptr_dtor(&((shape_iter *)it)->cur);
	zval_ptr_dtor(&it->data);
}

static int iter_valid(zend_object_iterator *it)
{
	return ((shape_iter *)it)->idx < buf_shape_from(Z_OBJ(it->data))->n ? SUCCESS : FAILURE;
}

static zval *iter_current(zend_object_iterator *it)
{
	shape_iter *si = (shape_iter *)it;

	zval_ptr_dtor(&si->cur);
	ZVAL_COPY(&si->cur, &buf_shape_from(Z_OBJ(it->data))->dims[si->idx]);
	return &si->cur;
}

static void iter_key(zend_object_iterator *it, zval *key) { ZVAL_LONG(key, ((shape_iter *)it)->idx); }
static void iter_next(zend_object_iterator *it) { ((shape_iter *)it)->idx++; }
static void iter_rewind(zend_object_iterator *it) { ((shape_iter *)it)->idx = 0; }

static const zend_object_iterator_funcs iter_funcs = {
	iter_dtor, iter_valid, iter_current, iter_key, iter_next, iter_rewind, NULL, NULL
};

static zend_object_iterator *shape_get_iterator(zend_class_entry *ce, zval *object, int by_ref)
{
	shape_iter *si;

	if (by_ref) {
		zend_throw_error(NULL, "An iterator cannot be used with foreach by reference");
		return NULL;
	}
	si = emalloc(sizeof(shape_iter));
	zend_iterator_init(&si->it);
	ZVAL_OBJ_COPY(&si->it.data, Z_OBJ_P(object));
	si->it.funcs = &iter_funcs;
	si->idx = 0;
	ZVAL_UNDEF(&si->cur);
	return &si->it;
}

PHP_METHOD(Buffers_Shape, getIterator)
{
	ZEND_PARSE_PARAMETERS_NONE();
	zend_create_internal_iterator_zval(return_value, ZEND_THIS);
}

/* shapes are equal when their dimensions are: ints by value, symbolic dimensions by identity */
static int shape_compare(zval *a, zval *b)
{
	buf_shape *x, *y;

	ZEND_COMPARE_OBJECTS_FALLBACK(a, b);
	if (Z_OBJ_P(a) == Z_OBJ_P(b)) return 0;
	if (Z_OBJCE_P(a) != buf_ce_shape || Z_OBJCE_P(b) != buf_ce_shape) return ZEND_UNCOMPARABLE;
	x = buf_shape_from(Z_OBJ_P(a));
	y = buf_shape_from(Z_OBJ_P(b));
	if (x->n != y->n) return ZEND_UNCOMPARABLE;
	for (uint32_t i = 0; i < x->n; i++) {
		if (Z_TYPE(x->dims[i]) != Z_TYPE(y->dims[i])) return ZEND_UNCOMPARABLE;
		if (Z_TYPE(x->dims[i]) == IS_LONG ? Z_LVAL(x->dims[i]) != Z_LVAL(y->dims[i]) : Z_OBJ(x->dims[i]) != Z_OBJ(y->dims[i])) {
			return ZEND_UNCOMPARABLE;
		}
	}
	return 0;
}

PHP_METHOD(Buffers_Shape, jsonSerialize)
{
	buf_shape *s = SELF;

	ZEND_PARSE_PARAMETERS_NONE();
	if (!shape_concrete(s)) {
		zend_throw_error(NULL, "Cannot JSON-encode a Shape with symbolic dimensions: no extension that builds deferred computations is loaded");
		RETURN_THROWS();
	}
	array_init_size(return_value, s->n);
	for (uint32_t i = 0; i < s->n; i++) {
		zend_hash_next_index_insert_new(Z_ARRVAL_P(return_value), &s->dims[i]);
	}
}

/* var_dump() shows the dimensions */
static HashTable *shape_debug_info(zend_object *o, int *is_temp)
{
	buf_shape *s = buf_shape_from(o);
	HashTable *ht;
	zval arr;

	array_init_size(&arr, s->n);
	for (uint32_t i = 0; i < s->n; i++) {
		Z_TRY_ADDREF(s->dims[i]);
		zend_hash_next_index_insert_new(Z_ARRVAL(arr), &s->dims[i]);
	}
	ALLOC_HASHTABLE(ht);
	zend_hash_init(ht, 1, NULL, ZVAL_PTR_DTOR, 0);
	zend_hash_str_add_new(ht, "dims", sizeof("dims") - 1, &arr);
	*is_temp = 1;
	return ht;
}

/* print_r(), var_export(), get_object_vars() and (array) see the dimensions too */
static HashTable *shape_get_properties_for(zend_object *o, zend_prop_purpose purpose)
{
	int is_temp;

	return shape_debug_info(o, &is_temp);
}

void buf_shape_minit(void)
{
	zend_class_entry ce;

	INIT_CLASS_ENTRY(ce, "Buffers\\Shape", class_Buffers_Shape_methods);
	buf_ce_shape = zend_register_internal_class_ex(&ce, NULL);
	buf_ce_shape->ce_flags |= ZEND_ACC_FINAL | ZEND_ACC_NO_DYNAMIC_PROPERTIES | ZEND_ACC_NOT_SERIALIZABLE;
	buf_ce_shape->create_object = shape_create;
	zend_class_implements(buf_ce_shape, 3, zend_ce_arrayaccess, zend_ce_countable, zend_ce_aggregate);
	buf_ce_shape->get_iterator = shape_get_iterator;
	if (buf_json_serializable()) {
		zend_class_implements(buf_ce_shape, 1, buf_json_serializable());
	}

	memcpy(&shape_handlers, &std_object_handlers, sizeof shape_handlers);
	shape_handlers.offset = XtOffsetOf(buf_shape, std);
	shape_handlers.free_obj = shape_free;
	shape_handlers.clone_obj = NULL;
	shape_handlers.compare = shape_compare;
	shape_handlers.read_dimension = shape_read;
	shape_handlers.write_dimension = shape_write;
	shape_handlers.has_dimension = shape_has;
	shape_handlers.unset_dimension = shape_unset;
	shape_handlers.count_elements = shape_count;
	shape_handlers.get_debug_info = shape_debug_info;
	shape_handlers.get_properties_for = shape_get_properties_for;
}
