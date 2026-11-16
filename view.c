/* Tensor: an n-dimensional window onto a buffer.
 *
 * Element access is a chain of single integer offsets, $v[$x][$y][$z]. All but the last offset
 * hand out sub-views, which come from a small pool and cost no allocation in steady state.
 * Every dtype has its own read/write handlers (generated below), selected when the view is
 * created, so an access never switches on the dtype. A one-dimensional contiguous view gets
 * the shortest handlers of all: one bounds check, one load, no stride, no ndim test. */
#include "php_buffers.h"
#include "codec.h"
#include "zend_exceptions.h"
#include "zend_interfaces.h"
#include "buffers_arginfo.h"

zend_class_entry *buf_ce_tensor, *buf_ce_tensor_agg;
extern double buf_tab[][256];

typedef zval *(*read_fn)(zend_object *, zval *, int, zval *);
typedef void (*write_fn)(zend_object *, zval *, zval *);
typedef void (*peek_fn)(const buf_view *, zend_long, zval *);
typedef void (*leaf_fn)(const uint8_t *, zend_ulong, zval *);   /* one element: a row (packed) or the element's own address */
typedef bool (*store_fn)(uint8_t *, zend_ulong, zval *);          /* writes one element, addressed like leaf_fn reads it */
typedef bool (*poke_fn)(const buf_view *, zval *);                /* writes the first element of the view */

/* [dtype][1-D contiguous][read-only][slow]; slow = the memory is not host accessible or has a readiness hook,
 * which the generic wrappers handle before delegating to the fast handlers. */
static zend_object_handlers view_handlers[BUF_NTYPES][2][2][2];
static zend_object_handlers view0_handlers; /* 0-d views: no axes, every access throws */

/* ---- element codecs ---------------------------------------------------------------- */

#define DEF_MEM(n, T) \
	static zend_always_inline T ld_##n(const uint8_t *p) { T v; memcpy(&v, p, sizeof v); return v; } \
	static zend_always_inline void st_##n(uint8_t *p, T v) { memcpy(p, &v, sizeof v); }
DEF_MEM(i8, int8_t) DEF_MEM(u8, uint8_t) DEF_MEM(i16, int16_t) DEF_MEM(u16, uint16_t)
DEF_MEM(i32, int32_t) DEF_MEM(u32, uint32_t) DEF_MEM(i64, int64_t) DEF_MEM(u64, uint64_t)
DEF_MEM(f32, float) DEF_MEM(f64, double)

static zend_always_inline bool buf_dbl(zval *v, double *d)
{
	if (EXPECTED(Z_TYPE_P(v) == IS_DOUBLE)) { *d = Z_DVAL_P(v); return true; }
	if (Z_TYPE_P(v) == IS_LONG) { *d = (double)Z_LVAL_P(v); return true; }
	return false;
}

static zend_always_inline bool buf_lval(zval *v, zend_long *l)
{
	if (EXPECTED(Z_TYPE_P(v) == IS_LONG)) { *l = Z_LVAL_P(v); return true; }
	if (Z_TYPE_P(v) == IS_DOUBLE) { *l = buf_dval_to_lval(Z_DVAL_P(v)); return true; }
	return false;
}

/* a number, or [re, im] */
static bool buf_cplx(zval *v, double *re, double *im)
{
	zval *a, *b;

	if (buf_dbl(v, re)) { *im = 0; return true; }
	if (Z_TYPE_P(v) != IS_ARRAY || zend_hash_num_elements(Z_ARRVAL_P(v)) != 2) return false;
	a = zend_hash_index_find(Z_ARRVAL_P(v), 0);
	b = zend_hash_index_find(Z_ARRVAL_P(v), 1);
	if (!a || !b) return false;
	ZVAL_DEREF(a); ZVAL_DEREF(b);
	return buf_dbl(a, re) && buf_dbl(b, im);
}

/* rd_ID(p, zval *) reads, wr_ID(p, zval *) writes and returns false if the value has the wrong type */
#define CODEC_INT(ID, n, T) \
	static zend_always_inline void rd_##ID(const uint8_t *p, zval *z) { ZVAL_LONG(z, (zend_long)ld_##n(p)); } \
	static zend_always_inline bool wr_##ID(uint8_t *p, zval *v) { zend_long l; if (!buf_lval(v, &l)) return false; st_##n(p, (T)l); return true; }
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
	static zend_always_inline bool wr_##ID(uint8_t *p, zval *v) { double d; if (!buf_dbl(v, &d)) return false; WR; return true; }
CODEC_FLT(FLOAT16, buf_dec_FLOAT16(ld_u16(p)), st_u16(p, buf_enc_FLOAT16(d)))
CODEC_FLT(BFLOAT16, buf_dec_BFLOAT16(ld_u16(p)), st_u16(p, buf_enc_BFLOAT16(d)))
CODEC_FLT(FLOAT32, (double)ld_f32(p), st_f32(p, (float)d))
CODEC_FLT(FLOAT64, ld_f64(p), st_f64(p, d))
#define X(id, E, M, B, K) CODEC_FLT(id, buf_tab[BUF_T_##id - BUF_T_F8E3M4][*p], *p = buf_enc_##id(d))
BUF_SMALL_BYTE(X)
#undef X
CODEC_FLT(F8E8M0FNU, buf_tab[BUF_T_F8E8M0FNU - BUF_T_F8E3M4][*p], *p = buf_enc_F8E8M0FNU(d))

