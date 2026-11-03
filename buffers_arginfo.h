/* This is a generated file, edit the .stub.php file instead.
 * Stub hash: 5e5949c5b7a4c24d6a84fce7255561dadf02dce0 */

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_Buffers_DType_bits, 0, 0, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_Buffers_DType_byteSize, 0, 0, IS_LONG, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_Buffers_DType_isByteAligned, 0, 0, _IS_BOOL, 0)
ZEND_END_ARG_INFO()

#define arginfo_class_Buffers_DType_isInteger arginfo_class_Buffers_DType_isByteAligned

#define arginfo_class_Buffers_DType_isFloatingPoint arginfo_class_Buffers_DType_isByteAligned

#define arginfo_class_Buffers_DType_isComplex arginfo_class_Buffers_DType_isByteAligned

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_Buffers_DType_dlpack, 0, 0, IS_ARRAY, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_Buffers_Device_name, 0, 0, IS_STRING, 0)
ZEND_END_ARG_INFO()

#define arginfo_class_Buffers_Device_dlpackType arginfo_class_Buffers_DType_bits

#define arginfo_class_Buffers_Device_id arginfo_class_Buffers_DType_bits

#define arginfo_class_Buffers_Device_isHostAccessible arginfo_class_Buffers_DType_isByteAligned

ZEND_BEGIN_ARG_INFO_EX(arginfo_class_Buffers_CpuDevice___construct, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_class_Buffers_CpuDevice_get, 0, 0, Buffers\\CpuDevice, 0)
ZEND_END_ARG_INFO()

#define arginfo_class_Buffers_CpuDevice_name arginfo_class_Buffers_Device_name

#define arginfo_class_Buffers_CpuDevice_dlpackType arginfo_class_Buffers_DType_bits

#define arginfo_class_Buffers_CpuDevice_id arginfo_class_Buffers_DType_bits

#define arginfo_class_Buffers_CpuDevice_isHostAccessible arginfo_class_Buffers_DType_isByteAligned

#define arginfo_class_Buffers_ForeignDevice_name arginfo_class_Buffers_Device_name

#define arginfo_class_Buffers_ForeignDevice_dlpackType arginfo_class_Buffers_DType_bits

#define arginfo_class_Buffers_ForeignDevice_id arginfo_class_Buffers_DType_bits

#define arginfo_class_Buffers_ForeignDevice_isHostAccessible arginfo_class_Buffers_DType_isByteAligned

ZEND_BEGIN_ARG_INFO_EX(arginfo_class_Buffers_Shape___construct, 0, 0, 1)
	ZEND_ARG_TYPE_INFO(0, dims, IS_ARRAY, 0)
ZEND_END_ARG_INFO()

#define arginfo_class_Buffers_Shape_isConcrete arginfo_class_Buffers_DType_isByteAligned

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_TYPE_MASK_EX(arginfo_class_Buffers_Shape_size, 0, 0, Buffers\\Tensor, MAY_BE_LONG)
ZEND_END_ARG_INFO()

#define arginfo_class_Buffers_Shape_toArray arginfo_class_Buffers_DType_dlpack

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

#define arginfo_class_Buffers_Shape_jsonSerialize arginfo_class_Buffers_DType_dlpack

#define arginfo_class_Buffers_BufferInterface_byteLength arginfo_class_Buffers_DType_bits

#define arginfo_class_Buffers_BufferInterface_isWritable arginfo_class_Buffers_DType_isByteAligned

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_class_Buffers_BufferInterface_device, 0, 0, Buffers\\Device, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_class_Buffers_BufferInterface_view, 0, 1, Buffers\\BufferView, 0)
	ZEND_ARG_OBJ_INFO(0, type, Buffers\\DType, 0)
	ZEND_ARG_TYPE_MASK(0, shape, MAY_BE_LONG|MAY_BE_ARRAY|MAY_BE_NULL, "null")
	ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, strides, IS_ARRAY, 1, "null")
	ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, offset, IS_LONG, 0, "0")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_class_Buffers_ArrayBuffer___construct, 0, 0, 1)
	ZEND_ARG_TYPE_INFO(0, byteLength, IS_LONG, 0)
