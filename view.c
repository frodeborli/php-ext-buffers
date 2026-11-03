/* TypedView: an n-dimensional window onto a buffer.
 *
 * Element access is a chain of single integer offsets, $v[$x][$y][$z]. All but the last offset
 * hand out sub-views, which come from a small pool and cost no allocation in steady state.
 * Every dtype has its own read/write handlers (generated below), selected when the view is
 * created, so an access never switches on the dtype. A one-dimensional contiguous view gets
 * the shortest handlers of all: one bounds check, one load, no stride, no ndim test. */
#include "php_dlpack.h"
#include "codec.h"
#include "zend_exceptions.h"
#include "zend_interfaces.h"
#include "dlpack_arginfo.h"

zend_class_entry *dlp_ce_view;
extern double dlp_tab[][256];

typedef zval *(*read_fn)(zend_object *, zval *, int, zval *);
typedef void (*write_fn)(zend_object *, zval *, zval *);

/* [dtype][1-D contiguous][read-only] */
static zend_object_handlers view_handlers[DLP_NTYPES][2][2];

/* ---- element codecs ---------------------------------------------------------------- */

#define DEF_MEM(n, T) \
	static zend_always_inline T ld_##n(const uint8_t *p) { T v; memcpy(&v, p, sizeof v); return v; } \
	static zend_always_inline void st_##n(uint8_t *p, T v) { memcpy(p, &v, sizeof v); }
DEF_MEM(i8, int8_t) DEF_MEM(u8, uint8_t) DEF_MEM(i16, int16_t) DEF_MEM(u16, uint16_t)
DEF_MEM(i32, int32_t) DEF_MEM(u32, uint32_t) DEF_MEM(i64, int64_t) DEF_MEM(u64, uint64_t)
DEF_MEM(f32, float) DEF_MEM(f64, double)

static zend_always_inline bool dlp_dbl(zval *v, double *d)
{
	if (EXPECTED(Z_TYPE_P(v) == IS_DOUBLE)) { *d = Z_DVAL_P(v); return true; }
	if (Z_TYPE_P(v) == IS_LONG) { *d = (double)Z_LVAL_P(v); return true; }
	return false;
}

static zend_always_inline bool dlp_lval(zval *v, zend_long *l)
{
	if (EXPECTED(Z_TYPE_P(v) == IS_LONG)) { *l = Z_LVAL_P(v); return true; }
	if (Z_TYPE_P(v) == IS_DOUBLE) { *l = dlp_dval_to_lval(Z_DVAL_P(v)); return true; }
	return false;
}

/* a number, or [re, im] */
static bool dlp_cplx(zval *v, double *re, double *im)
{
	zval *a, *b;

	if (dlp_dbl(v, re)) { *im = 0; return true; }
	if (Z_TYPE_P(v) != IS_ARRAY || zend_hash_num_elements(Z_ARRVAL_P(v)) != 2) return false;
	a = zend_hash_index_find(Z_ARRVAL_P(v), 0);
	b = zend_hash_index_find(Z_ARRVAL_P(v), 1);
	if (!a || !b) return false;
	ZVAL_DEREF(a); ZVAL_DEREF(b);
	return dlp_dbl(a, re) && dlp_dbl(b, im);
}

/* rd_ID(p, zval *) reads, wr_ID(p, zval *) writes and returns false if the value has the wrong type */
#define CODEC_INT(ID, n, T) \
	static zend_always_inline void rd_##ID(const uint8_t *p, zval *z) { ZVAL_LONG(z, (zend_long)ld_##n(p)); } \
	static zend_always_inline bool wr_##ID(uint8_t *p, zval *v) { zend_long l; if (!dlp_lval(v, &l)) return false; st_##n(p, (T)l); return true; }
CODEC_INT(INT8, i8, int8_t) CODEC_INT(INT16, i16, int16_t) CODEC_INT(INT32, i32, int32_t) CODEC_INT(INT64, i64, int64_t)
CODEC_INT(UINT8, u8, uint8_t) CODEC_INT(UINT16, u16, uint16_t) CODEC_INT(UINT32, u32, uint32_t) CODEC_INT(UINT64, u64, uint64_t)

