/* DataView: single values at any byte offset of a Buffer, like JavaScript's DataView over an ArrayBuffer. Little-endian by default
 * (JavaScript defaults to big-endian); every multi-byte accessor takes $bigEndian. Values are loaded and stored with memcpy, so any
 * offset works. Element conversion is the one of Tensor element access (same codecs).
 *
 * DataViewAccessor ($d->uint32 ...): the same values with index syntax, $a[$byteOffset], through object handlers (no method call). */
#include "php_buffers.h"
#include "codec.h"
#include "zend_exceptions.h"
#include "zend_interfaces.h"
#include "buffers_arginfo.h"

extern double buf_tab[][256];

zend_class_entry *buf_ce_dataview, *buf_ce_dataview_accessor;
static zend_object_handlers dataview_handlers;

/* the byte-indexed accessor types ($d->uint32 ...), in the order of the DataView properties */
#define DVA_TYPES(X) \
	X(int8,     INT8,     1) X(uint8,    UINT8,    1) \
	X(int16,    INT16,    2) X(uint16,   UINT16,   2) \
	X(int32,    INT32,    4) X(uint32,   UINT32,   4) \
	X(int64,    INT64,    8) X(uint64,   UINT64,   8) \
	X(float16,  FLOAT16,  2) X(bfloat16, BFLOAT16, 2) \
	X(float32,  FLOAT32,  4) X(float64,  FLOAT64,  8)
enum {
#define X(name, ID, size) DVA_##ID,
	DVA_TYPES(X)
#undef X
	DVA_N
};

typedef struct {
	zend_object *buf;            /* owned; NULL until the constructor ran */
	uint64_t off, len;           /* the window of the buffer, in bytes */
	zend_object *acc[DVA_N];     /* the accessors handed out so far (owned), created on first read of the property */
	zend_object std;
} buf_dataview;

static inline buf_dataview *dv_from(zend_object *o) { return (buf_dataview *)((char *)o - XtOffsetOf(buf_dataview, std)); }
#define DV dv_from(Z_OBJ_P(ZEND_THIS))

static zend_object *dataview_create(zend_class_entry *ce)
{
	buf_dataview *d = zend_object_alloc(sizeof(buf_dataview), ce);

	zend_object_std_init(&d->std, ce);
	object_properties_init(&d->std, ce);
	d->std.handlers = &dataview_handlers;
	d->buf = NULL;
	d->off = d->len = 0;
	memset(d->acc, 0, sizeof d->acc);
	return &d->std;
}

static void dataview_free(zend_object *o)
{
	buf_dataview *d = dv_from(o);

	for (int i = 0; i < DVA_N; i++) {
		if (d->acc[i]) {
			OBJ_RELEASE(d->acc[i]);
		}
	}
	if (d->buf) {
		OBJ_RELEASE(d->buf);
	}
	zend_object_std_dtor(o);
}

PHP_METHOD(Buffers_DataView, __construct)
{
	buf_dataview *d = DV;
	zend_object *bo;
	zend_long off = 0, len = 0;
	bool len_null = true;
	uint64_t blen;

	ZEND_PARSE_PARAMETERS_START(1, 3)
		Z_PARAM_OBJ_OF_CLASS(bo, buf_ce_buffer)
		Z_PARAM_OPTIONAL
		Z_PARAM_LONG(off)
		Z_PARAM_LONG_OR_NULL(len, len_null)
	ZEND_PARSE_PARAMETERS_END();
	if (d->buf) {
		zend_throw_error(NULL, "Cannot modify a DataView: it is already constructed");
		RETURN_THROWS();
	}
	blen = buf_buffer_from(bo)->len;
	if (off < 0 || (uint64_t)off > blen) {
		zend_argument_value_error(2, "must be between 0 and the byte length of the buffer");
		RETURN_THROWS();
	}
	if (len_null) {
		len = (zend_long)(blen - (uint64_t)off > (uint64_t)ZEND_LONG_MAX ? ZEND_LONG_MAX : blen - (uint64_t)off);
	} else if (len < 0 || (uint64_t)len > blen - (uint64_t)off) {
		zend_argument_value_error(3, "must be between 0 and the byte length of the buffer minus the offset");
		RETURN_THROWS();
	}
	GC_ADDREF(bo);
	d->buf = bo;
	d->off = (uint64_t)off;
	d->len = (uint64_t)len;
}

