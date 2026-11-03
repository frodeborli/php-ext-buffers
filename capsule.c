/* DLPack interop: TypedView::toDLPack() / fromDLPack() and the opaque DLPackCapsule. */
#include "php_dlpack.h"
#include "zend_exceptions.h"
#include "dlpack_arginfo.h"

zend_class_entry *dlp_ce_capsule;
static zend_object_handlers capsule_handlers;

static zend_object *capsule_create(zend_class_entry *ce)
{
	dlp_capsule *c = zend_object_alloc(sizeof(dlp_capsule), ce);

	zend_object_std_init(&c->std, ce);
	c->std.handlers = &capsule_handlers;
	c->t = NULL;
	return &c->std;
}

static zend_function *capsule_no_constructor(zend_object *o)
{
	zend_throw_error(NULL, "DLPackCapsule cannot be instantiated; use TypedView::toDLPack()");
	return NULL;
}

/* An unconsumed capsule that is dropped releases its tensor. */
static void capsule_free(zend_object *o)
{
	dlp_capsule *c = dlp_capsule_from(o);

	if (c->t && c->t->deleter) {
		c->t->deleter(c->t);
	}
	zend_object_std_dtor(o);
}

static zend_object *capsule_new(DLManagedTensorVersioned *t)
{
	zend_object *o = capsule_create(dlp_ce_capsule);

	dlp_capsule_from(o)->t = t;
	return o;
}

/* ---- export --------------------------------------------------------------------------- */

typedef struct {
	DLManagedTensorVersioned t; /* first: the pointer handed to the deleter is the context */
	int64_t shape[DLP_MAX_NDIM], strides[DLP_MAX_NDIM];
	zend_object *buf;
} export_ctx;

static void export_deleter(DLManagedTensorVersioned *t)
{
	export_ctx *c = (export_ctx *)t;

	if (DLPG(request_active)) { /* otherwise the engine is already tearing every object down */
		OBJ_RELEASE(c->buf);
	}
	free(c);
}

PHP_METHOD(TypedView, toDLPack)
{
	dlp_view *v = dlp_view_from(Z_OBJ_P(ZEND_THIS));
	dlp_buffer *b = dlp_buffer_from(v->buf);
	const dlp_dtype_info *info = &dlp_dtypes[v->dt];
	export_ctx *c;
	DLTensor *d;

	ZEND_PARSE_PARAMETERS_NONE();

	c = malloc(sizeof(export_ctx));
	if (!c) {
		zend_throw_error(NULL, "Out of memory");
		RETURN_THROWS();
	}
	c->t.version.major = DLPACK_MAJOR_VERSION;
	c->t.version.minor = DLPACK_MINOR_VERSION;
	c->t.manager_ctx = c;
	c->t.deleter = export_deleter;
	c->t.flags = (v->writable ? 0 : DLPACK_FLAG_BITMASK_READ_ONLY) | (info->bits < 8 ? DLPACK_FLAG_BITMASK_IS_SUBBYTE_TYPE_PADDED : 0);
	d = &c->t.dl_tensor;
	d->data = b->base;
	d->device.device_type = kDLCPU;
	d->device.device_id = 0;
	d->ndim = v->ndim;
	d->dtype.code = info->code;
	d->dtype.bits = info->bits;
	d->dtype.lanes = 1;
	d->shape = c->shape;
	d->strides = c->strides; /* always materialised: NULL strides are illegal since DLPack 1.2 */
	d->byte_offset = (uint64_t)(v->data - b->base);
	for (int i = 0; i < v->ndim; i++) {
		c->shape[i] = v->shape[i];
		c->strides[i] = v->sbytes[i] / info->size;
	}
	GC_ADDREF(v->buf);
	c->buf = v->buf;
	RETURN_OBJ(capsule_new(&c->t));
}

/* ---- import --------------------------------------------------------------------------- */