ZEND_END_ARG_INFO()

#define arginfo_class_Buffers_ArrayBuffer_byteLength arginfo_class_Buffers_DType_bits

#define arginfo_class_Buffers_ArrayBuffer_isWritable arginfo_class_Buffers_DType_isByteAligned

#define arginfo_class_Buffers_ArrayBuffer_device arginfo_class_Buffers_BufferInterface_device

#define arginfo_class_Buffers_ArrayBuffer_view arginfo_class_Buffers_BufferInterface_view

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_class_Buffers_ArrayBuffer_fromString, 0, 1, Buffers\\ArrayBuffer, 0)
	ZEND_ARG_TYPE_INFO(0, data, IS_STRING, 0)
ZEND_END_ARG_INFO()

#define arginfo_class_Buffers_ArrayBuffer_toString arginfo_class_Buffers_Device_name

#define arginfo_class_Buffers_DLPackBuffer_byteLength arginfo_class_Buffers_DType_bits

#define arginfo_class_Buffers_DLPackBuffer_isWritable arginfo_class_Buffers_DType_isByteAligned

#define arginfo_class_Buffers_DLPackBuffer_device arginfo_class_Buffers_BufferInterface_device

#define arginfo_class_Buffers_DLPackBuffer_view arginfo_class_Buffers_BufferInterface_view

#define arginfo_class_Buffers_BufferView_count arginfo_class_Buffers_DType_bits

#define arginfo_class_Buffers_BufferView_offsetExists arginfo_class_Buffers_Shape_offsetExists

#define arginfo_class_Buffers_BufferView_offsetGet arginfo_class_Buffers_Shape_offsetGet

#define arginfo_class_Buffers_BufferView_offsetSet arginfo_class_Buffers_Shape_offsetSet

#define arginfo_class_Buffers_BufferView_offsetUnset arginfo_class_Buffers_Shape_offsetUnset

#define arginfo_class_Buffers_BufferView_getIterator arginfo_class_Buffers_Shape_getIterator

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_class_Buffers_BufferView_toDLPack, 0, 0, Buffers\\DLPackCapsule, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_class_Buffers_BufferView_fromDLPack, 0, 1, Buffers\\BufferView, 0)
	ZEND_ARG_OBJ_INFO(0, capsule, Buffers\\DLPackCapsule, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_Buffers_BufferView_jsonSerialize, 0, 0, IS_MIXED, 0)
ZEND_END_ARG_INFO()

