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

static zend_long tensors_made, tensors_deleted, made, deleted, ctx_freed, ready_calls, owners_made, owners_freed, pending_made, transfers, backend_asks, backend_runs;

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
	zend_class_entry *ce = buffers_get_class(BUFFERS_CE_BUFFER);
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
	add_assoc_long(return_value, "transfers", transfers);
	add_assoc_long(return_value, "tensors_made", tensors_made);
	add_assoc_long(return_value, "tensors_deleted", tensors_deleted);
	add_assoc_long(return_value, "ctx_freed", ctx_freed);
	add_assoc_long(return_value, "ready_calls", ready_calls);
	add_assoc_long(return_value, "owners_made", owners_made);
	add_assoc_long(return_value, "owners_freed", owners_freed);
	add_assoc_long(return_value, "backend_asks", backend_asks);
	add_assoc_long(return_value, "backend_runs", backend_runs);
}

static void list_i64(zval *arr, const char *key, const int64_t *v, int n)
{
	zval l;

	array_init_size(&l, n);
	for (int i = 0; i < n; i++) add_next_index_long(&l, v[i]);
	add_assoc_zval(arr, key, &l);
}

/* bat_info(Tensor $v): everything buffers_tensor_get_info() reports */
PHP_FUNCTION(bat_info)
{
	zend_object *v;
	buffers_tensor_info i;
	buffers_buffer_info bi;

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_OBJ(v)
	ZEND_PARSE_PARAMETERS_END();
	if (buffers_tensor_get_info(v, &i) == FAILURE || buffers_buffer_get_info(i.buffer, &bi) == FAILURE) {
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
	add_assoc_bool(return_value, "is_view", buffers_is_tensor(v));
}

/* bat_max_ndim(): buffers_max_ndim() */
PHP_FUNCTION(bat_max_ndim)
{
	ZEND_PARSE_PARAMETERS_NONE();
	RETURN_LONG(buffers_max_ndim());
}

/* bat_info_twice(Tensor $v): whether two buffers_tensor_get_info() calls hand out the same shape and strides pointers */
PHP_FUNCTION(bat_info_twice)
{
	zend_object *v;
	buffers_tensor_info a, b;

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_OBJ(v)
	ZEND_PARSE_PARAMETERS_END();
	if (buffers_tensor_get_info(v, &a) == FAILURE || buffers_tensor_get_info(v, &b) == FAILURE) {
		RETURN_THROWS();
	}
	RETURN_BOOL(a.shape == b.shape && a.strides == b.strides);
}

/* bat_is(mixed $x): [is_view, is_buffer] */
PHP_FUNCTION(bat_is)
{
	zval *x;

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_ZVAL(x)
	ZEND_PARSE_PARAMETERS_END();
	array_init(return_value);
	add_next_index_bool(return_value, Z_TYPE_P(x) == IS_OBJECT && buffers_is_tensor(Z_OBJ_P(x)));
	add_next_index_bool(return_value, Z_TYPE_P(x) == IS_OBJECT && buffers_is_buffer(Z_OBJ_P(x)));
}

/* bat_view(BufferInterface $b, int $dtype, array $shape, ?array $strides = null, int $offset = 0): buffers_tensor_create() */
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
	v = buffers_tensor_create(b, (int)dtype, (int)n, sh, strides ? st : NULL, offset);
	if (!v) RETURN_THROWS();
	RETURN_OBJ(v);
}

/* bat_dtype_promote(string $a, string $b, string $op): buffers_dtype_promote() with operands and result spelled as in
 * tests/promotion.tsv (DType values, "php-int", "php-float", "php-bool"; "-" for the missing operand of ~); "error" for -1 */
static int promote_operand(zend_string *s)
{
	if (zend_string_equals_literal(s, "php-int")) return BUFFERS_SCALAR_INT;
	if (zend_string_equals_literal(s, "php-float")) return BUFFERS_SCALAR_FLOAT;
	if (zend_string_equals_literal(s, "php-bool")) return BUFFERS_SCALAR_BOOL;
	if (zend_string_equals_literal(s, "-")) return -1;
	for (int t = 0; t < BUFFERS_DT_COUNT; t++) {
		if (zend_string_equals_cstr(s, buffers_dtype_describe(t)->value, strlen(buffers_dtype_describe(t)->value))) return t;
	}
	return -100;
}

