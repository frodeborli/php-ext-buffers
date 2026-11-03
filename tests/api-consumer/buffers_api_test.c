/* buffers_api_test: an extension that uses ext-buffers through php_buffers_api.h and nothing else.
 * It exists to prove that the C API is enough for another extension (a graph extension, a device
 * backend, a matrix library) and to give the test suite something to drive. Not for production. */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "zend_exceptions.h"
#include "zend_smart_str.h"
#include "zend_enum.h"
#include "ext/standard/info.h"
#include <ext/buffers/php_buffers_api.h>

/* ---- counters ----------------------------------------------------------------------------- */

static zend_long made, deleted, ctx_freed, ready_calls, owners_made, owners_freed, pending_made;

/* ---- a buffer over malloc'd memory with a counting deleter ------------------------------------ */

static void counting_deleter(void *ctx)
{
	free(ctx);
	deleted++;
}

/* bat_make_buffer(int $length, bool $writable = true): a buffer whose byte i is i & 0xff */
PHP_FUNCTION(bat_make_buffer)
{
	zend_long len;
	bool writable = true;
	uint8_t *mem;
	zend_object *b;

	ZEND_PARSE_PARAMETERS_START(1, 2)
		Z_PARAM_LONG(len)
		Z_PARAM_OPTIONAL
		Z_PARAM_BOOL(writable)
	ZEND_PARSE_PARAMETERS_END();

	mem = malloc(len ? len : 1);
	for (zend_long i = 0; i < len; i++) mem[i] = (uint8_t)i;
	b = buffers_buffer_create_foreign(mem, (size_t)len, writable, NULL, counting_deleter, mem);
	if (!b) {
		free(mem);
		RETURN_THROWS();
	}
	made++;
	RETURN_OBJ(b);
}

/* ---- a buffer with a readiness callback --------------------------------------------------------- */

typedef struct { uint8_t *mem; size_t len; int fill; bool fail_once; } pending_ctx;

static zend_result pending_ready(zend_object *buffer, void *ctx)
{
	pending_ctx *c = ctx;

	ready_calls++;
	if (c->fail_once) {
		c->fail_once = false;
		zend_throw_exception(NULL, "the computation failed", 0);
		return FAILURE;
	}
	if (c->mem) {
		memset(c->mem, c->fill, c->len);
	} else { /* an ArrayBuffer: the memory is the buffer's own */
		buffers_buffer_info info;
		if (buffers_buffer_get_info(buffer, &info) == FAILURE) return FAILURE;
		memset(info.base, c->fill, info.length);
	}
	return SUCCESS;
}

static void pending_ctx_free(void *ctx)
{
	free(ctx);
	ctx_freed++;
}

/* bat_make_pending(int $length, int $fill, bool $failOnce = false): zero bytes until something reads them */
PHP_FUNCTION(bat_make_pending)
{
	zend_long len, fill;
	bool fail_once = false;
	pending_ctx *c;
	zend_object *b;

	ZEND_PARSE_PARAMETERS_START(2, 3)
		Z_PARAM_LONG(len)
		Z_PARAM_LONG(fill)
		Z_PARAM_OPTIONAL
		Z_PARAM_BOOL(fail_once)
	ZEND_PARSE_PARAMETERS_END();

	c = malloc(sizeof *c);
	c->mem = calloc(1, len ? len : 1);
	c->len = (size_t)len;
	c->fill = (int)fill;
	c->fail_once = fail_once;
	b = buffers_buffer_create_foreign(c->mem, (size_t)len, true, NULL, counting_deleter, c->mem);
	if (!b) {
		free(c->mem);
		free(c);
		RETURN_THROWS();
	}
	made++;
	pending_made++;
	if (buffers_buffer_set_ready_callback(b, pending_ready, c, pending_ctx_free) == FAILURE) {
		OBJ_RELEASE(b);
		free(c);
		zend_throw_error(NULL, "could not set the readiness callback");
		RETURN_THROWS();
	}
	buffers_buffer_set_pending(b, true);
	RETURN_OBJ(b);
}

