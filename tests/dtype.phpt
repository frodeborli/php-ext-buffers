--TEST--
DType: cases, backing values and predicates
--SKIPIF--
<?php if (!extension_loaded('dlpack')) die('skip dlpack extension not loaded'); ?>
--FILE--
<?php
$n = 0;
foreach (DType::cases() as $c) {
    $n++;
    printf("%-20s %-20s bits=%-3d bytes=%-2d int=%d float=%d complex=%d dl=%s\n", $c->name, $c->value, $c->bits(),
        $c->byteSize(), $c->isInteger(), $c->isFloatingPoint(), $c->isComplex(), json_encode($c->dlpack()));
    var_dump($c->isByteAligned() === true, DType::from($c->value) === $c, DType::tryFrom($c->value . 'x'));
}
echo $n, " cases\n";
echo implode(', ', array_map(fn($c) => $c->name, DType::cases())) === 'Bool, Int8, Int16, Int32, Int64, UInt8, UInt16, UInt32, UInt64, Float16, Float32, Float64, BFloat16, Float8E3M4, Float8E4M3, Float8E4M3B11FNUZ, Float8E4M3FN, Float8E4M3FNUZ, Float8E5M2, Float8E5M2FNUZ, Float8E8M0FNU, Float6E2M3FN, Float6E3M2FN, Float4E2M1FN, Complex32, Complex64, Complex128, BComplex32' ? "order ok\n" : "ORDER WRONG\n";
var_dump(DType::UInt8 instanceof UnitEnum, DType::UInt8 instanceof BackedEnum);
--EXPECT--
Bool                 bool                 bits=8   bytes=1  int=0 float=0 complex=0 dl={"code":6,"bits":8,"lanes":1}
bool(true)
bool(true)
NULL
Int8                 int8                 bits=8   bytes=1  int=1 float=0 complex=0 dl={"code":0,"bits":8,"lanes":1}
bool(true)
bool(true)
NULL
Int16                int16                bits=16  bytes=2  int=1 float=0 complex=0 dl={"code":0,"bits":16,"lanes":1}
bool(true)
bool(true)
NULL
Int32                int32                bits=32  bytes=4  int=1 float=0 complex=0 dl={"code":0,"bits":32,"lanes":1}
bool(true)
bool(true)
NULL
Int64                int64                bits=64  bytes=8  int=1 float=0 complex=0 dl={"code":0,"bits":64,"lanes":1}
bool(true)
bool(true)
NULL
UInt8                uint8                bits=8   bytes=1  int=1 float=0 complex=0 dl={"code":1,"bits":8,"lanes":1}
bool(true)
bool(true)
NULL
UInt16               uint16               bits=16  bytes=2  int=1 float=0 complex=0 dl={"code":1,"bits":16,"lanes":1}
bool(true)
bool(true)
NULL
UInt32               uint32               bits=32  bytes=4  int=1 float=0 complex=0 dl={"code":1,"bits":32,"lanes":1}
bool(true)
bool(true)
NULL
UInt64               uint64               bits=64  bytes=8  int=1 float=0 complex=0 dl={"code":1,"bits":64,"lanes":1}
bool(true)
bool(true)
NULL
Float16              float16              bits=16  bytes=2  int=0 float=1 complex=0 dl={"code":2,"bits":16,"lanes":1}
bool(true)
bool(true)
NULL
Float32              float32              bits=32  bytes=4  int=0 float=1 complex=0 dl={"code":2,"bits":32,"lanes":1}
bool(true)
bool(true)
NULL
Float64              float64              bits=64  bytes=8  int=0 float=1 complex=0 dl={"code":2,"bits":64,"lanes":1}
bool(true)
bool(true)
NULL
BFloat16             bfloat16             bits=16  bytes=2  int=0 float=1 complex=0 dl={"code":4,"bits":16,"lanes":1}
bool(true)
bool(true)
NULL
Float8E3M4           float8_e3m4          bits=8   bytes=1  int=0 float=1 complex=0 dl={"code":7,"bits":8,"lanes":1}
bool(true)
bool(true)
NULL
Float8E4M3           float8_e4m3          bits=8   bytes=1  int=0 float=1 complex=0 dl={"code":8,"bits":8,"lanes":1}
bool(true)
bool(true)
NULL
Float8E4M3B11FNUZ    float8_e4m3b11fnuz   bits=8   bytes=1  int=0 float=1 complex=0 dl={"code":9,"bits":8,"lanes":1}
bool(true)
bool(true)
NULL
Float8E4M3FN         float8_e4m3fn        bits=8   bytes=1  int=0 float=1 complex=0 dl={"code":10,"bits":8,"lanes":1}
bool(true)
bool(true)
NULL
Float8E4M3FNUZ       float8_e4m3fnuz      bits=8   bytes=1  int=0 float=1 complex=0 dl={"code":11,"bits":8,"lanes":1}
bool(true)
bool(true)
NULL
Float8E5M2           float8_e5m2          bits=8   bytes=1  int=0 float=1 complex=0 dl={"code":12,"bits":8,"lanes":1}
bool(true)
bool(true)
NULL
Float8E5M2FNUZ       float8_e5m2fnuz      bits=8   bytes=1  int=0 float=1 complex=0 dl={"code":13,"bits":8,"lanes":1}
bool(true)
bool(true)
NULL
Float8E8M0FNU        float8_e8m0fnu       bits=8   bytes=1  int=0 float=1 complex=0 dl={"code":14,"bits":8,"lanes":1}
bool(true)
bool(true)
NULL
Float6E2M3FN         float6_e2m3fn        bits=6   bytes=1  int=0 float=1 complex=0 dl={"code":15,"bits":6,"lanes":1}
bool(true)
bool(true)
NULL
Float6E3M2FN         float6_e3m2fn        bits=6   bytes=1  int=0 float=1 complex=0 dl={"code":16,"bits":6,"lanes":1}
bool(true)
bool(true)
NULL
Float4E2M1FN         float4_e2m1fn        bits=4   bytes=1  int=0 float=1 complex=0 dl={"code":17,"bits":4,"lanes":1}
bool(true)
bool(true)
NULL
Complex32            complex32            bits=32  bytes=4  int=0 float=0 complex=1 dl={"code":5,"bits":32,"lanes":1}
bool(true)
bool(true)
NULL
Complex64            complex64            bits=64  bytes=8  int=0 float=0 complex=1 dl={"code":5,"bits":64,"lanes":1}
bool(true)
bool(true)
NULL
Complex128           complex128           bits=128 bytes=16 int=0 float=0 complex=1 dl={"code":5,"bits":128,"lanes":1}
bool(true)
bool(true)
NULL
BComplex32           bcomplex32           bits=32  bytes=4  int=0 float=0 complex=1 dl={"code":18,"bits":32,"lanes":1}
bool(true)
bool(true)
NULL
28 cases
order ok
bool(true)
bool(true)
