#ifndef PHP_BUFFERS_H
#define PHP_BUFFERS_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define BUFFERS_BUILDING 1
#include "php.h"
#include "dlpack.h"
#include "php_buffers_api.h"

extern zend_module_entry buffers_module_entry;
#define phpext_buffers_ptr &buffers_module_entry
#define PHP_BUFFERS_VERSION "0.1.0"

#if defined(ZTS) && defined(COMPILE_DL_BUFFERS)
ZEND_TSRMLS_CACHE_EXTERN()
#endif

#define BUF_MAX_NDIM BUFFERS_MAX_NDIM
#define BUF_POOL_MAX 64
#define BUF_ALIGN 64

/* kinds */
enum { BUF_BOOL = BUFFERS_KIND_BOOL, BUF_INT, BUF_UINT, BUF_FLOAT, BUF_COMPLEX };

/* X(id, CaseName, "backing", kind, bits, byteSize, dlpackCode)
 * bits is the width of one element. byteSize is 0 for the packed sub-byte formats (float6, float4):
 * their elements are packed little-bit-endian, element i at bit i*bits of the row it belongs to. */
#define BUF_DTYPES(X) \
	X(BOOL,     Bool,      "bool",      BUF_BOOL,    8,   1,  kDLBool) \
	X(INT8,     Int8,      "int8",      BUF_INT,     8,   1,  kDLInt) \
	X(INT16,    Int16,     "int16",     BUF_INT,     16,  2,  kDLInt) \
	X(INT32,    Int32,     "int32",     BUF_INT,     32,  4,  kDLInt) \
	X(INT64,    Int64,     "int64",     BUF_INT,     64,  8,  kDLInt) \
	X(UINT8,    UInt8,     "uint8",     BUF_UINT,    8,   1,  kDLUInt) \
	X(UINT16,   UInt16,    "uint16",    BUF_UINT,    16,  2,  kDLUInt) \
	X(UINT32,   UInt32,    "uint32",    BUF_UINT,    32,  4,  kDLUInt) \
	X(UINT64,   UInt64,    "uint64",    BUF_UINT,    64,  8,  kDLUInt) \
	X(FLOAT16,  Float16,   "float16",   BUF_FLOAT,   16,  2,  kDLFloat) \
	X(FLOAT32,  Float32,   "float32",   BUF_FLOAT,   32,  4,  kDLFloat) \
	X(FLOAT64,  Float64,   "float64",   BUF_FLOAT,   64,  8,  kDLFloat) \
	X(BFLOAT16, BFloat16,  "bfloat16",  BUF_FLOAT,   16,  2,  kDLBfloat) \
	X(F8E3M4,       Float8E3M4,       "float8_e3m4",       BUF_FLOAT, 8, 1, kDLFloat8_e3m4) \
	X(F8E4M3,       Float8E4M3,       "float8_e4m3",       BUF_FLOAT, 8, 1, kDLFloat8_e4m3) \
	X(F8E4M3B11FNUZ, Float8E4M3B11FNUZ, "float8_e4m3b11fnuz", BUF_FLOAT, 8, 1, kDLFloat8_e4m3b11fnuz) \
	X(F8E4M3FN,     Float8E4M3FN,     "float8_e4m3fn",     BUF_FLOAT, 8, 1, kDLFloat8_e4m3fn) \
	X(F8E4M3FNUZ,   Float8E4M3FNUZ,   "float8_e4m3fnuz",   BUF_FLOAT, 8, 1, kDLFloat8_e4m3fnuz) \
	X(F8E5M2,       Float8E5M2,       "float8_e5m2",       BUF_FLOAT, 8, 1, kDLFloat8_e5m2) \
	X(F8E5M2FNUZ,   Float8E5M2FNUZ,   "float8_e5m2fnuz",   BUF_FLOAT, 8, 1, kDLFloat8_e5m2fnuz) \
	X(F8E8M0FNU,    Float8E8M0FNU,    "float8_e8m0fnu",    BUF_FLOAT, 8, 1, kDLFloat8_e8m0fnu) \
	X(F6E2M3FN,     Float6E2M3FN,     "float6_e2m3fn",     BUF_FLOAT, 6, 0, kDLFloat6_e2m3fn) \
	X(F6E3M2FN,     Float6E3M2FN,     "float6_e3m2fn",     BUF_FLOAT, 6, 0, kDLFloat6_e3m2fn) \
	X(F4E2M1FN,     Float4E2M1FN,     "float4_e2m1fn",     BUF_FLOAT, 4, 0, kDLFloat4_e2m1fn) \
	X(COMPLEX32,  Complex32,  "complex32",  BUF_COMPLEX, 32,  4,  kDLComplex) \
	X(COMPLEX64,  Complex64,  "complex64",  BUF_COMPLEX, 64,  8,  kDLComplex) \
	X(COMPLEX128, Complex128, "complex128", BUF_COMPLEX, 128, 16, kDLComplex) \
	X(BCOMPLEX32, BComplex32, "bcomplex32", BUF_COMPLEX, 32,  4,  kDLBcomplex)