#define CODEC_CPLX(ID, C, DEC, ENC) \
	static zend_always_inline double cd_##ID(const uint8_t *q) { return DEC; } \
	static zend_always_inline void ce_##ID(uint8_t *q, double x) { ENC; } \
	static zend_always_inline void rd_##ID(const uint8_t *p, zval *z) { \
		array_init_size(z, 2); add_next_index_double(z, cd_##ID(p)); add_next_index_double(z, cd_##ID(p + C)); } \
	static zend_always_inline bool wr_##ID(uint8_t *p, zval *v) { \
		double re, im; if (!buf_cplx(v, &re, &im)) return false; ce_##ID(p, re); ce_##ID(p + C, im); return true; }
CODEC_CPLX(COMPLEX32, 2, buf_dec_FLOAT16(ld_u16(q)), st_u16(q, buf_enc_FLOAT16(x)))
CODEC_CPLX(BCOMPLEX32, 2, buf_dec_BFLOAT16(ld_u16(q)), st_u16(q, buf_enc_BFLOAT16(x)))
CODEC_CPLX(COMPLEX64, 4, (double)ld_f32(q), st_f32(q, (float)x))
CODEC_CPLX(COMPLEX128, 8, ld_f64(q), st_f64(q, x))

/* Packed sub-byte formats: element i of a row is the bits [i*BITS, (i+1)*BITS) of the row's bytes, little-bit-endian
 * (DLPack's default). A row starts on a byte boundary. fp6 elements can straddle a byte boundary: they read
 * a 16-bit window only when they do (the second byte may lie past the end of the buffer otherwise). The window
 * is assembled byte by byte, (b0 | b1 << 8) >> shift, on every host: the bit order inside packed data is defined
 * by the format, not by the byte order of the machine. Whole elements (int16, float32, ...) use the host's
 * native byte order, like NumPy, PyTorch, DLPack and JS typed arrays; they are loaded with memcpy. */
#define CODEC_PK(ID, E, M, B, K) \
	static zend_always_inline void rdp_##ID(const uint8_t *row, zend_ulong i, zval *z) { \
		ZVAL_DOUBLE(z, buf_tab[BUF_T_##ID - BUF_T_F8E3M4][pk_get(row, i, E + M + 1)]); } \
	static zend_always_inline bool wrp_##ID(uint8_t *row, zend_ulong i, zval *v) { \
		double d; if (!buf_dbl(v, &d)) return false; pk_put(row, i, E + M + 1, buf_enc_##ID(d)); return true; }
BUF_SMALL_PACKED(CODEC_PK)

/* packed integers: a code of BITS bits; signed ones are sign-extended on read, writes wrap by truncation like every integer dtype */
#define CODEC_PKI(ID, BITS, SIGNED) \
	static zend_always_inline void rdp_##ID(const uint8_t *row, zend_ulong i, zval *z) { \
		zend_long c = (zend_long)pk_get(row, i, BITS); \
		if (SIGNED && c >= (1L << (BITS - 1))) c -= 1L << BITS; \
		ZVAL_LONG(z, c); } \
	static zend_always_inline bool wrp_##ID(uint8_t *row, zend_ulong i, zval *v) { \
		zend_long l; if (!buf_lval(v, &l)) return false; pk_put(row, i, BITS, (unsigned)l & ((1u << BITS) - 1)); return true; }
CODEC_PKI(INT2, 2, 1) CODEC_PKI(INT4, 4, 1) CODEC_PKI(UINT2, 2, 0) CODEC_PKI(UINT4, 4, 0)

/* ---- operators: eager element-wise arithmetic (arith.c) ------------------------------------------------- */

static zend_result buf_view_do_operation(uint8_t opcode, zval *result, zval *op1, zval *op2)
{
	zval op1_copy;
	zend_result r;
	bool compound = result == op1;

	if (compound) { /* `$a += 1`: the result is written while the operands are still read */
		ZVAL_COPY_VALUE(&op1_copy, op1);
		op1 = &op1_copy;
	}
	r = buf_arith_do_operation(opcode, result, op1, op2, compound);
	if (r == SUCCESS && op1 == &op1_copy) {
		zval_ptr_dtor(op1);
	}
	return r;
}


static int64_t view_elements(const buf_view *v);

/* ---- extent and copies (Tensor::to()) ---------------------------------------------------------- */

void buf_view_extent(const buf_view *v, uint64_t *first, uint64_t *len)
{
	const buf_dtype_info *info = &buf_dtypes[v->dt];
	uint64_t n = 0;

	*first = (uint64_t)(v->data - buf_buffer_from(v->buf)->base);
	if (view_elements(v) == 0) {
		*len = 0;
		return;
	}
	for (int i = 0; i < v->ndim - (info->size ? 0 : 1); i++) {
		n += (uint64_t)(v->shape[i] - 1) * (uint64_t)v->sbytes[i];
	}
	if (info->size) {
		*len = n + info->size;
	} else if (v->ndim == 0) {
		*len = 1;
	} else {
		*len = n + ((uint64_t)v->shape[v->ndim - 1] * info->bits + 7) / 8;
	}
}

static void copy_axis(const buf_view *v, int axis, const uint8_t *src, uint8_t *dst, const int64_t *dsb, bool packed)
{
	int64_t n = v->shape[axis];
	unsigned bits = buf_dtypes[v->dt].bits;
	size_t size = buf_dtypes[v->dt].size;

	if (axis < v->ndim - 1) {
		for (int64_t i = 0; i < n; i++) copy_axis(v, axis + 1, src + i * v->sbytes[axis], dst + i * dsb[axis], dsb, packed);
	} else if (packed) {   /* codes copied bit by bit, no conversion */
		for (int64_t i = 0; i < n; i++) pk_put(dst, (zend_ulong)i, bits, pk_get(src, (zend_ulong)i, bits));
	} else if (v->sbytes[axis] == (int64_t)size && dsb[axis] == (int64_t)size) {
		memcpy(dst, src, (size_t)n * size);
	} else {
		for (int64_t i = 0; i < n; i++) memcpy(dst + i * dsb[axis], src + i * v->sbytes[axis], size);
	}
}

zend_object *buf_view_copy_host(const buf_view *v, const uint8_t *src)
{
	const buf_dtype_info *info = &buf_dtypes[v->dt];
	bool packed = info->size == 0;
	int64_t dsb[BUF_MAX_NDIM] = {0}, st[BUF_MAX_NDIM] = {0}, acc = 1, total = 1;
	int step = info->bits == 4 ? 2 : 4;
	zend_object *buf, *res;
	buf_buffer *nb;

	if (view_elements(v) == 0) {
		total = 0;
	}
	/* C-contiguous strides; the rows of a packed dtype start on byte boundaries */
	for (int i = v->ndim - 1; i >= 0; i--) {
		int64_t dim = v->shape[i] ? v->shape[i] : 1;

		if (packed && i < v->ndim - 1) {
			acc = (acc + step - 1) / step * step;
		}
		st[i] = acc;
		if (buf_mul_ovf(acc, dim, &acc)) goto too_large;
	}
	if (total) {
		if (packed) {
			if (v->ndim == 0) total = 1;
			else if (v->ndim == 1) total = (v->shape[0] * (int64_t)info->bits + 7) / 8;
			else if (st[0] > INT64_MAX / 8 || buf_mul_ovf(st[0] * (int64_t)info->bits / 8, v->shape[0], &total)) goto too_large;
		} else if (buf_mul_ovf(acc, (int64_t)info->size, &total)) {
			goto too_large;
		}
	}
	for (int i = 0; i < v->ndim; i++) {
		dsb[i] = packed ? (i == v->ndim - 1 ? 0 : st[i] * (int64_t)info->bits / 8) : st[i] * (int64_t)info->size;
	}
	buf = buf_buffer_new_host((uint64_t)total);
	if (!buf) return NULL;
	nb = buf_buffer_from(buf);
	if (total) {
		if (v->ndim == 0) {
			if (packed) pk_put(nb->base, 0, info->bits, pk_get(src, 0, info->bits));
			else memcpy(nb->base, src, info->size);
		} else {
			copy_axis(v, 0, src, nb->base, dsb, packed);
		}
	}
	res = buf_view_new(buf, v->dt, v->ndim, v->shape, dsb, nb->base, true, false);
	OBJ_RELEASE(buf);
	return res;
too_large:
	zend_throw_error(NULL, "The result is too large");
	return NULL;
}

/* ---- view objects and the pool ----------------------------------------------------- */

static zend_always_inline zend_object *view_alloc(zend_object *buf, int dt, int ndim, const int64_t *shape, const int64_t *sbytes, uint8_t *data, bool writable, bool slow)
{
	buf_view *v;
	zend_object *o;

	if (BUFG(npool) > 0) {
		o = BUFG(pool)[--BUFG(npool)];
		v = buf_view_from(o);
		GC_DEL_FLAGS(o, IS_OBJ_DESTRUCTOR_CALLED); /* refcount is 1, handed to the caller */
	} else {
		v = zend_object_alloc(sizeof(buf_view), buf_ce_tensor);
		zend_object_std_init(&v->std, buf_ce_tensor);
		o = &v->std;
	}
	GC_ADDREF(buf);
	v->buf = buf;
	v->data = data;
	v->dt = dt;
	v->ndim = ndim;
	v->writable = writable;
	v->slow = slow;
	for (int i = 0; i < ndim; i++) { /* ndim is 1-3 in practice: a short loop beats a call or a 128-byte copy */
		v->shape[i] = shape[i];
		v->sbytes[i] = sbytes[i];
	}
	return o;
}

/* A packed dtype has no byte stride on its last axis (it is stored as 0, like the element size of
 * the dtype), so "ndim == 1 && sbytes[0] == size" selects the contiguous handlers for it too. */
static zend_always_inline zend_object *view_new(zend_object *buf, int dt, int ndim, const int64_t *shape, const int64_t *sbytes, uint8_t *data, bool writable, bool slow)
{
	zend_object *o = view_alloc(buf, dt, ndim, shape, sbytes, data, writable, slow);

	o->handlers = &view_handlers[dt][ndim == 1 && sbytes[0] == buf_dtypes[dt].size][!writable][slow];
	return o;
}

zend_object *buf_view_new(zend_object *buf, int dt, int ndim, const int64_t *shape, const int64_t *sbytes, uint8_t *data, bool writable, bool slow)
{
	if (ndim == 0) {
		zend_object *o = view_alloc(buf, dt, 0, shape, sbytes, data, writable, slow);

		o->handlers = &view0_handlers;
		return o;
	}
	return view_new(buf, dt, ndim, shape, sbytes, data, writable, slow);
}

/* Last reference gone: park the object in the pool instead of freeing it (the engine skips
 * free_obj because the refcount is raised again). Only when this really is the last reference;
 * the shutdown and GC paths call dtor_obj with extra references held. */
static void view_dtor(zend_object *o)
{
	buf_view *v = buf_view_from(o);
	zend_object *buf = v->buf;

	if (GC_REFCOUNT(o) == 1 && buf && BUFG(request_active) && BUFG(npool) < BUF_POOL_MAX
			&& !(EG(flags) & EG_FLAGS_IN_SHUTDOWN)) {
		v->buf = NULL;
		BUFG(pool)[BUFG(npool)++] = o;
		GC_ADDREF(o);
		OBJ_RELEASE(buf);
	}
}

static void view_free(zend_object *o)
{
	buf_view *v = buf_view_from(o);

	if (v->buf) {
		OBJ_RELEASE(v->buf);
	}
	zend_object_std_dtor(o);
}

void buf_view_rshutdown(void)
{
	while (BUFG(npool) > 0) {
		OBJ_RELEASE(BUFG(pool)[--BUFG(npool)]);
	}
}

/* strides in elements; the last axis of a packed dtype has stride 1 */
void buf_view_strides_elements(const buf_view *v, int64_t *out)
{
	const buf_dtype_info *info = &buf_dtypes[v->dt];

	for (int i = 0; i < v->ndim; i++) {
		if (info->size) {
			out[i] = v->sbytes[i] / info->size;
		} else {
			out[i] = i == v->ndim - 1 ? 1 : v->sbytes[i] * 8 / info->bits;
		}
	}
}

/* ---- errors (cold) ------------------------------------------------------------------- */

static zend_never_inline zval *buf_bad_offset(zval *off, int type, zval *rv)
{
	if (type == BP_VAR_IS) {
		ZVAL_NULL(rv);
		return rv;
	}
	if (!off) {
		zend_throw_error(NULL, "Cannot append to a Tensor");
	} else {
		zend_type_error("Tensor offset must be of type int, %s given", zend_zval_type_name(off));
	}
	return NULL;
}

static zend_never_inline zval *buf_out_of_range(buf_view *v, zend_long i, int type, zval *rv)
{
	if (type == BP_VAR_IS) {
		ZVAL_NULL(rv);
		return rv;
	}
	zend_value_error("Index " ZEND_LONG_FMT " is out of range for an axis of length " ZEND_LONG_FMT, i, (zend_long)v->shape[0]);
	return NULL;
}

static zend_never_inline void buf_bad_write(zval *off, zval *val, buf_view *v)
{
	if (!off) {
		zend_throw_error(NULL, "Cannot append to a Tensor");
	} else if (Z_TYPE_P(off) != IS_LONG) {
		zend_type_error("Tensor offset must be of type int, %s given", zend_zval_type_name(off));
	} else if (v->ndim > 1) {
		zend_throw_error(NULL, "Cannot assign to a sub-view; index every axis down to one element");
	} else if (Z_TYPE_P(off) == IS_LONG && (zend_ulong)Z_LVAL_P(off) >= (zend_ulong)v->shape[0]) {
		zend_value_error("Index " ZEND_LONG_FMT " is out of range for an axis of length " ZEND_LONG_FMT, Z_LVAL_P(off), (zend_long)v->shape[0]);
	} else {
		zend_type_error("Cannot write %s to a Tensor of dtype %s", zend_zval_type_name(val), buf_dtypes[v->dt].name);
	}
}

static void buf_write_ro(zend_object *o, zval *off, zval *val)
{
	zend_throw_error(NULL, "Cannot write to a read-only Tensor");
}

/* ---- per-dtype handlers ----------------------------------------------------------- */

#define GEN(ID, NAME, VALUE, KIND, BITS, SIZE, CODE) \
static zval *rd1_##ID(zend_object *o, zval *off, int type, zval *rv) \
{ \
	buf_view *v = buf_view_from(o); \
	zend_ulong i; \
	if (UNEXPECTED(!off || Z_TYPE_P(off) != IS_LONG)) return buf_bad_offset(off, type, rv); \
	i = (zend_ulong)Z_LVAL_P(off); \
	if (UNEXPECTED(i >= (zend_ulong)v->shape[0])) return buf_out_of_range(v, Z_LVAL_P(off), type, rv); \
	rd_##ID(v->data + i * SIZE, rv); \
	return rv; \
} \
static zval *rdn_##ID(zend_object *o, zval *off, int type, zval *rv) \
{ \
	buf_view *v = buf_view_from(o); \
	zend_ulong i; \
	uint8_t *p; \
	if (UNEXPECTED(!off || Z_TYPE_P(off) != IS_LONG)) return buf_bad_offset(off, type, rv); \
	i = (zend_ulong)Z_LVAL_P(off); \
	if (UNEXPECTED(i >= (zend_ulong)v->shape[0])) return buf_out_of_range(v, Z_LVAL_P(off), type, rv); \
	p = v->data + (int64_t)i * v->sbytes[0]; \
	if (v->ndim == 1) { rd_##ID(p, rv); return rv; } \
	ZVAL_OBJ(rv, view_new(v->buf, v->dt, v->ndim - 1, v->shape + 1, v->sbytes + 1, p, v->writable, v->slow)); \
	return rv; \
} \
static void wr1_##ID(zend_object *o, zval *off, zval *val) \
{ \
	buf_view *v = buf_view_from(o); \
	zend_ulong i; \
	if (UNEXPECTED(!off || Z_TYPE_P(off) != IS_LONG)) return buf_bad_write(off, val, v); \
	i = (zend_ulong)Z_LVAL_P(off); \
	ZVAL_DEREF(val); \
	if (UNEXPECTED(i >= (zend_ulong)v->shape[0] || !wr_##ID(v->data + i * SIZE, val))) buf_bad_write(off, val, v); \
} \
static void wrn_##ID(zend_object *o, zval *off, zval *val) \
{ \
	buf_view *v = buf_view_from(o); \
	zend_ulong i; \
	if (UNEXPECTED(!off || Z_TYPE_P(off) != IS_LONG || v->ndim > 1)) return buf_bad_write(off, val, v); \
	i = (zend_ulong)Z_LVAL_P(off); \
	ZVAL_DEREF(val); \
	if (UNEXPECTED(i >= (zend_ulong)v->shape[0] || !wr_##ID(v->data + (int64_t)i * v->sbytes[0], val))) buf_bad_write(off, val, v); \
} \
static void peek_##ID(const buf_view *v, zend_long i, zval *z) { rd_##ID(v->data + i * v->sbytes[0], z); } \
static void leaf_##ID(const uint8_t *p, zend_ulong i, zval *z) { rd_##ID(p, z); } \
static bool store_##ID(uint8_t *p, zend_ulong i, zval *z) { return wr_##ID(p, z); } \
static bool poke_##ID(const buf_view *v, zval *val) { return wr_##ID(v->data, val); }

/* packed dtypes: a 1-D view is always contiguous, so the only n-D handler that can run is the one that makes sub-views */
#define GENP(ID, NAME, VALUE, KIND, BITS, SIZE, CODE) \
static zval *rd1_##ID(zend_object *o, zval *off, int type, zval *rv) \
{ \
	buf_view *v = buf_view_from(o); \
	zend_ulong i; \
	if (UNEXPECTED(!off || Z_TYPE_P(off) != IS_LONG)) return buf_bad_offset(off, type, rv); \
	i = (zend_ulong)Z_LVAL_P(off); \
	if (UNEXPECTED(i >= (zend_ulong)v->shape[0])) return buf_out_of_range(v, Z_LVAL_P(off), type, rv); \
	rdp_##ID(v->data, i, rv); \
	return rv; \
} \
static zval *rdn_##ID(zend_object *o, zval *off, int type, zval *rv) \
{ \
	buf_view *v = buf_view_from(o); \
	zend_ulong i; \
	if (UNEXPECTED(!off || Z_TYPE_P(off) != IS_LONG)) return buf_bad_offset(off, type, rv); \
	i = (zend_ulong)Z_LVAL_P(off); \
	if (UNEXPECTED(i >= (zend_ulong)v->shape[0])) return buf_out_of_range(v, Z_LVAL_P(off), type, rv); \
	ZVAL_OBJ(rv, view_new(v->buf, v->dt, v->ndim - 1, v->shape + 1, v->sbytes + 1, v->data + (int64_t)i * v->sbytes[0], v->writable, v->slow)); \
	return rv; \
} \
static void wr1_##ID(zend_object *o, zval *off, zval *val) \
{ \
	buf_view *v = buf_view_from(o); \
	zend_ulong i; \
	if (UNEXPECTED(!off || Z_TYPE_P(off) != IS_LONG)) return buf_bad_write(off, val, v); \
	i = (zend_ulong)Z_LVAL_P(off); \
	ZVAL_DEREF(val); \
	if (UNEXPECTED(i >= (zend_ulong)v->shape[0] || !wrp_##ID(v->data, i, val))) buf_bad_write(off, val, v); \
} \
static void wrn_##ID(zend_object *o, zval *off, zval *val) { buf_bad_write(off, val, buf_view_from(o)); } \
static void peek_##ID(const buf_view *v, zend_long i, zval *z) { rdp_##ID(v->data, i, z); } \
static void leaf_##ID(const uint8_t *row, zend_ulong i, zval *z) { rdp_##ID(row, i, z); } \
static bool store_##ID(uint8_t *row, zend_ulong i, zval *z) { return wrp_##ID(row, i, z); } \
static bool poke_##ID(const buf_view *v, zval *val) { return wrp_##ID(v->data, 0, val); }

#define GENSEL_0 GENP
#define GENSEL_1 GEN
#define GENSEL_2 GEN
#define GENSEL_4 GEN
#define GENSEL_8 GEN
#define GENSEL_16 GEN
#define GENSEL2(size) GENSEL_##size
#define GENSEL(size) GENSEL2(size)
#define X(id, name, value, kind, bits, size, code) GENSEL(size)(id, name, value, kind, bits, size, code)
BUF_DTYPES(X)
#undef X

static const struct { read_fn rd[2]; write_fn wr[2]; peek_fn peek; leaf_fn leaf; store_fn store; poke_fn poke; } fns[BUF_NTYPES] = {
#define X(id, ...) { { rdn_##id, rd1_##id }, { wrn_##id, wr1_##id }, peek_##id, leaf_##id, store_##id, poke_##id },
	BUF_DTYPES(X)
#undef X
};

/* ---- memory that needs a check before elements are touched ------------------------------- */

static zend_never_inline bool view_not_accessible(buf_buffer *b)
{
	zend_string *n = buf_device_name(b->dev_type, b->dev_id);

	zend_throw_error(NULL, "Cannot access the elements of a tensor on device %s: its memory is not host accessible; transfer to the host first with to()", ZSTR_VAL(n));
	zend_string_release(n);
	return false;
}

/* before reading or writing an element: host accessible memory, and ready */
bool buf_view_check_access(const buf_view *v)
{
	buf_buffer *b = buf_buffer_from(v->buf);

	if (UNEXPECTED(!b->host)) {
		return view_not_accessible(b);
	}
	return buf_buffer_make_ready(b) == SUCCESS;
}

static zval *view_read_slow(zend_object *o, zval *off, int type, zval *rv)
{
	buf_view *v = buf_view_from(o);

	if (v->ndim == 1 && !buf_view_check_access(v)) return NULL;
	return fns[v->dt].rd[v->ndim == 1 && v->sbytes[0] == (int64_t)buf_dtypes[v->dt].size](o, off, type, rv);
}

static void view_write_slow(zend_object *o, zval *off, zval *val)
{
	buf_view *v = buf_view_from(o);

	if (v->ndim == 1 && !buf_view_check_access(v)) return;
	fns[v->dt].wr[v->ndim == 1 && v->sbytes[0] == (int64_t)buf_dtypes[v->dt].size](o, off, val);
}

static int view_has_dimension(zend_object *o, zval *off, int check_empty)
{
	buf_view *v = buf_view_from(o);
	zval tmp;
	int r;

	ZVAL_DEREF(off);
	if (Z_TYPE_P(off) != IS_LONG || (zend_ulong)Z_LVAL_P(off) >= (zend_ulong)v->shape[0]) return 0;
	if (!check_empty || v->ndim > 1) return 1;
	if (v->slow && !buf_view_check_access(v)) return 0;
	fns[v->dt].peek(v, Z_LVAL_P(off), &tmp);
	r = zend_is_true(&tmp);
	zval_ptr_dtor(&tmp);
	return r;
}

static void view_unset_dimension(zend_object *o, zval *off)
{
	zend_throw_error(NULL, "Cannot unset an element of a Tensor");
}

static zend_result view_count_elements(zend_object *o, zend_long *count)
{
	*count = buf_view_from(o)->shape[0];
	return SUCCESS;
}

static zend_function *view_no_constructor(zend_object *o)
{
	zend_throw_error(NULL, "Tensor cannot be instantiated; use Buffer::view()");
	return NULL;
}

/* ---- 0-d views: one element, no axes ---------------------------------------------------------- */

static ZEND_COLD void view0_error(void)
{
	zend_throw_error(NULL, "A 0-d view has no axes");
}

static zval *view0_read(zend_object *o, zval *off, int type, zval *rv) { view0_error(); return NULL; }
static void view0_write(zend_object *o, zval *off, zval *val) { view0_error(); }
static int view0_has(zend_object *o, zval *off, int check_empty) { view0_error(); return 0; }
static void view0_unset(zend_object *o, zval *off) { view0_error(); }
static zend_result view0_count(zend_object *o, zend_long *count) { view0_error(); return FAILURE; }

/* ---- iteration over the first axis ---------------------------------------------------- */

typedef struct { zend_object_iterator it; zend_long idx; zval cur; } view_iter;

static void iter_dtor(zend_object_iterator *it)
{
	zval_ptr_dtor(&((view_iter *)it)->cur);
	zval_ptr_dtor(&it->data);
}

static int iter_valid(zend_object_iterator *it)
{
	return ((view_iter *)it)->idx < buf_view_from(Z_OBJ(it->data))->shape[0] ? SUCCESS : FAILURE;
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
	if (buf_view_from(Z_OBJ_P(object))->ndim == 0) {
		view0_error();
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

#define VIEW buf_view_from(Z_OBJ_P(ZEND_THIS))

PHP_METHOD(Buffers_Tensor, count)
{
	buf_view *v = VIEW;

	ZEND_PARSE_PARAMETERS_NONE();
	if (v->ndim == 0) {
		view0_error();
		RETURN_THROWS();
	}
	RETURN_LONG(v->shape[0]);
}

PHP_METHOD(Buffers_Tensor, offsetExists)
{
	zval *off;
	zend_object *o = Z_OBJ_P(ZEND_THIS);

	ZEND_PARSE_PARAMETERS_START(1, 1) Z_PARAM_ZVAL(off) ZEND_PARSE_PARAMETERS_END();
	RETURN_BOOL(o->handlers->has_dimension(o, off, 0));
}

PHP_METHOD(Buffers_Tensor, offsetGet)
{
	zval *off;
	zend_object *o = Z_OBJ_P(ZEND_THIS);

	ZEND_PARSE_PARAMETERS_START(1, 1) Z_PARAM_ZVAL(off) ZEND_PARSE_PARAMETERS_END();
	if (!o->handlers->read_dimension(o, off, BP_VAR_R, return_value)) RETURN_THROWS();
}

PHP_METHOD(Buffers_Tensor, offsetSet)
{
	zval *off, *val;
	zend_object *o = Z_OBJ_P(ZEND_THIS);

	ZEND_PARSE_PARAMETERS_START(2, 2) Z_PARAM_ZVAL(off) Z_PARAM_ZVAL(val) ZEND_PARSE_PARAMETERS_END();
	o->handlers->write_dimension(o, Z_TYPE_P(off) == IS_NULL ? NULL : off, val);
}

PHP_METHOD(Buffers_Tensor, offsetUnset)
{
	zval *off;
	zend_object *o = Z_OBJ_P(ZEND_THIS);

	ZEND_PARSE_PARAMETERS_START(1, 1) Z_PARAM_ZVAL(off) ZEND_PARSE_PARAMETERS_END();
	o->handlers->unset_dimension(o, off);
}

PHP_METHOD(Buffers_Tensor, getIterator)
{
	ZEND_PARSE_PARAMETERS_NONE();
	zend_create_internal_iterator_zval(return_value, ZEND_THIS);
}

/* ---- Buffer::view() ------------------------------------------------------------- */

/* shape/strides arrays: 0..8 integers, each at least `min` */
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
		if (c == BUF_MAX_NDIM) {
			zend_argument_value_error(argn, "must have at most %d elements", BUF_MAX_NDIM);
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

#define FAIL(a, ...) do { *arg = (a); snprintf(err, errlen, __VA_ARGS__); return NULL; } while (0)

/* The one place views are validated. Everything is checked once, with overflow-safe arithmetic, so that
 * element access needs only a bounds check. `strides` are in elements (NULL: C-contiguous). */
zend_object *buf_view_make(zend_object *bufobj, int dt, int ndim, const int64_t *shape_in, const int64_t *strides_in,
	int64_t offset, char *err, size_t errlen, int *arg)
{
	buf_buffer *b = buf_buffer_from(bufobj);
	const buf_dtype_info *info = &buf_dtypes[dt];
	bool packed = info->size == 0, empty = false;
	int64_t size = info->size, bits = info->bits;
	int64_t shape[BUF_MAX_NDIM] = {0}, st[BUF_MAX_NDIM] = {0}, sb[BUF_MAX_NDIM] = {0}, t, last = 0, extent;

#if SIZEOF_ZEND_LONG == 4
	if (!buf_dtype_fits_php_int(dt)) {
		FAIL(1, "the dtype %s is not supported on 32-bit PHP: its values do not fit a PHP int", info->name);
	}
#endif
	if (ndim < 0 || ndim > BUF_MAX_NDIM) {
		FAIL(2, "must have between 0 and %d elements", BUF_MAX_NDIM);
	}
	if (offset < 0 || (uint64_t)offset > b->len) {
		FAIL(4, "must be between 0 and the byte length of the buffer");
	}
	for (int i = 0; i < ndim; i++) {
		if (shape_in[i] < 0) {
			FAIL(2, "must contain only non-negative values");
		}
		shape[i] = shape_in[i];
		if (shape[i] == 0) {
			empty = true;
		}
	}
	if (strides_in) {
		for (int i = 0; i < ndim; i++) {
			if (strides_in[i] < 1) {
				FAIL(3, "must contain only values of at least 1");
			}
			st[i] = strides_in[i];
		}
	} else { /* C-contiguous; empty axes count as 1 so the strides stay valid */
		int64_t acc = 1;
		for (int i = ndim - 1; i >= 0; i--) {
			st[i] = acc;
			if (buf_mul_ovf(acc, shape[i] ? shape[i] : 1, &acc)) {
				FAIL(2, "describes more elements than can be addressed");
			}
		}
	}

	if (packed) {
		/* elements are bits apart on the last axis; every row starts on a byte boundary */
		if (ndim > 0 && st[ndim - 1] != 1) {
			FAIL(3, "the last axis of a packed %s view must have stride 1", info->name);
		}
		for (int i = 0; i < ndim - 1; i++) {
			if (buf_mul_ovf(st[i], bits, &t)) {
				FAIL(3, "stride is too large");
			}
			if (t % 8) {
				FAIL(strides_in ? 3 : 2, "axis %d of a packed %s view has a stride of %lld elements = %lld bits, which is not a whole number of bytes; "
					"pass explicit strides that are multiples of %d elements", i, info->name, (long long)st[i], (long long)t, bits == 4 ? 2 : 4);
			}
			sb[i] = t / 8;
		}
	} else {
		for (int i = 0; i < ndim; i++) {
			if (buf_mul_ovf(st[i], size, &sb[i])) {
				FAIL(3, "stride is too large");
			}
		}
	}

	if (!empty) {
		bool fits = true;

		if (packed) { /* bytes from the start of the last element's row to the end of the last element */
			int64_t row_bytes = 1;

			if (ndim > 0) {
				if (buf_mul_ovf(shape[ndim - 1], bits, &t)) {
					FAIL(2, "describes more elements than can be addressed");
				}
				row_bytes = (t + 7) / 8;
			}
			for (int i = 0; i < ndim - 1 && fits; i++) {
				if (buf_mul_ovf(shape[i] - 1, sb[i], &t) || last > INT64_MAX - t) {
					fits = false;
				} else {
					last += t;
				}
			}
			extent = row_bytes;
		} else {
			for (int i = 0; i < ndim && fits; i++) {
				if (buf_mul_ovf(shape[i] - 1, sb[i], &t) || last > INT64_MAX - t) {
					fits = false;
				} else {
					last += t;
				}
			}
			extent = size;
		}
		fits = fits && last <= INT64_MAX - extent && (uint64_t)offset + (uint64_t)last + (uint64_t)extent <= b->len;
		if (!fits) {
			FAIL(2, "with the given strides and offset extends beyond the end of the buffer (%" PRIu64 " bytes)", b->len);
		}
	}

	return buf_view_new(bufobj, dt, ndim, shape, sb, b->base + offset, b->writable, b->slow);
}

void buf_buffer_view_impl(zend_object *bufobj, INTERNAL_FUNCTION_PARAMETERS)
{
	buf_buffer *b = buf_buffer_from(bufobj);
	zend_object *dtobj, *v;
	zval *shape_zv = NULL;
	HashTable *strides_ht = NULL;
	zend_long offset = 0;
	int64_t shape[BUF_MAX_NDIM] = {0}, st[BUF_MAX_NDIM] = {0};
	int dt, ndim, n, arg;
	char err[256];
	bool packed;

	ZEND_PARSE_PARAMETERS_START(1, 4)
		Z_PARAM_OBJ_OF_CLASS(dtobj, buf_ce_dtype)
		Z_PARAM_OPTIONAL
		Z_PARAM_ZVAL(shape_zv)
		Z_PARAM_ARRAY_HT_OR_NULL(strides_ht)
		Z_PARAM_LONG(offset)
	ZEND_PARSE_PARAMETERS_END();

	dt = buf_dtype_from_object(dtobj);
	if (!buf_dtype_fits_php_int(dt)) {
		zend_argument_value_error(1, "the dtype %s is not supported on 32-bit PHP: its values do not fit a PHP int", buf_dtypes[dt].name);
		RETURN_THROWS();
	}
	packed = buf_is_packed(dt);

	if (!shape_zv || Z_TYPE_P(shape_zv) == IS_NULL) {
		/* with an offset that buf_view_make() is about to reject, any shape will do */
		if (offset >= 0 && (uint64_t)offset <= b->len) {
			uint64_t rest = b->len - (uint64_t)offset;

			if (packed) {
				int64_t total;
				if (buf_mul_ovf((int64_t)rest, 8, &total) || total % buf_dtypes[dt].bits) {
					zend_argument_value_error(2, "is required: the number of bits in the buffer after the offset is not a multiple of the element width (%d bits)", (int)buf_dtypes[dt].bits);
					RETURN_THROWS();
				}
				shape[0] = total / buf_dtypes[dt].bits;
			} else {
				if (rest % buf_dtypes[dt].size) {
					zend_argument_value_error(2, "is required: the buffer length minus the offset is not a multiple of the element size (%d bytes)", (int)buf_dtypes[dt].size);
					RETURN_THROWS();
				}
				shape[0] = rest / buf_dtypes[dt].size;
			}
		}
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
	}

	v = buf_view_make(bufobj, dt, ndim, shape, strides_ht ? st : NULL, offset, err, sizeof err, &arg);
	if (!v) {
		zend_argument_value_error(arg, "%s", err);
		RETURN_THROWS();
	}
	RETURN_OBJ(v);
}

/* `new Tensor` must produce a valid object before get_constructor refuses it */
static zend_object *view_create_blank(zend_class_entry *ce)
{
	buf_view *v = zend_object_alloc(sizeof(buf_view), ce);

	zend_object_std_init(&v->std, ce);
	v->std.handlers = &view_handlers[0][0][1][0];
	v->buf = NULL;
	v->data = NULL;
	v->dt = v->ndim = v->writable = v->slow = 0;
	v->shape[0] = 0;
	return &v->std;
}

/* ---- whole-view data, the 0-d `value`, debug output and JSON ------------------------------------
 * None of this is on the element access path. */

/* the number of elements, saturating */
static int64_t view_elements(const buf_view *v)
{
	int64_t n = 1;

	for (int i = 0; i < v->ndim; i++) {
		if (v->shape[i] == 0) return 0;
		if (buf_mul_ovf(n, v->shape[i], &n)) return INT64_MAX;
	}
	return n;
}

/* nested arrays of the elements below `p` (the first element of the sub-view on `axis`) */
static void view_data_axis(const buf_view *v, int axis, const uint8_t *p, zval *out)
{
	int64_t n = v->shape[axis];
	bool last = axis == v->ndim - 1, packed = buf_is_packed(v->dt);

	array_init_size(out, (uint32_t)(n < 1024 ? n : 1024));
	for (int64_t i = 0; i < n; i++) {
		zval e;

		if (!last) {
			view_data_axis(v, axis + 1, p + i * v->sbytes[axis], &e);
		} else if (packed) {
			fns[v->dt].leaf(p, (zend_ulong)i, &e);
		} else {
			fns[v->dt].leaf(p + i * v->sbytes[axis], 0, &e);
		}
		zend_hash_next_index_insert_new(Z_ARRVAL_P(out), &e);
	}
}

/* the whole contents; a 0-d view gives the scalar. The memory must be host accessible and ready. */
static void view_data_zval(const buf_view *v, zval *out)
{
	if (v->ndim == 0) {
		fns[v->dt].leaf(v->data, 0, out);
	} else {
		view_data_axis(v, 0, v->data, out);
	}
}

static HashTable *view_props_array(zend_object *o, bool debug)
{
	buf_view *v = buf_view_from(o);
	HashTable *ht;
	zval z, l;
	int64_t st[BUF_MAX_NDIM];

	ALLOC_HASHTABLE(ht);
	zend_hash_init(ht, 8, NULL, ZVAL_PTR_DTOR, 0);
	if (!v->buf) {
		return ht;
	}
	{
		buf_buffer *b = buf_buffer_from(v->buf);

		buf_dtype_to_zval(v->dt, &z);
		zend_hash_str_add_new(ht, "dtype", 5, &z);
		if (debug) {
			array_init_size(&z, v->ndim);
			for (int i = 0; i < v->ndim; i++) add_next_index_long(&z, v->shape[i]);
		} else {
			ZVAL_OBJ(&z, buf_shape_new_ints(v->ndim, v->shape));
		}
		zend_hash_str_add_new(ht, "shape", 5, &z);
		if (!debug) {
			ZVAL_LONG(&z, v->ndim);
			zend_hash_str_add_new(ht, "ndim", 4, &z);
		}
		buf_view_strides_elements(v, st);
		array_init_size(&l, v->ndim);
		for (int i = 0; i < v->ndim; i++) add_next_index_long(&l, st[i]);
		zend_hash_str_add_new(ht, "strides", 7, &l);
		ZVAL_LONG(&z, v->data - b->base);
		zend_hash_str_add_new(ht, "offset", 6, &z);
		if (debug) {
			ZVAL_STR(&z, buf_device_name(b->dev_type, b->dev_id));
			zend_hash_str_add_new(ht, "device", 6, &z);
			ZVAL_BOOL(&z, v->writable);
			zend_hash_str_add_new(ht, "writable", 8, &z);
			if (!b->host) {
				ZVAL_STRING(&z, "(not host-accessible)");
			} else if (b->pending) {
				ZVAL_STRING(&z, "(pending)");
			} else if (view_elements(v) > 1000) {
				ZVAL_STR(&z, zend_strpprintf(0, "(%" PRId64 " elements)", view_elements(v)));
			} else {
				view_data_zval(v, &z);
			}
			zend_hash_str_add_new(ht, "data", 4, &z);
		} else {
			GC_ADDREF(v->buf);
			ZVAL_OBJ(&z, v->buf);
			zend_hash_str_add_new(ht, "buffer", 6, &z);
			if (v->ndim == 0 && b->host && !b->pending) {
				view_data_zval(v, &z);
				zend_hash_str_add_new(ht, "value", 5, &z);
			}
		}
	}
	return ht;
}

/* var_dump(), print_r(), var_export(), get_object_vars() and (array): the virtual properties are not stored, so the
 * default would find none (and enumerating the hooks would make `value` throw on views with axes) */
static HashTable *view_get_properties_for(zend_object *o, zend_prop_purpose purpose)
{
	return view_props_array(o, purpose == ZEND_PROP_PURPOSE_DEBUG);
}

static HashTable *view_get_debug_info(zend_object *o, int *is_temp)
{
	*is_temp = 1;
	return view_props_array(o, true);
}

/* ---- Tensor::fromArray() and toArray() ------------------------------------------------------- */

/* "[2][1]" for the indices idx[0..depth) */
static void array_path(char *out, size_t n, const int64_t *idx, int depth)
{
	size_t len = 0;

	out[0] = 0;
	for (int i = 0; i < depth && len < n; i++) {
		len += snprintf(out + len, n - len, "[%" PRId64 "]", idx[i]);
	}
	if (!depth) snprintf(out, n, "the top level");
}

typedef struct {
	int nd;                          /* dimensions found */
	int64_t shape[BUF_MAX_NDIM];
	int64_t idx[BUF_MAX_NDIM];       /* where the walk is, for error messages */
	bool saw_int, saw_float, saw_bool;
} array_scan;

/* checks the shape against sc->shape and the types of the leaves; every array at depth d must have shape[d] elements */
static bool array_scan_level(array_scan *sc, HashTable *ht, int depth)
{
	zval *e;
	int64_t i = 0;
	char path[96];

	ZEND_HASH_FOREACH_VAL(ht, e) {
		ZVAL_DEREF(e);
		sc->idx[depth] = i;
		if (depth + 1 < sc->nd) {
			if (Z_TYPE_P(e) != IS_ARRAY) {
				array_path(path, sizeof path, sc->idx, depth + 1);
				zend_value_error("Tensor::fromArray(): ragged array at %s: expected an array, found %s", path, zend_zval_type_name(e));
				return false;
			}
			if ((int64_t)zend_hash_num_elements(Z_ARRVAL_P(e)) != sc->shape[depth + 1]) {
				array_path(path, sizeof path, sc->idx, depth + 1);
				zend_value_error("Tensor::fromArray(): ragged array at %s: expected %" PRId64 " elements, found %u",
					path, sc->shape[depth + 1], zend_hash_num_elements(Z_ARRVAL_P(e)));
				return false;
			}
			if (!array_scan_level(sc, Z_ARRVAL_P(e), depth + 1)) return false;
		} else {
			switch (Z_TYPE_P(e)) {
				case IS_LONG: sc->saw_int = true; break;
				case IS_DOUBLE: sc->saw_float = true; break;
				case IS_TRUE: case IS_FALSE: sc->saw_bool = true; break;
				case IS_ARRAY:
					array_path(path, sizeof path, sc->idx, depth + 1);
					zend_value_error("Tensor::fromArray(): ragged array at %s: expected a scalar, found an array", path);
					return false;
				default:
					array_path(path, sizeof path, sc->idx, depth + 1);
					zend_value_error("Tensor::fromArray(): unsupported value at %s: expected an int, float or bool, found %s", path, zend_zval_type_name(e));
					return false;
			}
		}
		i++;
	} ZEND_HASH_FOREACH_END();
	return true;
}

/* the integer range of an integer dtype; PHP ints are signed 64-bit, so UInt64 stops at ZEND_LONG_MAX */
static void int_range(int dt, int64_t *lo, int64_t *hi)
{
	int bits = buf_dtypes[dt].bits;

	if (buf_dtypes[dt].kind == BUF_INT) {
		*lo = bits == 64 ? INT64_MIN : -((int64_t)1 << (bits - 1));
		*hi = bits == 64 ? INT64_MAX : ((int64_t)1 << (bits - 1)) - 1;
	} else {
		*lo = 0;
		*hi = bits >= 63 ? INT64_MAX : ((int64_t)1 << bits) - 1;
	}
}

/* converts the leaf `e` for dtype dt and stores it at element i of p; false after throwing (the path is idx[0..nd)) */
static bool array_store(array_scan *sc, int dt, uint8_t *p, zend_ulong i, zval *e)
{
	int kind = buf_dtypes[dt].kind;
	zval tmp;
	char path[96];
	const char *why;

	if (kind == BUF_BOOL) {
		if (Z_TYPE_P(e) == IS_TRUE || Z_TYPE_P(e) == IS_FALSE) goto store;
		why = "a Bool tensor takes only bool values";
		goto bad;
	}
	if (Z_TYPE_P(e) != IS_LONG && Z_TYPE_P(e) != IS_DOUBLE) {
		why = "a numeric tensor takes only int and float values";
		goto bad;
	}
	if (kind == BUF_INT || kind == BUF_UINT) {
		int64_t lo, hi, l;

		int_range(dt, &lo, &hi);
		if (Z_TYPE_P(e) == IS_DOUBLE) {
			double d = Z_DVAL_P(e);

			if (!(d >= -9223372036854775808.0 && d < 9223372036854775808.0) || d != floor(d)) {
				why = "the float is not an integer in range";
				goto bad;
			}
			l = (int64_t)d;
		} else {
			l = Z_LVAL_P(e);
		}
		if (l < lo || l > hi) {
			why = "out of range";
			goto bad;
		}
		ZVAL_LONG(&tmp, l);
		e = &tmp;
	}
store:
	if (!fns[dt].store(p, i, e)) { why = "unsupported"; goto bad; }
	return true;
bad:
	array_path(path, sizeof path, sc->idx, sc->nd);
	if (Z_TYPE_P(e) == IS_LONG) {
		zend_value_error("Tensor::fromArray(): cannot store %" PRId64 " at %s as %s: %s", (int64_t)Z_LVAL_P(e), path, buf_dtypes[dt].name, why);
	} else {
		zend_value_error("Tensor::fromArray(): cannot store %s at %s as %s: %s", zend_zval_type_name(e), path, buf_dtypes[dt].name, why);
	}
	return false;
}

static bool array_fill(array_scan *sc, const buf_view *v, int dt, HashTable *ht, int depth, uint8_t *p)
{
	zval *e;
	int64_t i = 0;
	bool last = depth + 1 == sc->nd, packed = buf_is_packed(dt);

	ZEND_HASH_FOREACH_VAL(ht, e) {
		ZVAL_DEREF(e);
		sc->idx[depth] = i;
		if (!last) {
			if (!array_fill(sc, v, dt, Z_ARRVAL_P(e), depth + 1, p + i * v->sbytes[depth])) return false;
		} else if (packed) {
			if (!array_store(sc, dt, p, (zend_ulong)i, e)) return false;
		} else if (!array_store(sc, dt, p + i * v->sbytes[depth], 0, e)) {
			return false;
		}
		i++;
	} ZEND_HASH_FOREACH_END();
	return true;
}

PHP_METHOD(Buffers_Tensor, fromArray)
{
	zval *data;
	zend_object *dtobj = NULL, *devobj = NULL, *buf, *view, *res;
	array_scan sc = {0};
	int dt, ndim;
	int64_t strides[BUF_MAX_NDIM], acc = 1, total = 1, bytes;
	bool packed;
	char err[160];
	int arg;
	zval *z;

	ZEND_PARSE_PARAMETERS_START(1, 3)
		Z_PARAM_ZVAL(data)
		Z_PARAM_OPTIONAL
		Z_PARAM_OBJ_OF_CLASS_OR_NULL(dtobj, buf_ce_dtype)
		Z_PARAM_OBJ_OF_CLASS_OR_NULL(devobj, buf_ce_device_if)
	ZEND_PARSE_PARAMETERS_END();

	/* the shape is that of the first path down the nesting; an empty array ends it */
	z = data;
	while (Z_TYPE_P(z) == IS_ARRAY) {
		if (sc.nd == BUF_MAX_NDIM) {
			zend_value_error("Tensor::fromArray(): the array is nested more than %d levels deep", BUF_MAX_NDIM);
			RETURN_THROWS();
		}
		sc.shape[sc.nd++] = zend_hash_num_elements(Z_ARRVAL_P(z));
		if (!zend_hash_num_elements(Z_ARRVAL_P(z))) break;
		{
			HashPosition pos;

			zend_hash_internal_pointer_reset_ex(Z_ARRVAL_P(z), &pos);
			z = zend_hash_get_current_data_ex(Z_ARRVAL_P(z), &pos);
		}
		ZVAL_DEREF(z);
	}
	if (sc.nd == 0) {                                  /* a scalar: the leaf checks of the scan, done here */
		switch (Z_TYPE_P(data)) {
			case IS_LONG: sc.saw_int = true; break;
			case IS_DOUBLE: sc.saw_float = true; break;
			case IS_TRUE: case IS_FALSE: sc.saw_bool = true; break;
			default: zend_argument_type_error(1, "must be an array, int, float or bool, %s given", zend_zval_type_name(data)); RETURN_THROWS();
		}
	} else if (zend_hash_num_elements(Z_ARRVAL_P(data)) && !array_scan_level(&sc, Z_ARRVAL_P(data), 0)) {
		RETURN_THROWS();
	}
	ndim = sc.nd;

	if (dtobj) {
		dt = buf_dtype_from_object(dtobj);
		if (dt < 0) RETURN_THROWS();
	} else if (sc.saw_bool) {
		if (sc.saw_int || sc.saw_float) {
			zend_value_error("Tensor::fromArray(): cannot infer a dtype from a mix of bool and numbers; pass a dtype");
			RETURN_THROWS();
		}
		dt = BUF_T_BOOL;
	} else if (sc.saw_float) {
		dt = BUF_T_FLOAT64;
	} else if (sc.saw_int) {
		dt = SIZEOF_ZEND_LONG == 4 ? BUF_T_INT32 : BUF_T_INT64;
	} else {
		dt = BUF_T_FLOAT64;                            /* nothing to infer from: empty */
	}
	if (buf_dtypes[dt].kind == BUF_COMPLEX) {
		zend_argument_value_error(2, "must not be a complex dtype");
		RETURN_THROWS();
	}
	if (!buf_dtype_fits_php_int(dt)) {
		zend_argument_value_error(2, "the dtype %s is not supported on 32-bit PHP", buf_dtypes[dt].name);
		RETURN_THROWS();
	}

	/* C-contiguous; the rows of a packed dtype are padded to whole bytes (as buf_view_copy_host() lays them out) */
	packed = buf_is_packed(dt);
	for (int i = ndim - 1; i >= 0; i--) {
		int64_t dim = sc.shape[i] ? sc.shape[i] : 1;

		strides[i] = acc;
		if (buf_mul_ovf(acc, dim, &acc)) goto too_large;
		if (packed && i == ndim - 1) {
			int step = buf_dtypes[dt].bits == 4 ? 2 : 4;
			acc = (acc + step - 1) / step * step;
		}
		if (sc.shape[i] == 0) total = 0;
	}
	if (!total) {
		bytes = 0;
	} else if (ndim == 0) {
		bytes = packed ? 1 : buf_dtypes[dt].size;
	} else if (packed) {
		if (buf_mul_ovf(acc, buf_dtypes[dt].bits, &bytes)) goto too_large;
		bytes = (bytes + 7) / 8;
	} else if (buf_mul_ovf(acc, buf_dtypes[dt].size, &bytes)) {
		goto too_large;
	}
	buf = buf_buffer_new_host((uint64_t)bytes);
	if (!buf) RETURN_THROWS();
	view = buf_view_make(buf, dt, ndim, sc.shape, ndim ? strides : NULL, 0, err, sizeof err, &arg);
	OBJ_RELEASE(buf);
	if (!view) {
		zend_value_error("Tensor::fromArray(): %s", err);
		RETURN_THROWS();
	}
	if (total) {
		buf_view *v = buf_view_from(view);
		bool ok;

		if (ndim == 0) {
			ok = array_store(&sc, dt, v->data, 0, data);
		} else {
			ok = array_fill(&sc, v, dt, Z_ARRVAL_P(data), 0, v->data);
		}
		if (!ok) {
			OBJ_RELEASE(view);
			RETURN_THROWS();
		}
	}
	if (devobj) {
		res = buf_view_to(view, devobj);
		OBJ_RELEASE(view);
		if (!res) RETURN_THROWS();
		view = res;
	}
	RETURN_OBJ(view);
too_large:
	zend_value_error("Tensor::fromArray(): the shape is too large");
}

PHP_METHOD(Buffers_Tensor, toArray)
{
	buf_view *v = VIEW;
	buf_buffer *b = buf_buffer_from(v->buf);

	ZEND_PARSE_PARAMETERS_NONE();
	if (!b->host) {
		view_not_accessible(b);
		RETURN_THROWS();
	}
	if (buf_buffer_make_ready(b) == FAILURE) {
		RETURN_THROWS();
	}
	view_data_zval(v, return_value);
}

PHP_METHOD(Buffers_Tensor, jsonSerialize)
{
	ZEND_MN(Buffers_Tensor_toArray)(INTERNAL_FUNCTION_PARAM_PASSTHRU);
}

static ZEND_COLD void value_needs_0d(void)
{
	zend_throw_error(NULL, "value is only available on 0-d views; index the view instead");
}

/* ---- properties: dtype, shape, ndim, strides, offset, buffer, value --------------------------------------
 * Virtual properties with internal get hooks (see props.c): computed when read, nothing stored in the
 * view, so making a sub-view allocates nothing for them. */

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(ai_get_dtype, 0, 0, Buffers\\DType, 0)
ZEND_END_ARG_INFO()
ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(ai_get_shape, 0, 0, Buffers\\Shape, 0)
ZEND_END_ARG_INFO()
ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(ai_get_buffer, 0, 0, Buffers\\Buffer, 0)
ZEND_END_ARG_INFO()
ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(ai_get_int, 0, 0, IS_LONG, 0)
ZEND_END_ARG_INFO()
ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(ai_get_array, 0, 0, IS_ARRAY, 0)
ZEND_END_ARG_INFO()
ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(ai_get_mixed, 0, 0, IS_MIXED, 0)
ZEND_END_ARG_INFO()
ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(ai_set_value, 0, 1, IS_VOID, 0)
	ZEND_ARG_TYPE_INFO(0, value, IS_MIXED, 0)
ZEND_END_ARG_INFO()

static ZEND_NAMED_FUNCTION(hook_dtype) { ZEND_PARSE_PARAMETERS_NONE(); buf_dtype_to_zval(VIEW->dt, return_value); }
static ZEND_NAMED_FUNCTION(hook_ndim) { ZEND_PARSE_PARAMETERS_NONE(); RETURN_LONG(VIEW->ndim); }
static ZEND_NAMED_FUNCTION(hook_buffer) { ZEND_PARSE_PARAMETERS_NONE(); RETURN_OBJ_COPY(VIEW->buf); }

static ZEND_NAMED_FUNCTION(hook_shape)
{
	buf_view *v = VIEW;

	ZEND_PARSE_PARAMETERS_NONE();
	RETURN_OBJ(buf_shape_new_ints(v->ndim, v->shape));
}

static ZEND_NAMED_FUNCTION(hook_strides)
{
	buf_view *v = VIEW;
	int64_t st[BUF_MAX_NDIM];

	ZEND_PARSE_PARAMETERS_NONE();
	buf_view_strides_elements(v, st);
	array_init_size(return_value, v->ndim);
	for (int i = 0; i < v->ndim; i++) add_next_index_long(return_value, st[i]);
}

static ZEND_NAMED_FUNCTION(hook_offset)
{
	buf_view *v = VIEW;

	ZEND_PARSE_PARAMETERS_NONE();
	RETURN_LONG(v->data - buf_buffer_from(v->buf)->base);
}

/* $v->value: the single element of a 0-d view. Same rules as element access: read-only buffers, memory that is
 * not host accessible and pending buffers behave as they do there. */
static ZEND_NAMED_FUNCTION(hook_value_get)
{
	buf_view *v = VIEW;

	ZEND_PARSE_PARAMETERS_NONE();
	if (v->ndim != 0) {
		value_needs_0d();
		RETURN_THROWS();
	}
	if (v->slow && !buf_view_check_access(v)) {
		RETURN_THROWS();
	}
	fns[v->dt].leaf(v->data, 0, return_value);
}

static ZEND_NAMED_FUNCTION(hook_value_set)
{
	buf_view *v = VIEW;
	zval *val;

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_ZVAL(val)
	ZEND_PARSE_PARAMETERS_END();
	if (v->ndim != 0) {
		value_needs_0d();
		RETURN_THROWS();
	}
	if (!v->writable) {
		zend_throw_error(NULL, "Cannot write to a read-only Tensor");
		RETURN_THROWS();
	}
	if (v->slow && !buf_view_check_access(v)) {
		RETURN_THROWS();
	}
	ZVAL_DEREF(val);
	if (!fns[v->dt].poke(v, val)) {
		zend_type_error("Cannot write %s to a Tensor of dtype %s", zend_zval_type_name(val), buf_dtypes[v->dt].name);
		RETURN_THROWS();
	}
}

static zend_type class_type(const char *name, bool nullable)
{
	return (zend_type) ZEND_TYPE_INIT_CLASS(zend_string_init(name, strlen(name), 1), nullable, 0);
}

/* TensorAggregate: a Tensor is its own tensor */
PHP_METHOD(Buffers_Tensor, getTensor) { ZEND_PARSE_PARAMETERS_NONE(); RETURN_OBJ_COPY(Z_OBJ_P(ZEND_THIS)); }
PHP_METHOD(Buffers_Tensor, getDType) { hook_dtype(INTERNAL_FUNCTION_PARAM_PASSTHRU); }
PHP_METHOD(Buffers_Tensor, getShape) { hook_shape(INTERNAL_FUNCTION_PARAM_PASSTHRU); }

/* ---- registration --------------------------------------------------------------------------- */

static void init_view_handlers(zend_object_handlers *h)
{
	memcpy(h, &std_object_handlers, sizeof *h);
	h->offset = XtOffsetOf(buf_view, std);
	h->dtor_obj = view_dtor;
	h->free_obj = view_free;
	h->clone_obj = NULL;
	h->compare = buf_compare_identity;
	h->get_constructor = view_no_constructor;
	h->do_operation = buf_view_do_operation;
	h->get_properties_for = view_get_properties_for;
	h->get_debug_info = view_get_debug_info;
}

void buf_view_minit(void)
{
	zend_class_entry ce;
	const buf_prop_def view_props[] = {
		{ "dtype", class_type("Buffers\\DType", false), hook_dtype, ai_get_dtype, 0 },
		{ "shape", class_type("Buffers\\Shape", false), hook_shape, ai_get_shape, 0 },
		{ "ndim", (zend_type) ZEND_TYPE_INIT_MASK(MAY_BE_LONG), hook_ndim, ai_get_int, 0 },
		{ "strides", (zend_type) ZEND_TYPE_INIT_MASK(MAY_BE_ARRAY), hook_strides, ai_get_array, 0 },
		{ "offset", (zend_type) ZEND_TYPE_INIT_MASK(MAY_BE_LONG), hook_offset, ai_get_int, 0 },
		{ "buffer", class_type("Buffers\\Buffer", false), hook_buffer, ai_get_buffer, 0 },
		{ "value", (zend_type) ZEND_TYPE_INIT_MASK(MAY_BE_ANY), hook_value_get, ai_get_mixed, 0, hook_value_set, ai_set_value },
	};

	INIT_CLASS_ENTRY(ce, "Buffers\\TensorAggregate", class_Buffers_TensorAggregate_methods);
	buf_ce_tensor_agg = zend_register_internal_interface(&ce);

	INIT_CLASS_ENTRY(ce, "Buffers\\Tensor", class_Buffers_Tensor_methods);
	buf_ce_tensor = zend_register_internal_class_ex(&ce, NULL);
	buf_ce_tensor->ce_flags |= ZEND_ACC_FINAL | ZEND_ACC_NO_DYNAMIC_PROPERTIES | ZEND_ACC_NOT_SERIALIZABLE;
	buf_ce_tensor->create_object = view_create_blank; /* real instances come from buf_view_new() */
	buf_declare_hooked_props(buf_ce_tensor, view_props, 7); /* before the interface that requires them */
	zend_class_implements(buf_ce_tensor, 4, buf_ce_tensor_agg, zend_ce_countable, zend_ce_arrayaccess, zend_ce_aggregate);
	if (buf_json_serializable()) {
		zend_class_implements(buf_ce_tensor, 1, buf_json_serializable());
	}
	buf_ce_tensor->get_iterator = view_get_iterator;

	for (int dt = 0; dt < BUF_NTYPES; dt++) {
		for (int contig = 0; contig < 2; contig++) {
			for (int ro = 0; ro < 2; ro++) {
				for (int slow = 0; slow < 2; slow++) {
					zend_object_handlers *h = &view_handlers[dt][contig][ro][slow];

					init_view_handlers(h);
					h->count_elements = view_count_elements;
					h->has_dimension = view_has_dimension;
					h->unset_dimension = view_unset_dimension;
					h->read_dimension = slow ? view_read_slow : fns[dt].rd[contig];
					h->write_dimension = ro ? buf_write_ro : slow ? view_write_slow : fns[dt].wr[contig];
				}
			}
		}
	}
	init_view_handlers(&view0_handlers);
	view0_handlers.count_elements = view0_count;
	view0_handlers.has_dimension = view0_has;
	view0_handlers.unset_dimension = view0_unset;
	view0_handlers.read_dimension = view0_read;
	view0_handlers.write_dimension = view0_write;
}