PHP_METHOD(TypedView, fromDLPack)
{
	zend_object *co;
	dlp_capsule *cap;
	DLManagedTensorVersioned *t;
	DLTensor *d;
	int dt = -1, ndim;
	int64_t shape[DLP_MAX_NDIM] = {0}, sb[DLP_MAX_NDIM] = {0}, size, last = 0, tmp, len;
	bool empty = false;
	zend_object *buf;

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_OBJ_OF_CLASS(co, dlp_ce_capsule)
	ZEND_PARSE_PARAMETERS_END();

	cap = dlp_capsule_from(co);
	t = cap->t;
	if (!t) {
		zend_throw_error(NULL, "The DLPackCapsule has already been consumed");
		RETURN_THROWS();
	}
	d = &t->dl_tensor;
	if (t->version.major != DLPACK_MAJOR_VERSION) {
		zend_value_error("Unsupported DLPack major version %u (expected %d)", (unsigned)t->version.major, DLPACK_MAJOR_VERSION);
		RETURN_THROWS();
	}
	if (d->device.device_type != kDLCPU) {
		zend_value_error("Only CPU tensors can be imported (device type %d)", (int)d->device.device_type);
		RETURN_THROWS();
	}
	if (d->dtype.lanes != 1) {
		zend_value_error("Vector dtypes are not supported (%u lanes)", (unsigned)d->dtype.lanes);
		RETURN_THROWS();
	}
	for (int i = 0; i < DLP_NTYPES; i++) {
		if (dlp_dtypes[i].code == d->dtype.code && dlp_dtypes[i].bits == d->dtype.bits) {
			dt = i;
			break;
		}
	}
	if (dt < 0) {
		zend_value_error("Unsupported DLPack dtype (code %u, %u bits)", (unsigned)d->dtype.code, (unsigned)d->dtype.bits);
		RETURN_THROWS();
	}
	if (d->dtype.bits < 8 && !(t->flags & DLPACK_FLAG_BITMASK_IS_SUBBYTE_TYPE_PADDED)) {
		zend_value_error("Packed sub-byte tensors are not supported; the tensor must be padded (one element per byte)");
		RETURN_THROWS();
	}
	if (d->ndim < 1 || d->ndim > DLP_MAX_NDIM) {
		zend_value_error("Unsupported number of dimensions (%d); expected 1 to %d", (int)d->ndim, DLP_MAX_NDIM);
		RETURN_THROWS();
	}
	ndim = d->ndim;
	size = dlp_dtypes[dt].size;

	for (int i = 0; i < ndim; i++) {
		shape[i] = d->shape[i];
		if (shape[i] < 0) {
			zend_value_error("Negative dimension in shape");
			RETURN_THROWS();
		}
		if (shape[i] == 0) {
			empty = true;
		}
	}
	if (d->strides) {
		for (int i = 0; i < ndim; i++) {
			if (d->strides[i] < 1) {
				zend_value_error("Zero and negative strides are not supported");
				RETURN_THROWS();
			}
			if (d->strides[i] > INT64_MAX / size) {
				zend_value_error("Stride is too large");
				RETURN_THROWS();
			}
			sb[i] = d->strides[i] * size;
		}
	} else if (t->version.minor >= 2) {
		zend_value_error("Tensors without strides are illegal since DLPack 1.2");
		RETURN_THROWS();
	} else { /* DLPack < 1.2: NULL strides mean compact row-major */
		int64_t acc = size;
		for (int i = ndim - 1; i >= 0; i--) {
			sb[i] = acc;
			if (shape[i] > 1 && acc > INT64_MAX / shape[i]) {
				zend_value_error("Shape is too large");
				RETURN_THROWS();
			}
			acc *= shape[i] ? shape[i] : 1;
		}
	}

	if (empty) {
		len = 0;
	} else {
		for (int i = 0; i < ndim; i++) {
			if (dlp_mul_ovf(shape[i] - 1, sb[i], &tmp) || last > INT64_MAX - tmp) {
				zend_value_error("The tensor is too large");
				RETURN_THROWS();
			}
			last += tmp;
		}
		if (last > INT64_MAX - size) {
			zend_value_error("The tensor is too large");
			RETURN_THROWS();
		}
		len = last + size;
		if (!d->data) {
			zend_value_error("Tensor data pointer is NULL");
			RETURN_THROWS();
		}
	}
	if (((uintptr_t)d->data + d->byte_offset) % size) {
		zend_value_error("Tensor data is not aligned to the element size (%d bytes)", (int)size);
		RETURN_THROWS();
	}

	buf = dlp_foreign_buffer_new(t, (uint8_t *)d->data + d->byte_offset, (size_t)len);
	cap->t = NULL; /* consumed: the buffer owns the tensor and calls its deleter exactly once */
	ZVAL_OBJ(return_value, dlp_view_new(buf, dt, ndim, shape, sb, dlp_buffer_from(buf)->base, dlp_buffer_from(buf)->writable));
	OBJ_RELEASE(buf);
}

void dlp_capsule_minit(void)
{
	zend_class_entry ce;

	INIT_CLASS_ENTRY(ce, "DLPackCapsule", NULL);
	dlp_ce_capsule = zend_register_internal_class_ex(&ce, NULL);
	dlp_ce_capsule->ce_flags |= ZEND_ACC_FINAL | ZEND_ACC_NO_DYNAMIC_PROPERTIES | ZEND_ACC_NOT_SERIALIZABLE;
	dlp_ce_capsule->create_object = capsule_create;
	memcpy(&capsule_handlers, &std_object_handlers, sizeof capsule_handlers);
	capsule_handlers.offset = XtOffsetOf(dlp_capsule, std);
	capsule_handlers.free_obj = capsule_free;
	capsule_handlers.clone_obj = NULL;
	capsule_handlers.compare = dlp_compare_identity;
	capsule_handlers.get_constructor = capsule_no_constructor;
}