static zend_always_inline void rd_BOOL(const uint8_t *p, zval *z) { ZVAL_BOOL(z, *p != 0); }
static zend_always_inline bool wr_BOOL(uint8_t *p, zval *v)
{
	if (Z_TYPE_P(v) != IS_TRUE && Z_TYPE_P(v) != IS_FALSE) return false;
	*p = Z_TYPE_P(v) == IS_TRUE;
	return true;
}

#define CODEC_FLT(ID, RD, WR) \
	static zend_always_inline void rd_##ID(const uint8_t *p, zval *z) { ZVAL_DOUBLE(z, RD); } \
	static zend_always_inline bool wr_##ID(uint8_t *p, zval *v) { double d; if (!dlp_dbl(v, &d)) return false; WR; return true; }
CODEC_FLT(FLOAT16, dlp_dec_FLOAT16(ld_u16(p)), st_u16(p, dlp_enc_FLOAT16(d)))
CODEC_FLT(BFLOAT16, dlp_dec_BFLOAT16(ld_u16(p)), st_u16(p, dlp_enc_BFLOAT16(d)))
CODEC_FLT(FLOAT32, (double)ld_f32(p), st_f32(p, (float)d))
CODEC_FLT(FLOAT64, ld_f64(p), st_f64(p, d))
#define X(id, E, M, B, K) CODEC_FLT(id, dlp_tab[DLP_T_##id - DLP_T_F8E3M4][*p], *p = dlp_enc_##id(d))
DLP_SMALL(X)
#undef X
CODEC_FLT(F8E8M0FNU, dlp_tab[DLP_T_F8E8M0FNU - DLP_T_F8E3M4][*p], *p = dlp_enc_F8E8M0FNU(d))

