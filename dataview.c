/* DataView: single values at any byte offset of a Buffer, like JavaScript's DataView over an ArrayBuffer. Little-endian by default
 * (JavaScript defaults to big-endian); every multi-byte accessor takes $bigEndian. Values are loaded and stored with memcpy, so any
 * offset works. Element conversion is the one of Tensor element access (same codecs). */
#include "php_buffers.h"
#include "codec.h"
#include "zend_exceptions.h"
#include "buffers_arginfo.h"

extern double buf_tab[][256];

zend_class_entry *buf_ce_dataview;
static zend_object_handlers dataview_handlers;

typedef struct {
	zend_object *buf;   /* owned; NULL until the constructor ran */
	uint64_t off, len;  /* the window of the buffer, in bytes */
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
	return &d->std;
}

static void dataview_free(zend_object *o)
{
	buf_dataview *d = dv_from(o);

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

void buf_dataview_minit(void)
{
	zend_class_entry ce;
	const buf_prop_def props[] = {
		{ "buffer", (zend_type) ZEND_TYPE_INIT_CLASS(zend_string_init("Buffers\\Buffer", sizeof("Buffers\\Buffer") - 1, 1), 0, 0), hook_dv_buffer, ai_dv_buffer, 0 },
		{ "byteOffset", (zend_type) ZEND_TYPE_INIT_MASK(MAY_BE_LONG), hook_dv_offset, ai_dv_int, 0 },
		{ "byteLength", (zend_type) ZEND_TYPE_INIT_MASK(MAY_BE_LONG), hook_dv_length, ai_dv_int, 0 },
	};

	INIT_CLASS_ENTRY(ce, "Buffers\\DataView", class_Buffers_DataView_methods);
	buf_ce_dataview = zend_register_internal_class_ex(&ce, NULL);
	buf_ce_dataview->ce_flags |= ZEND_ACC_FINAL | ZEND_ACC_NO_DYNAMIC_PROPERTIES | ZEND_ACC_NOT_SERIALIZABLE;
	buf_ce_dataview->create_object = dataview_create;
	buf_declare_hooked_props(buf_ce_dataview, props, 3);
	memcpy(&dataview_handlers, &std_object_handlers, sizeof dataview_handlers);
	dataview_handlers.offset = XtOffsetOf(buf_dataview, std);
	dataview_handlers.free_obj = dataview_free;
	dataview_handlers.clone_obj = NULL;
	dataview_handlers.compare = buf_compare_identity;
}
