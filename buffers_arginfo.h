/* This is a generated file, edit the .stub.php file instead.
 * Stub hash: 2bfd80ba29c7f66b70c49d2a37b2b9f4e29f935e */

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_Buffers_DType_bits, 0, 0, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_Buffers_DType_byteSize, 0, 0, IS_LONG, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_Buffers_DType_isByteAligned, 0, 0, _IS_BOOL, 0)
ZEND_END_ARG_INFO()

#define arginfo_class_Buffers_DType_isInteger arginfo_class_Buffers_DType_isByteAligned

#define arginfo_class_Buffers_DType_isFloatingPoint arginfo_class_Buffers_DType_isByteAligned

#define arginfo_class_Buffers_DType_isComplex arginfo_class_Buffers_DType_isByteAligned

ZEND_BEGIN_ARG_INFO_EX(arginfo_class_Buffers_CpuDevice___construct, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_class_Buffers_CpuDevice_get, 0, 0, Buffers\\CpuDevice, 0)
ZEND_END_ARG_INFO()

#define arginfo_class_Buffers_FileDevice___construct arginfo_class_Buffers_CpuDevice___construct

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_class_Buffers_FileDevice_get, 0, 0, Buffers\\FileDevice, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_class_Buffers_Shape___construct, 0, 0, 1)
	ZEND_ARG_TYPE_INFO(0, dims, IS_ARRAY, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_Buffers_Shape_toArray, 0, 0, IS_ARRAY, 0)
ZEND_END_ARG_INFO()

#define arginfo_class_Buffers_Shape_count arginfo_class_Buffers_DType_bits

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_Buffers_Shape_offsetExists, 0, 1, _IS_BOOL, 0)
	ZEND_ARG_TYPE_INFO(0, offset, IS_MIXED, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_Buffers_Shape_offsetGet, 0, 1, IS_MIXED, 0)
	ZEND_ARG_TYPE_INFO(0, offset, IS_MIXED, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_Buffers_Shape_offsetSet, 0, 2, IS_VOID, 0)
	ZEND_ARG_TYPE_INFO(0, offset, IS_MIXED, 0)
	ZEND_ARG_TYPE_INFO(0, value, IS_MIXED, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_Buffers_Shape_offsetUnset, 0, 1, IS_VOID, 0)
	ZEND_ARG_TYPE_INFO(0, offset, IS_MIXED, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_class_Buffers_Shape_getIterator, 0, 0, Iterator, 0)
ZEND_END_ARG_INFO()

#define arginfo_class_Buffers_Shape_jsonSerialize arginfo_class_Buffers_Shape_toArray

ZEND_BEGIN_ARG_INFO_EX(arginfo_class_Buffers_Buffer___construct, 0, 0, 1)
	ZEND_ARG_TYPE_INFO(0, byteLength, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_class_Buffers_Buffer_fromString, 0, 1, Buffers\\Buffer, 0)
	ZEND_ARG_TYPE_INFO(0, data, IS_STRING, 0)
	ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, writable, _IS_BOOL, 0, "true")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_class_Buffers_Buffer_fromFile, 0, 1, Buffers\\Buffer, 0)
	ZEND_ARG_TYPE_INFO(0, path, IS_STRING, 0)
	ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, offset, IS_LONG, 0, "0")
	ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, length, IS_LONG, 1, "null")
	ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, map, _IS_BOOL, 0, "true")
	ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, writable, _IS_BOOL, 0, "false")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_class_Buffers_Buffer_createFile, 0, 2, Buffers\\Buffer, 0)
	ZEND_ARG_TYPE_INFO(0, path, IS_STRING, 0)
	ZEND_ARG_TYPE_INFO(0, byteLength, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_Buffers_Buffer_flush, 0, 0, IS_VOID, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_Buffers_Buffer_writeTo, 0, 1, IS_LONG, 0)
	ZEND_ARG_TYPE_INFO(0, stream, IS_MIXED, 0)
	ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, offset, IS_LONG, 0, "0")
	ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, length, IS_LONG, 1, "null")