#ifdef DLPACK_TESTING
/* Test-only (configure --enable-dlpack-testing): produces tensors as a foreign framework would,
 * with a deleter that is counted, so the suite can prove the deleter runs exactly once. */
typedef struct {
	DLManagedTensorVersioned t;
	int64_t shape[16], strides[16]; /* more than DLP_MAX_NDIM, so tests can ask for 9 dimensions */
	void *mem;
} test_ctx;
static zend_long test_made, test_deleted;

static void test_deleter(DLManagedTensorVersioned *t)
{
	test_ctx *c = (test_ctx *)t;

	free(c->mem);
	free(c);
	test_deleted++;
}

ZEND_BEGIN_ARG_INFO_EX(arginfo_dlpack_test_make, 0, 0, 4)
	ZEND_ARG_TYPE_INFO(0, code, IS_LONG, 0)
	ZEND_ARG_TYPE_INFO(0, bits, IS_LONG, 0)
	ZEND_ARG_TYPE_INFO(0, lanes, IS_LONG, 0)
	ZEND_ARG_TYPE_INFO(0, shape, IS_ARRAY, 0)
	ZEND_ARG_TYPE_INFO(0, strides, IS_ARRAY, 1)
	ZEND_ARG_TYPE_INFO(0, flags, IS_LONG, 0)
	ZEND_ARG_TYPE_INFO(0, device, IS_LONG, 0)
	ZEND_ARG_TYPE_INFO(0, byteOffset, IS_LONG, 0)
	ZEND_ARG_TYPE_INFO(0, minor, IS_LONG, 0)
	ZEND_ARG_TYPE_INFO(0, major, IS_LONG, 0)
ZEND_END_ARG_INFO()
ZEND_BEGIN_ARG_INFO_EX(arginfo_dlpack_test_counts, 0, 0, 0)
ZEND_END_ARG_INFO()

/* dlpack_test_make(code, bits, lanes, shape, ?strides = null, flags = 0, device = 1, byteOffset = 0, minor = 3, major = 1)
 * The memory holds the byte pattern i & 0xff, so tests can check what a view reads. */
PHP_FUNCTION(dlpack_test_make)
{
	zend_long code, bits, lanes, flags = 0, device = 1, byte_offset = 0, minor = DLPACK_MINOR_VERSION, major = DLPACK_MAJOR_VERSION;
	HashTable *shape, *strides = NULL;
	test_ctx *c;
	zval *z;
	int n = 0;
	int64_t extent = 1, esize, acc = 1;
	size_t total;

	ZEND_PARSE_PARAMETERS_START(4, 10)
		Z_PARAM_LONG(code) Z_PARAM_LONG(bits) Z_PARAM_LONG(lanes) Z_PARAM_ARRAY_HT(shape)
		Z_PARAM_OPTIONAL
		Z_PARAM_ARRAY_HT_OR_NULL(strides) Z_PARAM_LONG(flags) Z_PARAM_LONG(device) Z_PARAM_LONG(byte_offset) Z_PARAM_LONG(minor) Z_PARAM_LONG(major)
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
	if (posix_memalign(&c->mem, 64, total) != 0) { free(c); RETURN_FALSE; }
	for (size_t i = 0; i < total; i++) ((uint8_t *)c->mem)[i] = (uint8_t)i;

	c->t.version.major = (uint32_t)major;
	c->t.version.minor = (uint32_t)minor;
	c->t.manager_ctx = c;
	c->t.deleter = test_deleter;
	c->t.flags = (uint64_t)flags;
	c->t.dl_tensor.data = c->mem;
	c->t.dl_tensor.device.device_type = (DLDeviceType)device;
	c->t.dl_tensor.ndim = n;
	c->t.dl_tensor.dtype.code = (uint8_t)code;
	c->t.dl_tensor.dtype.bits = (uint8_t)bits;
	c->t.dl_tensor.dtype.lanes = (uint16_t)lanes;
	c->t.dl_tensor.shape = c->shape;
	c->t.dl_tensor.strides = strides ? c->strides : NULL;
	c->t.dl_tensor.byte_offset = (uint64_t)byte_offset;
	test_made++;
	RETURN_OBJ(capsule_new(&c->t));
}

PHP_FUNCTION(dlpack_test_counts)
{
	ZEND_PARSE_PARAMETERS_NONE();
	array_init(return_value);
	add_assoc_long(return_value, "made", test_made);
	add_assoc_long(return_value, "deleted", test_deleted);
}

const zend_function_entry dlp_testing_functions[] = {
	PHP_FE(dlpack_test_make, arginfo_dlpack_test_make)
	PHP_FE(dlpack_test_counts, arginfo_dlpack_test_counts)
	PHP_FE_END
};
#endif
