/* ext-buffers C API.
 *
 * Installed with the extension (include <ext/buffers/php_buffers_api.h>). Other extensions use only
 * this header: they never see the internal layout of buffers and views, only the functions below,
 * which are exported from buffers.so. The extension must be loaded before the one that uses it
 * (zend_module_dep "buffers"), and BUFFERS_API_VERSION must be checked at MINIT.
 *
 * Conventions: a function that returns zend_result or a pointer has thrown a PHP exception when it
 * fails, unless stated otherwise. Objects are ordinary refcounted zend_objects; hold them with
 * GC_ADDREF / buffers_hold() and release them with OBJ_RELEASE / buffers_release().
 *
 * Version history: 1 = first version.
 */
#ifndef PHP_BUFFERS_API_H
#define PHP_BUFFERS_API_H

#include "php.h"
#include "dlpack.h"

#define BUFFERS_API_VERSION 1

#if defined(BUFFERS_BUILDING) && defined(__GNUC__)
# define BUFFERS_API __attribute__((visibility("default")))
#elif defined(BUFFERS_BUILDING) && defined(_WIN32)
# define BUFFERS_API __declspec(dllexport)
#elif defined(_WIN32)
# define BUFFERS_API __declspec(dllimport)
#elif defined(__GNUC__)
# define BUFFERS_API __attribute__((visibility("default")))
#else
# define BUFFERS_API
#endif

#define BUFFERS_MAX_NDIM 8

/* ---- dtypes ---------------------------------------------------------------------------- */

/* The order of the cases of the Buffers\DType enum. */
enum {
	BUFFERS_DT_BOOL, BUFFERS_DT_INT8, BUFFERS_DT_INT16, BUFFERS_DT_INT32, BUFFERS_DT_INT64,
	BUFFERS_DT_UINT8, BUFFERS_DT_UINT16, BUFFERS_DT_UINT32, BUFFERS_DT_UINT64,
	BUFFERS_DT_FLOAT16, BUFFERS_DT_FLOAT32, BUFFERS_DT_FLOAT64, BUFFERS_DT_BFLOAT16,
	BUFFERS_DT_F8E3M4, BUFFERS_DT_F8E4M3, BUFFERS_DT_F8E4M3B11FNUZ, BUFFERS_DT_F8E4M3FN,
	BUFFERS_DT_F8E4M3FNUZ, BUFFERS_DT_F8E5M2, BUFFERS_DT_F8E5M2FNUZ, BUFFERS_DT_F8E8M0FNU,
	BUFFERS_DT_F6E2M3FN, BUFFERS_DT_F6E3M2FN, BUFFERS_DT_F4E2M1FN,
	BUFFERS_DT_COMPLEX32, BUFFERS_DT_COMPLEX64, BUFFERS_DT_COMPLEX128, BUFFERS_DT_BCOMPLEX32,
	BUFFERS_DT_COUNT
};

enum { BUFFERS_KIND_BOOL, BUFFERS_KIND_INT, BUFFERS_KIND_UINT, BUFFERS_KIND_FLOAT, BUFFERS_KIND_COMPLEX };

typedef struct {
	const char *name;   /* case name, "Float8E4M3FN" */
	const char *value;  /* backing string, "float8_e4m3fn" */
	uint8_t kind;       /* BUFFERS_KIND_* */
	uint8_t bits;       /* width of one element in bits */
	uint8_t size;       /* bytes of one element; 0 for the packed sub-byte formats (float6, float4) */
	uint8_t dlpack_code;
} buffers_dtype_desc;

/* dtype is a BUFFERS_DT_* value */
BUFFERS_API const buffers_dtype_desc *buffers_dtype_describe(int dtype);
/* the Buffers\DType case object (a new reference in *rv) */
BUFFERS_API void buffers_dtype_to_zval(int dtype, zval *rv);
/* index of a Buffers\DType case object */
BUFFERS_API int buffers_dtype_from_object(zend_object *dtype_case);

/* ---- classes --------------------------------------------------------------------------- */

