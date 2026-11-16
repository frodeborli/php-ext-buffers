/* This is a generated file, edit the .stub.php file instead.
 * Stub hash: 60124954f2d747234234c88e1fd0377015029c73 */

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

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_class_Buffers_TensorAggregate_getTensor, 0, 0, Buffers\\Tensor, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_class_Buffers_TensorAggregate_getDType, 0, 0, Buffers\\DType, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_class_Buffers_TensorAggregate_getShape, 0, 0, Buffers\\Shape, 0)
ZEND_END_ARG_INFO()

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

ZEND_BEGIN_ARG_INFO_EX(arginfo_class_Buffers_DataView___construct, 0, 0, 1)
	ZEND_ARG_OBJ_INFO(0, buffer, Buffers\\Buffer, 0)
	ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, offset, IS_LONG, 0, "0")
	ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, length, IS_LONG, 1, "null")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_Buffers_DataView_getInt8, 0, 1, IS_LONG, 0)
	ZEND_ARG_TYPE_INFO(0, offset, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_Buffers_DataView_setInt8, 0, 2, IS_VOID, 0)
	ZEND_ARG_TYPE_INFO(0, offset, IS_LONG, 0)
	ZEND_ARG_TYPE_INFO(0, value, IS_LONG, 0)
ZEND_END_ARG_INFO()

#define arginfo_class_Buffers_DataView_getUInt8 arginfo_class_Buffers_DataView_getInt8

#define arginfo_class_Buffers_DataView_setUInt8 arginfo_class_Buffers_DataView_setInt8

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_Buffers_DataView_getInt16, 0, 1, IS_LONG, 0)
	ZEND_ARG_TYPE_INFO(0, offset, IS_LONG, 0)
	ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, bigEndian, _IS_BOOL, 0, "false")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_Buffers_DataView_setInt16, 0, 2, IS_VOID, 0)
	ZEND_ARG_TYPE_INFO(0, offset, IS_LONG, 0)
	ZEND_ARG_TYPE_INFO(0, value, IS_LONG, 0)
	ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, bigEndian, _IS_BOOL, 0, "false")
ZEND_END_ARG_INFO()

#define arginfo_class_Buffers_DataView_getInt32 arginfo_class_Buffers_DataView_getInt16

#define arginfo_class_Buffers_DataView_setInt32 arginfo_class_Buffers_DataView_setInt16

#define arginfo_class_Buffers_DataView_getInt64 arginfo_class_Buffers_DataView_getInt16

#define arginfo_class_Buffers_DataView_setInt64 arginfo_class_Buffers_DataView_setInt16

#define arginfo_class_Buffers_DataView_getUInt16 arginfo_class_Buffers_DataView_getInt16

#define arginfo_class_Buffers_DataView_setUInt16 arginfo_class_Buffers_DataView_setInt16

#define arginfo_class_Buffers_DataView_getUInt32 arginfo_class_Buffers_DataView_getInt16

#define arginfo_class_Buffers_DataView_setUInt32 arginfo_class_Buffers_DataView_setInt16

#define arginfo_class_Buffers_DataView_getUInt64 arginfo_class_Buffers_DataView_getInt16

#define arginfo_class_Buffers_DataView_setUInt64 arginfo_class_Buffers_DataView_setInt16

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_Buffers_DataView_getFloat16, 0, 1, IS_DOUBLE, 0)
	ZEND_ARG_TYPE_INFO(0, offset, IS_LONG, 0)
	ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, bigEndian, _IS_BOOL, 0, "false")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_Buffers_DataView_setFloat16, 0, 2, IS_VOID, 0)
	ZEND_ARG_TYPE_INFO(0, offset, IS_LONG, 0)
	ZEND_ARG_TYPE_INFO(0, value, IS_DOUBLE, 0)
	ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, bigEndian, _IS_BOOL, 0, "false")
ZEND_END_ARG_INFO()

#define arginfo_class_Buffers_DataView_getBFloat16 arginfo_class_Buffers_DataView_getFloat16

#define arginfo_class_Buffers_DataView_setBFloat16 arginfo_class_Buffers_DataView_setFloat16

#define arginfo_class_Buffers_DataView_getFloat32 arginfo_class_Buffers_DataView_getFloat16

#define arginfo_class_Buffers_DataView_setFloat32 arginfo_class_Buffers_DataView_setFloat16

#define arginfo_class_Buffers_DataView_getFloat64 arginfo_class_Buffers_DataView_getFloat16

