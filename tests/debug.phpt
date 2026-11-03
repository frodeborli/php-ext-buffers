--TEST--
var_dump, print_r and var_export of BufferView, Shape and friends
--SKIPIF--
<?php if (!extension_loaded('buffers') || !function_exists('buffers_test_make')) die('skip needs a build with --enable-buffers-testing'); ?>
--FILE--
<?php
use Buffers\{ArrayBuffer, DType, BufferView, Shape};
$b = new ArrayBuffer(64);
$raw = $b->view(DType::UInt8); for ($i = 0; $i < 64; $i++) $raw[$i] = $i;
$v = $b->view(DType::Int16, [2, 3], [4, 1], 2);
for ($i = 0; $i < 2; $i++) for ($j = 0; $j < 3; $j++) $v[$i][$j] = ($i + 1) * 100 + $j;
var_dump($v);
print_r($v); echo "\n";
var_export($v); echo "\n";
echo "0-d\n";
var_dump($b->view(DType::UInt8, [], null, 5));
echo "packed\n";
var_dump($b->view(DType::Float4E2M1FN, [2, 4]));
var_dump($b->view(DType::Int4, 6));
echo "complex\n";
$c = (new ArrayBuffer(16))->view(DType::Complex64, 2); $c[0] = [1.5, -2]; $c[1] = 3;
var_dump($c);
echo "empty\n";
var_dump($b->view(DType::UInt8, [0, 3]));
echo "boundary: 1000 elements are shown, 1001 are not\n";
$big = (new ArrayBuffer(2000))->view(DType::UInt8, 1000);
var_dump(count($big), is_array(array_values((array)$big)));
ob_start(); var_dump($big); $out = ob_get_clean(); echo strlen($out) > 5000 ? "1000 shown\n" : "NOT SHOWN\n";
var_dump((new ArrayBuffer(2000))->view(DType::UInt8, 1001));
var_dump((new ArrayBuffer(2000))->view(DType::UInt8, [10, 100]) instanceof BufferView);
ob_start(); var_dump((new ArrayBuffer(1100))->view(DType::UInt8, [10, 100])); $o2 = ob_get_clean(); echo substr_count($o2, 'int('), " ints in the output of 1000 elements\n";
echo "read-only and a sub-view\n";
$ro = BufferView::fromDLPack(buffers_test_make(1, 8, 1, [2, 2], [2, 1], 1));
var_dump($ro[1]);
echo "device memory\n";
var_dump(BufferView::fromDLPack(buffers_test_make(1, 8, 1, [4], [1], 0, 2, 0, 3, 1, 3)));
var_dump(BufferView::fromDLPack(buffers_test_make(1, 8, 1, [4], [1], 0, 99, 0, 3, 1, 3)));
echo "Shape\n";
var_dump(new Shape([2, 3]));
print_r(new Shape([4])); echo "\n";
var_export(new Shape([])); echo "\n";
var_dump((array)new Shape([1, 2]), get_object_vars(new Shape([5])));
echo "debug output leaves the view usable\n";
var_dump($v[1][2], $v->offset);
echo "end\n";
--EXPECTF--
object(Buffers\BufferView)#%d (7) {
  ["dtype"]=>
  enum(Buffers\DType::Int16)
  ["shape"]=>
  array(2) {
    [0]=>
    int(2)
    [1]=>
    int(3)
  }
  ["strides"]=>
  array(2) {
    [0]=>
    int(4)
    [1]=>
    int(1)
  }
  ["offset"]=>
  int(2)
  ["device"]=>
  string(3) "cpu"
  ["writable"]=>
  bool(true)
  ["data"]=>
  array(2) {
    [0]=>
    array(3) {
      [0]=>
      int(100)
      [1]=>
      int(101)
      [2]=>
      int(102)
    }
    [1]=>
    array(3) {
      [0]=>
      int(200)
      [1]=>
      int(201)
      [2]=>
      int(202)
    }
  }
}
Buffers\BufferView Object
(
    [dtype] => Buffers\DType Enum:string
        (
            [name] => Int16
            [value] => int16
        )

    [shape] => Array
        (
            [0] => 2
            [1] => 3
        )

    [strides] => Array
        (
            [0] => 4
            [1] => 1
        )

    [offset] => 2
    [device] => cpu
    [writable] => 1
    [data] => Array
        (
            [0] => Array
                (
                    [0] => 100
                    [1] => 101
                    [2] => 102
                )

            [1] => Array
                (
                    [0] => 200
                    [1] => 201
                    [2] => 202
                )

        )

)

