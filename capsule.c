/* DLPack interop: BufferView::toDLPack() / fromDLPack() and the opaque DLPackCapsule. */
#include "php_buffers.h"
#include "zend_exceptions.h"
#include "buffers_arginfo.h"

zend_class_entry *buf_ce_capsule;
static zend_object_handlers capsule_handlers;

static zend_object *capsule_create(zend_class_entry *ce)
{
	buf_capsule *c = zend_object_alloc(sizeof(buf_capsule), ce);

	zend_object_std_init(&c->std, ce);
	c->std.handlers = &capsule_handlers;
	c->t = NULL;
	return &c->std;
}

static zend_function *capsule_no_constructor(zend_object *o)
{
	zend_throw_error(NULL, "DLPackCapsule cannot be instantiated; use BufferView::toDLPack()");
	return NULL;
}

/* An unconsumed capsule that is dropped releases its tensor. */
static void capsule_free(zend_object *o)
{
	buf_capsule *c = buf_capsule_from(o);

	if (c->t && c->t->deleter) {
		c->t->deleter(c->t);
	}
	zend_object_std_dtor(o);
}

static zend_object *capsule_new(DLManagedTensorVersioned *t)
{
	zend_object *o = capsule_create(buf_ce_capsule);

	buf_capsule_from(o)->t = t;
	return o;
}

/* ---- export --------------------------------------------------------------------------- */

typedef struct {
	DLManagedTensorVersioned t; /* first: the pointer handed to the deleter is the context */
	int64_t shape[BUF_MAX_NDIM], strides[BUF_MAX_NDIM];
	zend_object *buf;
} export_ctx;

static void export_deleter(DLManagedTensorVersioned *t)
{
	export_ctx *c = (export_ctx *)t;

	if (BUFG(request_active)) { /* otherwise the engine is already tearing every object down */
		OBJ_RELEASE(c->buf);
	}
	free(c);
}

zend_result buf_view_export(zend_object *vo, DLManagedTensorVersioned **out)
{
	buf_view *v = buf_view_from(vo);
	buf_buffer *b = buf_buffer_from(v->buf);
	const buf_dtype_info *info = &buf_dtypes[v->dt];
	export_ctx *c;
	DLTensor *d;

	if (UNEXPECTED(b->pending) && buf_buffer_make_ready(b) == FAILURE) {
		return FAILURE;
	}
	c = malloc(sizeof(export_ctx));
	if (!c) {
		zend_throw_error(NULL, "Out of memory");
		return FAILURE;
	}
	c->t.version.major = DLPACK_MAJOR_VERSION;
	c->t.version.minor = DLPACK_MINOR_VERSION;
	c->t.manager_ctx = c;
	c->t.deleter = export_deleter;
	c->t.flags = v->writable ? 0 : DLPACK_FLAG_BITMASK_READ_ONLY; /* packed sub-byte data is DLPack's default: no PADDED flag */
	d = &c->t.dl_tensor;
	d->data = b->base;
	d->device.device_type = (DLDeviceType)b->dev_type;
	d->device.device_id = b->dev_id;
	d->ndim = v->ndim;
	d->dtype.code = info->dlpack_code;
	d->dtype.bits = info->bits;
	d->dtype.lanes = 1;
	d->shape = c->shape;
	d->strides = c->strides; /* always materialised: NULL strides are illegal since DLPack 1.2 */
	d->byte_offset = (uint64_t)(v->data - b->base);
	for (int i = 0; i < v->ndim; i++) {
		c->shape[i] = v->shape[i];
	}
	buf_view_strides_elements(v, c->strides);
	GC_ADDREF(v->buf);
	c->buf = v->buf;
	*out = &c->t;
	return SUCCESS;
}

zend_result buffers_dlpack_export(zend_object *view, DLManagedTensorVersioned **out)
{
	return buf_view_export(view, out);
}