#define arginfo_class_Buffers_DataView_setFloat64 arginfo_class_Buffers_DataView_setFloat16

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_Buffers_DataView_getFloat8E3M4, 0, 1, IS_DOUBLE, 0)
	ZEND_ARG_TYPE_INFO(0, offset, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_Buffers_DataView_setFloat8E3M4, 0, 2, IS_VOID, 0)
	ZEND_ARG_TYPE_INFO(0, offset, IS_LONG, 0)
	ZEND_ARG_TYPE_INFO(0, value, IS_DOUBLE, 0)
ZEND_END_ARG_INFO()

#define arginfo_class_Buffers_DataView_getFloat8E4M3 arginfo_class_Buffers_DataView_getFloat8E3M4

#define arginfo_class_Buffers_DataView_setFloat8E4M3 arginfo_class_Buffers_DataView_setFloat8E3M4

#define arginfo_class_Buffers_DataView_getFloat8E4M3B11FNUZ arginfo_class_Buffers_DataView_getFloat8E3M4

#define arginfo_class_Buffers_DataView_setFloat8E4M3B11FNUZ arginfo_class_Buffers_DataView_setFloat8E3M4

#define arginfo_class_Buffers_DataView_getFloat8E4M3FN arginfo_class_Buffers_DataView_getFloat8E3M4

#define arginfo_class_Buffers_DataView_setFloat8E4M3FN arginfo_class_Buffers_DataView_setFloat8E3M4

#define arginfo_class_Buffers_DataView_getFloat8E4M3FNUZ arginfo_class_Buffers_DataView_getFloat8E3M4

#define arginfo_class_Buffers_DataView_setFloat8E4M3FNUZ arginfo_class_Buffers_DataView_setFloat8E3M4

#define arginfo_class_Buffers_DataView_getFloat8E5M2 arginfo_class_Buffers_DataView_getFloat8E3M4

#define arginfo_class_Buffers_DataView_setFloat8E5M2 arginfo_class_Buffers_DataView_setFloat8E3M4

#define arginfo_class_Buffers_DataView_getFloat8E5M2FNUZ arginfo_class_Buffers_DataView_getFloat8E3M4

#define arginfo_class_Buffers_DataView_setFloat8E5M2FNUZ arginfo_class_Buffers_DataView_setFloat8E3M4

#define arginfo_class_Buffers_DataView_getFloat8E8M0FNU arginfo_class_Buffers_DataView_getFloat8E3M4

#define arginfo_class_Buffers_DataView_setFloat8E8M0FNU arginfo_class_Buffers_DataView_setFloat8E3M4

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_Buffers_DataView_getBytes, 0, 2, IS_STRING, 0)
	ZEND_ARG_TYPE_INFO(0, offset, IS_LONG, 0)
	ZEND_ARG_TYPE_INFO(0, length, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_Buffers_DataView_setBytes, 0, 2, IS_VOID, 0)
	ZEND_ARG_TYPE_INFO(0, offset, IS_LONG, 0)
	ZEND_ARG_TYPE_INFO(0, bytes, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_Buffers_DataView_fill, 0, 3, IS_VOID, 0)
	ZEND_ARG_TYPE_INFO(0, offset, IS_LONG, 0)
	ZEND_ARG_TYPE_INFO(0, length, IS_LONG, 0)
	ZEND_ARG_TYPE_INFO(0, byte, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_Buffers_DataView_copyBytes, 0, 3, IS_VOID, 0)
	ZEND_ARG_TYPE_INFO(0, target, IS_LONG, 0)
	ZEND_ARG_TYPE_INFO(0, source, IS_LONG, 0)
	ZEND_ARG_TYPE_INFO(0, length, IS_LONG, 0)
ZEND_END_ARG_INFO()

#define arginfo_class_Buffers_DataViewAccessor_offsetExists arginfo_class_Buffers_Shape_offsetExists

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_MASK_EX(arginfo_class_Buffers_DataViewAccessor_offsetGet, 0, 1, MAY_BE_LONG|MAY_BE_DOUBLE)
	ZEND_ARG_TYPE_INFO(0, offset, IS_MIXED, 0)
ZEND_END_ARG_INFO()

#define arginfo_class_Buffers_DataViewAccessor_offsetSet arginfo_class_Buffers_Shape_offsetSet

#define arginfo_class_Buffers_DataViewAccessor_offsetUnset arginfo_class_Buffers_Shape_offsetUnset