enum {
	BUFFERS_CE_DTYPE, BUFFERS_CE_BUFFER_INTERFACE, BUFFERS_CE_ARRAY_BUFFER, BUFFERS_CE_DLPACK_BUFFER,
	BUFFERS_CE_VIEW, BUFFERS_CE_TENSOR, BUFFERS_CE_SHAPE, BUFFERS_CE_DEVICE, BUFFERS_CE_CPU_DEVICE
};
/* The class entries, for extensions that implement Device or Tensor (usable from MINIT on). NULL if which is invalid. */
BUFFERS_API zend_class_entry *buffers_get_class(int which);

/* Makes an internal class implement Buffers\Tensor. The interface's properties are virtual and read-only
 * (`$dtype` ?DType, `$shape` Shape, `$ndim` int); the class declares them with these three get hooks, which
 * are internal functions called with the object as $this (ZEND_THIS) and returning the value, so the
 * object stores nothing for them. Call it from MINIT, once per class, before the class is used. Userland
 * classes cannot implement Tensor. */
BUFFERS_API void buffers_implement_tensor(zend_class_entry *ce,
	void (*get_dtype)(INTERNAL_FUNCTION_PARAMETERS),
	void (*get_shape)(INTERNAL_FUNCTION_PARAMETERS),
	void (*get_ndim)(INTERNAL_FUNCTION_PARAMETERS));

/* ---- devices --------------------------------------------------------------------------- */

/* The CpuDevice singleton of this request (borrowed: not owned by the caller) */
BUFFERS_API zend_object *buffers_cpu_device(void);

/* ---- buffers --------------------------------------------------------------------------- */

typedef struct {
	uint8_t *base;           /* start of the memory (an opaque handle if !host_accessible) */
	size_t length;           /* bytes */
	bool writable;
	bool host_accessible;
	bool pending;            /* work that produces the contents is not finished */
	zend_object *device;     /* borrowed; NULL means the CPU */
	int device_type;         /* DLPack device type and id */
	int device_id;
} buffers_buffer_info;

/* true for ArrayBuffer and DLPackBuffer objects (the only objects the functions below accept) */
BUFFERS_API bool buffers_is_buffer(const zend_object *o);
BUFFERS_API zend_result buffers_buffer_get_info(zend_object *buffer, buffers_buffer_info *out);

/* A DLPackBuffer over memory the extension owns. `deleter(ctx)` runs exactly once, when the buffer is
 * freed (never earlier), possibly during request shutdown. `device` is a Buffers\Device object or NULL
 * for the CPU; it is held until the buffer is freed. Returns a new reference. */
BUFFERS_API zend_object *buffers_buffer_create_foreign(void *data, size_t length, bool writable,
	zend_object *device, void (*deleter)(void *ctx), void *ctx);

/* A DLPackBuffer over memory owned by a PHP object (for example a Matrix that keeps its data in C):
 * the buffer holds a reference to `owner` (GC_ADDREF) and releases it (OBJ_RELEASE) when it is freed,
 * so the memory lives at least as long as any view of it. No deleter is needed. */
BUFFERS_API zend_object *buffers_buffer_create_owned(void *data, size_t length, bool writable,
	zend_object *device, zend_object *owner);

static inline void buffers_hold(zend_object *o) { GC_ADDREF(o); }
static inline void buffers_release(zend_object *o) { OBJ_RELEASE(o); }

/* Readiness. A buffer whose contents are produced asynchronously or lazily carries a callback and a
 * pending flag. While the flag is set, element reads and writes, iteration to a leaf, toString() and
 * DLPack export first call the callback; on SUCCESS the flag is cleared. The callback runs with the
 * flag cleared (so it may use the views of the buffer) and the flag is set again if it fails. It must
 * either make the contents valid and return SUCCESS, or throw and return FAILURE.
 * The callback must be set while the caller holds the only reference to the buffer (no view exists
 * yet); otherwise FAILURE is returned and nothing changes (no exception). `ctx_dtor`, if not NULL,
 * runs when the buffer is freed. */
