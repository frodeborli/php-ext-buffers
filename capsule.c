/* DLPack interop for the C API: buffers_dlpack_export() and buffers_dlpack_import(). There is no PHP-level DLPack. */
#include "php_buffers.h"
#include "zend_exceptions.h"
#include "buffers_arginfo.h"

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

	if (BUF_SWAP && info->size > 1) {
		/* DLPack data is in the host's byte order; tensor elements are little-endian */
		zend_throw_error(NULL, "Cannot export a tensor of dtype %s through DLPack on a big-endian host: DLPack uses the host's byte order", info->name);
		return FAILURE;
	}
	if (UNEXPECTED(b->pending) && buf_buffer_make_ready(b) == FAILURE) {
		return FAILURE;
	}
	if (b->dev_type < 0) {
		if (b->dev_type == BUF_DEV_FILE && !b->host) {
			zend_throw_error(NULL, "Cannot export a view of an unmapped file as a DLPack tensor: transfer to the host first with to()");
		} else if (b->dev_type != BUF_DEV_FILE) {
			zend_throw_error(NULL, "Cannot export a view on a device that has no DLPack device type");
		}
		if (EG(exception)) return FAILURE;
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
	d->device.device_type = b->dev_type == BUF_DEV_FILE ? kDLCPU : (DLDeviceType)b->dev_type;   /* a mapped file is host memory */
	d->device.device_id = b->dev_type == BUF_DEV_FILE ? 0 : b->dev_id;
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
	if (BUF_SWAP && buf_dtypes[dt].size > 1) {
		IMPORT_FAIL("Cannot import a DLPack tensor of dtype %s on a big-endian host: DLPack uses the host's byte order", buf_dtypes[dt].name);
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
			if (d->strides[i] < 0) {
				IMPORT_FAIL("Negative strides are not supported");
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
			IMPORT_FAIL("the data pointer is NULL");
		}
	}
	host = dtype_dev == kDLCPU || dtype_dev == kDLCUDAHost || dtype_dev == kDLROCMHost || dtype_dev == kDLCUDAManaged;
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
	view = buf_view_new(buf, dt, ndim, shape, sb, host ? base : base + d->byte_offset,
		buf_buffer_from(buf)->writable && !buf_is_broadcast(ndim, shape, st), buf_buffer_from(buf)->slow);
	OBJ_RELEASE(buf);
	return view;
}

zend_object *buffers_dlpack_import(DLManagedTensorVersioned *t)
{
	return buf_view_import(t);
}
