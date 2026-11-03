/* This is a generated file, edit the .stub.php file instead.
 * Stub hash: d3bb1a42b2860643a2bf48df180dfeebcd7847bf */

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_Buffers_DType_bits, 0, 0, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_Buffers_DType_byteSize, 0, 0, IS_LONG, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_Buffers_DType_isByteAligned, 0, 0, _IS_BOOL, 0)
ZEND_END_ARG_INFO()

#define arginfo_class_Buffers_DType_isInteger arginfo_class_Buffers_DType_isByteAligned

#define arginfo_class_Buffers_DType_isFloatingPoint arginfo_class_Buffers_DType_isByteAligned

#define arginfo_class_Buffers_DType_isComplex arginfo_class_Buffers_DType_isByteAligned

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_Buffers_Device_name, 0, 0, IS_STRING, 0)
ZEND_END_ARG_INFO()

#define arginfo_class_Buffers_Device_id arginfo_class_Buffers_DType_bits

#define arginfo_class_Buffers_Device_isHostAccessible arginfo_class_Buffers_DType_isByteAligned

ZEND_BEGIN_ARG_INFO_EX(arginfo_class_Buffers_CpuDevice___construct, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_class_Buffers_CpuDevice_get, 0, 0, Buffers\\CpuDevice, 0)
ZEND_END_ARG_INFO()

#define arginfo_class_Buffers_CpuDevice_name arginfo_class_Buffers_Device_name

#define arginfo_class_Buffers_CpuDevice_id arginfo_class_Buffers_DType_bits

#define arginfo_class_Buffers_CpuDevice_isHostAccessible arginfo_class_Buffers_DType_isByteAligned

#define arginfo_class_Buffers_FileDevice___construct arginfo_class_Buffers_CpuDevice___construct

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_class_Buffers_FileDevice_get, 0, 0, Buffers\\FileDevice, 0)
ZEND_END_ARG_INFO()

#define arginfo_class_Buffers_FileDevice_name arginfo_class_Buffers_Device_name

#define arginfo_class_Buffers_FileDevice_id arginfo_class_Buffers_DType_bits

#define arginfo_class_Buffers_FileDevice_isHostAccessible arginfo_class_Buffers_DType_isByteAligned

#define arginfo_class_Buffers_ForeignDevice_name arginfo_class_Buffers_Device_name

#define arginfo_class_Buffers_ForeignDevice_id arginfo_class_Buffers_DType_bits

#define arginfo_class_Buffers_ForeignDevice_isHostAccessible arginfo_class_Buffers_DType_isByteAligned

ZEND_BEGIN_ARG_INFO_EX(arginfo_class_Buffers_Shape___construct, 0, 0, 1)
	ZEND_ARG_TYPE_INFO(0, dims, IS_ARRAY, 0)
ZEND_END_ARG_INFO()

#define arginfo_class_Buffers_Shape_isConcrete arginfo_class_Buffers_DType_isByteAligned

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_TYPE_MASK_EX(arginfo_class_Buffers_Shape_size, 0, 0, Buffers\\Tensor, MAY_BE_LONG)
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
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_class_Buffers_Buffer_fromFile, 0, 1, Buffers\\Buffer, 0)
	ZEND_ARG_TYPE_INFO(0, path, IS_STRING, 0)
	ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, offset, IS_LONG, 0, "0")
	ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, length, IS_LONG, 1, "null")
	ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, map, _IS_BOOL, 0, "true")
ZEND_END_ARG_INFO()

#define arginfo_class_Buffers_Buffer_byteLength arginfo_class_Buffers_DType_bits

#define arginfo_class_Buffers_Buffer_isWritable arginfo_class_Buffers_DType_isByteAligned

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_class_Buffers_Buffer_device, 0, 0, Buffers\\Device, 0)
ZEND_END_ARG_INFO()

#define arginfo_class_Buffers_Buffer_isHostAccessible arginfo_class_Buffers_DType_isByteAligned

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_class_Buffers_Buffer_view, 0, 1, Buffers\\BufferView, 0)
	ZEND_ARG_OBJ_INFO(0, type, Buffers\\DType, 0)
	ZEND_ARG_TYPE_MASK(0, shape, MAY_BE_LONG|MAY_BE_ARRAY|MAY_BE_NULL, "null")
	ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, strides, IS_ARRAY, 1, "null")
	ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, offset, IS_LONG, 0, "0")