typedef zend_result (*buffers_ready_fn)(zend_object *buffer, void *ctx);
BUFFERS_API zend_result buffers_buffer_set_ready_callback(zend_object *buffer, buffers_ready_fn fn, void *ctx, void (*ctx_dtor)(void *ctx));
BUFFERS_API void buffers_buffer_set_pending(zend_object *buffer, bool pending);
/* Runs the callback if the buffer is pending. For consumers that read the memory directly. */
BUFFERS_API zend_result buffers_buffer_ensure_ready(zend_object *buffer);

/* ---- views ----------------------------------------------------------------------------- */

typedef struct {
	uint8_t *data;                       /* address of element 0 (buffer base + byte_offset) */
	int dtype;                           /* BUFFERS_DT_* */
	int ndim;                            /* 0 to BUFFERS_MAX_NDIM */
	int64_t shape[BUFFERS_MAX_NDIM];
	int64_t strides[BUFFERS_MAX_NDIM];   /* in elements; 1 for the last axis of a packed dtype */
	int64_t byte_offset;
	zend_object *buffer;                 /* borrowed */
	bool writable;
} buffers_view_info;

BUFFERS_API bool buffers_is_view(const zend_object *o);
/* Does not wait for readiness: call buffers_buffer_ensure_ready(info.buffer) before reading the data. */
BUFFERS_API zend_result buffers_view_get_info(zend_object *view, buffers_view_info *out);

/* A BufferView over `buffer`, validated exactly like BufferInterface::view(): `strides` in elements
 * (NULL = C-contiguous), `byte_offset` in bytes, ndim 0 gives a 0-d view. Returns a new reference. */
BUFFERS_API zend_object *buffers_view_create(zend_object *buffer, int dtype, int ndim, const int64_t *shape,
	const int64_t *strides, int64_t byte_offset);

/* ---- shapes ---------------------------------------------------------------------------- */

/* A Shape from `n` zvals, each an int >= 0 or a 0-d Tensor object. Returns a new reference. */
BUFFERS_API zend_object *buffers_shape_create(uint32_t n, const zval *dims);
/* The dimensions of a Shape (borrowed) */
BUFFERS_API const zval *buffers_shape_dims(zend_object *shape, uint32_t *n);

/* ---- DLPack ---------------------------------------------------------------------------- */

/* Exports a view. The tensor is malloc'd; whoever ends up owning it calls its deleter exactly once.
 * Waits for readiness first. The view's buffer is held until the deleter runs. */
BUFFERS_API zend_result buffers_dlpack_export(zend_object *view, DLManagedTensorVersioned **out);
/* Imports a tensor of any device. On success the tensor is owned by the returned DLPackBuffer (its
 * deleter runs when that buffer is freed) and a new BufferView reference is returned; on failure an
 * exception is thrown, NULL is returned and the caller still owns the tensor. */
BUFFERS_API zend_object *buffers_dlpack_import(DLManagedTensorVersioned *tensor);

/* ---- operators ------------------------------------------------------------------------- */

/* Called for an arithmetic or bitwise operator when either operand is a BufferView: opcode is one of
 * ZEND_ADD, ZEND_SUB, ZEND_MUL, ZEND_DIV, ZEND_MOD, ZEND_POW, ZEND_SL, ZEND_SR, ZEND_BW_OR, ZEND_BW_AND,
 * ZEND_BW_XOR, ZEND_BW_NOT (op2 is then unused). Like zend_object_handlers.do_operation: write the
 * result to `result` and return SUCCESS, or return FAILURE (without an exception) to leave the
 * default "Unsupported operand types" TypeError. `result` never aliases op1 or op2. There is one handler per
 * process: register it from MINIT; FAILURE if another one is already registered. Comparisons are
 * unaffected (a view is only equal to itself). */
typedef zend_result (*buffers_operator_fn)(uint8_t opcode, zval *result, zval *op1, zval *op2);
BUFFERS_API zend_result buffers_set_operator_handler(buffers_operator_fn fn);

BUFFERS_API int buffers_api_version(void);

#endif
