/* The front end: a closure's opcodes compiled to CIR for the types of one call (a specialization). */
#ifndef BUF_CC_FRONT_H
#define BUF_CC_FRONT_H

#include "cir.h"

/* where a value of the call's environment comes from, read at every call */
enum { SRC_ARG, SRC_CAPTURE, SRC_PROP };

typedef struct {
	uint8_t kind;        /* SRC_* */
	uint8_t tensor;      /* a tensor (or a TensorAggregate, or a captured array: arr) bound to env slots */
	uint8_t arr;         /* a PHP array, its zvals read and written in place (zarr.h); ak: what it holds (AK_*) */
	uint8_t ak;
	uint8_t byref;       /* a by-reference parameter or capture: writes go to the referenced array */
	uint8_t written;     /* the code writes into the tensor or array */
	uint8_t copied;      /* (while compiling) the array is copied: assigned, passed by value, iterated */
	uint8_t str;         /* a PHP string, read as its bytes (UInt8, read-only) */
	int16_t dt;          /* a tensor's dtype; a scalar's type (BUFFERS_SCALAR_*) */
	uint8_t ndim;
	uint32_t index;      /* ARG: the parameter; CAPTURE: the byte offset of its bucket in the static variables; PROP: the
	                      * byte offset of the property slot */
	int32_t env;         /* first env slot: a tensor's data, then shape[ndim], then byte strides[ndim] (an array: then
	                      * its data's address as an integer); a scalar's value */
} cc_src;

#define CC_T_NULL (-10)   /* cc_src.dt of a null */
#define CC_MAX_RET 64

typedef struct cc_kernel {
	cc_func **fns;
	int nfn;
	cc_src *src;
	int nsrc;
	int nenv;
	/* the returned value: none, one value, or an array of nret values */
	int nret;
	bool ret_list;
	int16_t retdt[CC_MAX_RET];   /* dtype or BUFFERS_SCALAR_* of each */
	int ret_src;                 /* > 0: the code returns the PHP array of source ret_src - 1 */
	void *native;                /* the native backend's code (NULL: interpreted) */
	void (*native_free)(void *);
} cc_kernel;

/* compiles the closure for these arguments; NULL after throwing */
cc_kernel *cc_compile(zend_object *closure, zval *args, uint32_t nargs);
void cc_kernel_free(cc_kernel *k);

#endif