ZEND_METHOD(Buffers_DType, bits);
ZEND_METHOD(Buffers_DType, byteSize);
ZEND_METHOD(Buffers_DType, isByteAligned);
ZEND_METHOD(Buffers_DType, isInteger);
ZEND_METHOD(Buffers_DType, isFloatingPoint);
ZEND_METHOD(Buffers_DType, isComplex);
ZEND_METHOD(Buffers_DType, dlpack);
ZEND_METHOD(Buffers_CpuDevice, __construct);
ZEND_METHOD(Buffers_CpuDevice, get);
ZEND_METHOD(Buffers_CpuDevice, name);
ZEND_METHOD(Buffers_CpuDevice, dlpackType);
ZEND_METHOD(Buffers_CpuDevice, id);
ZEND_METHOD(Buffers_CpuDevice, isHostAccessible);
ZEND_METHOD(Buffers_ForeignDevice, name);
ZEND_METHOD(Buffers_ForeignDevice, dlpackType);
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
ZEND_METHOD(Buffers_ArrayBuffer, __construct);
ZEND_METHOD(Buffers_ArrayBuffer, byteLength);
ZEND_METHOD(Buffers_ArrayBuffer, isWritable);
ZEND_METHOD(Buffers_ArrayBuffer, device);
ZEND_METHOD(Buffers_ArrayBuffer, view);
ZEND_METHOD(Buffers_ArrayBuffer, fromString);
ZEND_METHOD(Buffers_ArrayBuffer, toString);
ZEND_METHOD(Buffers_DLPackBuffer, byteLength);
ZEND_METHOD(Buffers_DLPackBuffer, isWritable);
ZEND_METHOD(Buffers_DLPackBuffer, device);
ZEND_METHOD(Buffers_DLPackBuffer, view);
ZEND_METHOD(Buffers_BufferView, count);
ZEND_METHOD(Buffers_BufferView, offsetExists);
ZEND_METHOD(Buffers_BufferView, offsetGet);
ZEND_METHOD(Buffers_BufferView, offsetSet);
ZEND_METHOD(Buffers_BufferView, offsetUnset);
ZEND_METHOD(Buffers_BufferView, getIterator);
ZEND_METHOD(Buffers_BufferView, toDLPack);
ZEND_METHOD(Buffers_BufferView, fromDLPack);
ZEND_METHOD(Buffers_BufferView, jsonSerialize);

static const zend_function_entry class_Buffers_DType_methods[] = {
	ZEND_ME(Buffers_DType, bits, arginfo_class_Buffers_DType_bits, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_DType, byteSize, arginfo_class_Buffers_DType_byteSize, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_DType, isByteAligned, arginfo_class_Buffers_DType_isByteAligned, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_DType, isInteger, arginfo_class_Buffers_DType_isInteger, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_DType, isFloatingPoint, arginfo_class_Buffers_DType_isFloatingPoint, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_DType, isComplex, arginfo_class_Buffers_DType_isComplex, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_DType, dlpack, arginfo_class_Buffers_DType_dlpack, ZEND_ACC_PUBLIC)
	ZEND_FE_END
};

static const zend_function_entry class_Buffers_Device_methods[] = {
	ZEND_RAW_FENTRY("name", NULL, arginfo_class_Buffers_Device_name, ZEND_ACC_PUBLIC|ZEND_ACC_ABSTRACT, NULL, NULL)
	ZEND_RAW_FENTRY("dlpackType", NULL, arginfo_class_Buffers_Device_dlpackType, ZEND_ACC_PUBLIC|ZEND_ACC_ABSTRACT, NULL, NULL)
	ZEND_RAW_FENTRY("id", NULL, arginfo_class_Buffers_Device_id, ZEND_ACC_PUBLIC|ZEND_ACC_ABSTRACT, NULL, NULL)
	ZEND_RAW_FENTRY("isHostAccessible", NULL, arginfo_class_Buffers_Device_isHostAccessible, ZEND_ACC_PUBLIC|ZEND_ACC_ABSTRACT, NULL, NULL)
	ZEND_FE_END
};

static const zend_function_entry class_Buffers_CpuDevice_methods[] = {
	ZEND_ME(Buffers_CpuDevice, __construct, arginfo_class_Buffers_CpuDevice___construct, ZEND_ACC_PRIVATE)
	ZEND_ME(Buffers_CpuDevice, get, arginfo_class_Buffers_CpuDevice_get, ZEND_ACC_PUBLIC|ZEND_ACC_STATIC)
	ZEND_ME(Buffers_CpuDevice, name, arginfo_class_Buffers_CpuDevice_name, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_CpuDevice, dlpackType, arginfo_class_Buffers_CpuDevice_dlpackType, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_CpuDevice, id, arginfo_class_Buffers_CpuDevice_id, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_CpuDevice, isHostAccessible, arginfo_class_Buffers_CpuDevice_isHostAccessible, ZEND_ACC_PUBLIC)
	ZEND_FE_END
};

