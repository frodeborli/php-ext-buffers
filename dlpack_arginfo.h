/* This is a generated file, edit the .stub.php file instead.
 * Stub hash: 5964e8b2342854606053e5312f14b2d8dd1f86b5 */

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_DType_bits, 0, 0, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_DType_byteSize, 0, 0, IS_LONG, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_DType_isByteAligned, 0, 0, _IS_BOOL, 0)
ZEND_END_ARG_INFO()

#define arginfo_class_DType_isInteger arginfo_class_DType_isByteAligned

#define arginfo_class_DType_isFloatingPoint arginfo_class_DType_isByteAligned

#define arginfo_class_DType_isComplex arginfo_class_DType_isByteAligned

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_DType_dlpack, 0, 0, IS_ARRAY, 0)
ZEND_END_ARG_INFO()

#define arginfo_class_BufferInterface_byteLength arginfo_class_DType_bits

#define arginfo_class_BufferInterface_isWritable arginfo_class_DType_isByteAligned

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_class_BufferInterface_view, 0, 1, TypedView, 0)
	ZEND_ARG_OBJ_INFO(0, type, DType, 0)
	ZEND_ARG_TYPE_MASK(0, shape, MAY_BE_LONG|MAY_BE_ARRAY|MAY_BE_NULL, "null")
	ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, strides, IS_ARRAY, 1, "null")
	ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, offset, IS_LONG, 0, "0")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_class_ArrayBuffer___construct, 0, 0, 1)
	ZEND_ARG_TYPE_INFO(0, byteLength, IS_LONG, 0)
ZEND_END_ARG_INFO()

#define arginfo_class_ArrayBuffer_byteLength arginfo_class_DType_bits

#define arginfo_class_ArrayBuffer_isWritable arginfo_class_DType_isByteAligned

#define arginfo_class_ArrayBuffer_view arginfo_class_BufferInterface_view

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_class_ArrayBuffer_fromString, 0, 1, ArrayBuffer, 0)
	ZEND_ARG_TYPE_INFO(0, data, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_ArrayBuffer_toString, 0, 0, IS_STRING, 0)
ZEND_END_ARG_INFO()

#define arginfo_class_DLPackBuffer_byteLength arginfo_class_DType_bits

#define arginfo_class_DLPackBuffer_isWritable arginfo_class_DType_isByteAligned

#define arginfo_class_DLPackBuffer_view arginfo_class_BufferInterface_view

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_class_TypedView_dtype, 0, 0, DType, 0)
ZEND_END_ARG_INFO()

#define arginfo_class_TypedView_shape arginfo_class_DType_dlpack

#define arginfo_class_TypedView_strides arginfo_class_DType_dlpack

#define arginfo_class_TypedView_byteOffset arginfo_class_DType_bits

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_class_TypedView_buffer, 0, 0, BufferInterface, 0)
ZEND_END_ARG_INFO()

#define arginfo_class_TypedView_ndim arginfo_class_DType_bits

#define arginfo_class_TypedView_count arginfo_class_DType_bits

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_TypedView_offsetExists, 0, 1, _IS_BOOL, 0)
	ZEND_ARG_TYPE_INFO(0, offset, IS_MIXED, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_TypedView_offsetGet, 0, 1, IS_MIXED, 0)
	ZEND_ARG_TYPE_INFO(0, offset, IS_MIXED, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_TypedView_offsetSet, 0, 2, IS_VOID, 0)
	ZEND_ARG_TYPE_INFO(0, offset, IS_MIXED, 0)
	ZEND_ARG_TYPE_INFO(0, value, IS_MIXED, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_TypedView_offsetUnset, 0, 1, IS_VOID, 0)
	ZEND_ARG_TYPE_INFO(0, offset, IS_MIXED, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_class_TypedView_getIterator, 0, 0, Iterator, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_class_TypedView_toDLPack, 0, 0, DLPackCapsule, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_class_TypedView_fromDLPack, 0, 1, TypedView, 0)
	ZEND_ARG_OBJ_INFO(0, capsule, DLPackCapsule, 0)
ZEND_END_ARG_INFO()

ZEND_METHOD(DType, bits);
ZEND_METHOD(DType, byteSize);
ZEND_METHOD(DType, isByteAligned);
ZEND_METHOD(DType, isInteger);
ZEND_METHOD(DType, isFloatingPoint);
ZEND_METHOD(DType, isComplex);
ZEND_METHOD(DType, dlpack);
ZEND_METHOD(ArrayBuffer, __construct);
ZEND_METHOD(ArrayBuffer, byteLength);
ZEND_METHOD(ArrayBuffer, isWritable);
ZEND_METHOD(ArrayBuffer, view);
ZEND_METHOD(ArrayBuffer, fromString);
ZEND_METHOD(ArrayBuffer, toString);
ZEND_METHOD(DLPackBuffer, byteLength);
ZEND_METHOD(DLPackBuffer, isWritable);
ZEND_METHOD(DLPackBuffer, view);
ZEND_METHOD(TypedView, dtype);
ZEND_METHOD(TypedView, shape);
ZEND_METHOD(TypedView, strides);
ZEND_METHOD(TypedView, byteOffset);
ZEND_METHOD(TypedView, buffer);
ZEND_METHOD(TypedView, ndim);
ZEND_METHOD(TypedView, count);
ZEND_METHOD(TypedView, offsetExists);
ZEND_METHOD(TypedView, offsetGet);
ZEND_METHOD(TypedView, offsetSet);
ZEND_METHOD(TypedView, offsetUnset);
ZEND_METHOD(TypedView, getIterator);
ZEND_METHOD(TypedView, toDLPack);
ZEND_METHOD(TypedView, fromDLPack);