ZEND_END_ARG_INFO()

#define arginfo_class_Buffers_Buffer_readFrom arginfo_class_Buffers_Buffer_writeTo

#define arginfo_class_Buffers_Buffer_isWritable arginfo_class_Buffers_DType_isByteAligned

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_class_Buffers_Buffer_readOnly, 0, 0, Buffers\\Buffer, 0)
ZEND_END_ARG_INFO()

#define arginfo_class_Buffers_Buffer_isHostAccessible arginfo_class_Buffers_DType_isByteAligned

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_class_Buffers_Buffer_view, 0, 1, Buffers\\Tensor, 0)
	ZEND_ARG_OBJ_INFO(0, type, Buffers\\DType, 0)
	ZEND_ARG_TYPE_MASK(0, shape, MAY_BE_LONG|MAY_BE_ARRAY|MAY_BE_NULL, "null")
	ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, strides, IS_ARRAY, 1, "null")
	ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, offset, IS_LONG, 0, "0")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_Buffers_Buffer_toString, 0, 0, IS_STRING, 0)
ZEND_END_ARG_INFO()

#define arginfo_class_Buffers_Tensor_count arginfo_class_Buffers_DType_bits

#define arginfo_class_Buffers_Tensor_offsetExists arginfo_class_Buffers_Shape_offsetExists

#define arginfo_class_Buffers_Tensor_offsetGet arginfo_class_Buffers_Shape_offsetGet

#define arginfo_class_Buffers_Tensor_offsetSet arginfo_class_Buffers_Shape_offsetSet

#define arginfo_class_Buffers_Tensor_offsetUnset arginfo_class_Buffers_Shape_offsetUnset

#define arginfo_class_Buffers_Tensor_getIterator arginfo_class_Buffers_Shape_getIterator

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_class_Buffers_Tensor_to, 0, 1, Buffers\\Tensor, 0)
	ZEND_ARG_OBJ_INFO(0, device, Buffers\\Device, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_class_Buffers_Tensor_convert, 0, 1, Buffers\\Tensor, 0)
	ZEND_ARG_OBJ_INFO(0, dtype, Buffers\\DType, 0)
	ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, axes, IS_ARRAY, 1, "null")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_Buffers_Tensor_jsonSerialize, 0, 0, IS_MIXED, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_class_Buffers_Tensor_fromArray, 0, 1, Buffers\\Tensor, 0)
	ZEND_ARG_TYPE_MASK(0, data, MAY_BE_ARRAY|MAY_BE_LONG|MAY_BE_DOUBLE|MAY_BE_BOOL, NULL)
	ZEND_ARG_OBJ_INFO_WITH_DEFAULT_VALUE(0, dtype, Buffers\\DType, 1, "null")
	ZEND_ARG_OBJ_INFO_WITH_DEFAULT_VALUE(0, device, Buffers\\Device, 1, "null")
ZEND_END_ARG_INFO()

#define arginfo_class_Buffers_Tensor_toArray arginfo_class_Buffers_Tensor_jsonSerialize

#if defined(HAVE_BUFFERS_GD)
ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_class_Buffers_Tensor_fromGdImage, 0, 1, Buffers\\Tensor, 0)
	ZEND_ARG_OBJ_INFO(0, image, GdImage, 0)
	ZEND_ARG_OBJ_INFO_WITH_DEFAULT_VALUE(0, into, Buffers\\Tensor, 1, "null")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_class_Buffers_Tensor_toGdImage, 0, 0, GdImage, 0)
ZEND_END_ARG_INFO()
#endif

