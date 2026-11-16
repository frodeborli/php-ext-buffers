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
 * Version history: 1 = first version; 2 = buffers_convert(), buffers_declare_virtual_props().
 */
#ifndef PHP_BUFFERS_API_H
#define PHP_BUFFERS_API_H

#include "php.h"
#include "dlpack.h"

#define BUFFERS_API_VERSION 2

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
	BUFFERS_DT_BOOL, BUFFERS_DT_INT2, BUFFERS_DT_INT4, BUFFERS_DT_INT8, BUFFERS_DT_INT16, BUFFERS_DT_INT32, BUFFERS_DT_INT64,
	BUFFERS_DT_UINT2, BUFFERS_DT_UINT4, BUFFERS_DT_UINT8, BUFFERS_DT_UINT16, BUFFERS_DT_UINT32, BUFFERS_DT_UINT64,
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
	uint8_t size;       /* bytes of one element; 0 for the packed sub-byte formats (int2, int4, uint2, uint4, float6, float4) */
	uint8_t dlpack_code;
} buffers_dtype_desc;

/* dtype is a BUFFERS_DT_* value */
BUFFERS_API const buffers_dtype_desc *buffers_dtype_describe(int dtype);
/* the Buffers\DType case object (a new reference in *rv) */
BUFFERS_API void buffers_dtype_to_zval(int dtype, zval *rv);
/* index of a Buffers\DType case object */
BUFFERS_API int buffers_dtype_from_object(zend_object *dtype_case);
/* the dtype for a DLPack type code and bit width (one lane), or -1 */
BUFFERS_API int buffers_dtype_from_dlpack(int code, int bits);

/* ---- element conversion ---------------------------------------------------------------- */

/* Converts n elements of one dtype to another: element k is read at src + k * src_stride and written at dst + k * dst_stride
 * (strides in elements; a packed sub-byte dtype ignores its stride: its elements are consecutive from bit 0 of the pointer, which
 * must be the start of a row, i.e. byte aligned). Does not throw, and does not check the memory: the caller guarantees that every
 * element lies inside readable and writable memory. Every pair of dtypes is supported. Rules: a decoded value is an int64
 * (Bool, Int*), a uint64 (UInt*), a double (floats) or a pair of doubles (complex), converted to the class of the destination:
 *  - to an integer dtype: integers wrap to the width (two's complement); a float is truncated toward zero first, a complex number
 *    loses its imaginary part; out-of-range floats and NaN give an unspecified integer;
 *  - to Bool: non-zero (a complex number: either part non-zero);
 *  - to a float dtype: the value rounded to that format (ml_dtypes' rules, see the README); a complex number loses its imaginary part;
 *  - to a complex dtype: real values get an imaginary part of 0; both parts are rounded to the component format. */
BUFFERS_API void buffers_convert(int src_dtype, const uint8_t *src, int64_t src_stride,
	int dst_dtype, uint8_t *dst, int64_t dst_stride, int64_t n);

/* ---- classes --------------------------------------------------------------------------- */

enum {
	BUFFERS_CE_DTYPE, BUFFERS_CE_BUFFER, BUFFERS_CE_TENSOR, BUFFERS_CE_TENSOR_INTERFACE, BUFFERS_CE_SHAPE,
	BUFFERS_CE_DEVICE, BUFFERS_CE_CPU_DEVICE, BUFFERS_CE_FILE_DEVICE
};
/* The class entries, for extensions that implement Device or TensorInterface (usable from MINIT on). NULL if which is invalid. */
BUFFERS_API zend_class_entry *buffers_get_class(int which);

/* Makes an internal class implement Buffers\TensorInterface. The interface's properties are virtual and read-only
 * (`$dtype` ?DType, `$shape` Shape, `$ndim` int); the class declares them with these three get hooks, which
 * are internal functions called with the object as $this (ZEND_THIS) and returning the value, so the
 * object stores nothing for them. Call it from MINIT, once per class, before the class is used. Userland
 * classes cannot implement TensorInterface. */
BUFFERS_API void buffers_implement_tensor_interface(zend_class_entry *ce,
	zif_handler get_dtype,
	zif_handler get_shape,
	zif_handler get_ndim);