/* bat_make_pending_arraybuffer(int $length, int $fill): an ArrayBuffer (its own memory) that is filled when something reads it */
PHP_FUNCTION(bat_make_pending_arraybuffer)
{
	zend_long len, fill;
	zval obj, arg;
	zend_class_entry *ce = buffers_get_class(BUFFERS_CE_ARRAY_BUFFER);
	pending_ctx *c;

	ZEND_PARSE_PARAMETERS_START(2, 2)
		Z_PARAM_LONG(len)
		Z_PARAM_LONG(fill)
	ZEND_PARSE_PARAMETERS_END();

	object_init_ex(&obj, ce);
	ZVAL_LONG(&arg, len);
	zend_call_known_instance_method_with_1_params(ce->constructor, Z_OBJ(obj), NULL, &arg);
	if (EG(exception)) {
		zval_ptr_dtor(&obj);
		RETURN_THROWS();
	}
	c = calloc(1, sizeof *c);
	c->len = (size_t)len;
	c->fill = (int)fill;
	pending_made++;
	if (buffers_buffer_set_ready_callback(Z_OBJ(obj), pending_ready, c, pending_ctx_free) == FAILURE) {
		free(c);
		zval_ptr_dtor(&obj);
		zend_throw_error(NULL, "could not set the readiness callback");
		RETURN_THROWS();
	}
	buffers_buffer_set_pending(Z_OBJ(obj), true);
	RETURN_COPY_VALUE(&obj);
}

/* bat_set_pending(BufferInterface $b, bool $pending): marks a buffer pending again */
PHP_FUNCTION(bat_set_pending)
{
	zend_object *b;
	bool pending;

	ZEND_PARSE_PARAMETERS_START(2, 2)
		Z_PARAM_OBJ(b)
		Z_PARAM_BOOL(pending)
	ZEND_PARSE_PARAMETERS_END();
	buffers_buffer_set_pending(b, pending);
}

/* bat_set_ready_late(BufferInterface $b): the callback can only be set while no view exists */
PHP_FUNCTION(bat_set_ready_late)
{
	zend_object *b;

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_OBJ(b)
	ZEND_PARSE_PARAMETERS_END();
	RETURN_BOOL(buffers_buffer_set_ready_callback(b, pending_ready, NULL, NULL) == SUCCESS);
}

/* bat_peek(BufferInterface $b, int $i): byte i read straight from memory, like a consumer working on the C pointer */
PHP_FUNCTION(bat_peek)
{
	zend_object *b;
	zend_long i;
	buffers_buffer_info info;

	ZEND_PARSE_PARAMETERS_START(2, 2)
		Z_PARAM_OBJ(b)
		Z_PARAM_LONG(i)
	ZEND_PARSE_PARAMETERS_END();
	if (buffers_buffer_ensure_ready(b) == FAILURE || buffers_buffer_get_info(b, &info) == FAILURE) {
		RETURN_THROWS();
	}
	if (!info.host_accessible || i < 0 || (size_t)i >= info.length) {
		zend_value_error("cannot peek");
		RETURN_THROWS();
	}
	RETURN_LONG(info.base[i]);
}

/* ---- information about buffers and views ------------------------------------------------------- */

PHP_FUNCTION(bat_counts)
{
	ZEND_PARSE_PARAMETERS_NONE();
	array_init(return_value);
	add_assoc_long(return_value, "made", made);
	add_assoc_long(return_value, "deleted", deleted);
	add_assoc_long(return_value, "pending_made", pending_made);
	add_assoc_long(return_value, "ctx_freed", ctx_freed);
	add_assoc_long(return_value, "ready_calls", ready_calls);
	add_assoc_long(return_value, "owners_made", owners_made);
	add_assoc_long(return_value, "owners_freed", owners_freed);
}

static void list_i64(zval *arr, const char *key, const int64_t *v, int n)
{
	zval l;

	array_init_size(&l, n);
	for (int i = 0; i < n; i++) add_next_index_long(&l, v[i]);
	add_assoc_zval(arr, key, &l);
}