PHP_METHOD(Buffers_BufferView, toDLPack)
{
	DLManagedTensorVersioned *t;

	ZEND_PARSE_PARAMETERS_NONE();
	if (buf_view_export(Z_OBJ_P(ZEND_THIS), &t) == FAILURE) {
		RETURN_THROWS();
	}
	RETURN_OBJ(capsule_new(t));
}

/* ---- import --------------------------------------------------------------------------- */

static void dlpack_buffer_deleter(void *ctx)
{
	DLManagedTensorVersioned *t = ctx;

	if (t->deleter) {
		t->deleter(t);
	}
}

#define IMPORT_FAIL(...) do { zend_value_error(__VA_ARGS__); return NULL; } while (0)

/* Imports a tensor of any device; on success the buffer owns the tensor. NULL after throwing, tensor untouched. */
zend_object *buf_view_import(DLManagedTensorVersioned *t)
{
	DLTensor *d = &t->dl_tensor;
	int dt = -1, ndim;
	int64_t shape[BUF_MAX_NDIM] = {0}, st[BUF_MAX_NDIM] = {0}, sb[BUF_MAX_NDIM] = {0}, size, bits, last = 0, tmp, len, extent;
	bool empty = false, packed, host;
	int32_t dtype_dev = d->device.device_type;
	zend_object *buf, *dev = NULL, *view;
	uint8_t *base;

	if (t->version.major != DLPACK_MAJOR_VERSION) {
		IMPORT_FAIL("Unsupported DLPack major version %u (expected %d)", (unsigned)t->version.major, DLPACK_MAJOR_VERSION);
	}
	if (dtype_dev < 1) {
		IMPORT_FAIL("Invalid DLPack device type %d", (int)dtype_dev);
	}
	if (d->dtype.lanes != 1) {
		IMPORT_FAIL("Vector dtypes are not supported (%u lanes)", (unsigned)d->dtype.lanes);
	}
	for (int i = 0; i < BUF_NTYPES; i++) {
		if (buf_dtypes[i].dlpack_code == d->dtype.code && buf_dtypes[i].bits == d->dtype.bits) {
			dt = i;
			break;
		}
	}
	if (dt < 0) {
		IMPORT_FAIL("Unsupported DLPack dtype (code %u, %u bits)", (unsigned)d->dtype.code, (unsigned)d->dtype.bits);
	}
	if (!buf_dtype_fits_php_int(dt)) {
		IMPORT_FAIL("The dtype %s is not supported on 32-bit PHP: its values do not fit a PHP int", buf_dtypes[dt].name);
	}
	packed = buf_is_packed(dt);
	size = buf_dtypes[dt].size;
	bits = buf_dtypes[dt].bits;
	if (packed && (t->flags & DLPACK_FLAG_BITMASK_IS_SUBBYTE_TYPE_PADDED)) {
		IMPORT_FAIL("Padded sub-byte tensors are not supported; the tensor must be packed (DLPack's default: little-bit-endian, no padding)");
	}
	if (d->ndim < 0 || d->ndim > BUF_MAX_NDIM) {
		IMPORT_FAIL("Unsupported number of dimensions (%d); expected 0 to %d", (int)d->ndim, BUF_MAX_NDIM);
	}
	ndim = d->ndim;

	for (int i = 0; i < ndim; i++) {
		shape[i] = d->shape[i];
		if (shape[i] < 0) {
			IMPORT_FAIL("Negative dimension in shape");
		}
		if (shape[i] == 0) {
			empty = true;
		}
	}
	if (ndim == 0) {
		/* a scalar: nothing to read */
	} else if (d->strides) {
		for (int i = 0; i < ndim; i++) {
			if (d->strides[i] < 1) {
				IMPORT_FAIL("Zero and negative strides are not supported");
			}
			st[i] = d->strides[i];
		}
	} else if (t->version.minor >= 2) {
		IMPORT_FAIL("Tensors without strides are illegal since DLPack 1.2");
	} else { /* DLPack < 1.2: NULL strides mean compact row-major */
		int64_t acc = 1;
		for (int i = ndim - 1; i >= 0; i--) {
			st[i] = acc;
			if (shape[i] > 1 && acc > INT64_MAX / shape[i]) {
				IMPORT_FAIL("Shape is too large");
			}
			acc *= shape[i] ? shape[i] : 1;
		}
	}
	if (packed) {
		if (ndim > 0 && st[ndim - 1] != 1) {
			IMPORT_FAIL("The last axis of a packed sub-byte tensor must have stride 1");
		}
		for (int i = 0; i < ndim - 1; i++) {
			if (buf_mul_ovf(st[i], bits, &tmp)) {
				IMPORT_FAIL("Stride is too large");
			}
			if (tmp % 8) {
				IMPORT_FAIL("Axis %d of a packed sub-byte tensor has a stride of %lld bits, which is not a whole number of bytes", i, (long long)tmp);
			}
			sb[i] = tmp / 8;
		}
	} else {
		for (int i = 0; i < ndim; i++) {
			if (buf_mul_ovf(st[i], size, &sb[i])) {
				IMPORT_FAIL("Stride is too large");
			}
		}
	}

	/* bytes from the first to past the last element */
	if (empty) {
		extent = 0;
	} else {
		if (packed) {
			if (ndim > 0) {
				if (buf_mul_ovf(shape[ndim - 1], bits, &tmp)) {
					IMPORT_FAIL("The tensor is too large");
				}
				extent = (tmp + 7) / 8;
			} else {
				extent = 1;
			}
		} else {
			extent = size;
		}
		for (int i = 0; i < ndim - (packed ? 1 : 0); i++) {
			if (buf_mul_ovf(shape[i] - 1, sb[i], &tmp) || last > INT64_MAX - tmp) {
				IMPORT_FAIL("The tensor is too large");
			}
			last += tmp;
		}
		if (last > INT64_MAX - extent) {
			IMPORT_FAIL("The tensor is too large");
		}
		extent += last;
		if (!d->data) {
			IMPORT_FAIL("Tensor data pointer is NULL");
		}
	}
	host = dtype_dev == kDLCPU || dtype_dev == kDLCUDAHost || dtype_dev == kDLROCMHost || dtype_dev == kDLCUDAManaged;
	if (host && !packed && ((uintptr_t)d->data + d->byte_offset) % size) {
		IMPORT_FAIL("Tensor data is not aligned to the element size (%d bytes)", (int)size);
	}
	if (d->byte_offset > (uint64_t)INT64_MAX || (uint64_t)extent > (uint64_t)INT64_MAX - d->byte_offset) {
		IMPORT_FAIL("The tensor is too large");
	}

	if (dtype_dev != kDLCPU) {
		dev = buf_foreign_device_new(dtype_dev, d->device.device_id);
	}
	if (host) {
		/* the buffer covers exactly the tensor's extent: the view starts at offset 0 */
		base = (uint8_t *)d->data + d->byte_offset;
		len = extent;
	} else {
		/* an opaque device handle: never dereferenced, passed through unchanged on export */
		base = (uint8_t *)d->data;
		len = (int64_t)d->byte_offset + extent;
	}
	buf = buf_foreign_buffer_new(base, (size_t)len, !(t->flags & DLPACK_FLAG_BITMASK_READ_ONLY), dev, dlpack_buffer_deleter, t, NULL);
	if (dev) {
		OBJ_RELEASE(dev);
	}
	if (!buf) {
		return NULL;
	}
	/* the buffer now owns the tensor: its deleter runs exactly once, when the buffer is freed */
	view = buf_view_new(buf, dt, ndim, shape, sb, host ? base : base + d->byte_offset, buf_buffer_from(buf)->writable, buf_buffer_from(buf)->slow);
	OBJ_RELEASE(buf);
	return view;
}