enum {
#define X(id, ...) BUF_T_##id,
	BUF_DTYPES(X)
#undef X
	BUF_NTYPES
};

#define X(id, ...) _Static_assert((int)BUF_T_##id == (int)BUFFERS_DT_##id, "dtype order differs from php_buffers_api.h");
BUF_DTYPES(X)
#undef X
_Static_assert((int)BUF_NTYPES == (int)BUFFERS_DT_COUNT, "dtype count differs from php_buffers_api.h");

typedef buffers_dtype_desc buf_dtype_info;
extern const buf_dtype_info buf_dtypes[BUF_NTYPES];

static inline bool buf_is_packed(int dt) { return buf_dtypes[dt].size == 0; }

/* Memory owner: ArrayBuffer (alloc != NULL) or DLPackBuffer (foreign memory or memory kept alive by an owner). */
typedef struct {
	uint8_t *base;
	size_t len;
	void *alloc;                         /* emalloc'd block behind an ArrayBuffer */
	void (*deleter)(void *ctx);          /* foreign memory: runs once when the buffer is freed */
	void *deleter_ctx;
	zend_object *owner;                  /* held until the buffer is freed */
	zend_object *device;                 /* held; NULL = CPU */
	buffers_ready_fn ready;              /* readiness hook, see php_buffers_api.h */
	void *ready_ctx;
	void (*ready_dtor)(void *ctx);
	int32_t dev_type, dev_id;
	bool writable, host, pending;
	bool slow;                           /* element access needs a check: !host || ready != NULL */
	zend_object std;
} buf_buffer;

typedef struct {
	uint8_t *data;       /* element 0 of this view */
	zend_object *buf;    /* owned reference to the buffer */
	int64_t shape[BUF_MAX_NDIM];
	/* strides in bytes; API strides are sbytes / size. A packed dtype has no byte stride on its last
	 * axis (elements are bits apart): that entry is 0. */
	int64_t sbytes[BUF_MAX_NDIM];
	uint8_t dt, ndim, writable, slow;
	zend_object std;
} buf_view;

typedef struct {
	DLManagedTensorVersioned *t;
	zend_object std;
} buf_capsule;

/* Shape: n dimensions, each an int or a 0-d Tensor object */
typedef struct {
	uint32_t n;
	zval *dims;
	zend_object std;
} buf_shape;

/* CpuDevice and ForeignDevice */
typedef struct {
	int32_t type, id;
	zend_object std;
} buf_device;

ZEND_BEGIN_MODULE_GLOBALS(buffers)
	zend_object *pool[BUF_POOL_MAX];
	int npool;
	bool request_active;
	zend_object *cpu_device;
ZEND_END_MODULE_GLOBALS(buffers)

ZEND_EXTERN_MODULE_GLOBALS(buffers)
#ifdef ZTS
#define BUFG(v) ZEND_MODULE_GLOBALS_ACCESSOR(buffers, v)
#else
#define BUFG(v) (buffers_globals.v)
#endif

extern zend_class_entry *buf_ce_dtype, *buf_ce_buffer_if, *buf_ce_arraybuffer, *buf_ce_dlpackbuffer,
	*buf_ce_view, *buf_ce_capsule, *buf_ce_tensor, *buf_ce_shape, *buf_ce_device_if, *buf_ce_cpudevice,
	*buf_ce_foreigndevice;