/* the address of `size` bytes at `offset` of the view, checked; NULL after throwing */
static uint8_t *dv_at(buf_dataview *d, zend_long offset, int size, bool write)
{
	buf_buffer *b;

	if (UNEXPECTED(!d->buf)) {
		zend_throw_error(NULL, "The DataView is not constructed");
		return NULL;
	}
	b = buf_buffer_from(d->buf);
	if (d->len < (uint64_t)size) {
		zend_argument_value_error(1, "is out of range: the view has %" PRIu64 " bytes, fewer than the %d of the value", d->len, size);
		return NULL;
	}
	if (offset < 0 || (uint64_t)offset > d->len - (uint64_t)size) {
		zend_argument_value_error(1, "must be between 0 and %" PRIu64 " (the byte length of the view minus %d)", d->len - (uint64_t)size, size);
		return NULL;
	}
	if (write && !b->writable) {
		zend_throw_error(NULL, "Cannot write to a read-only Buffer");
		return NULL;
	}
	if (UNEXPECTED(!b->host)) {
		zend_string *n = buf_device_name(b->dev_type, b->dev_id);

		zend_throw_error(NULL, "Cannot access the bytes of a buffer on device %s: its memory is not host accessible", ZSTR_VAL(n));
		zend_string_release(n);
		return NULL;
	}
	if (UNEXPECTED(b->pending) && buf_buffer_make_ready(b) == FAILURE) {
		return NULL;
	}
	return b->base + d->off + (uint64_t)offset;
}

#ifdef WORDS_BIGENDIAN
# define HOST_BE true
#else
# define HOST_BE false
#endif

/* n bytes between memory and a native-order value, reversed when the requested byte order is not the host's */
static zend_always_inline void dv_load(void *dst, const uint8_t *p, int n, bool be)
{
	if (be == HOST_BE) {
		memcpy(dst, p, n);
	} else {
		for (int i = 0; i < n; i++) ((uint8_t *)dst)[i] = p[n - 1 - i];
	}
}

static zend_always_inline void dv_store(uint8_t *p, const void *src, int n, bool be)
{
	if (be == HOST_BE) {
		memcpy(p, src, n);
	} else {
		for (int i = 0; i < n; i++) p[i] = ((const uint8_t *)src)[n - 1 - i];
	}
}

