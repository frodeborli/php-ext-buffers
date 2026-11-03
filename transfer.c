/* BufferView::to(): moving a view's data to another device. The core knows host memory and files; other devices
 * are the business of the extensions that own them, which register a transfer (buffers_register_transfer()). */
#include "php_buffers.h"
#include "zend_exceptions.h"
#include "buffers_arginfo.h"

#define MAX_TRANSFERS 16
static struct { zend_class_entry *ce; buffers_transfer_fn fn; } transfers[MAX_TRANSFERS];
static int ntransfers;

zend_result buffers_register_transfer(zend_class_entry *device_ce, buffers_transfer_fn fn)
{
	if (!device_ce || !fn || ntransfers == MAX_TRANSFERS) {
		return FAILURE;
	}
	for (int i = 0; i < ntransfers; i++) {
		if (transfers[i].ce == device_ce) return FAILURE;
	}
	transfers[ntransfers].ce = device_ce;
	transfers[ntransfers++].fn = fn;
	return SUCCESS;
}

static zend_object *transfer_find(zend_object *device, zend_object *view, bool *found)
{
	for (int i = 0; i < ntransfers; i++) {
		if (instanceof_function(device->ce, transfers[i].ce)) {
			*found = true;
			return transfers[i].fn(view, device);
		}
	}
	*found = false;
	return NULL;
}

/* a C-contiguous host copy of a view whose bytes the core can read: host memory, or an unmapped file */
static zend_object *to_host(zend_object *vo)
{
	buf_view *v = buf_view_from(vo);
	buf_buffer *b = buf_buffer_from(v->buf);
	zend_object *res;

	if (b->host) {
		if (UNEXPECTED(b->pending) && buf_buffer_make_ready(b) == FAILURE) return NULL;
		return buf_view_copy_host(v, v->data);
	} else {   /* an unmapped file: read the bytes the view touches, copy from there */
		uint64_t first, len;
		uint8_t *tmp;

		buf_view_extent(v, &first, &len);
		if (len > (uint64_t)SIZE_MAX / 2) {
			zend_throw_error(NULL, "The view is too large to read into memory");
			return NULL;
		}
		tmp = emalloc((size_t)len + 1);
		if (len && buf_buffer_read_file(b, first, tmp, (size_t)len) == FAILURE) {
			efree(tmp);
			return NULL;
		}
		res = buf_view_copy_host(v, tmp);
		efree(tmp);
		return res;
	}
}

zend_object *buf_view_to(zend_object *vo, zend_object *device)
{
	buf_view *v = buf_view_from(vo);
	buf_buffer *b = buf_buffer_from(v->buf);
	int32_t ttype, tid;
	bool thost, found, target_cpu;
	zend_object *res, *src_dev = b->device ? b->device : buffers_cpu_device();
	zend_string *n;

	if (buf_buffer_on_device(b, device)) {
		GC_ADDREF(vo);
		return vo;
	}
	if (!buf_device_query(device, &ttype, &tid, &thost)) {
		return NULL;
	}
	target_cpu = device == buffers_cpu_device() || ttype == kDLCPU;
	if (target_cpu && (b->host || b->file != -1)) {
		return to_host(vo);
	}
	res = transfer_find(device, vo, &found);   /* the extension that owns the target ... */
	if (found) return res;
	res = transfer_find(src_dev, vo, &found);  /* ... or the one that owns the source (device to host) */
	if (found) return res;
	n = target_cpu ? buf_device_name(b->dev_type, b->dev_id) : buf_device_name(ttype, tid);
	zend_throw_error(NULL, "no backend for device '%s'", ZSTR_VAL(n));
	zend_string_release(n);
	return NULL;
}

zend_object *buffers_view_to(zend_object *view, zend_object *device)
{
	if (!buffers_is_view(view) || !buf_view_from(view)->buf) {
		zend_type_error("Expected a BufferView");
		return NULL;
	}
	return buf_view_to(view, device);
}

PHP_METHOD(Buffers_BufferView, to)
{
	zend_object *dev, *res;

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_OBJ_OF_CLASS(dev, buf_ce_device_if)
	ZEND_PARSE_PARAMETERS_END();
	res = buf_view_to(Z_OBJ_P(ZEND_THIS), dev);
	if (!res) {
		RETURN_THROWS();
	}
	RETURN_OBJ(res);
}