ZEND_END_ARG_INFO()

#define arginfo_class_Buffers_Buffer_toString arginfo_class_Buffers_Device_name

#define arginfo_class_Buffers_BufferView_count arginfo_class_Buffers_DType_bits

#define arginfo_class_Buffers_BufferView_offsetExists arginfo_class_Buffers_Shape_offsetExists

#define arginfo_class_Buffers_BufferView_offsetGet arginfo_class_Buffers_Shape_offsetGet

#define arginfo_class_Buffers_BufferView_offsetSet arginfo_class_Buffers_Shape_offsetSet

#define arginfo_class_Buffers_BufferView_offsetUnset arginfo_class_Buffers_Shape_offsetUnset

#define arginfo_class_Buffers_BufferView_getIterator arginfo_class_Buffers_Shape_getIterator

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_class_Buffers_BufferView_to, 0, 1, Buffers\\BufferView, 0)
	ZEND_ARG_OBJ_INFO(0, device, Buffers\\Device, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_Buffers_BufferView_jsonSerialize, 0, 0, IS_MIXED, 0)
ZEND_END_ARG_INFO()

ZEND_METHOD(Buffers_DType, bits);
ZEND_METHOD(Buffers_DType, byteSize);
ZEND_METHOD(Buffers_DType, isByteAligned);
ZEND_METHOD(Buffers_DType, isInteger);
ZEND_METHOD(Buffers_DType, isFloatingPoint);
ZEND_METHOD(Buffers_DType, isComplex);
ZEND_METHOD(Buffers_CpuDevice, __construct);
ZEND_METHOD(Buffers_CpuDevice, get);
ZEND_METHOD(Buffers_CpuDevice, name);
ZEND_METHOD(Buffers_CpuDevice, id);
ZEND_METHOD(Buffers_CpuDevice, isHostAccessible);
ZEND_METHOD(Buffers_FileDevice, __construct);
ZEND_METHOD(Buffers_FileDevice, get);
ZEND_METHOD(Buffers_FileDevice, name);
ZEND_METHOD(Buffers_FileDevice, id);
ZEND_METHOD(Buffers_FileDevice, isHostAccessible);
ZEND_METHOD(Buffers_ForeignDevice, name);
ZEND_METHOD(Buffers_ForeignDevice, id);
ZEND_METHOD(Buffers_ForeignDevice, isHostAccessible);
ZEND_METHOD(Buffers_Shape, __construct);
ZEND_METHOD(Buffers_Shape, isConcrete);
ZEND_METHOD(Buffers_Shape, size);
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
ZEND_METHOD(Buffers_Buffer, byteLength);
ZEND_METHOD(Buffers_Buffer, isWritable);
ZEND_METHOD(Buffers_Buffer, device);
ZEND_METHOD(Buffers_Buffer, isHostAccessible);
ZEND_METHOD(Buffers_Buffer, view);
ZEND_METHOD(Buffers_Buffer, toString);
ZEND_METHOD(Buffers_BufferView, count);
ZEND_METHOD(Buffers_BufferView, offsetExists);
ZEND_METHOD(Buffers_BufferView, offsetGet);
ZEND_METHOD(Buffers_BufferView, offsetSet);
ZEND_METHOD(Buffers_BufferView, offsetUnset);
ZEND_METHOD(Buffers_BufferView, getIterator);
ZEND_METHOD(Buffers_BufferView, to);
ZEND_METHOD(Buffers_BufferView, jsonSerialize);

static const zend_function_entry class_Buffers_DType_methods[] = {
	ZEND_ME(Buffers_DType, bits, arginfo_class_Buffers_DType_bits, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_DType, byteSize, arginfo_class_Buffers_DType_byteSize, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_DType, isByteAligned, arginfo_class_Buffers_DType_isByteAligned, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_DType, isInteger, arginfo_class_Buffers_DType_isInteger, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_DType, isFloatingPoint, arginfo_class_Buffers_DType_isFloatingPoint, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_DType, isComplex, arginfo_class_Buffers_DType_isComplex, ZEND_ACC_PUBLIC)
	ZEND_FE_END
};