/* Declares read-only virtual properties on an internal class (a class that implements Buffers\TensorInterface declares its
 * three with buffers_implement_tensor_interface() instead). Like those, they are served by internal get hooks, functions called
 * with the object as $this that return the value, so the object stores nothing for them; writes fail in the engine
 * ("is read-only"). `arginfo` describes the hook: a zend_internal_arg_info array whose first entry carries the return type
 * (ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX), `type` is the declared type of the property. Call from MINIT, before the class is used.
 * On PHP 8.3 (no property hooks) the getters are served through internal __get/__isset/__set/__unset methods set on the class
 * instead, which subclasses inherit. A subclass that defines its own __get (or __set, __isset, __unset) replaces them, and the
 * properties then reach that method instead: a class that PHP code may extend should, on 8.3, serve its virtual properties from
 * its own read_property (and has_property, write_property, unset_property) handlers first and pass other names on to the std
 * handlers, which then call the user's magic methods as usual. */
typedef struct {
	const char *name;
	zend_type type;
	zif_handler get;
	const zend_internal_arg_info *arginfo;
} buffers_virtual_prop;
BUFFERS_API void buffers_declare_virtual_props(zend_class_entry *ce, const buffers_virtual_prop *props, int n);

/* ---- devices --------------------------------------------------------------------------- */

/* Makes an internal class implement Buffers\Device. The interface's properties are virtual and read-only
 * (`$name` string, `$id` int, `$hostAccessible` bool); the class declares them with these three get hooks: internal
 * functions called with the device object as $this (ZEND_THIS) that return the value. Call it from MINIT, once per class. */
BUFFERS_API void buffers_implement_device(zend_class_entry *ce, zif_handler get_name, zif_handler get_id, zif_handler get_host_accessible);

/* The CpuDevice and FileDevice singletons of this request (borrowed: not owned by the caller) */
BUFFERS_API zend_object *buffers_cpu_device(void);
BUFFERS_API zend_object *buffers_file_device(void);

/* DLPack device codes. Core devices: CpuDevice is kDLCPU/0, an imported tensor's ForeignDevice is its own type and id, FileDevice has none
 * (a mapped file buffer is exported as host memory, an unmapped one cannot be exported). The Device class of another extension says
 * which DLPack device type its instances are by registering it, from MINIT (the id comes from Device::id()). */
BUFFERS_API zend_result buffers_register_device_type(zend_class_entry *device_ce, int dlpack_type);
/* The DLPack device type and id of a Device object (NULL: the CPU); FAILURE, without an exception, if it has none. */
BUFFERS_API zend_result buffers_device_dlpack(zend_object *device, int *type, int *id);

/* ---- buffers --------------------------------------------------------------------------- */

typedef struct {
	uint8_t *base;           /* start of the memory (an opaque handle if !host_accessible) */
	size_t length;           /* bytes */
	bool writable;
	bool host_accessible;
	bool pending;            /* work that produces the contents is not finished */
	zend_object *device;     /* borrowed; NULL means the CPU */
	int device_type;         /* DLPack device type and id; -1 for FileDevice, -2 for a device that has no DLPack type */
	int device_id;
} buffers_buffer_info;

/* A Buffer made by Buffer::fromFile() over a mapped file: its absolute path, and where its byte 0 is in the file (true), so a consumer can refer to the bytes
 * instead of copying them. False for every other buffer (memory, foreign memory, an unmapped file). The path is valid while the buffer lives. Additive: the version number did not change (a consumer that uses it needs this build of ext-buffers). */
BUFFERS_API bool buffers_buffer_file_origin(zend_object *buffer, const char **path, uint64_t *offset);

/* true for Buffer objects (the only objects the functions below accept) */
BUFFERS_API bool buffers_is_buffer(const zend_object *o);
BUFFERS_API zend_result buffers_buffer_get_info(zend_object *buffer, buffers_buffer_info *out);

/* A Buffer over memory the extension owns. `deleter(ctx)` runs exactly once, when the buffer is
 * freed (never earlier), possibly during request shutdown. `device` is a Buffers\Device object or NULL
 * for the CPU; it is held until the buffer is freed. Returns a new reference. */
BUFFERS_API zend_object *buffers_buffer_create_foreign(void *data, size_t length, bool writable,
	zend_object *device, void (*deleter)(void *ctx), void *ctx);