static const zend_function_entry class_DType_methods[] = {
	ZEND_ME(DType, bits, arginfo_class_DType_bits, ZEND_ACC_PUBLIC)
	ZEND_ME(DType, byteSize, arginfo_class_DType_byteSize, ZEND_ACC_PUBLIC)
	ZEND_ME(DType, isByteAligned, arginfo_class_DType_isByteAligned, ZEND_ACC_PUBLIC)
	ZEND_ME(DType, isInteger, arginfo_class_DType_isInteger, ZEND_ACC_PUBLIC)
	ZEND_ME(DType, isFloatingPoint, arginfo_class_DType_isFloatingPoint, ZEND_ACC_PUBLIC)
	ZEND_ME(DType, isComplex, arginfo_class_DType_isComplex, ZEND_ACC_PUBLIC)
	ZEND_ME(DType, dlpack, arginfo_class_DType_dlpack, ZEND_ACC_PUBLIC)
	ZEND_FE_END
};

static const zend_function_entry class_BufferInterface_methods[] = {
	ZEND_ABSTRACT_ME_WITH_FLAGS(BufferInterface, byteLength, arginfo_class_BufferInterface_byteLength, ZEND_ACC_PUBLIC|ZEND_ACC_ABSTRACT)
	ZEND_ABSTRACT_ME_WITH_FLAGS(BufferInterface, isWritable, arginfo_class_BufferInterface_isWritable, ZEND_ACC_PUBLIC|ZEND_ACC_ABSTRACT)
	ZEND_ABSTRACT_ME_WITH_FLAGS(BufferInterface, view, arginfo_class_BufferInterface_view, ZEND_ACC_PUBLIC|ZEND_ACC_ABSTRACT)
	ZEND_FE_END
};

static const zend_function_entry class_ArrayBuffer_methods[] = {
	ZEND_ME(ArrayBuffer, __construct, arginfo_class_ArrayBuffer___construct, ZEND_ACC_PUBLIC)
	ZEND_ME(ArrayBuffer, byteLength, arginfo_class_ArrayBuffer_byteLength, ZEND_ACC_PUBLIC)
	ZEND_ME(ArrayBuffer, isWritable, arginfo_class_ArrayBuffer_isWritable, ZEND_ACC_PUBLIC)
	ZEND_ME(ArrayBuffer, view, arginfo_class_ArrayBuffer_view, ZEND_ACC_PUBLIC)
	ZEND_ME(ArrayBuffer, fromString, arginfo_class_ArrayBuffer_fromString, ZEND_ACC_PUBLIC|ZEND_ACC_STATIC)
	ZEND_ME(ArrayBuffer, toString, arginfo_class_ArrayBuffer_toString, ZEND_ACC_PUBLIC)
	ZEND_FE_END
};

static const zend_function_entry class_DLPackBuffer_methods[] = {
	ZEND_ME(DLPackBuffer, byteLength, arginfo_class_DLPackBuffer_byteLength, ZEND_ACC_PUBLIC)
	ZEND_ME(DLPackBuffer, isWritable, arginfo_class_DLPackBuffer_isWritable, ZEND_ACC_PUBLIC)
	ZEND_ME(DLPackBuffer, view, arginfo_class_DLPackBuffer_view, ZEND_ACC_PUBLIC)
	ZEND_FE_END
};

static const zend_function_entry class_TypedView_methods[] = {
	ZEND_ME(TypedView, dtype, arginfo_class_TypedView_dtype, ZEND_ACC_PUBLIC)
	ZEND_ME(TypedView, shape, arginfo_class_TypedView_shape, ZEND_ACC_PUBLIC)
	ZEND_ME(TypedView, strides, arginfo_class_TypedView_strides, ZEND_ACC_PUBLIC)
	ZEND_ME(TypedView, byteOffset, arginfo_class_TypedView_byteOffset, ZEND_ACC_PUBLIC)
	ZEND_ME(TypedView, buffer, arginfo_class_TypedView_buffer, ZEND_ACC_PUBLIC)
	ZEND_ME(TypedView, ndim, arginfo_class_TypedView_ndim, ZEND_ACC_PUBLIC)
	ZEND_ME(TypedView, count, arginfo_class_TypedView_count, ZEND_ACC_PUBLIC)
	ZEND_ME(TypedView, offsetExists, arginfo_class_TypedView_offsetExists, ZEND_ACC_PUBLIC)
	ZEND_ME(TypedView, offsetGet, arginfo_class_TypedView_offsetGet, ZEND_ACC_PUBLIC)
	ZEND_ME(TypedView, offsetSet, arginfo_class_TypedView_offsetSet, ZEND_ACC_PUBLIC)
	ZEND_ME(TypedView, offsetUnset, arginfo_class_TypedView_offsetUnset, ZEND_ACC_PUBLIC)
	ZEND_ME(TypedView, getIterator, arginfo_class_TypedView_getIterator, ZEND_ACC_PUBLIC)
	ZEND_ME(TypedView, toDLPack, arginfo_class_TypedView_toDLPack, ZEND_ACC_PUBLIC)
	ZEND_ME(TypedView, fromDLPack, arginfo_class_TypedView_fromDLPack, ZEND_ACC_PUBLIC|ZEND_ACC_STATIC)
	ZEND_FE_END
};
