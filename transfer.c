/* Tensor::to(): moving a view's data to another device. The core knows host memory and files; other devices
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

static zend_object *transfer_find(zend_object *match, zend_object *target, zend_object *view, bool *found)
{
	for (int i = 0; i < ntransfers; i++) {
		if (instanceof_function(match->ce, transfers[i].ce)) {
			*found = true;
			return transfers[i].fn(view, target);
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
	res = transfer_find(device, device, vo, &found);   /* the extension that owns the target ... */
	if (found) return res;
	res = transfer_find(src_dev, device, vo, &found);  /* ... or the one that owns the source (device to host) */
	if (found) return res;
	n = target_cpu ? buf_device_name(b->dev_type, b->dev_id) : buf_device_name(ttype, tid);
	zend_throw_error(NULL, "no backend for device '%s'", ZSTR_VAL(n));
	zend_string_release(n);
	return NULL;
}

zend_object *buffers_tensor_to(zend_object *view, zend_object *device)
{
	if (!buffers_is_tensor(view) || !buf_view_from(view)->buf) {
		zend_type_error("Expected a Tensor");
		return NULL;
	}
	return buf_view_to(view, device);
}

PHP_METHOD(Buffers_Tensor, to)
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

/* Tensor::convert(DType $dtype, ?array $axes = null): the elements in another dtype and, with $axes, in another axis order (a permutation: out axis i is axis axes[i] of this view), as a new
 * C-contiguous host tensor. Streamed: nothing but the result is allocated. For the weights of a model: one pass from the mapped file to the layout the backend wants. */
PHP_METHOD(Buffers_Tensor, convert)
{
	zend_object *dto;
	HashTable *axes_ht = NULL;
	buf_view *v = buf_view_from(Z_OBJ_P(ZEND_THIS));
	buf_buffer *b = buf_buffer_from(v->buf);
	const buf_dtype_info *si = &buf_dtypes[v->dt];
	int ddt, nd = v->ndim, axes[BUF_MAX_NDIM];
	int64_t oshape[BUF_MAX_NDIM], ostr[BUF_MAX_NDIM], sstr[BUF_MAX_NDIM], total = 1, rows, L, acc = 1;
	zend_object *nbuf, *res;
	buf_buffer *nb;
	bool seen[BUF_MAX_NDIM] = {0};

	ZEND_PARSE_PARAMETERS_START(1, 2)
		Z_PARAM_OBJ(dto)
		Z_PARAM_OPTIONAL
		Z_PARAM_ARRAY_HT_OR_NULL(axes_ht)
	ZEND_PARSE_PARAMETERS_END();
	ddt = buffers_dtype_from_object(dto);
	if (ddt < 0) { zend_argument_type_error(1, "must be a DType"); RETURN_THROWS(); }
	if (!b->host) { zend_throw_error(NULL, "convert(): the memory is not host accessible; transfer to the host first with to()"); RETURN_THROWS(); }
	if (si->size == 0 || buf_dtypes[ddt].size == 0) { zend_throw_error(NULL, "convert(): packed dtypes are not supported (use a graph cast())"); RETURN_THROWS(); }
	if (UNEXPECTED(b->pending) && buf_buffer_make_ready(b) == FAILURE) RETURN_THROWS();
	for (int i = 0; i < nd; i++) axes[i] = i;
	if (axes_ht) {
		zval *z;
		int i = 0;

		if ((int)zend_hash_num_elements(axes_ht) != nd) { zend_argument_value_error(2, "must list every axis once (the tensor has %d)", nd); RETURN_THROWS(); }
		ZEND_HASH_FOREACH_VAL(axes_ht, z) {
			zend_long a = zval_get_long(z);

			if (a < 0) a += nd;
			if (a < 0 || a >= nd || seen[a]) { zend_argument_value_error(2, "must be a permutation of the axes"); RETURN_THROWS(); }
			seen[a] = true;
			axes[i++] = (int)a;
		} ZEND_HASH_FOREACH_END();
	}
	for (int i = 0; i < nd; i++) {
		oshape[i] = v->shape[axes[i]];
		sstr[i] = v->sbytes[axes[i]];
	}
	for (int i = nd - 1; i >= 0; i--) {
		ostr[i] = acc;
		if (buf_mul_ovf(acc, oshape[i] ? oshape[i] : 1, &acc)) { zend_throw_error(NULL, "convert(): the result is too large"); RETURN_THROWS(); }
	}
	for (int i = 0; i < nd; i++) if (buf_mul_ovf(total, oshape[i], &total)) { zend_throw_error(NULL, "convert(): the result is too large"); RETURN_THROWS(); }
	nbuf = buf_buffer_new_host((uint64_t)total * buf_dtypes[ddt].size);
	if (!nbuf) RETURN_THROWS();
	nb = buf_buffer_from(nbuf);
	if (total) {
		int64_t ssz = si->size, dsz = buf_dtypes[ddt].size;

		if (nd == 0) {
			buffers_convert(v->dt, v->data, 1, ddt, nb->base, 1, 1);
		} else {
			L = oshape[nd - 1];
			rows = total / L;
			for (int64_t r = 0; r < rows; r++) {
				int64_t soff = 0, rem = r;
				const uint8_t *sp;

				for (int i = nd - 2; i >= 0; i--) { int64_t q = rem % oshape[i]; rem /= oshape[i]; soff += q * sstr[i]; }
				sp = v->data + soff;
				if (sstr[nd - 1] % ssz == 0) {
					buffers_convert(v->dt, sp, sstr[nd - 1] / ssz, ddt, nb->base + r * L * dsz, 1, L);
				} else {
					for (int64_t j = 0; j < L; j++) buffers_convert(v->dt, sp + j * sstr[nd - 1], 1, ddt, nb->base + (r * L + j) * dsz, 1, 1);
				}
			}
		}
	}
	{
		int64_t bs[BUF_MAX_NDIM];

		for (int i = 0; i < nd; i++) bs[i] = ostr[i] * (int64_t)buf_dtypes[ddt].size;
		res = buf_view_new(nbuf, ddt, nd, oshape, bs, nb->base, true, false);
	}
	OBJ_RELEASE(nbuf);
	if (!res) RETURN_THROWS();
	RETURN_OBJ(res);
}