ZEND_METHOD(Buffers_DType, bits);
ZEND_METHOD(Buffers_DType, byteSize);
ZEND_METHOD(Buffers_DType, isByteAligned);
ZEND_METHOD(Buffers_DType, isInteger);
ZEND_METHOD(Buffers_DType, isFloatingPoint);
ZEND_METHOD(Buffers_DType, isComplex);
ZEND_METHOD(Buffers_CpuDevice, __construct);
ZEND_METHOD(Buffers_CpuDevice, get);
ZEND_METHOD(Buffers_FileDevice, __construct);
ZEND_METHOD(Buffers_FileDevice, get);
ZEND_METHOD(Buffers_Shape, __construct);
ZEND_METHOD(Buffers_Shape, toArray);
ZEND_METHOD(Buffers_Shape, count);
ZEND_METHOD(Buffers_Shape, offsetExists);
ZEND_METHOD(Buffers_Shape, offsetGet);
ZEND_METHOD(Buffers_Shape, offsetSet);
ZEND_METHOD(Buffers_Shape, offsetUnset);
ZEND_METHOD(Buffers_Shape, getIterator);
ZEND_METHOD(Buffers_Shape, jsonSerialize);
ZEND_METHOD(Buffers_Buffer, __construct);
ZEND_METHOD(Buffers_Buffer, fromString);
ZEND_METHOD(Buffers_Buffer, fromFile);
ZEND_METHOD(Buffers_Buffer, createFile);
ZEND_METHOD(Buffers_Buffer, flush);
ZEND_METHOD(Buffers_Buffer, writeTo);
ZEND_METHOD(Buffers_Buffer, readFrom);
ZEND_METHOD(Buffers_Buffer, isWritable);
ZEND_METHOD(Buffers_Buffer, readOnly);
ZEND_METHOD(Buffers_Buffer, isHostAccessible);
ZEND_METHOD(Buffers_Buffer, view);
ZEND_METHOD(Buffers_Buffer, toString);
ZEND_METHOD(Buffers_Tensor, count);
ZEND_METHOD(Buffers_Tensor, offsetExists);
ZEND_METHOD(Buffers_Tensor, offsetGet);
ZEND_METHOD(Buffers_Tensor, offsetSet);
ZEND_METHOD(Buffers_Tensor, offsetUnset);
ZEND_METHOD(Buffers_Tensor, getIterator);
ZEND_METHOD(Buffers_Tensor, to);
ZEND_METHOD(Buffers_Tensor, convert);
ZEND_METHOD(Buffers_Tensor, jsonSerialize);
ZEND_METHOD(Buffers_Tensor, fromArray);
ZEND_METHOD(Buffers_Tensor, toArray);
#if defined(HAVE_BUFFERS_GD)
ZEND_METHOD(Buffers_Tensor, fromGdImage);
ZEND_METHOD(Buffers_Tensor, toGdImage);
#endif

static const zend_function_entry class_Buffers_DType_methods[] = {
	ZEND_ME(Buffers_DType, bits, arginfo_class_Buffers_DType_bits, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_DType, byteSize, arginfo_class_Buffers_DType_byteSize, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_DType, isByteAligned, arginfo_class_Buffers_DType_isByteAligned, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_DType, isInteger, arginfo_class_Buffers_DType_isInteger, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_DType, isFloatingPoint, arginfo_class_Buffers_DType_isFloatingPoint, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_DType, isComplex, arginfo_class_Buffers_DType_isComplex, ZEND_ACC_PUBLIC)
	ZEND_FE_END
};

static const zend_function_entry class_Buffers_CpuDevice_methods[] = {
	ZEND_ME(Buffers_CpuDevice, __construct, arginfo_class_Buffers_CpuDevice___construct, ZEND_ACC_PRIVATE)
	ZEND_ME(Buffers_CpuDevice, get, arginfo_class_Buffers_CpuDevice_get, ZEND_ACC_PUBLIC|ZEND_ACC_STATIC)
	ZEND_FE_END
};

static const zend_function_entry class_Buffers_FileDevice_methods[] = {
	ZEND_ME(Buffers_FileDevice, __construct, arginfo_class_Buffers_FileDevice___construct, ZEND_ACC_PRIVATE)
	ZEND_ME(Buffers_FileDevice, get, arginfo_class_Buffers_FileDevice_get, ZEND_ACC_PUBLIC|ZEND_ACC_STATIC)
	ZEND_FE_END
};