/* bat_info(BufferView $v): everything buffers_view_get_info() reports */
PHP_FUNCTION(bat_info)
{
	zend_object *v;
	buffers_view_info i;
	buffers_buffer_info bi;

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_OBJ(v)
	ZEND_PARSE_PARAMETERS_END();
	if (buffers_view_get_info(v, &i) == FAILURE || buffers_buffer_get_info(i.buffer, &bi) == FAILURE) {
		RETURN_THROWS();
	}
	array_init(return_value);
	add_assoc_string(return_value, "dtype", buffers_dtype_describe(i.dtype)->name);
	add_assoc_long(return_value, "bits", buffers_dtype_describe(i.dtype)->bits);
	add_assoc_long(return_value, "size", buffers_dtype_describe(i.dtype)->size);
	add_assoc_long(return_value, "ndim", i.ndim);
	list_i64(return_value, "shape", i.shape, i.ndim);
	list_i64(return_value, "strides", i.strides, i.ndim);
	add_assoc_long(return_value, "byte_offset", i.byte_offset);
	add_assoc_bool(return_value, "data_is_base_plus_offset", i.data == bi.base + i.byte_offset);
	add_assoc_bool(return_value, "writable", i.writable);
	add_assoc_long(return_value, "buffer_length", (zend_long)bi.length);
	add_assoc_bool(return_value, "host_accessible", bi.host_accessible);
	add_assoc_long(return_value, "device_type", bi.device_type);
	add_assoc_long(return_value, "device_id", bi.device_id);
	add_assoc_bool(return_value, "is_view", buffers_is_view(v));
}

/* bat_is(mixed $x): [is_view, is_buffer] */
PHP_FUNCTION(bat_is)
{
	zval *x;

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_ZVAL(x)
	ZEND_PARSE_PARAMETERS_END();
	array_init(return_value);
	add_next_index_bool(return_value, Z_TYPE_P(x) == IS_OBJECT && buffers_is_view(Z_OBJ_P(x)));
	add_next_index_bool(return_value, Z_TYPE_P(x) == IS_OBJECT && buffers_is_buffer(Z_OBJ_P(x)));
}

/* bat_view(BufferInterface $b, int $dtype, array $shape, ?array $strides = null, int $offset = 0): buffers_view_create() */
PHP_FUNCTION(bat_view)
{
	zend_object *b, *v;
	zend_long dtype, offset = 0, n = 0, sn = 0;
	HashTable *shape, *strides = NULL;
	zval *z;
	int64_t sh[BUFFERS_MAX_NDIM + 1], st[BUFFERS_MAX_NDIM + 1];

	ZEND_PARSE_PARAMETERS_START(3, 5)
		Z_PARAM_OBJ(b)
		Z_PARAM_LONG(dtype)
		Z_PARAM_ARRAY_HT(shape)
		Z_PARAM_OPTIONAL
		Z_PARAM_ARRAY_HT_OR_NULL(strides)
		Z_PARAM_LONG(offset)
	ZEND_PARSE_PARAMETERS_END();

	if (zend_hash_num_elements(shape) > BUFFERS_MAX_NDIM + 1 || (strides && zend_hash_num_elements(strides) > BUFFERS_MAX_NDIM + 1)) {
		zend_value_error("too many dimensions");
		RETURN_THROWS();
	}
	ZEND_HASH_FOREACH_VAL(shape, z) { sh[n++] = zval_get_long(z); } ZEND_HASH_FOREACH_END();
	if (strides) {
		ZEND_HASH_FOREACH_VAL(strides, z) { st[sn++] = zval_get_long(z); } ZEND_HASH_FOREACH_END();
	}
	v = buffers_view_create(b, (int)dtype, (int)n, sh, strides ? st : NULL, offset);
	if (!v) RETURN_THROWS();
	RETURN_OBJ(v);
}

/* bat_dtype_info(int $dtype): buffers_dtype_describe() and the DType case object */
PHP_FUNCTION(bat_dtype_info)
{
	zend_long dt;
	const buffers_dtype_desc *d;
	zval c;

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_LONG(dt)
	ZEND_PARSE_PARAMETERS_END();
	d = buffers_dtype_describe((int)dt);
	if (!d) RETURN_NULL();
	buffers_dtype_to_zval((int)dt, &c);
	array_init(return_value);
	add_assoc_string(return_value, "name", d->name);
	add_assoc_string(return_value, "value", d->value);
	add_assoc_long(return_value, "kind", d->kind);
	add_assoc_long(return_value, "bits", d->bits);
	add_assoc_long(return_value, "size", d->size);
	add_assoc_long(return_value, "dlpack_code", d->dlpack_code);
	add_assoc_long(return_value, "roundtrip", buffers_dtype_from_object(Z_OBJ(c)));
	zval_ptr_dtor(&c);
}