PHP_FUNCTION(bat_dtype_promote)
{
	zend_string *a, *b, *op;
	static const struct { const char *sym; uint8_t op; } ops[] = {
		{ "+", ZEND_ADD }, { "-", ZEND_SUB }, { "*", ZEND_MUL }, { "/", ZEND_DIV }, { "%", ZEND_MOD }, { "**", ZEND_POW },
		{ "<<", ZEND_SL }, { ">>", ZEND_SR }, { "&", ZEND_BW_AND }, { "|", ZEND_BW_OR }, { "^", ZEND_BW_XOR }, { "~", ZEND_BW_NOT },
	};
	int ia, ib, r;
	int opcode = -1;

	ZEND_PARSE_PARAMETERS_START(3, 3)
		Z_PARAM_STR(a)
		Z_PARAM_STR(b)
		Z_PARAM_STR(op)
	ZEND_PARSE_PARAMETERS_END();
	for (size_t i = 0; i < sizeof ops / sizeof ops[0]; i++) {
		if (zend_string_equals_cstr(op, ops[i].sym, strlen(ops[i].sym))) opcode = ops[i].op;
	}
	ia = promote_operand(a);
	ib = promote_operand(b);
	if (opcode < 0 || ia == -100 || ib == -100) {
		zend_value_error("unknown operand or operator");
		RETURN_THROWS();
	}
	r = buffers_dtype_promote(ia, ib, (uint8_t)opcode);
	RETURN_STRING(r < 0 ? "error" : buffers_dtype_describe(r)->value);
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
	add_assoc_long(return_value, "dlpack_roundtrip", buffers_dtype_from_dlpack(d->dlpack_code, d->bits));
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

/* toView(bool $writable = false): Tensor over the matrix's own memory */
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
	view = buffers_tensor_create(buf, BUFFERS_DT_FLOAT64, 2, shape, NULL, 0);
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

static ZEND_NAMED_FUNCTION(device_get_name) { ZEND_PARSE_PARAMETERS_NONE(); RETURN_STRING("bat:5"); }
static ZEND_NAMED_FUNCTION(device_get_id) { ZEND_PARSE_PARAMETERS_NONE(); RETURN_LONG(5); }
static ZEND_NAMED_FUNCTION(device_get_host) { ZEND_PARSE_PARAMETERS_NONE(); RETURN_FALSE; }

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

/* ---- a TensorAggregate implemented by another extension ----------------------------------------------------------
 * BatTensor(int $ndim = 0, int $dtype = BUFFERS_DT_INT64): a stand-in for a deferred tensor. It implements
 * Buffers\TensorAggregate from C, so it can be a symbolic dimension of a Shape (when 0-d); its getTensor() throws (no numbers). */

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

PHP_METHOD(BatTensor, getDType)
{
	ZEND_PARSE_PARAMETERS_NONE();
	buffers_dtype_to_zval(tensor_from(Z_OBJ_P(ZEND_THIS))->dtype, return_value);
}

PHP_METHOD(BatTensor, getTensor)
{
	ZEND_PARSE_PARAMETERS_NONE();
	zend_throw_error(NULL, "BatTensor has no numbers");
}

PHP_METHOD(BatTensor, getShape)
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
ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(ai_tensor_get_tensor, 0, 0, Buffers\\Tensor, 0)
ZEND_END_ARG_INFO()
ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(ai_tensor_get_dtype, 0, 0, Buffers\\DType, 1)
ZEND_END_ARG_INFO()
ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(ai_tensor_get_shape, 0, 0, Buffers\\Shape, 0)
ZEND_END_ARG_INFO()
static const zend_function_entry tensor_methods[] = {
	PHP_ME(BatTensor, __construct, ai_tensor_ctor, ZEND_ACC_PUBLIC)
	PHP_ME(BatTensor, getTensor, ai_tensor_get_tensor, ZEND_ACC_PUBLIC)
	PHP_ME(BatTensor, getDType, ai_tensor_get_dtype, ZEND_ACC_PUBLIC)
	PHP_ME(BatTensor, getShape, ai_tensor_get_shape, ZEND_ACC_PUBLIC)
	PHP_FE_END
};

/* A transfer for BatDevice: "uploads" a contiguous host copy to malloc'd memory that stands in for the device,
 * and "downloads" it again. Registered with buffers_register_transfer(). */
static zend_object *bat_transfer(zend_object *view, zend_object *device)
{
	buffers_tensor_info vi;
	buffers_buffer_info bi;
	zend_object *src = view, *buf, *res;
	bool upload = instanceof_function(device->ce, ce_device);
	uint8_t *mem;

	if (!upload && device != buffers_cpu_device()) {
		zend_throw_error(NULL, "bat transfer: only to BatDevice or the CPU");
		return NULL;
	}
	transfers++;
	if (upload) {   /* the contiguous host copy comes from the core */
		src = buffers_tensor_to(view, buffers_cpu_device());
		if (!src) return NULL;
	}
	if (buffers_tensor_get_info(src, &vi) == FAILURE || buffers_buffer_get_info(vi.buffer, &bi) == FAILURE) {
		if (upload) OBJ_RELEASE(src);
		return NULL;
	}
	mem = malloc(bi.length ? bi.length : 1);
	memcpy(mem, bi.base, bi.length);
	buf = buffers_buffer_create_foreign(mem, bi.length, true, upload ? device : NULL, counting_deleter, mem);
	if (upload) OBJ_RELEASE(src);
	if (!buf) {
		free(mem);
		return NULL;
	}
	made++;
	res = buffers_tensor_create(buf, vi.dtype, vi.ndim, vi.shape, vi.strides, vi.byte_offset);
	OBJ_RELEASE(buf);
	return res;
}

/* ---- a producer of foreign DLPack tensors ----------------------------------------------------------
 * bat_import(code, bits, lanes, shape, ?strides = null, flags = 0, device = 1, byteOffset = 0, minor = 3, major = 1, deviceId = 0)
 * builds a tensor the way a foreign framework would (memory holding the byte pattern i & 0xff, a counted deleter) and imports it
 * with buffers_dlpack_import(). A rejected tensor is released by its deleter, once. */
typedef struct {
	DLManagedTensorVersioned t;
	int64_t shape[16], strides[16]; /* more than BUFFERS_MAX_NDIM, so tests can ask for 9 dimensions */
	void *mem;
} tensor_ctx;

static void tensor_deleter(DLManagedTensorVersioned *t)
{
	tensor_ctx *c = (tensor_ctx *)t;

	free(c->mem);
	free(c);
	tensors_deleted++;
}

PHP_FUNCTION(bat_import)
{
	zend_long code, bits, lanes, flags = 0, device = 1, device_id = 0, byte_offset = 0, minor = DLPACK_MINOR_VERSION, major = DLPACK_MAJOR_VERSION;
	HashTable *shape, *strides = NULL;
	tensor_ctx *c;
	zval *z;
	int n = 0;
	int64_t extent = 1, esize, acc = 1;
	size_t total;
	zend_object *view;

	ZEND_PARSE_PARAMETERS_START(4, 11)
		Z_PARAM_LONG(code) Z_PARAM_LONG(bits) Z_PARAM_LONG(lanes) Z_PARAM_ARRAY_HT(shape)
		Z_PARAM_OPTIONAL
		Z_PARAM_ARRAY_HT_OR_NULL(strides) Z_PARAM_LONG(flags) Z_PARAM_LONG(device) Z_PARAM_LONG(byte_offset) Z_PARAM_LONG(minor) Z_PARAM_LONG(major) Z_PARAM_LONG(device_id)
	ZEND_PARSE_PARAMETERS_END();

	if (zend_hash_num_elements(shape) > 16 || (strides && zend_hash_num_elements(strides) > 16)) {
		zend_argument_value_error(4, "has too many dimensions for the test helper");
		RETURN_THROWS();
	}
	c = calloc(1, sizeof *c);
	ZEND_HASH_FOREACH_VAL(shape, z) { c->shape[n++] = zval_get_long(z); } ZEND_HASH_FOREACH_END();
	esize = (bits * lanes + 7) / 8;
	if (esize < 1) esize = 1;
	if (strides) {
		int m = 0;
		ZEND_HASH_FOREACH_VAL(strides, z) { c->strides[m++] = zval_get_long(z); } ZEND_HASH_FOREACH_END();
		extent = 1;
		for (int i = 0; i < n; i++) {
			if (c->shape[i] == 0) { extent = 0; break; }
			if (c->strides[i] > 0) extent += (c->shape[i] - 1) * c->strides[i];
		}
	} else {
		for (int i = n - 1; i >= 0; i--) { acc *= c->shape[i] ? c->shape[i] : 1; }
		extent = acc;
	}
	total = (size_t)byte_offset + (size_t)extent * esize + 64;
	c->mem = malloc(total);
	for (size_t i = 0; i < total; i++) ((uint8_t *)c->mem)[i] = (uint8_t)i;

	c->t.version.major = (uint32_t)major;
	c->t.version.minor = (uint32_t)minor;
	c->t.manager_ctx = c;
	c->t.deleter = tensor_deleter;
	c->t.flags = (uint64_t)flags;
	c->t.dl_tensor.data = c->mem;
	c->t.dl_tensor.device.device_type = (DLDeviceType)device;
	c->t.dl_tensor.device.device_id = (int32_t)device_id;
	c->t.dl_tensor.ndim = n;
	c->t.dl_tensor.dtype.code = (uint8_t)code;
	c->t.dl_tensor.dtype.bits = (uint8_t)bits;
	c->t.dl_tensor.dtype.lanes = (uint16_t)lanes;
	c->t.dl_tensor.shape = c->shape;
	c->t.dl_tensor.strides = strides ? c->strides : NULL;
	c->t.dl_tensor.byte_offset = (uint64_t)byte_offset;
	tensors_made++;
	view = buffers_dlpack_import(&c->t);
	if (!view) {
		c->t.deleter(&c->t);   /* not imported: still ours */
		RETURN_THROWS();
	}
	RETURN_OBJ(view);
}

/* ---- DLPack ----------------------------------------------------------------------------------- */

/* bat_export(Tensor $v): exports through the C API, describes the tensor, then calls the deleter */
PHP_FUNCTION(bat_export)
{
	zend_object *v;
	DLManagedTensorVersioned *t;
	DLTensor *d;
	buffers_tensor_info vi;
	buffers_buffer_info bi;

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_OBJ(v)
	ZEND_PARSE_PARAMETERS_END();
	if (buffers_tensor_get_info(v, &vi) == FAILURE || buffers_buffer_get_info(vi.buffer, &bi) == FAILURE) RETURN_THROWS();
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

/* the elements of host memory in C order, their bytes concatenated (whole-byte dtypes; strides in elements) */
static void append_elements(smart_str *out, int ndim, const int64_t *shape, const int64_t *strides, int axis, const uint8_t *p, size_t size)
{
	if (axis == ndim) {
		smart_str_appendl(out, (const char *)p, size);
		return;
	}
	for (int64_t i = 0; i < shape[axis]; i++) append_elements(out, ndim, shape, strides, axis + 1, p + i * strides[axis] * (int64_t)size, size);
}

static void add_elements(zval *arr, int ndim, const int64_t *shape, const int64_t *strides, const uint8_t *p, size_t size)
{
	smart_str el = {0};

	add_assoc_bool(arr, "data_aligned", (uintptr_t)p % size == 0);
	append_elements(&el, ndim, shape, strides, 0, p, size);
	smart_str_0(&el);
	if (el.s) add_assoc_str(arr, "elements", el.s);
	else add_assoc_string(arr, "elements", "");
}

/* bat_tensor_bytes(Tensor $v): what buffers_tensor_get_info() hands out for a host view of a whole-byte dtype, with the elements read through
 * the pointer (bytewise, so at any address) */
PHP_FUNCTION(bat_tensor_bytes)
{
	zend_object *v;
	buffers_tensor_info i;
	buffers_buffer_info bi;

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_OBJ(v)
	ZEND_PARSE_PARAMETERS_END();
	if (buffers_tensor_get_info(v, &i) == FAILURE || buffers_buffer_get_info(i.buffer, &bi) == FAILURE || buffers_buffer_ensure_ready(i.buffer) == FAILURE) {
		RETURN_THROWS();
	}
	array_init(return_value);
	add_assoc_bool(return_value, "writable", i.writable);
	add_assoc_long(return_value, "byte_offset", i.byte_offset);
	list_i64(return_value, "strides", i.strides, i.ndim);
	add_elements(return_value, i.ndim, i.shape, i.strides, i.data, buffers_dtype_describe(i.dtype)->size);
}

/* bat_export_bytes(Tensor $v): a DLPack export of a host view of a whole-byte dtype, with the elements read through it, then the deleter */
PHP_FUNCTION(bat_export_bytes)
{
	zend_object *v;
	DLManagedTensorVersioned *t;
	DLTensor *d;

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_OBJ(v)
	ZEND_PARSE_PARAMETERS_END();
	if (buffers_dlpack_export(v, &t) == FAILURE) RETURN_THROWS();
	d = &t->dl_tensor;
	array_init(return_value);
	add_assoc_bool(return_value, "read_only", (t->flags & DLPACK_FLAG_BITMASK_READ_ONLY) != 0);
	add_assoc_long(return_value, "byte_offset", (zend_long)d->byte_offset);
	list_i64(return_value, "strides", d->strides, d->ndim);
	add_elements(return_value, d->ndim, d->shape, d->strides, (const uint8_t *)d->data + d->byte_offset, d->dtype.bits / 8);
	t->deleter(t);
}

/* bat_roundtrip(Tensor $v): export through the C API and import the tensor again */
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

/* ---- BatOperand: a class with arithmetic of its own (docs/arithmetic.md, Operands) -------------
 * Its do_operation describes the operation as a string. A Tensor operator with a BatOperand on either side must reach it:
 * Tensor's handler returns FAILURE without an exception for objects with arithmetic of their own. */

static zend_class_entry *ce_operand;
static zend_object_handlers operand_handlers;

static void describe(smart_str *s, zval *z)
{
	buffers_tensor_info i;

	ZVAL_DEREF(z);
	if (Z_TYPE_P(z) == IS_OBJECT && buffers_is_tensor(Z_OBJ_P(z)) && buffers_tensor_get_info(Z_OBJ_P(z), &i) == SUCCESS) {
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
	} else if (z && Z_TYPE_P(z) == IS_OBJECT) {
		smart_str_append(s, Z_OBJCE_P(z)->name);
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
	case ZEND_POW: name = "pow"; break;
	default: return FAILURE;
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

/* ---- an arithmetic backend for BatDevice ------------------------------------------------------------------
 * Adds and subtracts Float32 tensors of the same shape on BatDevice (whose memory is really malloc'd, so it can compute
 * there) into a new buffer on BatDevice. Declines everything else, so host operations fall back to ext-buffers. */

static bool bat_backend_can(uint8_t op, const buffers_operand *a, const buffers_operand *b, int rt, int ndim, const int64_t *shape)
{
	backend_asks++;
	if ((op != ZEND_ADD && op != ZEND_SUB) || !b || a->is_scalar || b->is_scalar || rt != BUFFERS_DT_FLOAT32) return false;
	if (!a->device || !b->device || a->device->ce != ce_device || b->device->ce != ce_device) return false;
	for (int i = 0; i < 2; i++) {
		const buffers_tensor_info *t = i ? &b->info : &a->info;
		int64_t expect = 1;

		if (t->dtype != BUFFERS_DT_FLOAT32 || t->ndim != ndim) return false;
		for (int j = ndim - 1; j >= 0; j--) {
			if (t->shape[j] != shape[j] || (shape[j] > 1 && t->strides[j] != expect)) return false;
			expect *= shape[j];
		}
	}
	return true;
}

static zend_object *bat_backend_run(uint8_t op, const buffers_operand *a, const buffers_operand *b, int rt, int ndim, const int64_t *shape)
{
	int64_t n = 1;
	float *mem, x, y;
	zval dev;
	zend_object *buf, *t;

	for (int j = 0; j < ndim; j++) n *= shape[j];
	mem = calloc((size_t)(n ? n : 1), sizeof(float));
	for (int64_t k = 0; k < n; k++) {
		memcpy(&x, a->info.data + k * 4, 4);
		memcpy(&y, b->info.data + k * 4, 4);
		mem[k] = op == ZEND_ADD ? x + y : x - y;
	}
	object_init_ex(&dev, ce_device);
	buf = buffers_buffer_create_foreign(mem, (size_t)n * 4, true, Z_OBJ(dev), counting_deleter, mem);
	zval_ptr_dtor(&dev);
	if (!buf) { free(mem); return NULL; }
	made++;
	t = buffers_tensor_create(buf, rt, ndim, shape, NULL, 0);
	OBJ_RELEASE(buf);
	backend_runs++;
	return t;
}

static const buffers_backend bat_backend = { "bat", bat_backend_can, bat_backend_run };

/* ---- element conversion ------------------------------------------------------------------------- */

/* bat_convert(DType $from, string $bytes, DType $to, int $n, int $srcStride = 1, int $dstStride = 1): string - the destination
 * bytes (zero-filled first), n elements converted with buffers_convert(). The source is padded so that reading n elements is safe. */
PHP_FUNCTION(bat_convert)
{
	zend_object *from, *to;
	zend_string *bytes, *out;
	zend_long n, ss = 1, ds = 1;
	int sd, dd;
	size_t slen, dlen;
	uint8_t *src;

	ZEND_PARSE_PARAMETERS_START(4, 6)
		Z_PARAM_OBJ(from)
		Z_PARAM_STR(bytes)
		Z_PARAM_OBJ(to)
		Z_PARAM_LONG(n)
		Z_PARAM_OPTIONAL
		Z_PARAM_LONG(ss)
		Z_PARAM_LONG(ds)
	ZEND_PARSE_PARAMETERS_END();

	sd = buffers_dtype_from_object(from);
	dd = buffers_dtype_from_object(to);
	if (n < 0 || ss < 1 || ds < 1) {
		zend_value_error("bad count or stride");
		RETURN_THROWS();
	}
	slen = (size_t)(((n ? (n - 1) * ss + 1 : 0) * buffers_dtype_describe(sd)->bits + 7) / 8) + 16;
	dlen = (size_t)(((n ? (n - 1) * ds + 1 : 0) * buffers_dtype_describe(dd)->bits + 7) / 8);
	if (ZSTR_LEN(bytes) > slen) slen = ZSTR_LEN(bytes);
	src = calloc(1, slen);
	memcpy(src, ZSTR_VAL(bytes), ZSTR_LEN(bytes));
	out = zend_string_alloc(dlen, 0);
	memset(ZSTR_VAL(out), 0, dlen);
	ZSTR_VAL(out)[dlen] = 0;
	buffers_convert(sd, src, ss, dd, (uint8_t *)ZSTR_VAL(out), ds, n);
	free(src);
	RETURN_NEW_STR(out);
}

/* ---- virtual properties ------------------------------------------------------------------------- */

static zend_class_entry *ce_props;

static ZEND_NAMED_FUNCTION(props_get_answer) { ZEND_PARSE_PARAMETERS_NONE(); RETURN_LONG(42); }
static ZEND_NAMED_FUNCTION(props_get_label) { ZEND_PARSE_PARAMETERS_NONE(); RETURN_STRING("label"); }

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(ai_props_int, 0, 0, IS_LONG, 0)
ZEND_END_ARG_INFO()
ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(ai_props_str, 0, 0, IS_STRING, 1)
ZEND_END_ARG_INFO()

/* ---- module ---------------------------------------------------------------------------------------- */

/* arginfo that matches each function's ZEND_PARSE_PARAMETERS (debug builds check it) */
ZEND_BEGIN_ARG_INFO_EX(ai_bat_import, 0, 0, 4)
	ZEND_ARG_INFO(0, a0)
	ZEND_ARG_INFO(0, a1)
	ZEND_ARG_INFO(0, a2)
	ZEND_ARG_INFO(0, a3)
	ZEND_ARG_INFO(0, a4)
	ZEND_ARG_INFO(0, a5)
	ZEND_ARG_INFO(0, a6)
	ZEND_ARG_INFO(0, a7)
	ZEND_ARG_INFO(0, a8)
	ZEND_ARG_INFO(0, a9)
	ZEND_ARG_INFO(0, a10)
ZEND_END_ARG_INFO()
ZEND_BEGIN_ARG_INFO_EX(ai_bat_make_buffer, 0, 0, 1)
	ZEND_ARG_INFO(0, a0)
	ZEND_ARG_INFO(0, a1)
ZEND_END_ARG_INFO()
ZEND_BEGIN_ARG_INFO_EX(ai_bat_make_pending, 0, 0, 2)
	ZEND_ARG_INFO(0, a0)
	ZEND_ARG_INFO(0, a1)
	ZEND_ARG_INFO(0, a2)
ZEND_END_ARG_INFO()
ZEND_BEGIN_ARG_INFO_EX(ai_bat_make_pending_arraybuffer, 0, 0, 2)
	ZEND_ARG_INFO(0, a0)
	ZEND_ARG_INFO(0, a1)
ZEND_END_ARG_INFO()
ZEND_BEGIN_ARG_INFO_EX(ai_bat_set_pending, 0, 0, 2)
	ZEND_ARG_INFO(0, a0)
	ZEND_ARG_INFO(0, a1)
ZEND_END_ARG_INFO()
ZEND_BEGIN_ARG_INFO_EX(ai_bat_set_ready_late, 0, 0, 1)
	ZEND_ARG_INFO(0, a0)
ZEND_END_ARG_INFO()
ZEND_BEGIN_ARG_INFO_EX(ai_bat_peek, 0, 0, 2)
	ZEND_ARG_INFO(0, a0)
	ZEND_ARG_INFO(0, a1)
ZEND_END_ARG_INFO()
ZEND_BEGIN_ARG_INFO_EX(ai_bat_counts, 0, 0, 0)
ZEND_END_ARG_INFO()
ZEND_BEGIN_ARG_INFO_EX(ai_bat_info, 0, 0, 1)
	ZEND_ARG_INFO(0, a0)
ZEND_END_ARG_INFO()
ZEND_BEGIN_ARG_INFO_EX(ai_bat_max_ndim, 0, 0, 0)
ZEND_END_ARG_INFO()
ZEND_BEGIN_ARG_INFO_EX(ai_bat_is, 0, 0, 1)
	ZEND_ARG_INFO(0, a0)
ZEND_END_ARG_INFO()
ZEND_BEGIN_ARG_INFO_EX(ai_bat_view, 0, 0, 3)
	ZEND_ARG_INFO(0, a0)
	ZEND_ARG_INFO(0, a1)
	ZEND_ARG_INFO(0, a2)
	ZEND_ARG_INFO(0, a3)
	ZEND_ARG_INFO(0, a4)
ZEND_END_ARG_INFO()
ZEND_BEGIN_ARG_INFO_EX(ai_bat_dtype_promote, 0, 0, 3)
	ZEND_ARG_INFO(0, a)
	ZEND_ARG_INFO(0, b)
	ZEND_ARG_INFO(0, op)
ZEND_END_ARG_INFO()
ZEND_BEGIN_ARG_INFO_EX(ai_bat_dtype_info, 0, 0, 1)
	ZEND_ARG_INFO(0, a0)
ZEND_END_ARG_INFO()
ZEND_BEGIN_ARG_INFO_EX(ai_bat_device_buffer, 0, 0, 1)
	ZEND_ARG_INFO(0, a0)
ZEND_END_ARG_INFO()
ZEND_BEGIN_ARG_INFO_EX(ai_bat_export, 0, 0, 1)
	ZEND_ARG_INFO(0, a0)
ZEND_END_ARG_INFO()
ZEND_BEGIN_ARG_INFO_EX(ai_bat_roundtrip, 0, 0, 1)
	ZEND_ARG_INFO(0, a0)
ZEND_END_ARG_INFO()
ZEND_BEGIN_ARG_INFO_EX(ai_bat_shape, 0, 0, 1)
	ZEND_ARG_INFO(0, a0)
ZEND_END_ARG_INFO()
ZEND_BEGIN_ARG_INFO_EX(ai_bat_shape_dims, 0, 0, 1)
	ZEND_ARG_INFO(0, a0)
ZEND_END_ARG_INFO()
ZEND_BEGIN_ARG_INFO_EX(ai_bat_convert, 0, 0, 4)
	ZEND_ARG_INFO(0, a0)
	ZEND_ARG_INFO(0, a1)
	ZEND_ARG_INFO(0, a2)
	ZEND_ARG_INFO(0, a3)
	ZEND_ARG_INFO(0, a4)
	ZEND_ARG_INFO(0, a5)
ZEND_END_ARG_INFO()

static const zend_function_entry functions[] = {
	PHP_FE(bat_import, ai_bat_import)
	PHP_FE(bat_dtype_promote, ai_bat_dtype_promote)
	PHP_FE(bat_make_buffer, ai_bat_make_buffer)
	PHP_FE(bat_make_pending, ai_bat_make_pending)
	PHP_FE(bat_make_pending_arraybuffer, ai_bat_make_pending_arraybuffer)
	PHP_FE(bat_set_pending, ai_bat_set_pending)
	PHP_FE(bat_set_ready_late, ai_bat_set_ready_late)
	PHP_FE(bat_peek, ai_bat_peek)
	PHP_FE(bat_counts, ai_bat_counts)
	PHP_FE(bat_info, ai_bat_info)
	PHP_FE(bat_max_ndim, ai_bat_max_ndim)
	PHP_FE(bat_info_twice, ai_bat_info)
	PHP_FE(bat_is, ai_bat_is)
	PHP_FE(bat_view, ai_bat_view)
	PHP_FE(bat_dtype_info, ai_bat_dtype_info)
	PHP_FE(bat_device_buffer, ai_bat_device_buffer)
	PHP_FE(bat_export, ai_bat_export)
	PHP_FE(bat_tensor_bytes, ai_bat_export)
	PHP_FE(bat_export_bytes, ai_bat_export)
	PHP_FE(bat_roundtrip, ai_bat_roundtrip)
	PHP_FE(bat_shape, ai_bat_shape)
	PHP_FE(bat_shape_dims, ai_bat_shape_dims)
	PHP_FE(bat_convert, ai_bat_convert)
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
	zend_class_entry ce;

#if defined(ZTS) && defined(COMPILE_DL_BUFFERS_API_TEST)
	ZEND_TSRMLS_CACHE_UPDATE();
#endif
	if (buffers_api_version() != BUFFERS_API_VERSION) {
		zend_error(E_CORE_ERROR, "buffers_api_test was built for ext-buffers API %d, found %d", BUFFERS_API_VERSION, buffers_api_version());
		return FAILURE;
	}
	INIT_CLASS_ENTRY(ce, "BatOperand", NULL);
	ce_operand = zend_register_internal_class_ex(&ce, NULL);
	ce_operand->ce_flags |= ZEND_ACC_FINAL | ZEND_ACC_NOT_SERIALIZABLE;
	memcpy(&operand_handlers, &std_object_handlers, sizeof operand_handlers);
	operand_handlers.do_operation = describing_handler;
	ce_operand->default_object_handlers = &operand_handlers;

	INIT_CLASS_ENTRY(ce, "BatMatrix", matrix_methods);
	ce_matrix = zend_register_internal_class_ex(&ce, NULL);
	ce_matrix->ce_flags |= ZEND_ACC_FINAL | ZEND_ACC_NO_DYNAMIC_PROPERTIES | ZEND_ACC_NOT_SERIALIZABLE;
	ce_matrix->create_object = matrix_create;
	memcpy(&matrix_handlers, &std_object_handlers, sizeof matrix_handlers);
	matrix_handlers.offset = XtOffsetOf(matrix, std);
	matrix_handlers.free_obj = matrix_free;
	matrix_handlers.clone_obj = NULL;

	INIT_CLASS_ENTRY(ce, "BatTensor", tensor_methods);
	ce_tensor = zend_register_internal_class_ex(&ce, NULL);
	ce_tensor->ce_flags |= ZEND_ACC_FINAL | ZEND_ACC_NO_DYNAMIC_PROPERTIES | ZEND_ACC_NOT_SERIALIZABLE;
	ce_tensor->create_object = tensor_create;
	memcpy(&tensor_handlers, &std_object_handlers, sizeof tensor_handlers);
	tensor_handlers.offset = XtOffsetOf(bat_tensor, std);
	tensor_handlers.clone_obj = NULL;
	zend_class_implements(ce_tensor, 1, buffers_get_class(BUFFERS_CE_TENSOR_AGGREGATE));

	{
		const buffers_virtual_prop props[] = {
			{ "answer", (zend_type) ZEND_TYPE_INIT_MASK(MAY_BE_LONG), props_get_answer, ai_props_int },
			{ "label", (zend_type) ZEND_TYPE_INIT_MASK(MAY_BE_STRING | MAY_BE_NULL), props_get_label, ai_props_str },
		};
		INIT_CLASS_ENTRY(ce, "BatProps", NULL);
		ce_props = zend_register_internal_class_ex(&ce, NULL);
		ce_props->ce_flags |= ZEND_ACC_FINAL | ZEND_ACC_NOT_SERIALIZABLE;
		buffers_declare_virtual_props(ce_props, props, 2);
	}

	INIT_CLASS_ENTRY(ce, "BatDevice", NULL);
	ce_device = zend_register_internal_class_ex(&ce, NULL);
	ce_device->ce_flags |= ZEND_ACC_FINAL | ZEND_ACC_NO_DYNAMIC_PROPERTIES | ZEND_ACC_NOT_SERIALIZABLE;
	buffers_implement_device(ce_device, device_get_name, device_get_id, device_get_host);
	if (buffers_register_device_type(ce_device, kDLCUDA) == FAILURE || buffers_register_transfer(ce_device, bat_transfer) == FAILURE
		|| buffers_register_backend(&bat_backend) == FAILURE) {
		return FAILURE;
	}
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
