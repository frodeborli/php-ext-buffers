#ifndef PHP_DLPACK_H
#define PHP_DLPACK_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "dlpack.h"

extern zend_module_entry dlpack_module_entry;
#define phpext_dlpack_ptr &dlpack_module_entry
#define PHP_DLPACK_VERSION "0.1.0"

#if defined(ZTS) && defined(COMPILE_DL_DLPACK)
ZEND_TSRMLS_CACHE_EXTERN()
#endif

#define DLP_MAX_NDIM 8
#define DLP_POOL_MAX 64
#define DLP_ALIGN 64

/* kinds */
enum { DLP_BOOL, DLP_INT, DLP_UINT, DLP_FLOAT, DLP_COMPLEX };

/* X(id, CaseName, "backing", kind, bits, byteSize, dlpackCode)
 * bits is the storage width of one element (sub-byte formats are stored padded,
 * one element per byte, so byteSize is 1 for them). */
#define DLP_DTYPES(X) \
	X(BOOL,     Bool,      "bool",      DLP_BOOL,    8,   1,  kDLBool) \
	X(INT8,     Int8,      "int8",      DLP_INT,     8,   1,  kDLInt) \
	X(INT16,    Int16,     "int16",     DLP_INT,     16,  2,  kDLInt) \
	X(INT32,    Int32,     "int32",     DLP_INT,     32,  4,  kDLInt) \
	X(INT64,    Int64,     "int64",     DLP_INT,     64,  8,  kDLInt) \
	X(UINT8,    UInt8,     "uint8",     DLP_UINT,    8,   1,  kDLUInt) \
	X(UINT16,   UInt16,    "uint16",    DLP_UINT,    16,  2,  kDLUInt) \
	X(UINT32,   UInt32,    "uint32",    DLP_UINT,    32,  4,  kDLUInt) \
	X(UINT64,   UInt64,    "uint64",    DLP_UINT,    64,  8,  kDLUInt) \
	X(FLOAT16,  Float16,   "float16",   DLP_FLOAT,   16,  2,  kDLFloat) \
	X(BFLOAT16, BFloat16,  "bfloat16",  DLP_FLOAT,   16,  2,  kDLBfloat) \
	X(FLOAT32,  Float32,   "float32",   DLP_FLOAT,   32,  4,  kDLFloat) \
	X(FLOAT64,  Float64,   "float64",   DLP_FLOAT,   64,  8,  kDLFloat) \
	X(COMPLEX32,  Complex32,  "complex32",  DLP_COMPLEX, 32,  4,  kDLComplex) \
	X(BCOMPLEX32, BComplex32, "bcomplex32", DLP_COMPLEX, 32,  4,  kDLBcomplex) \
	X(COMPLEX64,  Complex64,  "complex64",  DLP_COMPLEX, 64,  8,  kDLComplex) \
	X(COMPLEX128, Complex128, "complex128", DLP_COMPLEX, 128, 16, kDLComplex) \
	X(F8E3M4,       Float8E3M4,       "float8_e3m4",       DLP_FLOAT, 8, 1, kDLFloat8_e3m4) \
	X(F8E4M3,       Float8E4M3,       "float8_e4m3",       DLP_FLOAT, 8, 1, kDLFloat8_e4m3) \
	X(F8E4M3B11FNUZ, Float8E4M3B11FNUZ, "float8_e4m3b11fnuz", DLP_FLOAT, 8, 1, kDLFloat8_e4m3b11fnuz) \
	X(F8E4M3FN,     Float8E4M3FN,     "float8_e4m3fn",     DLP_FLOAT, 8, 1, kDLFloat8_e4m3fn) \
	X(F8E4M3FNUZ,   Float8E4M3FNUZ,   "float8_e4m3fnuz",   DLP_FLOAT, 8, 1, kDLFloat8_e4m3fnuz) \
	X(F8E5M2,       Float8E5M2,       "float8_e5m2",       DLP_FLOAT, 8, 1, kDLFloat8_e5m2) \
	X(F8E5M2FNUZ,   Float8E5M2FNUZ,   "float8_e5m2fnuz",   DLP_FLOAT, 8, 1, kDLFloat8_e5m2fnuz) \
	X(F8E8M0FNU,    Float8E8M0FNU,    "float8_e8m0fnu",    DLP_FLOAT, 8, 1, kDLFloat8_e8m0fnu) \
	X(F6E2M3FN,     Float6E2M3FN,     "float6_e2m3fn",     DLP_FLOAT, 6, 1, kDLFloat6_e2m3fn) \
	X(F6E3M2FN,     Float6E3M2FN,     "float6_e3m2fn",     DLP_FLOAT, 6, 1, kDLFloat6_e3m2fn) \
	X(F4E2M1FN,     Float4E2M1FN,     "float4_e2m1fn",     DLP_FLOAT, 4, 1, kDLFloat4_e2m1fn)