/* ---- memory owned by a PHP object ---------------------------------------------------------------
 * BatMatrix keeps m x n doubles in C, like a matrix library does, and lends them out as a read-only
 * Float64 [m, n] view without copying: buffers_buffer_create_owned() holds the matrix. */

static zend_class_entry *ce_matrix, *ce_device;
static zend_object_handlers matrix_handlers;

typedef struct { zend_long m, n; double *data; zend_object std; } matrix;

static matrix *matrix_from(zend_object *o) { return (matrix *)((char *)o - XtOffsetOf(matrix, std)); }

static zend_object *matrix_create(zend_class_entry *ce)
{
	matrix *x = zend_object_alloc(sizeof(matrix), ce);

	zend_object_std_init(&x->std, ce);
	x->std.handlers = &matrix_handlers;
	return &x->std;
}

static void matrix_free(zend_object *o)
{
	matrix *x = matrix_from(o);

	if (x->data) {
		efree(x->data);
		owners_freed++;
	}
	zend_object_std_dtor(o);
}

PHP_METHOD(BatMatrix, __construct)
{
	zend_long m, n;
	matrix *x = matrix_from(Z_OBJ_P(ZEND_THIS));

	ZEND_PARSE_PARAMETERS_START(2, 2)
		Z_PARAM_LONG(m)
		Z_PARAM_LONG(n)
	ZEND_PARSE_PARAMETERS_END();
	x->m = m;
	x->n = n;
	x->data = safe_emalloc((m && n) ? m * n : 1, sizeof(double), 0);
	for (zend_long i = 0; i < m * n; i++) x->data[i] = (double)i + 0.5;
	owners_made++;
}

/* toView(bool $writable = false): BufferView over the matrix's own memory */
PHP_METHOD(BatMatrix, toView)
{
	matrix *x = matrix_from(Z_OBJ_P(ZEND_THIS));
	bool writable = false;
	int64_t shape[2];
	zend_object *buf, *view;

	ZEND_PARSE_PARAMETERS_START(0, 1)
		Z_PARAM_OPTIONAL
		Z_PARAM_BOOL(writable)
	ZEND_PARSE_PARAMETERS_END();

	shape[0] = x->m;
	shape[1] = x->n;
	buf = buffers_buffer_create_owned(x->data, (size_t)(x->m * x->n) * sizeof(double), writable, NULL, Z_OBJ_P(ZEND_THIS));
	if (!buf) RETURN_THROWS();
	view = buffers_view_create(buf, BUFFERS_DT_FLOAT64, 2, shape, NULL, 0);
	buffers_release(buf); /* the view holds the buffer, the buffer holds the matrix */
	if (!view) RETURN_THROWS();
	RETURN_OBJ(view);
}

PHP_METHOD(BatMatrix, get)
{
	matrix *x = matrix_from(Z_OBJ_P(ZEND_THIS));
	zend_long i;

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_LONG(i)
	ZEND_PARSE_PARAMETERS_END();
	RETURN_DOUBLE(x->data[i]);
}

ZEND_BEGIN_ARG_INFO_EX(ai_ctor, 0, 0, 2)
	ZEND_ARG_TYPE_INFO(0, m, IS_LONG, 0)
	ZEND_ARG_TYPE_INFO(0, n, IS_LONG, 0)
ZEND_END_ARG_INFO()
ZEND_BEGIN_ARG_INFO_EX(ai_toview, 0, 0, 0)
	ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, writable, _IS_BOOL, 0, "false")
ZEND_END_ARG_INFO()
ZEND_BEGIN_ARG_INFO_EX(ai_get, 0, 0, 1)
	ZEND_ARG_TYPE_INFO(0, i, IS_LONG, 0)