#if SIZEOF_ZEND_LONG == 4
# define CHECK_FITS(T) if (sizeof(T) > 4 || (sizeof(T) == 4 && (T)-1 > 0)) { \
	zend_throw_error(NULL, "%s is not supported on 32-bit PHP: its values do not fit a PHP int", #T); RETURN_THROWS(); }
#else
# define CHECK_FITS(T)
#endif

/* multi-byte integers: get returns the value (UInt64 above PHP_INT_MAX wraps to negative, like unpack()), set wraps to the width */
#define DV_INT(Name, T) \
	PHP_METHOD(Buffers_DataView, get##Name) { \
		zend_long off; bool be = false; uint8_t *p; T v; \
		ZEND_PARSE_PARAMETERS_START(1, 2) Z_PARAM_LONG(off) Z_PARAM_OPTIONAL Z_PARAM_BOOL(be) ZEND_PARSE_PARAMETERS_END(); \
		CHECK_FITS(T) \
		if (!(p = dv_at(DV, off, sizeof(T), false))) RETURN_THROWS(); \
		dv_load(&v, p, sizeof(T), be); \
		RETURN_LONG((zend_long)v); } \
	PHP_METHOD(Buffers_DataView, set##Name) { \
		zend_long off, val; bool be = false; uint8_t *p; T v; \
		ZEND_PARSE_PARAMETERS_START(2, 3) Z_PARAM_LONG(off) Z_PARAM_LONG(val) Z_PARAM_OPTIONAL Z_PARAM_BOOL(be) ZEND_PARSE_PARAMETERS_END(); \
		CHECK_FITS(T) \
		if (!(p = dv_at(DV, off, sizeof(T), true))) RETURN_THROWS(); \
		v = (T)val; \
		dv_store(p, &v, sizeof(T), be); }
DV_INT(Int16, int16_t) DV_INT(Int32, int32_t) DV_INT(Int64, int64_t)
DV_INT(UInt16, uint16_t) DV_INT(UInt32, uint32_t) DV_INT(UInt64, uint64_t)

/* single bytes: no byte order */
#define DV_BYTE_INT(Name, T) \
	PHP_METHOD(Buffers_DataView, get##Name) { \
		zend_long off; uint8_t *p; \
		ZEND_PARSE_PARAMETERS_START(1, 1) Z_PARAM_LONG(off) ZEND_PARSE_PARAMETERS_END(); \
		if (!(p = dv_at(DV, off, 1, false))) RETURN_THROWS(); \
		RETURN_LONG((zend_long)(T)*p); } \
	PHP_METHOD(Buffers_DataView, set##Name) { \
		zend_long off, val; uint8_t *p; \
		ZEND_PARSE_PARAMETERS_START(2, 2) Z_PARAM_LONG(off) Z_PARAM_LONG(val) ZEND_PARSE_PARAMETERS_END(); \
		if (!(p = dv_at(DV, off, 1, true))) RETURN_THROWS(); \
		*p = (uint8_t)val; }
DV_BYTE_INT(Int8, int8_t) DV_BYTE_INT(UInt8, uint8_t)

/* floats: T is the stored bits, DEC/ENC convert between them and a double (rounding as Tensor element writes do) */
#define DV_FLT(Name, T, DEC, ENC) \
	PHP_METHOD(Buffers_DataView, get##Name) { \
		zend_long off; bool be = false; uint8_t *p; T v; \
		ZEND_PARSE_PARAMETERS_START(1, 2) Z_PARAM_LONG(off) Z_PARAM_OPTIONAL Z_PARAM_BOOL(be) ZEND_PARSE_PARAMETERS_END(); \
		if (!(p = dv_at(DV, off, sizeof(T), false))) RETURN_THROWS(); \
		dv_load(&v, p, sizeof(T), be); \
		RETURN_DOUBLE(DEC); } \
	PHP_METHOD(Buffers_DataView, set##Name) { \
		zend_long off; double d; bool be = false; uint8_t *p; T v; \
		ZEND_PARSE_PARAMETERS_START(2, 3) Z_PARAM_LONG(off) Z_PARAM_DOUBLE(d) Z_PARAM_OPTIONAL Z_PARAM_BOOL(be) ZEND_PARSE_PARAMETERS_END(); \
		if (!(p = dv_at(DV, off, sizeof(T), true))) RETURN_THROWS(); \
		v = ENC; \
		dv_store(p, &v, sizeof(T), be); }
DV_FLT(Float16, uint16_t, buf_dec_FLOAT16(v), buf_enc_FLOAT16(d))
DV_FLT(BFloat16, uint16_t, buf_dec_BFLOAT16(v), buf_enc_BFLOAT16(d))
DV_FLT(Float32, float, (double)v, (float)d)
DV_FLT(Float64, double, v, d)

/* the one-byte float formats */
#define DV_F8(Name, ID) \
	PHP_METHOD(Buffers_DataView, get##Name) { \
		zend_long off; uint8_t *p; \
		ZEND_PARSE_PARAMETERS_START(1, 1) Z_PARAM_LONG(off) ZEND_PARSE_PARAMETERS_END(); \
		if (!(p = dv_at(DV, off, 1, false))) RETURN_THROWS(); \
		RETURN_DOUBLE(buf_tab[BUF_T_##ID - BUF_T_F8E3M4][*p]); } \
	PHP_METHOD(Buffers_DataView, set##Name) { \
		zend_long off; double d; uint8_t *p; \
		ZEND_PARSE_PARAMETERS_START(2, 2) Z_PARAM_LONG(off) Z_PARAM_DOUBLE(d) ZEND_PARSE_PARAMETERS_END(); \
		if (!(p = dv_at(DV, off, 1, true))) RETURN_THROWS(); \
		*p = buf_enc_##ID(d); }
DV_F8(Float8E3M4, F8E3M4) DV_F8(Float8E4M3, F8E4M3) DV_F8(Float8E4M3B11FNUZ, F8E4M3B11FNUZ) DV_F8(Float8E4M3FN, F8E4M3FN)
DV_F8(Float8E4M3FNUZ, F8E4M3FNUZ) DV_F8(Float8E5M2, F8E5M2) DV_F8(Float8E5M2FNUZ, F8E5M2FNUZ) DV_F8(Float8E8M0FNU, F8E8M0FNU)

/* ---- bytes: getBytes, setBytes, fill, copyBytes ---------------------------------------------------------------------------- */

/* the address of bytes $offset .. $offset + $length of the view, checked (argument numbers for the messages); NULL after throwing.
 * An empty range at the end of the view ($offset == byteLength, $length 0) is valid. */
static uint8_t *dv_span(buf_dataview *d, zend_long offset, uint32_t offset_arg, zend_long length, uint32_t length_arg, bool write)
{
	buf_buffer *b;

	if (UNEXPECTED(!d->buf)) {
		zend_throw_error(NULL, "The DataView is not constructed");
		return NULL;
	}
	if (offset < 0 || (uint64_t)offset > d->len) {
		zend_argument_value_error(offset_arg, "must be between 0 and %" PRIu64 " (the byte length of the view)", d->len);
		return NULL;
	}
	if (length < 0 || (uint64_t)length > d->len - (uint64_t)offset) {
		zend_argument_value_error(length_arg, "must be between 0 and %" PRIu64 " (the bytes from the offset to the end of the view)", d->len - (uint64_t)offset);
		return NULL;
	}
	b = buf_buffer_from(d->buf);
	if (write && !b->writable) {
		zend_throw_error(NULL, "Cannot write to a read-only Buffer");
		return NULL;
	}
	if (UNEXPECTED(!b->host)) {
		zend_string *n = buf_device_name(b->dev_type, b->dev_id);

		zend_throw_error(NULL, "Cannot access the bytes of a buffer on device %s: its memory is not host accessible", ZSTR_VAL(n));
		zend_string_release(n);
		return NULL;
	}
	if (UNEXPECTED(b->pending) && buf_buffer_make_ready(b) == FAILURE) {
		return NULL;
	}
	return b->base + d->off + (uint64_t)offset;
}

PHP_METHOD(Buffers_DataView, getBytes)
{
	zend_long off, len;
	uint8_t *p;

	ZEND_PARSE_PARAMETERS_START(2, 2) Z_PARAM_LONG(off) Z_PARAM_LONG(len) ZEND_PARSE_PARAMETERS_END();
	if (!(p = dv_span(DV, off, 1, len, 2, false))) RETURN_THROWS();
	if (len == 0) RETURN_EMPTY_STRING();
	RETURN_STRINGL((const char *)p, (size_t)len);
}

PHP_METHOD(Buffers_DataView, setBytes)
{
	zend_long off;
	zend_string *s;
	uint8_t *p;

	ZEND_PARSE_PARAMETERS_START(2, 2) Z_PARAM_LONG(off) Z_PARAM_STR(s) ZEND_PARSE_PARAMETERS_END();
	if (ZSTR_LEN(s) > (size_t)ZEND_LONG_MAX) {
		zend_argument_value_error(2, "is longer than the view");
		RETURN_THROWS();
	}
	if (!(p = dv_span(DV, off, 1, (zend_long)ZSTR_LEN(s), 2, true))) RETURN_THROWS();
	memcpy(p, ZSTR_VAL(s), ZSTR_LEN(s));
}

PHP_METHOD(Buffers_DataView, fill)
{
	zend_long off, len, byte;
	uint8_t *p;

	ZEND_PARSE_PARAMETERS_START(3, 3) Z_PARAM_LONG(off) Z_PARAM_LONG(len) Z_PARAM_LONG(byte) ZEND_PARSE_PARAMETERS_END();
	if (!(p = dv_span(DV, off, 1, len, 2, true))) RETURN_THROWS();
	memset(p, (int)(uint8_t)byte, (size_t)len);
}

PHP_METHOD(Buffers_DataView, copyBytes)
{
	zend_long dst, src, len;
	uint8_t *pd, *ps;

	ZEND_PARSE_PARAMETERS_START(3, 3) Z_PARAM_LONG(dst) Z_PARAM_LONG(src) Z_PARAM_LONG(len) ZEND_PARSE_PARAMETERS_END();
	if (!(ps = dv_span(DV, src, 2, len, 3, false))) RETURN_THROWS();
	if (!(pd = dv_span(DV, dst, 1, len, 3, true))) RETURN_THROWS();
	memmove(pd, ps, (size_t)len);
}

/* ---- DataViewAccessor: $a[$byteOffset] for one type, little-endian -------------------------------------------------------
 *
 * The index is a byte offset, so the read handler is Tensor's shortest one with an element size of 1: one type check, one
 * unsigned bounds check, a load. Every type has its own handlers (no switch on the type per access). The accessor caches
 * the address of the DataView's first byte, like a Tensor caches its data pointer; a buffer that is not host accessible
 * or has a readiness hook ("slow") is checked on every access, as Tensor does. */

static zend_object_handlers dva_handlers[DVA_N];

typedef struct {
	zend_object *buf;      /* owned: keeps the memory alive */
	uint8_t *data;         /* byte 0 of the DataView */
	zend_ulong n;          /* number of valid indexes: byteLength - size + 1, or 0 */
	uint8_t type;          /* DVA_* */
	bool writable, slow;
	zend_object std;
} buf_dva;

static inline buf_dva *dva_from(zend_object *o) { return (buf_dva *)((char *)o - XtOffsetOf(buf_dva, std)); }

static const struct { int dt; int size; } dva_info[DVA_N] = {
#define X(name, ID, size) { BUF_T_##ID, size },
	DVA_TYPES(X)
#undef X
};

static zend_never_inline zval *dva_bad_offset(zval *off, int type, zval *rv)
{
	if (type == BP_VAR_IS) {
		ZVAL_NULL(rv);
		return rv;
	}
	if (!off) {
		zend_throw_error(NULL, "Cannot append to a DataViewAccessor");
	} else {
		zend_type_error("DataViewAccessor offset must be of type int, %s given", zend_zval_type_name(off));
	}
	return NULL;
}

static zend_never_inline zval *dva_out_of_range(buf_dva *a, zend_long i, int type, zval *rv)
{
	if (type == BP_VAR_IS) {
		ZVAL_NULL(rv);
		return rv;
	}
	if (a->n == 0) {
		zend_value_error("Byte offset " ZEND_LONG_FMT " is out of range: the DataView is shorter than one %s", i, buf_dtypes[dva_info[a->type].dt].name);
	} else {
		zend_value_error("Byte offset " ZEND_LONG_FMT " is out of range: a %s must start between 0 and " ZEND_ULONG_FMT, i,
			buf_dtypes[dva_info[a->type].dt].name, a->n - 1);
	}
	return NULL;
}

static zend_never_inline void dva_bad_write(buf_dva *a, zval *off, zval *val)
{
	if (!off || Z_TYPE_P(off) != IS_LONG) {
		dva_bad_offset(off, BP_VAR_W, NULL);
	} else if ((zend_ulong)Z_LVAL_P(off) >= a->n) {
		dva_out_of_range(a, Z_LVAL_P(off), BP_VAR_W, NULL);
	} else if (!a->writable) {
		zend_throw_error(NULL, "Cannot write to a read-only Buffer");
	} else {
		zend_type_error("Cannot write %s to a DataViewAccessor of type %s", zend_zval_type_name(val), buf_dtypes[dva_info[a->type].dt].name);
	}
}

/* the checks of a slow buffer (not host accessible, or a readiness hook); false after throwing */
static zend_never_inline bool dva_check(buf_dva *a)
{
	buf_buffer *b = buf_buffer_from(a->buf);

	if (!b->host) {
		zend_string *n = buf_device_name(b->dev_type, b->dev_id);

		zend_throw_error(NULL, "Cannot access the bytes of a buffer on device %s: its memory is not host accessible", ZSTR_VAL(n));
		zend_string_release(n);
		return false;
	}
	return !b->pending || buf_buffer_make_ready(b) == SUCCESS;
}

/* value conversions of Tensor element writes: integers take an int (or a float, truncated), floats an int or a float */
static zend_always_inline bool dva_lval(zval *v, zend_long *l)
{
	if (EXPECTED(Z_TYPE_P(v) == IS_LONG)) { *l = Z_LVAL_P(v); return true; }
	if (Z_TYPE_P(v) == IS_DOUBLE) { *l = buf_dval_to_lval(Z_DVAL_P(v)); return true; }
	return false;
}

static zend_always_inline bool dva_dbl(zval *v, double *d)
{
	if (EXPECTED(Z_TYPE_P(v) == IS_DOUBLE)) { *d = Z_DVAL_P(v); return true; }
	if (Z_TYPE_P(v) == IS_LONG) { *d = (double)Z_LVAL_P(v); return true; }
	return false;
}

/* ld_ID(p, zval *) reads, st_ID(p, zval *) writes and returns false if the value has the wrong type */
#define DVA_INT(ID, T) \
	static zend_always_inline void dva_ld_##ID(const uint8_t *p, zval *z) { T v; dv_load(&v, p, sizeof v, false); ZVAL_LONG(z, (zend_long)v); } \
	static zend_always_inline bool dva_st_##ID(uint8_t *p, zval *z) { zend_long l; T v; if (!dva_lval(z, &l)) return false; v = (T)l; dv_store(p, &v, sizeof v, false); return true; }
DVA_INT(INT8, int8_t) DVA_INT(UINT8, uint8_t) DVA_INT(INT16, int16_t) DVA_INT(UINT16, uint16_t)
DVA_INT(INT32, int32_t) DVA_INT(UINT32, uint32_t) DVA_INT(INT64, int64_t) DVA_INT(UINT64, uint64_t)

#define DVA_FLT(ID, T, DEC, ENC) \
	static zend_always_inline void dva_ld_##ID(const uint8_t *p, zval *z) { T v; dv_load(&v, p, sizeof v, false); ZVAL_DOUBLE(z, DEC); } \
	static zend_always_inline bool dva_st_##ID(uint8_t *p, zval *z) { double d; T v; if (!dva_dbl(z, &d)) return false; v = ENC; dv_store(p, &v, sizeof v, false); return true; }
DVA_FLT(FLOAT16, uint16_t, buf_dec_FLOAT16(v), buf_enc_FLOAT16(d))
DVA_FLT(BFLOAT16, uint16_t, buf_dec_BFLOAT16(v), buf_enc_BFLOAT16(d))
DVA_FLT(FLOAT32, float, (double)v, (float)d)
DVA_FLT(FLOAT64, double, v, d)

#define X(name, ID, size) \
static zval *dva_read_##ID(zend_object *o, zval *off, int type, zval *rv) \
{ \
	buf_dva *a = dva_from(o); \
	zend_ulong i; \
	if (UNEXPECTED(!off || Z_TYPE_P(off) != IS_LONG)) return dva_bad_offset(off, type, rv); \
	i = (zend_ulong)Z_LVAL_P(off); \
	if (UNEXPECTED(i >= a->n)) return dva_out_of_range(a, Z_LVAL_P(off), type, rv); \
	if (UNEXPECTED(a->slow) && !dva_check(a)) return NULL; \
	dva_ld_##ID(a->data + i, rv); \
	return rv; \
} \
static void dva_write_##ID(zend_object *o, zval *off, zval *val) \
{ \
	buf_dva *a = dva_from(o); \
	zend_ulong i; \
	if (UNEXPECTED(!off || Z_TYPE_P(off) != IS_LONG)) { dva_bad_write(a, off, val); return; } \
	i = (zend_ulong)Z_LVAL_P(off); \
	ZVAL_DEREF(val); \
	if (UNEXPECTED(i >= a->n || !a->writable)) { dva_bad_write(a, off, val); return; } \
	if (UNEXPECTED(a->slow) && !dva_check(a)) return; \
	if (UNEXPECTED(!dva_st_##ID(a->data + i, val))) dva_bad_write(a, off, val); \
}
DVA_TYPES(X)
#undef X

static int dva_has_dimension(zend_object *o, zval *off, int check_empty)
{
	buf_dva *a = dva_from(o);
	zval tmp;
	int r;

	ZVAL_DEREF(off);
	if (Z_TYPE_P(off) != IS_LONG || (zend_ulong)Z_LVAL_P(off) >= a->n) return 0;
	if (!check_empty) return 1;
	if (!o->handlers->read_dimension(o, off, BP_VAR_IS, &tmp)) return 0;
	r = zend_is_true(&tmp);
	zval_ptr_dtor(&tmp);
	return r;
}

static void dva_unset_dimension(zend_object *o, zval *off)
{
	zend_throw_error(NULL, "Cannot unset bytes of a DataViewAccessor");
}

static zend_function *dva_no_constructor(zend_object *o)
{
	zend_throw_error(NULL, "DataViewAccessor cannot be instantiated; read a DataView property such as $dataView->uint32");
	return NULL;
}

static zend_object *dva_create_blank(zend_class_entry *ce)
{
	buf_dva *a = zend_object_alloc(sizeof(buf_dva), ce);

	zend_object_std_init(&a->std, ce);
	object_properties_init(&a->std, ce);
	a->std.handlers = &dva_handlers[0];
	a->buf = NULL;
	a->data = NULL;
	a->n = 0;
	a->type = 0;
	a->writable = a->slow = false;
	return &a->std;
}

static void dva_free(zend_object *o)
{
	buf_dva *a = dva_from(o);

	if (a->buf) {
		OBJ_RELEASE(a->buf);
	}
	zend_object_std_dtor(o);
}

/* the accessor of type t of the DataView into rv, created on first use; rv untouched after throwing */
static void dv_accessor(buf_dataview *d, int t, zval *rv)
{
	zend_object *o;
	buf_dva *a;
	buf_buffer *b;
	int size = dva_info[t].size;

	if (UNEXPECTED(!d->buf)) {
		zend_throw_error(NULL, "The DataView is not constructed");
		return;
	}
	if (EXPECTED(d->acc[t] != NULL)) {
		ZVAL_OBJ_COPY(rv, d->acc[t]);
		return;
	}
	if (!buf_dtype_fits_php_int(dva_info[t].dt)) {
		zend_throw_error(NULL, "%s is not supported on 32-bit PHP: its values do not fit a PHP int", buf_dtypes[dva_info[t].dt].name);
		return;
	}
	b = buf_buffer_from(d->buf);
	o = dva_create_blank(buf_ce_dataview_accessor);
	a = dva_from(o);
	o->handlers = &dva_handlers[t];
	GC_ADDREF(d->buf);
	a->buf = d->buf;
	a->data = b->base ? b->base + d->off : NULL;
	a->n = d->len >= (uint64_t)size ? (zend_ulong)(d->len - (uint64_t)size + 1) : 0;
	a->type = (uint8_t)t;
	a->writable = b->writable;
	a->slow = b->slow;
	d->acc[t] = o;
	ZVAL_OBJ_COPY(rv, o);
}

PHP_METHOD(Buffers_DataViewAccessor, offsetExists)
{
	zval *off;
	zend_object *o = Z_OBJ_P(ZEND_THIS);

	ZEND_PARSE_PARAMETERS_START(1, 1) Z_PARAM_ZVAL(off) ZEND_PARSE_PARAMETERS_END();
	RETURN_BOOL(o->handlers->has_dimension(o, off, 0));
}

PHP_METHOD(Buffers_DataViewAccessor, offsetGet)
{
	zval *off;
	zend_object *o = Z_OBJ_P(ZEND_THIS);

	ZEND_PARSE_PARAMETERS_START(1, 1) Z_PARAM_ZVAL(off) ZEND_PARSE_PARAMETERS_END();
	if (!o->handlers->read_dimension(o, off, BP_VAR_R, return_value)) RETURN_THROWS();
}

PHP_METHOD(Buffers_DataViewAccessor, offsetSet)
{
	zval *off, *val;
	zend_object *o = Z_OBJ_P(ZEND_THIS);

	ZEND_PARSE_PARAMETERS_START(2, 2) Z_PARAM_ZVAL(off) Z_PARAM_ZVAL(val) ZEND_PARSE_PARAMETERS_END();
	o->handlers->write_dimension(o, Z_TYPE_P(off) == IS_NULL ? NULL : off, val);
}

PHP_METHOD(Buffers_DataViewAccessor, offsetUnset)
{
	zval *off;
	zend_object *o = Z_OBJ_P(ZEND_THIS);

	ZEND_PARSE_PARAMETERS_START(1, 1) Z_PARAM_ZVAL(off) ZEND_PARSE_PARAMETERS_END();
	o->handlers->unset_dimension(o, off);
}

/* ---- properties: buffer, byteOffset, byteLength ---- */

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(ai_dv_buffer, 0, 0, Buffers\\Buffer, 0)
ZEND_END_ARG_INFO()
ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(ai_dv_int, 0, 0, IS_LONG, 0)
ZEND_END_ARG_INFO()

static bool dv_constructed(buf_dataview *d)
{
	if (!d->buf) zend_throw_error(NULL, "The DataView is not constructed");
	return d->buf != NULL;
}

static ZEND_NAMED_FUNCTION(hook_dv_buffer)
{
	ZEND_PARSE_PARAMETERS_NONE();
	if (!dv_constructed(DV)) RETURN_THROWS();
	RETURN_OBJ_COPY(DV->buf);
}

static ZEND_NAMED_FUNCTION(hook_dv_offset)
{
	ZEND_PARSE_PARAMETERS_NONE();
	if (!dv_constructed(DV)) RETURN_THROWS();
	RETURN_LONG((zend_long)DV->off);
}

static ZEND_NAMED_FUNCTION(hook_dv_length)
{
	ZEND_PARSE_PARAMETERS_NONE();
	if (!dv_constructed(DV)) RETURN_THROWS();
	RETURN_LONG((zend_long)DV->len);
}

/* $d->int8 ... $d->float64: the accessor of that type */
ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(ai_dv_accessor, 0, 0, Buffers\\DataViewAccessor, 0)
ZEND_END_ARG_INFO()
#define X(name, ID, size) static ZEND_NAMED_FUNCTION(hook_dv_##name) { ZEND_PARSE_PARAMETERS_NONE(); dv_accessor(DV, DVA_##ID, return_value); }
DVA_TYPES(X)
#undef X

/* DataViewAccessor::$dtype */
ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(ai_dva_dtype, 0, 0, Buffers\\DType, 0)
ZEND_END_ARG_INFO()
static ZEND_NAMED_FUNCTION(hook_dva_dtype)
{
	ZEND_PARSE_PARAMETERS_NONE();
	buf_dtype_to_zval(dva_info[dva_from(Z_OBJ_P(ZEND_THIS))->type].dt, return_value);
}

static zend_type dv_class_type(const char *name)
{
	return (zend_type) ZEND_TYPE_INIT_CLASS(zend_string_init(name, strlen(name), 1), 0, 0);
}

void buf_dataview_minit(void)
{
	zend_class_entry ce;
	const buf_prop_def props[] = {
		{ "buffer", dv_class_type("Buffers\\Buffer"), hook_dv_buffer, ai_dv_buffer, 0 },
		{ "byteOffset", (zend_type) ZEND_TYPE_INIT_MASK(MAY_BE_LONG), hook_dv_offset, ai_dv_int, 0 },
		{ "byteLength", (zend_type) ZEND_TYPE_INIT_MASK(MAY_BE_LONG), hook_dv_length, ai_dv_int, 0 },
#define X(name, ID, size) { #name, dv_class_type("Buffers\\DataViewAccessor"), hook_dv_##name, ai_dv_accessor, 0 },
		DVA_TYPES(X)
#undef X
	};
	const buf_prop_def acc_props[] = {
		{ "dtype", dv_class_type("Buffers\\DType"), hook_dva_dtype, ai_dva_dtype, 0 },
	};

	INIT_CLASS_ENTRY(ce, "Buffers\\DataView", class_Buffers_DataView_methods);
	buf_ce_dataview = zend_register_internal_class_ex(&ce, NULL);
	buf_ce_dataview->ce_flags |= ZEND_ACC_FINAL | ZEND_ACC_NO_DYNAMIC_PROPERTIES | ZEND_ACC_NOT_SERIALIZABLE;
	buf_ce_dataview->create_object = dataview_create;
	buf_declare_hooked_props(buf_ce_dataview, props, sizeof props / sizeof props[0]);
	memcpy(&dataview_handlers, &std_object_handlers, sizeof dataview_handlers);
	dataview_handlers.offset = XtOffsetOf(buf_dataview, std);
	dataview_handlers.free_obj = dataview_free;
	dataview_handlers.clone_obj = NULL;
	dataview_handlers.compare = buf_compare_identity;

	INIT_CLASS_ENTRY(ce, "Buffers\\DataViewAccessor", class_Buffers_DataViewAccessor_methods);
	buf_ce_dataview_accessor = zend_register_internal_class_ex(&ce, NULL);
	buf_ce_dataview_accessor->ce_flags |= ZEND_ACC_FINAL | ZEND_ACC_NO_DYNAMIC_PROPERTIES | ZEND_ACC_NOT_SERIALIZABLE;
	buf_ce_dataview_accessor->create_object = dva_create_blank; /* real instances come from dv_accessor() */
	buf_declare_hooked_props(buf_ce_dataview_accessor, acc_props, sizeof acc_props / sizeof acc_props[0]);
	zend_class_implements(buf_ce_dataview_accessor, 1, zend_ce_arrayaccess);
	for (int t = 0; t < DVA_N; t++) {
		zend_object_handlers *h = &dva_handlers[t];

		memcpy(h, &std_object_handlers, sizeof *h);
		h->offset = XtOffsetOf(buf_dva, std);
		h->free_obj = dva_free;
		h->clone_obj = NULL;
		h->compare = buf_compare_identity;
		h->get_constructor = dva_no_constructor;
		h->has_dimension = dva_has_dimension;
		h->unset_dimension = dva_unset_dimension;
	}
#define X(name, ID, size) dva_handlers[DVA_##ID].read_dimension = dva_read_##ID; dva_handlers[DVA_##ID].write_dimension = dva_write_##ID;
	DVA_TYPES(X)
#undef X
}