/* A Buffer over memory owned by a PHP object (for example a Matrix that keeps its data in C):
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
	uint8_t *data;                       /* address of element 0 (buffer base + byte_offset); any address, see below */
	int dtype;                           /* BUFFERS_DT_* */
	int ndim;                            /* 0 to BUFFERS_MAX_NDIM */
	int64_t shape[BUFFERS_MAX_NDIM];
	int64_t strides[BUFFERS_MAX_NDIM];   /* in elements; 1 for the last axis of a packed dtype */
	int64_t byte_offset;
	zend_object *buffer;                 /* borrowed */
	bool writable;
} buffers_tensor_info;

BUFFERS_API bool buffers_is_tensor(const zend_object *o);
/* Alignment: a view may start at any byte offset (Buffer::view(), buffers_tensor_create(), a DLPack import), so `data` need not be a
 * multiple of the element size. Strides are in elements, so a tensor is aligned (every element at a multiple of its size) exactly when
 * size <= 1 || (uintptr_t)info.data % buffers_dtype_describe(info.dtype)->size == 0 (packed dtypes have size 0: always byte aligned).
 * A consumer that loads elements through typed pointers (float *, SIMD) checks that itself and refuses, copies or uses memcpy;
 * buffers_convert() works at any address.
 * Does not wait for readiness: call buffers_buffer_ensure_ready(info.buffer) before reading the data. */
BUFFERS_API zend_result buffers_tensor_get_info(zend_object *view, buffers_tensor_info *out);

/* A Tensor over `buffer`, validated exactly like Buffer::view(): `strides` in elements
 * (NULL = C-contiguous), `byte_offset` in bytes, ndim 0 gives a 0-d view. Returns a new reference. */
BUFFERS_API zend_object *buffers_tensor_create(zend_object *buffer, int dtype, int ndim, const int64_t *shape,
	const int64_t *strides, int64_t byte_offset);

/* ---- shapes ---------------------------------------------------------------------------- */

/* A Shape from `n` zvals, each an int >= 0 or a 0-d TensorInterface object. Returns a new reference. */
BUFFERS_API zend_object *buffers_shape_create(uint32_t n, const zval *dims);
/* The dimensions of a Shape (borrowed) */
BUFFERS_API const zval *buffers_shape_dims(zend_object *shape, uint32_t *n);

/* ---- transfers ------------------------------------------------------------------------- */

/* Tensor::to($device) returns the view itself if its buffer is on that device; otherwise a C-contiguous copy in a new
 * Buffer on the device. The core can copy host-accessible buffers to CpuDevice and read unmapped file buffers; for any other
 * transfer an extension registers a function for its Device class (and its subclasses). It is called with the view and
 * the target device when either the target or the source device is an instance of that class, and returns a new reference to the
 * resulting Tensor, or NULL after throwing. Register from MINIT; one per class; FAILURE if the table is full or the class has one. */
typedef zend_object *(*buffers_transfer_fn)(zend_object *view, zend_object *device);
BUFFERS_API zend_result buffers_register_transfer(zend_class_entry *device_ce, buffers_transfer_fn fn);
/* the same as Tensor::to(); a new reference, or NULL after throwing */
BUFFERS_API zend_object *buffers_tensor_to(zend_object *view, zend_object *device);

/* ---- DLPack ---------------------------------------------------------------------------- */

/* Exports a view. The tensor is malloc'd; whoever ends up owning it calls its deleter exactly once.
 * Waits for readiness first. The view's buffer is held until the deleter runs. An unaligned view (see above) is exported as it is:
 * DLPack allows any byte_offset, but data + byte_offset is then not a multiple of the element size, and consumers may reject it. */
BUFFERS_API zend_result buffers_dlpack_export(zend_object *view, DLManagedTensorVersioned **out);
/* Imports a tensor of any device; host data need not be aligned to the element size. On success the tensor is owned by the returned view's Buffer (its
 * deleter runs when that buffer is freed) and a new Tensor reference is returned; on failure an
 * exception is thrown, NULL is returned and the caller still owns the tensor. */
BUFFERS_API zend_object *buffers_dlpack_import(DLManagedTensorVersioned *tensor);

/* ---- operators ------------------------------------------------------------------------- */

/* Called for an arithmetic or bitwise operator when either operand is a Tensor: opcode is one of
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