ZEND_END_ARG_INFO()
static const zend_function_entry matrix_methods[] = {
	PHP_ME(BatMatrix, __construct, ai_ctor, ZEND_ACC_PUBLIC)
	PHP_ME(BatMatrix, toView, ai_toview, ZEND_ACC_PUBLIC)
	PHP_ME(BatMatrix, get, ai_get, ZEND_ACC_PUBLIC)
	PHP_FE_END
};

/* ---- a Device implemented by another extension: CUDA device 5, not host accessible ------------------- */

PHP_METHOD(BatDevice, name) { ZEND_PARSE_PARAMETERS_NONE(); RETURN_STRING("bat:5"); }
PHP_METHOD(BatDevice, dlpackType) { ZEND_PARSE_PARAMETERS_NONE(); RETURN_LONG(kDLCUDA); }
PHP_METHOD(BatDevice, id) { ZEND_PARSE_PARAMETERS_NONE(); RETURN_LONG(5); }
PHP_METHOD(BatDevice, isHostAccessible) { ZEND_PARSE_PARAMETERS_NONE(); RETURN_FALSE; }

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(ai_dev_s, 0, 0, IS_STRING, 0) ZEND_END_ARG_INFO()
ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(ai_dev_i, 0, 0, IS_LONG, 0) ZEND_END_ARG_INFO()
ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(ai_dev_b, 0, 0, _IS_BOOL, 0) ZEND_END_ARG_INFO()
static const zend_function_entry device_methods[] = {
	PHP_ME(BatDevice, name, ai_dev_s, ZEND_ACC_PUBLIC)
	PHP_ME(BatDevice, dlpackType, ai_dev_i, ZEND_ACC_PUBLIC)
	PHP_ME(BatDevice, id, ai_dev_i, ZEND_ACC_PUBLIC)
	PHP_ME(BatDevice, isHostAccessible, ai_dev_b, ZEND_ACC_PUBLIC)
	PHP_FE_END
};

/* bat_device_buffer(int $length): memory on a device PHP cannot touch */
PHP_FUNCTION(bat_device_buffer)
{
	zend_long len;
	zval dev;
	uint8_t *mem;
	zend_object *b;

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_LONG(len)
	ZEND_PARSE_PARAMETERS_END();
	mem = calloc(1, len ? len : 1);
	object_init_ex(&dev, ce_device);
	b = buffers_buffer_create_foreign(mem, (size_t)len, true, Z_OBJ(dev), counting_deleter, mem);
	zval_ptr_dtor(&dev); /* the buffer holds the device */
	if (!b) {
		free(mem);
		RETURN_THROWS();
	}
	made++;
	RETURN_OBJ(b);
}

/* ---- a Tensor implemented by another extension ----------------------------------------------------------
 * BatTensor(int $ndim = 0, int $dtype = BUFFERS_DT_INT64): a stand-in for a deferred tensor. It implements
 * Buffers\Tensor from C with get hooks, so it can be used as a symbolic dimension of a Shape (when 0-d). */

static zend_class_entry *ce_tensor;
static zend_object_handlers tensor_handlers;

typedef struct { zend_long ndim; int dtype; zend_object std; } bat_tensor;

static bat_tensor *tensor_from(zend_object *o) { return (bat_tensor *)((char *)o - XtOffsetOf(bat_tensor, std)); }

static zend_object *tensor_create(zend_class_entry *ce)
{
	bat_tensor *t = zend_object_alloc(sizeof(bat_tensor), ce);

	zend_object_std_init(&t->std, ce);
	t->std.handlers = &tensor_handlers;
	t->dtype = BUFFERS_DT_INT64;
	return &t->std;
}

PHP_METHOD(BatTensor, __construct)
{
	zend_long ndim = 0, dtype = BUFFERS_DT_INT64;
	bat_tensor *t = tensor_from(Z_OBJ_P(ZEND_THIS));

	ZEND_PARSE_PARAMETERS_START(0, 2)
		Z_PARAM_OPTIONAL
		Z_PARAM_LONG(ndim)
		Z_PARAM_LONG(dtype)
	ZEND_PARSE_PARAMETERS_END();
	t->ndim = ndim;
	t->dtype = (int)dtype;
}