static const zend_function_entry class_Buffers_ForeignDevice_methods[] = {
	ZEND_ME(Buffers_ForeignDevice, name, arginfo_class_Buffers_ForeignDevice_name, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_ForeignDevice, dlpackType, arginfo_class_Buffers_ForeignDevice_dlpackType, ZEND_ACC_PUBLIC)
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

static const zend_function_entry class_Buffers_BufferInterface_methods[] = {
	ZEND_RAW_FENTRY("byteLength", NULL, arginfo_class_Buffers_BufferInterface_byteLength, ZEND_ACC_PUBLIC|ZEND_ACC_ABSTRACT, NULL, NULL)
	ZEND_RAW_FENTRY("isWritable", NULL, arginfo_class_Buffers_BufferInterface_isWritable, ZEND_ACC_PUBLIC|ZEND_ACC_ABSTRACT, NULL, NULL)
	ZEND_RAW_FENTRY("device", NULL, arginfo_class_Buffers_BufferInterface_device, ZEND_ACC_PUBLIC|ZEND_ACC_ABSTRACT, NULL, NULL)
	ZEND_RAW_FENTRY("view", NULL, arginfo_class_Buffers_BufferInterface_view, ZEND_ACC_PUBLIC|ZEND_ACC_ABSTRACT, NULL, NULL)
	ZEND_FE_END
};

static const zend_function_entry class_Buffers_ArrayBuffer_methods[] = {
	ZEND_ME(Buffers_ArrayBuffer, __construct, arginfo_class_Buffers_ArrayBuffer___construct, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_ArrayBuffer, byteLength, arginfo_class_Buffers_ArrayBuffer_byteLength, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_ArrayBuffer, isWritable, arginfo_class_Buffers_ArrayBuffer_isWritable, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_ArrayBuffer, device, arginfo_class_Buffers_ArrayBuffer_device, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_ArrayBuffer, view, arginfo_class_Buffers_ArrayBuffer_view, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_ArrayBuffer, fromString, arginfo_class_Buffers_ArrayBuffer_fromString, ZEND_ACC_PUBLIC|ZEND_ACC_STATIC)
	ZEND_ME(Buffers_ArrayBuffer, toString, arginfo_class_Buffers_ArrayBuffer_toString, ZEND_ACC_PUBLIC)
	ZEND_FE_END
};

static const zend_function_entry class_Buffers_DLPackBuffer_methods[] = {
	ZEND_ME(Buffers_DLPackBuffer, byteLength, arginfo_class_Buffers_DLPackBuffer_byteLength, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_DLPackBuffer, isWritable, arginfo_class_Buffers_DLPackBuffer_isWritable, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_DLPackBuffer, device, arginfo_class_Buffers_DLPackBuffer_device, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_DLPackBuffer, view, arginfo_class_Buffers_DLPackBuffer_view, ZEND_ACC_PUBLIC)
	ZEND_FE_END
};

static const zend_function_entry class_Buffers_BufferView_methods[] = {
	ZEND_ME(Buffers_BufferView, count, arginfo_class_Buffers_BufferView_count, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_BufferView, offsetExists, arginfo_class_Buffers_BufferView_offsetExists, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_BufferView, offsetGet, arginfo_class_Buffers_BufferView_offsetGet, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_BufferView, offsetSet, arginfo_class_Buffers_BufferView_offsetSet, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_BufferView, offsetUnset, arginfo_class_Buffers_BufferView_offsetUnset, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_BufferView, getIterator, arginfo_class_Buffers_BufferView_getIterator, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_BufferView, toDLPack, arginfo_class_Buffers_BufferView_toDLPack, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_BufferView, fromDLPack, arginfo_class_Buffers_BufferView_fromDLPack, ZEND_ACC_PUBLIC|ZEND_ACC_STATIC)
	ZEND_ME(Buffers_BufferView, jsonSerialize, arginfo_class_Buffers_BufferView_jsonSerialize, ZEND_ACC_PUBLIC)
	ZEND_FE_END
};