#define arginfo_class_Buffers_Tensor_getTensor arginfo_class_Buffers_TensorAggregate_getTensor

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_class_Buffers_Tensor_getDType, 0, 0, Buffers\\DType, 0)
ZEND_END_ARG_INFO()

#define arginfo_class_Buffers_Tensor_getShape arginfo_class_Buffers_TensorAggregate_getShape

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_class_Buffers_Tensor_slice, 0, 2, Buffers\\Tensor, 0)
	ZEND_ARG_TYPE_INFO(0, axis, IS_LONG, 0)
	ZEND_ARG_TYPE_INFO(0, start, IS_LONG, 0)
	ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, length, IS_LONG, 1, "null")
	ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, step, IS_LONG, 0, "1")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_class_Buffers_Tensor_transpose, 0, 0, Buffers\\Tensor, 0)
	ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, axis1, IS_LONG, 0, "-2")
	ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, axis2, IS_LONG, 0, "-1")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_class_Buffers_Tensor_permute, 0, 1, Buffers\\Tensor, 0)
	ZEND_ARG_TYPE_INFO(0, axes, IS_ARRAY, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_class_Buffers_Tensor_reshape, 0, 1, Buffers\\Tensor, 0)
	ZEND_ARG_TYPE_INFO(0, shape, IS_ARRAY, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_class_Buffers_Tensor_squeeze, 0, 1, Buffers\\Tensor, 0)
	ZEND_ARG_TYPE_INFO(0, axis, IS_LONG, 0)
ZEND_END_ARG_INFO()

#define arginfo_class_Buffers_Tensor_unsqueeze arginfo_class_Buffers_Tensor_squeeze

#define arginfo_class_Buffers_Tensor_broadcastTo arginfo_class_Buffers_Tensor_reshape

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_class_Buffers_Tensor_copy, 0, 0, Buffers\\Tensor, 0)
	ZEND_ARG_OBJ_INFO_WITH_DEFAULT_VALUE(0, into, Buffers\\Tensor, 1, "null")
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
ZEND_METHOD(Buffers_DataView, __construct);
ZEND_METHOD(Buffers_DataView, getInt8);
ZEND_METHOD(Buffers_DataView, setInt8);
ZEND_METHOD(Buffers_DataView, getUInt8);
ZEND_METHOD(Buffers_DataView, setUInt8);
ZEND_METHOD(Buffers_DataView, getInt16);
ZEND_METHOD(Buffers_DataView, setInt16);
ZEND_METHOD(Buffers_DataView, getInt32);
ZEND_METHOD(Buffers_DataView, setInt32);
ZEND_METHOD(Buffers_DataView, getInt64);
ZEND_METHOD(Buffers_DataView, setInt64);
ZEND_METHOD(Buffers_DataView, getUInt16);
ZEND_METHOD(Buffers_DataView, setUInt16);
ZEND_METHOD(Buffers_DataView, getUInt32);
ZEND_METHOD(Buffers_DataView, setUInt32);
ZEND_METHOD(Buffers_DataView, getUInt64);
ZEND_METHOD(Buffers_DataView, setUInt64);
ZEND_METHOD(Buffers_DataView, getFloat16);
ZEND_METHOD(Buffers_DataView, setFloat16);
ZEND_METHOD(Buffers_DataView, getBFloat16);
ZEND_METHOD(Buffers_DataView, setBFloat16);
ZEND_METHOD(Buffers_DataView, getFloat32);
ZEND_METHOD(Buffers_DataView, setFloat32);
ZEND_METHOD(Buffers_DataView, getFloat64);
ZEND_METHOD(Buffers_DataView, setFloat64);
ZEND_METHOD(Buffers_DataView, getFloat8E3M4);
ZEND_METHOD(Buffers_DataView, setFloat8E3M4);
ZEND_METHOD(Buffers_DataView, getFloat8E4M3);
ZEND_METHOD(Buffers_DataView, setFloat8E4M3);
ZEND_METHOD(Buffers_DataView, getFloat8E4M3B11FNUZ);
ZEND_METHOD(Buffers_DataView, setFloat8E4M3B11FNUZ);
ZEND_METHOD(Buffers_DataView, getFloat8E4M3FN);
ZEND_METHOD(Buffers_DataView, setFloat8E4M3FN);
ZEND_METHOD(Buffers_DataView, getFloat8E4M3FNUZ);
ZEND_METHOD(Buffers_DataView, setFloat8E4M3FNUZ);
ZEND_METHOD(Buffers_DataView, getFloat8E5M2);
ZEND_METHOD(Buffers_DataView, setFloat8E5M2);
ZEND_METHOD(Buffers_DataView, getFloat8E5M2FNUZ);
ZEND_METHOD(Buffers_DataView, setFloat8E5M2FNUZ);
ZEND_METHOD(Buffers_DataView, getFloat8E8M0FNU);
ZEND_METHOD(Buffers_DataView, setFloat8E8M0FNU);
ZEND_METHOD(Buffers_DataView, getBytes);
ZEND_METHOD(Buffers_DataView, setBytes);
ZEND_METHOD(Buffers_DataView, fill);
ZEND_METHOD(Buffers_DataView, copyBytes);
ZEND_METHOD(Buffers_DataViewAccessor, offsetExists);
ZEND_METHOD(Buffers_DataViewAccessor, offsetGet);
ZEND_METHOD(Buffers_DataViewAccessor, offsetSet);
ZEND_METHOD(Buffers_DataViewAccessor, offsetUnset);
ZEND_METHOD(Buffers_Tensor, getTensor);
ZEND_METHOD(Buffers_Tensor, getDType);
ZEND_METHOD(Buffers_Tensor, getShape);
ZEND_METHOD(Buffers_Tensor, slice);
ZEND_METHOD(Buffers_Tensor, transpose);
ZEND_METHOD(Buffers_Tensor, permute);
ZEND_METHOD(Buffers_Tensor, reshape);
ZEND_METHOD(Buffers_Tensor, squeeze);
ZEND_METHOD(Buffers_Tensor, unsqueeze);
ZEND_METHOD(Buffers_Tensor, broadcastTo);
ZEND_METHOD(Buffers_Tensor, copy);
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