static void tensor_get_dtype(INTERNAL_FUNCTION_PARAMETERS)
{
	ZEND_PARSE_PARAMETERS_NONE();
	buffers_dtype_to_zval(tensor_from(Z_OBJ_P(ZEND_THIS))->dtype, return_value);
}

static void tensor_get_ndim(INTERNAL_FUNCTION_PARAMETERS)
{
	ZEND_PARSE_PARAMETERS_NONE();
	RETURN_LONG(tensor_from(Z_OBJ_P(ZEND_THIS))->ndim);
}

static void tensor_get_shape(INTERNAL_FUNCTION_PARAMETERS)
{
	zend_long n = tensor_from(Z_OBJ_P(ZEND_THIS))->ndim;
	zval *dims = safe_emalloc(n ? n : 1, sizeof(zval), 0);
	zend_object *s;

	ZEND_PARSE_PARAMETERS_NONE();
	for (zend_long i = 0; i < n; i++) ZVAL_LONG(&dims[i], 2);
	s = buffers_shape_create((uint32_t)n, dims);
	efree(dims);
	if (!s) RETURN_THROWS();
	RETURN_OBJ(s);
}

ZEND_BEGIN_ARG_INFO_EX(ai_tensor_ctor, 0, 0, 0)
	ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, ndim, IS_LONG, 0, "0")
	ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, dtype, IS_LONG, 0, "0")
ZEND_END_ARG_INFO()
static const zend_function_entry tensor_methods[] = {
	PHP_ME(BatTensor, __construct, ai_tensor_ctor, ZEND_ACC_PUBLIC)
	PHP_FE_END
};

/* ---- DLPack ----------------------------------------------------------------------------------- */

/* bat_export(BufferView $v): exports through the C API, describes the tensor, then calls the deleter */
PHP_FUNCTION(bat_export)
{
	zend_object *v;
	DLManagedTensorVersioned *t;
	DLTensor *d;
	buffers_view_info vi;
	buffers_buffer_info bi;

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_OBJ(v)
	ZEND_PARSE_PARAMETERS_END();
	if (buffers_view_get_info(v, &vi) == FAILURE || buffers_buffer_get_info(vi.buffer, &bi) == FAILURE) RETURN_THROWS();
	if (buffers_dlpack_export(v, &t) == FAILURE) RETURN_THROWS();
	d = &t->dl_tensor;
	array_init(return_value);
	add_assoc_bool(return_value, "data_is_buffer_base", d->data == bi.base);
	add_assoc_long(return_value, "device_type", d->device.device_type);
	add_assoc_long(return_value, "device_id", d->device.device_id);
	add_assoc_long(return_value, "ndim", d->ndim);
	add_assoc_long(return_value, "code", d->dtype.code);
	add_assoc_long(return_value, "bits", d->dtype.bits);
	add_assoc_long(return_value, "byte_offset", (zend_long)d->byte_offset);
	add_assoc_long(return_value, "flags", (zend_long)t->flags);
	list_i64(return_value, "shape", d->shape, d->ndim);
	list_i64(return_value, "strides", d->strides, d->ndim);
	t->deleter(t);
}

/* bat_roundtrip(BufferView $v): export through the C API and import the tensor again */
PHP_FUNCTION(bat_roundtrip)
{
	zend_object *v, *w;
	DLManagedTensorVersioned *t;

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_OBJ(v)
	ZEND_PARSE_PARAMETERS_END();
	if (buffers_dlpack_export(v, &t) == FAILURE) RETURN_THROWS();
	w = buffers_dlpack_import(t);
	if (!w) {
		t->deleter(t);
		RETURN_THROWS();
	}
	RETURN_OBJ(w);
}

/* ---- shapes ------------------------------------------------------------------------------------ */