zend_object *buffers_dlpack_import(DLManagedTensorVersioned *t)
{
	return buf_view_import(t);
}

PHP_METHOD(Buffers_BufferView, fromDLPack)
{
	zend_object *co, *view;
	buf_capsule *cap;

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_OBJ_OF_CLASS(co, buf_ce_capsule)
	ZEND_PARSE_PARAMETERS_END();

	cap = buf_capsule_from(co);
	if (!cap->t) {
		zend_throw_error(NULL, "The DLPackCapsule has already been consumed");
		RETURN_THROWS();
	}
	view = buf_view_import(cap->t);
	if (!view) {
		RETURN_THROWS(); /* a rejected capsule is not consumed: its deleter still runs, once, when it is freed */
	}
	cap->t = NULL;
	RETURN_OBJ(view);
}

void buf_capsule_minit(void)
{
	zend_class_entry ce;

	INIT_CLASS_ENTRY(ce, "Buffers\\DLPackCapsule", NULL);
	buf_ce_capsule = zend_register_internal_class_ex(&ce, NULL);
	buf_ce_capsule->ce_flags |= ZEND_ACC_FINAL | ZEND_ACC_NO_DYNAMIC_PROPERTIES | ZEND_ACC_NOT_SERIALIZABLE;
	buf_ce_capsule->create_object = capsule_create;
	memcpy(&capsule_handlers, &std_object_handlers, sizeof capsule_handlers);
	capsule_handlers.offset = XtOffsetOf(buf_capsule, std);
	capsule_handlers.free_obj = capsule_free;
	capsule_handlers.clone_obj = NULL;
	capsule_handlers.compare = buf_compare_identity;
	capsule_handlers.get_constructor = capsule_no_constructor;
}