#define CODEC_CPLX(ID, C, DEC, ENC) \
	static zend_always_inline double cd_##ID(const uint8_t *q) { return DEC; } \
	static zend_always_inline void ce_##ID(uint8_t *q, double x) { ENC; } \
	static zend_always_inline void rd_##ID(const uint8_t *p, zval *z) { \
		array_init_size(z, 2); add_next_index_double(z, cd_##ID(p)); add_next_index_double(z, cd_##ID(p + C)); } \
	static zend_always_inline bool wr_##ID(uint8_t *p, zval *v) { \
		double re, im; if (!dlp_cplx(v, &re, &im)) return false; ce_##ID(p, re); ce_##ID(p + C, im); return true; }
CODEC_CPLX(COMPLEX32, 2, dlp_dec_FLOAT16(ld_u16(q)), st_u16(q, dlp_enc_FLOAT16(x)))
CODEC_CPLX(BCOMPLEX32, 2, dlp_dec_BFLOAT16(ld_u16(q)), st_u16(q, dlp_enc_BFLOAT16(x)))
CODEC_CPLX(COMPLEX64, 4, (double)ld_f32(q), st_f32(q, (float)x))
CODEC_CPLX(COMPLEX128, 8, ld_f64(q), st_f64(q, x))

/* ---- view objects and the pool ----------------------------------------------------- */

static zend_always_inline zend_object *view_new(zend_object *buf, int dt, int ndim, const int64_t *shape, const int64_t *sbytes, uint8_t *data, bool writable)
{
	dlp_view *v;
	zend_object *o;

	if (DLPG(npool) > 0) {
		o = DLPG(pool)[--DLPG(npool)];
		v = dlp_view_from(o);
		GC_DEL_FLAGS(o, IS_OBJ_DESTRUCTOR_CALLED); /* refcount is 1, handed to the caller */
	} else {
		v = zend_object_alloc(sizeof(dlp_view), dlp_ce_view);
		zend_object_std_init(&v->std, dlp_ce_view);
		o = &v->std;
	}
	GC_ADDREF(buf);
	v->buf = buf;
	v->data = data;
	v->dt = dt;
	v->ndim = ndim;
	v->writable = writable;
	for (int i = 0; i < ndim; i++) { /* ndim is 1-3 in practice: a short loop beats a call or a 128-byte copy */
		v->shape[i] = shape[i];
		v->sbytes[i] = sbytes[i];
	}
	o->handlers = &view_handlers[dt][ndim == 1 && sbytes[0] == dlp_dtypes[dt].size][!writable];
	return o;
}

zend_object *dlp_view_new(zend_object *buf, int dt, int ndim, const int64_t *shape, const int64_t *sbytes, uint8_t *data, bool writable)
{
	return view_new(buf, dt, ndim, shape, sbytes, data, writable);
}

/* Last reference gone: park the object in the pool instead of freeing it (the engine skips
 * free_obj because the refcount is raised again). Only when this really is the last reference;
 * the shutdown and GC paths call dtor_obj with extra references held. */
static void view_dtor(zend_object *o)
{
	dlp_view *v = dlp_view_from(o);
	zend_object *buf = v->buf;

	if (GC_REFCOUNT(o) == 1 && buf && DLPG(request_active) && DLPG(npool) < DLP_POOL_MAX
			&& !(EG(flags) & EG_FLAGS_IN_SHUTDOWN)) {
		v->buf = NULL;
		DLPG(pool)[DLPG(npool)++] = o;
		GC_ADDREF(o);
		OBJ_RELEASE(buf);
	}
}

static void view_free(zend_object *o)
{
	dlp_view *v = dlp_view_from(o);

	if (v->buf) {
		OBJ_RELEASE(v->buf);
	}
	zend_object_std_dtor(o);
}

void dlp_view_rshutdown(void)
{
	while (DLPG(npool) > 0) {
		OBJ_RELEASE(DLPG(pool)[--DLPG(npool)]);
	}
}

/* ---- errors (cold) ------------------------------------------------------------------- */

static zend_never_inline zval *dlp_bad_offset(zval *off, int type, zval *rv)
{
	if (type == BP_VAR_IS) {
		ZVAL_NULL(rv);
		return rv;
	}
	if (!off) {
		zend_throw_error(NULL, "Cannot append to a TypedView");
	} else {
		zend_type_error("TypedView offset must be of type int, %s given", zend_zval_type_name(off));
	}
	return NULL;
}

static zend_never_inline zval *dlp_out_of_range(dlp_view *v, zend_long i, int type, zval *rv)
{
	if (type == BP_VAR_IS) {
		ZVAL_NULL(rv);
		return rv;
	}
	zend_value_error("Index " ZEND_LONG_FMT " is out of range for an axis of length " ZEND_LONG_FMT, i, (zend_long)v->shape[0]);
	return NULL;
}

static zend_never_inline void dlp_bad_write(zval *off, zval *val, dlp_view *v)
{
	if (!off) {
		zend_throw_error(NULL, "Cannot append to a TypedView");
	} else if (Z_TYPE_P(off) != IS_LONG) {
		zend_type_error("TypedView offset must be of type int, %s given", zend_zval_type_name(off));
	} else if (v->ndim > 1) {
		zend_throw_error(NULL, "Cannot assign to a sub-view; index every axis down to one element");
	} else if (Z_TYPE_P(off) == IS_LONG && (zend_ulong)Z_LVAL_P(off) >= (zend_ulong)v->shape[0]) {
		zend_value_error("Index " ZEND_LONG_FMT " is out of range for an axis of length " ZEND_LONG_FMT, Z_LVAL_P(off), (zend_long)v->shape[0]);
	} else {
		zend_type_error("Cannot write %s to a TypedView of dtype %s", zend_zval_type_name(val), dlp_dtypes[v->dt].name);
	}
}

static void dlp_write_ro(zend_object *o, zval *off, zval *val)
{
	zend_throw_error(NULL, "Cannot write to a read-only TypedView");
}

/* ---- per-dtype handlers ----------------------------------------------------------- */

#define GEN(ID, NAME, VALUE, KIND, BITS, SIZE, CODE) \
static zval *rd1_##ID(zend_object *o, zval *off, int type, zval *rv) \
{ \
	dlp_view *v = dlp_view_from(o); \
	zend_ulong i; \
	if (UNEXPECTED(!off || Z_TYPE_P(off) != IS_LONG)) return dlp_bad_offset(off, type, rv); \
	i = (zend_ulong)Z_LVAL_P(off); \
	if (UNEXPECTED(i >= (zend_ulong)v->shape[0])) return dlp_out_of_range(v, Z_LVAL_P(off), type, rv); \
	rd_##ID(v->data + i * SIZE, rv); \
	return rv; \
} \
static zval *rdn_##ID(zend_object *o, zval *off, int type, zval *rv) \
{ \
	dlp_view *v = dlp_view_from(o); \
	zend_ulong i; \
	uint8_t *p; \
	if (UNEXPECTED(!off || Z_TYPE_P(off) != IS_LONG)) return dlp_bad_offset(off, type, rv); \
	i = (zend_ulong)Z_LVAL_P(off); \
	if (UNEXPECTED(i >= (zend_ulong)v->shape[0])) return dlp_out_of_range(v, Z_LVAL_P(off), type, rv); \
	p = v->data + (int64_t)i * v->sbytes[0]; \
	if (v->ndim == 1) { rd_##ID(p, rv); return rv; } \
	ZVAL_OBJ(rv, view_new(v->buf, v->dt, v->ndim - 1, v->shape + 1, v->sbytes + 1, p, v->writable)); \
	return rv; \
} \
static void wr1_##ID(zend_object *o, zval *off, zval *val) \
{ \
	dlp_view *v = dlp_view_from(o); \
	zend_ulong i; \
	if (UNEXPECTED(!off || Z_TYPE_P(off) != IS_LONG)) return dlp_bad_write(off, val, v); \
	i = (zend_ulong)Z_LVAL_P(off); \
	ZVAL_DEREF(val); \
	if (UNEXPECTED(i >= (zend_ulong)v->shape[0] || !wr_##ID(v->data + i * SIZE, val))) dlp_bad_write(off, val, v); \
} \
static void wrn_##ID(zend_object *o, zval *off, zval *val) \
{ \
	dlp_view *v = dlp_view_from(o); \
	zend_ulong i; \
	if (UNEXPECTED(!off || Z_TYPE_P(off) != IS_LONG || v->ndim > 1)) return dlp_bad_write(off, val, v); \
	i = (zend_ulong)Z_LVAL_P(off); \
	ZVAL_DEREF(val); \
	if (UNEXPECTED(i >= (zend_ulong)v->shape[0] || !wr_##ID(v->data + (int64_t)i * v->sbytes[0], val))) dlp_bad_write(off, val, v); \
} \
static void rdf_##ID(const uint8_t *p, zval *z) { rd_##ID(p, z); }
#define X(id, name, value, kind, bits, size, code) GEN(id, name, value, kind, bits, size, code)
DLP_DTYPES(X)
#undef X

static const struct { read_fn rd[2]; write_fn wr[2]; void (*rdf)(const uint8_t *, zval *); } fns[DLP_NTYPES] = {
#define X(id, ...) { { rdn_##id, rd1_##id }, { wrn_##id, wr1_##id }, rdf_##id },
	DLP_DTYPES(X)
#undef X
};

static int view_has_dimension(zend_object *o, zval *off, int check_empty)
{
	dlp_view *v = dlp_view_from(o);
	zval tmp;
	int r;

	ZVAL_DEREF(off);
	if (Z_TYPE_P(off) != IS_LONG || (zend_ulong)Z_LVAL_P(off) >= (zend_ulong)v->shape[0]) return 0;
	if (!check_empty || v->ndim > 1) return 1;
	fns[v->dt].rdf(v->data + Z_LVAL_P(off) * v->sbytes[0], &tmp);
	r = zend_is_true(&tmp);
	zval_ptr_dtor(&tmp);
	return r;
}

static void view_unset_dimension(zend_object *o, zval *off)
{
	zend_throw_error(NULL, "Cannot unset an element of a TypedView");
}

static zend_result view_count_elements(zend_object *o, zend_long *count)
{
	*count = dlp_view_from(o)->shape[0];
	return SUCCESS;
}


static zend_function *view_no_constructor(zend_object *o)
{
	zend_throw_error(NULL, "TypedView cannot be instantiated; use BufferInterface::view()");
	return NULL;
}

/* ---- iteration over the first axis ---------------------------------------------------- */

typedef struct { zend_object_iterator it; zend_long idx; zval cur; } view_iter;

static void iter_dtor(zend_object_iterator *it)
{
	zval_ptr_dtor(&((view_iter *)it)->cur);
	zval_ptr_dtor(&it->data);
}

static int iter_valid(zend_object_iterator *it)
{
	return ((view_iter *)it)->idx < dlp_view_from(Z_OBJ(it->data))->shape[0] ? SUCCESS : FAILURE;
}

static zval *iter_current(zend_object_iterator *it)
{
	view_iter *vi = (view_iter *)it;
	zend_object *o = Z_OBJ(it->data);
	zval off;

	zval_ptr_dtor(&vi->cur);
	ZVAL_LONG(&off, vi->idx);
	if (!o->handlers->read_dimension(o, &off, BP_VAR_R, &vi->cur)) {
		ZVAL_UNDEF(&vi->cur);
		return &EG(uninitialized_zval);
	}
	return &vi->cur;
}

static void iter_key(zend_object_iterator *it, zval *key) { ZVAL_LONG(key, ((view_iter *)it)->idx); }
static void iter_next(zend_object_iterator *it) { ((view_iter *)it)->idx++; }
static void iter_rewind(zend_object_iterator *it) { ((view_iter *)it)->idx = 0; }

static const zend_object_iterator_funcs iter_funcs = {
	iter_dtor, iter_valid, iter_current, iter_key, iter_next, iter_rewind, NULL, NULL
};

static zend_object_iterator *view_get_iterator(zend_class_entry *ce, zval *object, int by_ref)
{
	view_iter *vi;

	if (by_ref) {
		zend_throw_error(NULL, "An iterator cannot be used with foreach by reference");
		return NULL;
	}
	vi = emalloc(sizeof(view_iter));
	zend_iterator_init(&vi->it);
	ZVAL_OBJ_COPY(&vi->it.data, Z_OBJ_P(object));
	vi->it.funcs = &iter_funcs;
	vi->idx = 0;
	ZVAL_UNDEF(&vi->cur);
	return &vi->it;
}

/* ---- methods ----------------------------------------------------------------------------- */

#define VIEW dlp_view_from(Z_OBJ_P(ZEND_THIS))

PHP_METHOD(TypedView, dtype) { ZEND_PARSE_PARAMETERS_NONE(); dlp_dtype_to_zval(VIEW->dt, return_value); }
PHP_METHOD(TypedView, ndim) { ZEND_PARSE_PARAMETERS_NONE(); RETURN_LONG(VIEW->ndim); }
PHP_METHOD(TypedView, count) { ZEND_PARSE_PARAMETERS_NONE(); RETURN_LONG(VIEW->shape[0]); }

PHP_METHOD(TypedView, shape)
{
	dlp_view *v = VIEW;

	ZEND_PARSE_PARAMETERS_NONE();
	array_init_size(return_value, v->ndim);
	for (int i = 0; i < v->ndim; i++) add_next_index_long(return_value, v->shape[i]);
}

PHP_METHOD(TypedView, strides)
{
	dlp_view *v = VIEW;

	ZEND_PARSE_PARAMETERS_NONE();
	array_init_size(return_value, v->ndim);
	for (int i = 0; i < v->ndim; i++) add_next_index_long(return_value, v->sbytes[i] / dlp_dtypes[v->dt].size);
}

PHP_METHOD(TypedView, byteOffset)
{
	dlp_view *v = VIEW;

	ZEND_PARSE_PARAMETERS_NONE();
	RETURN_LONG(v->data - dlp_buffer_from(v->buf)->base);
}

PHP_METHOD(TypedView, buffer) { ZEND_PARSE_PARAMETERS_NONE(); RETURN_OBJ_COPY(VIEW->buf); }

PHP_METHOD(TypedView, offsetExists)
{
	zval *off;

	ZEND_PARSE_PARAMETERS_START(1, 1) Z_PARAM_ZVAL(off) ZEND_PARSE_PARAMETERS_END();
	RETURN_BOOL(view_has_dimension(Z_OBJ_P(ZEND_THIS), off, 0));
}

PHP_METHOD(TypedView, offsetGet)
{
	zval *off;
	zend_object *o = Z_OBJ_P(ZEND_THIS);

	ZEND_PARSE_PARAMETERS_START(1, 1) Z_PARAM_ZVAL(off) ZEND_PARSE_PARAMETERS_END();
	if (!o->handlers->read_dimension(o, off, BP_VAR_R, return_value)) RETURN_THROWS();
}

PHP_METHOD(TypedView, offsetSet)
{
	zval *off, *val;
	zend_object *o = Z_OBJ_P(ZEND_THIS);

	ZEND_PARSE_PARAMETERS_START(2, 2) Z_PARAM_ZVAL(off) Z_PARAM_ZVAL(val) ZEND_PARSE_PARAMETERS_END();
	o->handlers->write_dimension(o, Z_TYPE_P(off) == IS_NULL ? NULL : off, val);
}

PHP_METHOD(TypedView, offsetUnset)
{
	zval *off;

	ZEND_PARSE_PARAMETERS_START(1, 1) Z_PARAM_ZVAL(off) ZEND_PARSE_PARAMETERS_END();
	view_unset_dimension(Z_OBJ_P(ZEND_THIS), off);
}

PHP_METHOD(TypedView, getIterator)
{
	ZEND_PARSE_PARAMETERS_NONE();
	zend_create_internal_iterator_zval(return_value, ZEND_THIS);
}

/* ---- BufferInterface::view() ------------------------------------------------------------- */

/* shape/strides arrays: 1..8 non-negative integers (strides: at least 1) */
static bool read_dims(HashTable *ht, int64_t *out, int *n, uint32_t argn, int64_t min)
{
	zval *z;
	int c = 0;

	ZEND_HASH_FOREACH_VAL(ht, z) {
		ZVAL_DEREF(z);
		if (Z_TYPE_P(z) != IS_LONG) {
			zend_argument_type_error(argn, "must contain only integers, %s given", zend_zval_type_name(z));
			return false;
		}
		if (c == DLP_MAX_NDIM) {
			zend_argument_value_error(argn, "must have at most %d elements", DLP_MAX_NDIM);
			return false;
		}
		if (Z_LVAL_P(z) < min) {
			zend_argument_value_error(argn, "must contain only %s", min ? "values of at least 1" : "non-negative values");
			return false;
		}
		out[c++] = Z_LVAL_P(z);
	} ZEND_HASH_FOREACH_END();
	*n = c;
	return true;
}

void dlp_buffer_view_impl(zend_object *bufobj, INTERNAL_FUNCTION_PARAMETERS)
{
	dlp_buffer *b = dlp_buffer_from(bufobj);
	zend_object *dtobj;
	zval *shape_zv = NULL;
	HashTable *strides_ht = NULL;
	zend_long offset = 0;
	int64_t shape[DLP_MAX_NDIM] = {0}, st[DLP_MAX_NDIM] = {0}, sb[DLP_MAX_NDIM] = {0}, t, last = 0;
	int dt, ndim, n;
	int64_t size;
	bool empty = false;

	ZEND_PARSE_PARAMETERS_START(1, 4)
		Z_PARAM_OBJ_OF_CLASS(dtobj, dlp_ce_dtype)
		Z_PARAM_OPTIONAL
		Z_PARAM_ZVAL(shape_zv)
		Z_PARAM_ARRAY_HT_OR_NULL(strides_ht)
		Z_PARAM_LONG(offset)
	ZEND_PARSE_PARAMETERS_END();

	dt = dlp_dtype_from_object(dtobj);
	size = dlp_dtypes[dt].size;

	if (offset < 0 || (zend_ulong)offset > b->len) {
		zend_argument_value_error(4, "must be between 0 and the byte length of the buffer");
		RETURN_THROWS();
	}
	if (offset % size) {
		zend_argument_value_error(4, "must be a multiple of the element size (%d bytes)", (int)size);
		RETURN_THROWS();
	}

	if (!shape_zv || Z_TYPE_P(shape_zv) == IS_NULL) {
		if ((b->len - offset) % size) {
			zend_argument_value_error(2, "is required: the buffer length minus the offset is not a multiple of the element size (%d bytes)", (int)size);
			RETURN_THROWS();
		}
		shape[0] = (b->len - offset) / size;
		ndim = 1;
	} else if (Z_TYPE_P(shape_zv) == IS_LONG) {
		if (Z_LVAL_P(shape_zv) < 0) {
			zend_argument_value_error(2, "must be non-negative");
			RETURN_THROWS();
		}
		shape[0] = Z_LVAL_P(shape_zv);
		ndim = 1;
	} else if (Z_TYPE_P(shape_zv) == IS_ARRAY) {
		if (!read_dims(Z_ARRVAL_P(shape_zv), shape, &ndim, 2, 0)) RETURN_THROWS();
		if (ndim == 0) {
			zend_argument_value_error(2, "must have between 1 and %d elements", DLP_MAX_NDIM);
			RETURN_THROWS();
		}
	} else {
		zend_argument_type_error(2, "must be of type array|int|null, %s given", zend_zval_type_name(shape_zv));
		RETURN_THROWS();
	}

	if (strides_ht) {
		if (!read_dims(strides_ht, st, &n, 3, 1)) RETURN_THROWS();
		if (n != ndim) {
			zend_argument_value_error(3, "must have one entry per dimension (%d)", ndim);
			RETURN_THROWS();
		}
	} else { /* C-contiguous; empty axes count as 1 so the strides stay valid */
		int64_t acc = 1;
		for (int i = ndim - 1; i >= 0; i--) {
			st[i] = acc;
			if (dlp_mul_ovf(acc, shape[i] ? shape[i] : 1, &acc)) {
				zend_argument_value_error(2, "describes more elements than can be addressed");
				RETURN_THROWS();
			}
		}
	}

	for (int i = 0; i < ndim; i++) {
		if (dlp_mul_ovf(st[i], size, &sb[i])) {
			zend_argument_value_error(3, "stride is too large");
			RETURN_THROWS();
		}
		if (shape[i] == 0) {
			empty = true;
		}
	}
	if (!empty) {
		bool fits = true;

		for (int i = 0; i < ndim && fits; i++) {
			if (dlp_mul_ovf(shape[i] - 1, sb[i], &t) || last > INT64_MAX - t) {
				fits = false;
			} else {
				last += t;
			}
		}
		fits = fits && last <= INT64_MAX - size && (zend_ulong)offset + (zend_ulong)last + (zend_ulong)size <= b->len;
		if (!fits) {
			zend_argument_value_error(2, "with the given strides and offset extends beyond the end of the buffer (%zu bytes)", b->len);
			RETURN_THROWS();
		}
	}

	ZVAL_OBJ(return_value, dlp_view_new(bufobj, dt, ndim, shape, sb, b->base + offset, b->writable));
}

/* `new TypedView` must produce a valid object before get_constructor refuses it */
static zend_object *view_create_blank(zend_class_entry *ce)
{
	dlp_view *v = zend_object_alloc(sizeof(dlp_view), ce);

	zend_object_std_init(&v->std, ce);
	v->std.handlers = &view_handlers[0][0][1];
	v->buf = NULL;
	v->data = NULL;
	v->dt = v->ndim = v->writable = 0;
	v->shape[0] = 0;
	return &v->std;
}

/* ---- registration --------------------------------------------------------------------------- */

void dlp_view_minit(void)
{
	zend_class_entry ce;

	INIT_CLASS_ENTRY(ce, "TypedView", class_TypedView_methods);
	dlp_ce_view = zend_register_internal_class_ex(&ce, NULL);
	dlp_ce_view->ce_flags |= ZEND_ACC_FINAL | ZEND_ACC_NO_DYNAMIC_PROPERTIES | ZEND_ACC_NOT_SERIALIZABLE;
	dlp_ce_view->create_object = view_create_blank; /* real instances come from dlp_view_new() */
	zend_class_implements(dlp_ce_view, 3, zend_ce_countable, zend_ce_arrayaccess, zend_ce_aggregate);
	dlp_ce_view->get_iterator = view_get_iterator;

	for (int dt = 0; dt < DLP_NTYPES; dt++) {
		for (int contig = 0; contig < 2; contig++) {
			for (int ro = 0; ro < 2; ro++) {
				zend_object_handlers *h = &view_handlers[dt][contig][ro];

				memcpy(h, &std_object_handlers, sizeof *h);
				h->offset = XtOffsetOf(dlp_view, std);
				h->dtor_obj = view_dtor;
				h->free_obj = view_free;
				h->clone_obj = NULL;
				h->compare = dlp_compare_identity;
				h->get_constructor = view_no_constructor;
				h->count_elements = view_count_elements;
				h->has_dimension = view_has_dimension;
				h->unset_dimension = view_unset_dimension;
				h->read_dimension = fns[dt].rd[contig];
				h->write_dimension = ro ? dlp_write_ro : fns[dt].wr[contig];
			}
		}
	}
}