PHP_FUNCTION(bat_shape)
{
	HashTable *dims;
	zval *z, *tmp;
	uint32_t n = 0;
	zend_object *s;

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_ARRAY_HT(dims)
	ZEND_PARSE_PARAMETERS_END();
	tmp = safe_emalloc(zend_hash_num_elements(dims), sizeof(zval), 0);
	ZEND_HASH_FOREACH_VAL(dims, z) { ZVAL_COPY_VALUE(&tmp[n++], z); } ZEND_HASH_FOREACH_END();
	s = buffers_shape_create(n, tmp);
	efree(tmp);
	if (!s) RETURN_THROWS();
	RETURN_OBJ(s);
}

PHP_FUNCTION(bat_shape_dims)
{
	zend_object *s;
	const zval *dims;
	uint32_t n;

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_OBJ(s)
	ZEND_PARSE_PARAMETERS_END();
	dims = buffers_shape_dims(s, &n);
	array_init_size(return_value, n);
	for (uint32_t i = 0; i < n; i++) {
		zval c;
		ZVAL_COPY(&c, &dims[i]);
		add_next_index_zval(return_value, &c);
	}
}

/* ---- the operator hook ------------------------------------------------------------------------------ */

static void describe(smart_str *s, zval *z)
{
	buffers_view_info i;

	ZVAL_DEREF(z);
	if (Z_TYPE_P(z) == IS_OBJECT && buffers_is_view(Z_OBJ_P(z)) && buffers_view_get_info(Z_OBJ_P(z), &i) == SUCCESS) {
		smart_str_appends(s, "view<");
		smart_str_appends(s, buffers_dtype_describe(i.dtype)->name);
		for (int k = 0; k < i.ndim; k++) {
			smart_str_appendc(s, k ? 'x' : ' ');
			smart_str_append_long(s, i.shape[k]);
		}
		smart_str_appendc(s, '>');
	} else if (z && Z_TYPE_P(z) == IS_LONG) {
		smart_str_appends(s, "int(");
		smart_str_append_long(s, Z_LVAL_P(z));
		smart_str_appendc(s, ')');
	} else if (z) {
		smart_str_appends(s, zend_zval_type_name(z));
	} else {
		smart_str_appends(s, "-");
	}
}

static zend_result describing_handler(uint8_t opcode, zval *result, zval *op1, zval *op2)
{
	const char *name;
	smart_str s = {0};

	switch (opcode) {
	case ZEND_ADD: name = "add"; break;
	case ZEND_SUB: name = "sub"; break;
	case ZEND_MUL: name = "mul"; break;
	case ZEND_DIV: name = "div"; break;
	case ZEND_MOD: name = "mod"; break;
	case ZEND_SL: name = "shl"; break;
	case ZEND_SR: name = "shr"; break;
	case ZEND_BW_OR: name = "or"; break;
	case ZEND_BW_AND: name = "and"; break;
	case ZEND_BW_XOR: name = "xor"; break;
	case ZEND_BW_NOT: name = "not"; break;
	default: return FAILURE; /* ZEND_POW: left to the engine's TypeError */
	}
	smart_str_appends(&s, name);
	smart_str_appendc(&s, '(');
	describe(&s, op1);
	if (opcode != ZEND_BW_NOT) {
		smart_str_appends(&s, ", ");
		describe(&s, op2);
	}
	smart_str_appendc(&s, ')');
	smart_str_0(&s);
	ZVAL_STR(result, s.s);
	return SUCCESS;
}

/* ---- module ---------------------------------------------------------------------------------------- */

#define ARGS(name, n) ZEND_BEGIN_ARG_INFO_EX(ai_##name, 0, 0, n) ZEND_END_ARG_INFO()
ARGS(any0, 0)
ARGS(any1, 1)
ARGS(any2, 2)
ARGS(any3, 3)

static const zend_function_entry functions[] = {
	PHP_FE(bat_make_buffer, ai_any1)
	PHP_FE(bat_make_pending, ai_any2)
	PHP_FE(bat_make_pending_arraybuffer, ai_any2)
	PHP_FE(bat_set_pending, ai_any2)
	PHP_FE(bat_set_ready_late, ai_any1)
	PHP_FE(bat_peek, ai_any2)
	PHP_FE(bat_counts, ai_any0)
	PHP_FE(bat_info, ai_any1)
	PHP_FE(bat_is, ai_any1)
	PHP_FE(bat_view, ai_any3)
	PHP_FE(bat_dtype_info, ai_any1)
	PHP_FE(bat_device_buffer, ai_any1)
	PHP_FE(bat_export, ai_any1)
	PHP_FE(bat_roundtrip, ai_any1)
	PHP_FE(bat_shape, ai_any1)
	PHP_FE(bat_shape_dims, ai_any1)
	PHP_FE_END
};