#ifdef BUFFERS_TESTING
/* Test-only (configure --enable-buffers-testing): produces tensors as a foreign framework would,
 * with a deleter that is counted, so the suite can prove the deleter runs exactly once. */
typedef struct {
	DLManagedTensorVersioned t;
	int64_t shape[16], strides[16]; /* more than BUF_MAX_NDIM, so tests can ask for 9 dimensions */
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

ZEND_BEGIN_ARG_INFO_EX(arginfo_buffers_test_make, 0, 0, 4)
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
	ZEND_ARG_TYPE_INFO(0, deviceId, IS_LONG, 0)
ZEND_END_ARG_INFO()
ZEND_BEGIN_ARG_INFO_EX(arginfo_buffers_test_counts, 0, 0, 0)
ZEND_END_ARG_INFO()

/* buffers_test_make(code, bits, lanes, shape, ?strides = null, flags = 0, device = 1, byteOffset = 0, minor = 3, major = 1, deviceId = 0)
 * The memory holds the byte pattern i & 0xff, so tests can check what a view reads. */
PHP_FUNCTION(buffers_test_make)
{
	zend_long code, bits, lanes, flags = 0, device = 1, device_id = 0, byte_offset = 0, minor = DLPACK_MINOR_VERSION, major = DLPACK_MAJOR_VERSION;
	HashTable *shape, *strides = NULL;
	test_ctx *c;
	zval *z;
	int n = 0;
	int64_t extent = 1, esize, acc = 1;
	size_t total;

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
	if (posix_memalign(&c->mem, 64, total) != 0) { free(c); RETURN_FALSE; }
	for (size_t i = 0; i < total; i++) ((uint8_t *)c->mem)[i] = (uint8_t)i;

	c->t.version.major = (uint32_t)major;
	c->t.version.minor = (uint32_t)minor;
	c->t.manager_ctx = c;
	c->t.deleter = test_deleter;
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
	test_made++;
	RETURN_OBJ(capsule_new(&c->t));
}

PHP_FUNCTION(buffers_test_counts)
{
	ZEND_PARSE_PARAMETERS_NONE();
	array_init(return_value);
	add_assoc_long(return_value, "made", test_made);
	add_assoc_long(return_value, "deleted", test_deleted);
}

const zend_function_entry buf_testing_functions[] = {
	PHP_FE(buffers_test_make, arginfo_buffers_test_make)
	PHP_FE(buffers_test_counts, arginfo_buffers_test_counts)
	PHP_FE_END
};
#endif