static const zend_function_entry class_Buffers_TensorAggregate_methods[] = {
	ZEND_RAW_FENTRY("getTensor", NULL, arginfo_class_Buffers_TensorAggregate_getTensor, ZEND_ACC_PUBLIC|ZEND_ACC_ABSTRACT, NULL, NULL)
	ZEND_RAW_FENTRY("getDType", NULL, arginfo_class_Buffers_TensorAggregate_getDType, ZEND_ACC_PUBLIC|ZEND_ACC_ABSTRACT, NULL, NULL)
	ZEND_RAW_FENTRY("getShape", NULL, arginfo_class_Buffers_TensorAggregate_getShape, ZEND_ACC_PUBLIC|ZEND_ACC_ABSTRACT, NULL, NULL)
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

static const zend_function_entry class_Buffers_DataView_methods[] = {
	ZEND_ME(Buffers_DataView, __construct, arginfo_class_Buffers_DataView___construct, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_DataView, getInt8, arginfo_class_Buffers_DataView_getInt8, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_DataView, setInt8, arginfo_class_Buffers_DataView_setInt8, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_DataView, getUInt8, arginfo_class_Buffers_DataView_getUInt8, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_DataView, setUInt8, arginfo_class_Buffers_DataView_setUInt8, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_DataView, getInt16, arginfo_class_Buffers_DataView_getInt16, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_DataView, setInt16, arginfo_class_Buffers_DataView_setInt16, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_DataView, getInt32, arginfo_class_Buffers_DataView_getInt32, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_DataView, setInt32, arginfo_class_Buffers_DataView_setInt32, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_DataView, getInt64, arginfo_class_Buffers_DataView_getInt64, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_DataView, setInt64, arginfo_class_Buffers_DataView_setInt64, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_DataView, getUInt16, arginfo_class_Buffers_DataView_getUInt16, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_DataView, setUInt16, arginfo_class_Buffers_DataView_setUInt16, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_DataView, getUInt32, arginfo_class_Buffers_DataView_getUInt32, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_DataView, setUInt32, arginfo_class_Buffers_DataView_setUInt32, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_DataView, getUInt64, arginfo_class_Buffers_DataView_getUInt64, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_DataView, setUInt64, arginfo_class_Buffers_DataView_setUInt64, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_DataView, getFloat16, arginfo_class_Buffers_DataView_getFloat16, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_DataView, setFloat16, arginfo_class_Buffers_DataView_setFloat16, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_DataView, getBFloat16, arginfo_class_Buffers_DataView_getBFloat16, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_DataView, setBFloat16, arginfo_class_Buffers_DataView_setBFloat16, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_DataView, getFloat32, arginfo_class_Buffers_DataView_getFloat32, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_DataView, setFloat32, arginfo_class_Buffers_DataView_setFloat32, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_DataView, getFloat64, arginfo_class_Buffers_DataView_getFloat64, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_DataView, setFloat64, arginfo_class_Buffers_DataView_setFloat64, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_DataView, getFloat8E3M4, arginfo_class_Buffers_DataView_getFloat8E3M4, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_DataView, setFloat8E3M4, arginfo_class_Buffers_DataView_setFloat8E3M4, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_DataView, getFloat8E4M3, arginfo_class_Buffers_DataView_getFloat8E4M3, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_DataView, setFloat8E4M3, arginfo_class_Buffers_DataView_setFloat8E4M3, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_DataView, getFloat8E4M3B11FNUZ, arginfo_class_Buffers_DataView_getFloat8E4M3B11FNUZ, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_DataView, setFloat8E4M3B11FNUZ, arginfo_class_Buffers_DataView_setFloat8E4M3B11FNUZ, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_DataView, getFloat8E4M3FN, arginfo_class_Buffers_DataView_getFloat8E4M3FN, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_DataView, setFloat8E4M3FN, arginfo_class_Buffers_DataView_setFloat8E4M3FN, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_DataView, getFloat8E4M3FNUZ, arginfo_class_Buffers_DataView_getFloat8E4M3FNUZ, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_DataView, setFloat8E4M3FNUZ, arginfo_class_Buffers_DataView_setFloat8E4M3FNUZ, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_DataView, getFloat8E5M2, arginfo_class_Buffers_DataView_getFloat8E5M2, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_DataView, setFloat8E5M2, arginfo_class_Buffers_DataView_setFloat8E5M2, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_DataView, getFloat8E5M2FNUZ, arginfo_class_Buffers_DataView_getFloat8E5M2FNUZ, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_DataView, setFloat8E5M2FNUZ, arginfo_class_Buffers_DataView_setFloat8E5M2FNUZ, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_DataView, getFloat8E8M0FNU, arginfo_class_Buffers_DataView_getFloat8E8M0FNU, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_DataView, setFloat8E8M0FNU, arginfo_class_Buffers_DataView_setFloat8E8M0FNU, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_DataView, getBytes, arginfo_class_Buffers_DataView_getBytes, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_DataView, setBytes, arginfo_class_Buffers_DataView_setBytes, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_DataView, fill, arginfo_class_Buffers_DataView_fill, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_DataView, copyBytes, arginfo_class_Buffers_DataView_copyBytes, ZEND_ACC_PUBLIC)
	ZEND_FE_END
};

static const zend_function_entry class_Buffers_DataViewAccessor_methods[] = {
	ZEND_ME(Buffers_DataViewAccessor, offsetExists, arginfo_class_Buffers_DataViewAccessor_offsetExists, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_DataViewAccessor, offsetGet, arginfo_class_Buffers_DataViewAccessor_offsetGet, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_DataViewAccessor, offsetSet, arginfo_class_Buffers_DataViewAccessor_offsetSet, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_DataViewAccessor, offsetUnset, arginfo_class_Buffers_DataViewAccessor_offsetUnset, ZEND_ACC_PUBLIC)
	ZEND_FE_END
};

static const zend_function_entry class_Buffers_Tensor_methods[] = {
	ZEND_ME(Buffers_Tensor, getTensor, arginfo_class_Buffers_Tensor_getTensor, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_Tensor, getDType, arginfo_class_Buffers_Tensor_getDType, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_Tensor, getShape, arginfo_class_Buffers_Tensor_getShape, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_Tensor, slice, arginfo_class_Buffers_Tensor_slice, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_Tensor, transpose, arginfo_class_Buffers_Tensor_transpose, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_Tensor, permute, arginfo_class_Buffers_Tensor_permute, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_Tensor, reshape, arginfo_class_Buffers_Tensor_reshape, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_Tensor, squeeze, arginfo_class_Buffers_Tensor_squeeze, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_Tensor, unsqueeze, arginfo_class_Buffers_Tensor_unsqueeze, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_Tensor, broadcastTo, arginfo_class_Buffers_Tensor_broadcastTo, ZEND_ACC_PUBLIC)
	ZEND_ME(Buffers_Tensor, copy, arginfo_class_Buffers_Tensor_copy, ZEND_ACC_PUBLIC)
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