static inline buf_buffer *buf_buffer_from(zend_object *o) { return (buf_buffer *)((char *)o - XtOffsetOf(buf_buffer, std)); }
static inline buf_view *buf_view_from(zend_object *o) { return (buf_view *)((char *)o - XtOffsetOf(buf_view, std)); }
static inline buf_capsule *buf_capsule_from(zend_object *o) { return (buf_capsule *)((char *)o - XtOffsetOf(buf_capsule, std)); }
static inline buf_shape *buf_shape_from(zend_object *o) { return (buf_shape *)((char *)o - XtOffsetOf(buf_shape, std)); }
static inline buf_device *buf_device_from(zend_object *o) { return (buf_device *)((char *)o - XtOffsetOf(buf_device, std)); }

/* PHP's (int) conversion of a float, without the warning PHP 8.5 added for NaN, INF and out-of-range values */
#if PHP_VERSION_ID >= 80500
#define buf_dval_to_lval zend_dval_to_lval_silent
#else
#define buf_dval_to_lval zend_dval_to_lval
#endif

/* r = a * b for non-negative a and b; true on overflow */
static inline bool buf_mul_ovf(int64_t a, int64_t b, int64_t *r)
{
	if (a != 0 && b > INT64_MAX / a) return true;
	*r = a * b;
	return false;
}

/* buffers.c: objects are equal only to themselves (the default would compare the empty property tables: any two views are "==") */
int buf_compare_identity(zval *a, zval *b);

/* dtype.c */
void buf_dtype_minit(void);
void buf_dtype_mshutdown(void);
void buf_codec_init(void);
int buf_dtype_from_object(zend_object *o);          /* index, or -1 after throwing */
void buf_dtype_to_zval(int dt, zval *rv);

/* buffer.c */
void buf_buffer_minit(void);
zend_result buf_buffer_make_ready(buf_buffer *b);   /* runs the readiness hook if pending */
/* a DLPackBuffer over foreign memory; `device` (held, may be NULL) as in the C API */
zend_object *buf_foreign_buffer_new(uint8_t *base, size_t len, bool writable, zend_object *device,
	void (*deleter)(void *), void *ctx, zend_object *owner);

/* view.c */
void buf_view_minit(void);
void buf_view_rshutdown(void);
zend_object *buf_view_new(zend_object *buf, int dt, int ndim, const int64_t *shape, const int64_t *sbytes, uint8_t *data, bool writable, bool slow);
/* strides in elements or NULL (C-contiguous). On failure returns NULL with a message in err and the
 * number of the offending argument of BufferInterface::view() in *arg (2 shape, 3 strides, 4 offset). */
zend_object *buf_view_make(zend_object *buf, int dt, int ndim, const int64_t *shape, const int64_t *strides,
	int64_t offset, char *err, size_t errlen, int *arg);
void buf_buffer_view_impl(zend_object *buf, INTERNAL_FUNCTION_PARAMETERS);
void buf_view_strides_elements(const buf_view *v, int64_t *out);

/* capsule.c */
void buf_capsule_minit(void);
zend_result buf_view_export(zend_object *view, DLManagedTensorVersioned **out);
zend_object *buf_view_import(DLManagedTensorVersioned *t);

/* shape.c */
void buf_shape_minit(void);
zend_object *buf_shape_new_ints(int n, const int64_t *dims);

/* device.c */
void buf_device_minit(void);
void buf_device_rshutdown(void);
zend_object *buf_foreign_device_new(int32_t type, int32_t id);
/* dlpackType, id and host accessibility of a Device object (NULL = CPU); false after throwing */
bool buf_device_query(zend_object *dev, int32_t *type, int32_t *id, bool *host);
const char *buf_device_type_name(int32_t type);

/* props.c: virtual properties backed by internal get hooks */
typedef struct {
	const char *name;
	zend_type type;
	void (*get)(INTERNAL_FUNCTION_PARAMETERS);
	const zend_internal_arg_info *arginfo;
	uint32_t num_args;
} buf_prop_def;
void buf_declare_hooked_props(zend_class_entry *ce, const buf_prop_def *defs, int n);
void buf_props_mshutdown(void);

/* api.c */
extern buffers_operator_fn buf_operator_handler;

#ifdef BUFFERS_TESTING
void buf_testing_register(void);
#endif

#endif