enum {
#define X(id, ...) DLP_T_##id,
	DLP_DTYPES(X)
#undef X
	DLP_NTYPES
};

typedef struct { const char *name, *value; uint8_t kind, bits, size, code; } dlp_dtype_info;
extern const dlp_dtype_info dlp_dtypes[DLP_NTYPES];

/* Memory owner: ArrayBuffer (alloc != NULL) or DLPackBuffer (managed != NULL). */
typedef struct {
	uint8_t *base;
	size_t len;
	void *alloc;
	DLManagedTensorVersioned *managed;
	bool writable;
	zend_object std;
} dlp_buffer;

typedef struct {
	uint8_t *data;       /* element 0 of this view */
	zend_object *buf;    /* owned reference to the buffer */
	int64_t shape[DLP_MAX_NDIM];
	int64_t sbytes[DLP_MAX_NDIM]; /* strides in bytes; API strides are sbytes / size */
	uint8_t dt, ndim, writable;
	zend_object std;
} dlp_view;

typedef struct {
	DLManagedTensorVersioned *t;
	zend_object std;
} dlp_capsule;

ZEND_BEGIN_MODULE_GLOBALS(dlpack)
	zend_object *pool[DLP_POOL_MAX];
	int npool;
	bool request_active;
ZEND_END_MODULE_GLOBALS(dlpack)

ZEND_EXTERN_MODULE_GLOBALS(dlpack)
#ifdef ZTS
#define DLPG(v) ZEND_MODULE_GLOBALS_ACCESSOR(dlpack, v)
#else
#define DLPG(v) (dlpack_globals.v)
#endif

extern zend_class_entry *dlp_ce_dtype, *dlp_ce_buffer_if, *dlp_ce_arraybuffer,
	*dlp_ce_dlpackbuffer, *dlp_ce_view, *dlp_ce_capsule;

static inline dlp_buffer *dlp_buffer_from(zend_object *o) { return (dlp_buffer *)((char *)o - XtOffsetOf(dlp_buffer, std)); }
static inline dlp_view *dlp_view_from(zend_object *o) { return (dlp_view *)((char *)o - XtOffsetOf(dlp_view, std)); }
static inline dlp_capsule *dlp_capsule_from(zend_object *o) { return (dlp_capsule *)((char *)o - XtOffsetOf(dlp_capsule, std)); }

/* r = a * b for non-negative a and b; true on overflow */
static inline bool dlp_mul_ovf(int64_t a, int64_t b, int64_t *r)
{
	if (a != 0 && b > INT64_MAX / a) return true;
	*r = a * b;
	return false;
}

/* dtype.c */
void dlp_dtype_minit(void);
void dlp_dtype_mshutdown(void);
void dlp_codec_init(void);
int dlp_dtype_from_object(zend_object *o);          /* index, or -1 after throwing */
void dlp_dtype_to_zval(int dt, zval *rv);

/* buffer.c */
void dlp_buffer_minit(void);
zend_object *dlp_foreign_buffer_new(DLManagedTensorVersioned *t, uint8_t *base, size_t len);

/* view.c */
void dlp_view_minit(void);
void dlp_view_rshutdown(void);
zend_object *dlp_view_new(zend_object *buf, int dt, int ndim, const int64_t *shape, const int64_t *sbytes, uint8_t *data, bool writable);
void dlp_buffer_view_impl(zend_object *buf, INTERNAL_FUNCTION_PARAMETERS);

/* capsule.c */
void dlp_capsule_minit(void);
#ifdef DLPACK_TESTING
void dlp_testing_register(void);
#endif

#endif