\Buffers\BufferView::__set_state(array(
   'dtype' => 
  \Buffers\DType::Int16,
   'shape' => 
  \Buffers\Shape::__set_state(array(
     'dims' => 
    array (
      0 => 2,
      1 => 3,
    ),
  )),
   'ndim' => 2,
   'strides' => 
  array (
    0 => 4,
    1 => 1,
  ),
   'offset' => 2,
   'buffer' => 
  \Buffers\ArrayBuffer::__set_state(array(
  )),
))
0-d
object(Buffers\BufferView)#%d (7) {
  ["dtype"]=>
  enum(Buffers\DType::UInt8)
  ["shape"]=>
  array(0) {
  }
  ["strides"]=>
  array(0) {
  }
  ["offset"]=>
  int(5)
  ["device"]=>
  string(3) "cpu"
  ["writable"]=>
  bool(true)
  ["data"]=>
  int(0)
}
packed
object(Buffers\BufferView)#%d (7) {
  ["dtype"]=>
  enum(Buffers\DType::Float4E2M1FN)
  ["shape"]=>
  array(2) {
    [0]=>
    int(2)
    [1]=>
    int(4)
  }
  ["strides"]=>
  array(2) {
    [0]=>
    int(4)
    [1]=>
    int(1)
  }
  ["offset"]=>
  int(0)
  ["device"]=>
  string(3) "cpu"
  ["writable"]=>
  bool(true)
  ["data"]=>
  array(2) {
    [0]=>
    array(4) {
      [0]=>
      float(0)
      [1]=>
      float(0)
      [2]=>
      float(0.5)
      [3]=>
      float(0)
    }
    [1]=>
    array(4) {
      [0]=>
      float(2)
      [1]=>
      float(4)
      [2]=>
      float(0)
      [3]=>
      float(0)
    }
  }
}
object(Buffers\BufferView)#%d (7) {
  ["dtype"]=>
  enum(Buffers\DType::Int4)
  ["shape"]=>
  array(1) {
    [0]=>
    int(6)
  }
  ["strides"]=>
  array(1) {
    [0]=>
    int(1)
  }
  ["offset"]=>
  int(0)
  ["device"]=>
  string(3) "cpu"
  ["writable"]=>
  bool(true)
  ["data"]=>
  array(6) {
    [0]=>
    int(0)
    [1]=>
    int(0)
    [2]=>
    int(1)
    [3]=>
    int(0)
    [4]=>
    int(4)
    [5]=>
    int(6)
  }
}
complex
object(Buffers\BufferView)#%d (7) {
  ["dtype"]=>
  enum(Buffers\DType::Complex64)
  ["shape"]=>
  array(1) {
    [0]=>
    int(2)
  }
  ["strides"]=>
  array(1) {
    [0]=>
    int(1)
  }
  ["offset"]=>
  int(0)
  ["device"]=>
  string(3) "cpu"
  ["writable"]=>
  bool(true)
  ["data"]=>
  array(2) {
    [0]=>
    array(2) {
      [0]=>
      float(1.5)
      [1]=>
      float(-2)
    }
    [1]=>
    array(2) {
      [0]=>
      float(3)
      [1]=>
      float(0)
    }
  }
}
empty
object(Buffers\BufferView)#%d (7) {
  ["dtype"]=>
  enum(Buffers\DType::UInt8)
  ["shape"]=>
  array(2) {
    [0]=>
    int(0)
    [1]=>
    int(3)
  }
  ["strides"]=>
  array(2) {
    [0]=>
    int(3)
    [1]=>
    int(1)
  }
  ["offset"]=>
  int(0)
  ["device"]=>
  string(3) "cpu"
  ["writable"]=>
  bool(true)
  ["data"]=>
  array(0) {
  }
}
boundary: 1000 elements are shown, 1001 are not
int(1000)
bool(true)
1000 shown
object(Buffers\BufferView)#%d (7) {
  ["dtype"]=>
  enum(Buffers\DType::UInt8)
  ["shape"]=>
  array(1) {
    [0]=>
    int(1001)
  }
  ["strides"]=>
  array(1) {
    [0]=>
    int(1)
  }
  ["offset"]=>
  int(0)
  ["device"]=>
  string(3) "cpu"
  ["writable"]=>
  bool(true)
  ["data"]=>
  string(15) "(1001 elements)"
}
bool(true)
1005 ints in the output of 1000 elements
read-only and a sub-view
object(Buffers\BufferView)#%d (7) {
  ["dtype"]=>
  enum(Buffers\DType::UInt8)
  ["shape"]=>
  array(1) {
    [0]=>
    int(2)
  }
  ["strides"]=>
  array(1) {
    [0]=>
    int(1)
  }
  ["offset"]=>
  int(2)
  ["device"]=>
  string(3) "cpu"
  ["writable"]=>
  bool(false)
  ["data"]=>
  array(2) {
    [0]=>
    int(2)
    [1]=>
    int(3)
  }
}
device memory
object(Buffers\BufferView)#%d (7) {
  ["dtype"]=>
  enum(Buffers\DType::UInt8)
  ["shape"]=>
  array(1) {
    [0]=>
    int(4)
  }
  ["strides"]=>
  array(1) {
    [0]=>
    int(1)
  }
  ["offset"]=>
  int(0)
  ["device"]=>
  string(6) "cuda:3"
  ["writable"]=>
  bool(true)
  ["data"]=>
  string(21) "(not host-accessible)"
}
object(Buffers\BufferView)#%d (7) {
  ["dtype"]=>
  enum(Buffers\DType::UInt8)
  ["shape"]=>
  array(1) {
    [0]=>
    int(4)
  }
  ["strides"]=>
  array(1) {
    [0]=>
    int(1)
  }
  ["offset"]=>
  int(0)
  ["device"]=>
  string(10) "device99:3"
  ["writable"]=>
  bool(true)
  ["data"]=>
  string(21) "(not host-accessible)"
}
Shape
object(Buffers\Shape)#%d (1) {
  ["dims"]=>
  array(2) {
    [0]=>
    int(2)
    [1]=>
    int(3)
  }
}
Buffers\Shape Object
(
    [dims] => Array
        (
            [0] => 4
        )

)

\Buffers\Shape::__set_state(array(
   'dims' => 
  array (
  ),
))
array(1) {
  ["dims"]=>
  array(2) {
    [0]=>
    int(1)
    [1]=>
    int(2)
  }
}
array(1) {
  ["dims"]=>
  array(1) {
    [0]=>
    int(5)
  }
}
debug output leaves the view usable
int(202)
int(2)
end