static const zend_function_entry class_Buffers_Device_methods[] = {
	ZEND_RAW_FENTRY("name", NULL, arginfo_class_Buffers_Device_name, ZEND_ACC_PUBLIC|ZEND_ACC_ABSTRACT, NULL, NULL)
	ZEND_RAW_FENTRY("id", NULL, arginfo_class_Buffers_Device_id, ZEND_ACC_PUBLIC|ZEND_ACC_ABSTRACT, NULL, NULL)
	ZEND_RAW_FENTRY("isHostAccessible", NULL, arginfo_class_Buffers_Device_isHostAccessible, ZEND_ACC_PUBLIC|ZEND_ACC_ABSTRACT, NULL, NULL)
	ZEND_FE_END
};

static const zend_function_entry class_Buffers_CpuDevice_methods[] = {
	ZEND_ME(Buffers_CpuDevice, __construct, arginfo_class_Buffers_CpuDevice___construct, ZEND_ACC_PRIVATE)
	ZEND_ME(Buffers_CpuDevice, get, arginfo_class_Buffers_CpuDevice_get, ZEND_ACC_PUBLIC|ZEND_ACC_STATIC)
	ZEND_ME(Buffers_CpuDevice, name, arginfo_class_Buffers_CpuDevice_name, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_CpuDevice, id, arginfo_class_Buffers_CpuDevice_id, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_CpuDevice, isHostAccessible, arginfo_class_Buffers_CpuDevice_isHostAccessible, ZEND_ACC_PUBLIC)
	ZEND_FE_END
};

static const zend_function_entry class_Buffers_FileDevice_methods[] = {
	ZEND_ME(Buffers_FileDevice, __construct, arginfo_class_Buffers_FileDevice___construct, ZEND_ACC_PRIVATE)
	ZEND_ME(Buffers_FileDevice, get, arginfo_class_Buffers_FileDevice_get, ZEND_ACC_PUBLIC|ZEND_ACC_STATIC)
	ZEND_ME(Buffers_FileDevice, name, arginfo_class_Buffers_FileDevice_name, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_FileDevice, id, arginfo_class_Buffers_FileDevice_id, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_FileDevice, isHostAccessible, arginfo_class_Buffers_FileDevice_isHostAccessible, ZEND_ACC_PUBLIC)
	ZEND_FE_END
};

static const zend_function_entry class_Buffers_ForeignDevice_methods[] = {
	ZEND_ME(Buffers_ForeignDevice, name, arginfo_class_Buffers_ForeignDevice_name, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_ForeignDevice, id, arginfo_class_Buffers_ForeignDevice_id, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_ForeignDevice, isHostAccessible, arginfo_class_Buffers_ForeignDevice_isHostAccessible, ZEND_ACC_PUBLIC)
	ZEND_FE_END
};

static const zend_function_entry class_Buffers_Shape_methods[] = {
	ZEND_ME(Buffers_Shape, __construct, arginfo_class_Buffers_Shape___construct, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_Shape, isConcrete, arginfo_class_Buffers_Shape_isConcrete, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_Shape, size, arginfo_class_Buffers_Shape_size, ZEND_ACC_PUBLIC)
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
	ZEND_ME(Buffers_Buffer, byteLength, arginfo_class_Buffers_Buffer_byteLength, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_Buffer, isWritable, arginfo_class_Buffers_Buffer_isWritable, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_Buffer, device, arginfo_class_Buffers_Buffer_device, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_Buffer, isHostAccessible, arginfo_class_Buffers_Buffer_isHostAccessible, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_Buffer, view, arginfo_class_Buffers_Buffer_view, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_Buffer, toString, arginfo_class_Buffers_Buffer_toString, ZEND_ACC_PUBLIC)
	ZEND_FE_END
};

static const zend_function_entry class_Buffers_BufferView_methods[] = {
	ZEND_ME(Buffers_BufferView, count, arginfo_class_Buffers_BufferView_count, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_BufferView, offsetExists, arginfo_class_Buffers_BufferView_offsetExists, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_BufferView, offsetGet, arginfo_class_Buffers_BufferView_offsetGet, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_BufferView, offsetSet, arginfo_class_Buffers_BufferView_offsetSet, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_BufferView, offsetUnset, arginfo_class_Buffers_BufferView_offsetUnset, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_BufferView, getIterator, arginfo_class_Buffers_BufferView_getIterator, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_BufferView, to, arginfo_class_Buffers_BufferView_to, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_BufferView, jsonSerialize, arginfo_class_Buffers_BufferView_jsonSerialize, ZEND_ACC_PUBLIC)
	ZEND_FE_END
};