#if defined(ZTS) && defined(COMPILE_DL_BUFFERS_API_TEST)
ZEND_TSRMLS_CACHE_DEFINE()
#endif

static PHP_RINIT_FUNCTION(buffers_api_test)
{
#if defined(ZTS) && defined(COMPILE_DL_BUFFERS_API_TEST)
	ZEND_TSRMLS_CACHE_UPDATE();
#endif
	return SUCCESS;
}

static PHP_MINIT_FUNCTION(buffers_api_test)
{
	zend_class_entry ce, *device_if;

#if defined(ZTS) && defined(COMPILE_DL_BUFFERS_API_TEST)
	ZEND_TSRMLS_CACHE_UPDATE();
#endif
	if (buffers_api_version() != BUFFERS_API_VERSION) {
		zend_error(E_CORE_ERROR, "buffers_api_test was built for ext-buffers API %d, found %d", BUFFERS_API_VERSION, buffers_api_version());
		return FAILURE;
	}
	if (buffers_set_operator_handler(describing_handler) == FAILURE) {
		return FAILURE;
	}

	INIT_CLASS_ENTRY(ce, "BatMatrix", matrix_methods);
	ce_matrix = zend_register_internal_class_ex(&ce, NULL);
	ce_matrix->ce_flags |= ZEND_ACC_FINAL | ZEND_ACC_NO_DYNAMIC_PROPERTIES | ZEND_ACC_NOT_SERIALIZABLE;
	ce_matrix->create_object = matrix_create;
	memcpy(&matrix_handlers, &std_object_handlers, sizeof matrix_handlers);
	matrix_handlers.offset = XtOffsetOf(matrix, std);
	matrix_handlers.free_obj = matrix_free;
	matrix_handlers.clone_obj = NULL;

	device_if = buffers_get_class(BUFFERS_CE_DEVICE);
	INIT_CLASS_ENTRY(ce, "BatTensor", tensor_methods);
	ce_tensor = zend_register_internal_class_ex(&ce, NULL);
	ce_tensor->ce_flags |= ZEND_ACC_FINAL | ZEND_ACC_NO_DYNAMIC_PROPERTIES | ZEND_ACC_NOT_SERIALIZABLE;
	ce_tensor->create_object = tensor_create;
	memcpy(&tensor_handlers, &std_object_handlers, sizeof tensor_handlers);
	tensor_handlers.offset = XtOffsetOf(bat_tensor, std);
	tensor_handlers.clone_obj = NULL;
	buffers_implement_tensor(ce_tensor, tensor_get_dtype, tensor_get_shape, tensor_get_ndim);

	INIT_CLASS_ENTRY(ce, "BatDevice", device_methods);
	ce_device = zend_register_internal_class_ex(&ce, NULL);
	ce_device->ce_flags |= ZEND_ACC_FINAL | ZEND_ACC_NO_DYNAMIC_PROPERTIES | ZEND_ACC_NOT_SERIALIZABLE;
	zend_class_implements(ce_device, 1, device_if);
	return SUCCESS;
}

static PHP_MINFO_FUNCTION(buffers_api_test)
{
	php_info_print_table_start();
	php_info_print_table_row(2, "buffers_api_test", "enabled");
	php_info_print_table_end();
}

static const zend_module_dep deps[] = {
	ZEND_MOD_REQUIRED("buffers")
	ZEND_MOD_END
};

zend_module_entry buffers_api_test_module_entry = {
	STANDARD_MODULE_HEADER_EX, NULL, deps,
	"buffers_api_test", functions, PHP_MINIT(buffers_api_test), NULL, PHP_RINIT(buffers_api_test), NULL, PHP_MINFO(buffers_api_test),
	"0.1.0", STANDARD_MODULE_PROPERTIES
};

ZEND_GET_MODULE(buffers_api_test)