static const zend_function_entry class_Buffers_Shape_methods[] = {
	ZEND_ME(Buffers_Shape, __construct, arginfo_class_Buffers_Shape___construct, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_Shape, toArray, arginfo_class_Buffers_Shape_toArray, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_Shape, count, arginfo_class_Buffers_Shape_count, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_Shape, offsetExists, arginfo_class_Buffers_Shape_offsetExists, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_Shape, offsetGet, arginfo_class_Buffers_Shape_offsetGet, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_Shape, offsetSet, arginfo_class_Buffers_Shape_offsetSet, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_Shape, offsetUnset, arginfo_class_Buffers_Shape_offsetUnset, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_Shape, getIterator, arginfo_class_Buffers_Shape_getIterator, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_Shape, jsonSerialize, arginfo_class_Buffers_Shape_jsonSerialize, ZEND_ACC_PUBLIC)
	ZEND_FE_END
};

static const zend_function_entry class_Buffers_Buffer_methods[] = {
	ZEND_ME(Buffers_Buffer, __construct, arginfo_class_Buffers_Buffer___construct, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_Buffer, fromString, arginfo_class_Buffers_Buffer_fromString, ZEND_ACC_PUBLIC|ZEND_ACC_STATIC)
	ZEND_ME(Buffers_Buffer, fromFile, arginfo_class_Buffers_Buffer_fromFile, ZEND_ACC_PUBLIC|ZEND_ACC_STATIC)
	ZEND_ME(Buffers_Buffer, createFile, arginfo_class_Buffers_Buffer_createFile, ZEND_ACC_PUBLIC|ZEND_ACC_STATIC)
	ZEND_ME(Buffers_Buffer, flush, arginfo_class_Buffers_Buffer_flush, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_Buffer, writeTo, arginfo_class_Buffers_Buffer_writeTo, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_Buffer, readFrom, arginfo_class_Buffers_Buffer_readFrom, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_Buffer, isWritable, arginfo_class_Buffers_Buffer_isWritable, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_Buffer, readOnly, arginfo_class_Buffers_Buffer_readOnly, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_Buffer, isHostAccessible, arginfo_class_Buffers_Buffer_isHostAccessible, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_Buffer, view, arginfo_class_Buffers_Buffer_view, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_Buffer, toString, arginfo_class_Buffers_Buffer_toString, ZEND_ACC_PUBLIC)
	ZEND_FE_END
};

static const zend_function_entry class_Buffers_Tensor_methods[] = {
	ZEND_ME(Buffers_Tensor, count, arginfo_class_Buffers_Tensor_count, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_Tensor, offsetExists, arginfo_class_Buffers_Tensor_offsetExists, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_Tensor, offsetGet, arginfo_class_Buffers_Tensor_offsetGet, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_Tensor, offsetSet, arginfo_class_Buffers_Tensor_offsetSet, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_Tensor, offsetUnset, arginfo_class_Buffers_Tensor_offsetUnset, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_Tensor, getIterator, arginfo_class_Buffers_Tensor_getIterator, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_Tensor, to, arginfo_class_Buffers_Tensor_to, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_Tensor, convert, arginfo_class_Buffers_Tensor_convert, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_Tensor, jsonSerialize, arginfo_class_Buffers_Tensor_jsonSerialize, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_Tensor, fromArray, arginfo_class_Buffers_Tensor_fromArray, ZEND_ACC_PUBLIC|ZEND_ACC_STATIC)
	ZEND_ME(Buffers_Tensor, toArray, arginfo_class_Buffers_Tensor_toArray, ZEND_ACC_PUBLIC)
#if defined(HAVE_BUFFERS_GD)
	ZEND_ME(Buffers_Tensor, fromGdImage, arginfo_class_Buffers_Tensor_fromGdImage, ZEND_ACC_PUBLIC|ZEND_ACC_STATIC)
	ZEND_ME(Buffers_Tensor, toGdImage, arginfo_class_Buffers_Tensor_toGdImage, ZEND_ACC_PUBLIC)
#endif
	ZEND_FE_END
};
