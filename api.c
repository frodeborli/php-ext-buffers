/* The C API of php_buffers_api.h: thin, validated wrappers over the internals. */
#include "php_buffers.h"
#include "zend_exceptions.h"

buffers_operator_fn buf_operator_handler;

int buffers_api_version(void) { return BUFFERS_API_VERSION; }

zend_result buffers_set_operator_handler(buffers_operator_fn fn)
{
	if (buf_operator_handler && buf_operator_handler != fn) {
		return FAILURE;
	}
	buf_operator_handler = fn;
	return SUCCESS;
}

zend_class_entry *buffers_get_class(int which)
{
	switch (which) {
	case BUFFERS_CE_DTYPE: return buf_ce_dtype;
	case BUFFERS_CE_BUFFER: return buf_ce_buffer;
	case BUFFERS_CE_VIEW: return buf_ce_view;
	case BUFFERS_CE_TENSOR: return buf_ce_tensor;
	case BUFFERS_CE_SHAPE: return buf_ce_shape;
	case BUFFERS_CE_DEVICE: return buf_ce_device_if;
	case BUFFERS_CE_CPU_DEVICE: return buf_ce_cpudevice;
	case BUFFERS_CE_FILE_DEVICE: return buf_ce_filedevice;
	}
	return NULL;
}

/* ---- dtypes ---- */

const buffers_dtype_desc *buffers_dtype_describe(int dtype)
{
	return dtype >= 0 && dtype < BUF_NTYPES ? &buf_dtypes[dtype] : NULL;
}

void buffers_dtype_to_zval(int dtype, zval *rv) { buf_dtype_to_zval(dtype, rv); }
int buffers_dtype_from_object(zend_object *dtype_case) { return buf_dtype_from_object(dtype_case); }

int buffers_dtype_from_dlpack(int code, int bits)
{
	for (int i = 0; i < BUF_NTYPES; i++) {
		if (buf_dtypes[i].dlpack_code == code && buf_dtypes[i].bits == bits) return i;
	}
	return -1;
}

/* ---- buffers ---- */

bool buffers_is_buffer(const zend_object *o)
{
	return o && (o->ce == buf_ce_buffer);
}

zend_result buffers_buffer_get_info(zend_object *buffer, buffers_buffer_info *out)
{
	buf_buffer *b;

	if (!buffers_is_buffer(buffer)) {
		zend_type_error("Expected a Buffer");
		return FAILURE;
	}
	b = buf_buffer_from(buffer);
	out->base = b->base;
	out->length = b->len > SIZE_MAX ? SIZE_MAX : (size_t)b->len;   /* an unmapped file can be larger than the address space */
	out->writable = b->writable;
	out->host_accessible = b->host;
	out->pending = b->pending;
	out->device = b->device;
	out->device_type = b->dev_type;
	out->device_id = b->dev_id;
	return SUCCESS;
}

zend_object *buffers_file_device(void) { return buf_file_device(); }

zend_object *buffers_buffer_create_foreign(void *data, size_t length, bool writable, zend_object *device,
	void (*deleter)(void *ctx), void *ctx)
{
	if (length && !data) {
		zend_value_error("A buffer of non-zero length needs a data pointer");
		return NULL;
	}
	return buf_foreign_buffer_new(data, length, writable, device, deleter, ctx, NULL);
}

zend_object *buffers_buffer_create_owned(void *data, size_t length, bool writable, zend_object *device, zend_object *owner)
{
	if (length && !data) {
		zend_value_error("A buffer of non-zero length needs a data pointer");
		return NULL;
	}
	return buf_foreign_buffer_new(data, length, writable, device, NULL, NULL, owner);
}

zend_result buffers_buffer_set_ready_callback(zend_object *buffer, buffers_ready_fn fn, void *ctx, void (*ctx_dtor)(void *ctx))
{
	buf_buffer *b;

	if (!buffers_is_buffer(buffer) || GC_REFCOUNT(buffer) != 1) {
		return FAILURE; /* a view may already exist: it chose its handlers without the readiness check */
	}
	b = buf_buffer_from(buffer);
	if (b->ready_dtor) {
		b->ready_dtor(b->ready_ctx);
	}
	b->ready = fn;
	b->ready_ctx = ctx;
	b->ready_dtor = ctx_dtor;
	b->slow = !b->host || fn != NULL;
	if (!fn) {
		b->pending = false;
	}
	return SUCCESS;
}

void buffers_buffer_set_pending(zend_object *buffer, bool pending)
{
	buf_buffer *b = buf_buffer_from(buffer);

	b->pending = pending && b->ready != NULL;
}

zend_result buffers_buffer_ensure_ready(zend_object *buffer)
{
	return buf_buffer_make_ready(buf_buffer_from(buffer));
}

/* ---- views ---- */

bool buffers_is_view(const zend_object *o)
{
	return o && o->ce == buf_ce_view;
}

zend_result buffers_view_get_info(zend_object *view, buffers_view_info *out)
{
	buf_view *v;

	if (!buffers_is_view(view) || !buf_view_from(view)->buf) {
		zend_type_error("Expected a BufferView");
		return FAILURE;
	}
	v = buf_view_from(view);
	out->data = v->data;
	out->dtype = v->dt;
	out->ndim = v->ndim;
	memcpy(out->shape, v->shape, sizeof(int64_t) * v->ndim);
	buf_view_strides_elements(v, out->strides);
	out->byte_offset = v->data - buf_buffer_from(v->buf)->base;
	out->buffer = v->buf;
	out->writable = v->writable;
	return SUCCESS;
}

zend_object *buffers_view_create(zend_object *buffer, int dtype, int ndim, const int64_t *shape,
	const int64_t *strides, int64_t byte_offset)
{
	char err[256];
	int arg;
	zend_object *v;

	if (!buffers_is_buffer(buffer)) {
		zend_type_error("Expected a Buffer");
		return NULL;
	}
	if (dtype < 0 || dtype >= BUF_NTYPES) {
		zend_value_error("Invalid dtype %d", dtype);
		return NULL;
	}
	v = buf_view_make(buffer, dtype, ndim, shape, strides, byte_offset, err, sizeof err, &arg);
	if (!v) {
		zend_value_error("%s", err);
	}
	return v;
}
