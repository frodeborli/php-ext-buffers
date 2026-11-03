"""Compare codec.h against ml_dtypes (the reference) - decode of every code, encode of every float32.

  python3 oracle.py decode                 all codes of every <=16-bit format
  python3 oracle.py encode [fmt ...]       exhaustive over all 2^32 float32 inputs (minutes per format)
  python3 oracle.py doubles [fmt ...]      random doubles, midpoints and neighbours (double-rounding probes)
  python3 oracle.py ints [buffers.so]      the packed integers int2, int4, uint2, uint4 through the extension: every code, and writes
                                           of -1000..1000 (wrapping), against ml_dtypes

Run from a directory that does not shadow the stdlib; build with run.sh first.
"""
import ctypes, json, os, subprocess, sys, warnings
import numpy as np, ml_dtypes as m

warnings.filterwarnings("ignore")
here = os.path.dirname(os.path.abspath(__file__))
lib = ctypes.CDLL(os.path.join(here, "codec_oracle.so"))
lib.oracle_dec.restype = ctypes.c_double
lib.oracle_dec.argtypes = [ctypes.c_int, ctypes.c_uint]

FORMATS = ["float8_e3m4", "float8_e4m3", "float8_e4m3b11fnuz", "float8_e4m3fn", "float8_e4m3fnuz",
           "float8_e5m2", "float8_e5m2fnuz", "float6_e2m3fn", "float6_e3m2fn", "float4_e2m1fn",
           "float8_e8m0fnu", None, None]  # 11 = float16, 12 = bfloat16
DTYPES = [getattr(m, n) if n else None for n in FORMATS]
DTYPES[11], DTYPES[12] = np.float16, m.bfloat16
NAMES = FORMATS[:11] + ["float16", "bfloat16"]
BITS = [8, 8, 8, 8, 8, 8, 8, 6, 6, 4, 8, 16, 16]


def same_float(a, b):
    """equal values, NaN == NaN, and -0.0 != +0.0"""
    return (a.view(np.uint64) == b.view(np.uint64)) | (np.isnan(a) & np.isnan(b))


def decode():
    bad = 0
    for i, dt in enumerate(DTYPES):
        n = 1 << BITS[i]
        codes = np.arange(n, dtype=np.uint16)
        ref = (codes.astype(np.uint8) if BITS[i] <= 8 else codes).view(dt).astype(np.float64)
        mine = np.array([lib.oracle_dec(i, int(c)) for c in codes], dtype=np.float64)
        ok = same_float(ref, mine)
        print(f"decode {NAMES[i]:20s} {n:6d} codes  mismatches: {int((~ok).sum())}")
        for c in np.nonzero(~ok)[0][:5]:
            print("   code", hex(c), "ml_dtypes", ref[c], "mine", mine[c]); bad += 1
    return bad


def ints(so):
    """Packed integers go through the extension itself (they have no codec.h part): decode of every code and the wrap of
    every written value, compared with ml_dtypes' int2/int4/uint2/uint4."""
    php = os.environ.get("PHP", "php")
    code = r'''<?php use Buffers\{Buffer, DType};
$out = [];
foreach (["Int2", "Int4", "UInt2", "UInt4"] as $n) {
    $t = constant("Buffers\\DType::$n"); $mask = (1 << $t->bits()) - 1; $r = ["codes" => [], "writes" => []];
    for ($c = 0; $c <= $mask; $c++) $r["codes"][] = Buffer::fromString(chr($c))->view($t, 1)[0];
    $v = (new Buffer(1))->view($t, 1);
    for ($x = -1000; $x <= 1000; $x++) { $v[0] = $x; $r["writes"][] = $v[0]; }
    $out[$n] = $r;
}
echo json_encode($out);'''
    r = subprocess.run([php, "-n", "-d", "extension=" + so, "-r", code[5:]], capture_output=True, text=True)
    if r.returncode: print(r.stdout[-500:], r.stderr[-500:]); return 1
    res = json.loads(r.stdout)
    bad = 0
    for name, dt, lo, hi in [("Int2", m.int2, -2, 1), ("Int4", m.int4, -8, 7), ("UInt2", m.uint2, 0, 3), ("UInt4", m.uint4, 0, 15)]:
        bits = 2 if name.endswith("2") else 4
        vals = np.arange(lo, hi + 1)
        # ml_dtypes stores the value itself (padded); the code of a value is its low bits
        ref_codes = [int(x) & ((1 << bits) - 1) for x in np.array(vals).astype(dt).astype(np.int64)]
        mine = [res[name]["codes"][c] for c in ref_codes]
        miss = sum(1 for a, b in zip(mine, vals) if a != b)
        xs = np.arange(-1000, 1001)
        ref_wrap = np.array([int(v) for v in xs.astype(dt).astype(np.int64)])
        sat = int((ref_wrap != np.array(res[name]["writes"])).sum())
        wrap_model = [((int(v) & ((1 << bits) - 1)) - (1 << bits) if lo < 0 and (int(v) & ((1 << bits) - 1)) >= 1 << (bits - 1) else int(v) & ((1 << bits) - 1)) for v in xs]
        own = sum(1 for a, b in zip(wrap_model, res[name]["writes"]) if a != b)
        print(f"ints {name:6s} {1 << bits:3d} codes vs ml_dtypes: mismatches {miss}; wrap of -1000..1000 vs two's complement model: {own} mismatches; vs ml_dtypes astype: {sat} differ")
        bad += miss + own
    return bad


def encode_chunk(i, start, n):
    out = np.empty(n, dtype=np.uint16)
    lib.oracle_enc_f32(i, ctypes.c_uint32(start), ctypes.c_uint64(n), out.ctypes.data_as(ctypes.c_void_p))
    return out


def ref_codes(i, f):
    r = f.astype(DTYPES[i])
    return r.view(np.uint16 if BITS[i] > 8 else np.uint8).astype(np.uint16)


def encode(which):
    CH = 1 << 26
    bad = 0
    for i in which:
        mism = 0
        for start in range(0, 1 << 32, CH):
            u = np.arange(start, start + CH, dtype=np.uint64).astype(np.uint32)
            f = u.view(np.float32)
            a, b = encode_chunk(i, start, CH), ref_codes(i, f)
            d = np.nonzero(a != b)[0]
            mism += len(d)
            for k in d[:3]:
                print(f"   {NAMES[i]} f32 bits {hex(int(u[k]))} ml_dtypes {hex(int(b[k]))} mine {hex(int(a[k]))}")
        print(f"encode {NAMES[i]:20s} all 2^32 float32 inputs  mismatches: {mism}", flush=True)
        bad += mism
    return bad


def doubles(which):
    rng = np.random.default_rng(1234)
    bad = 0
    for i in which:
        # random doubles over many magnitudes, plus exact midpoints between consecutive decoded values
        # and the doubles just above/below them (the cases where double rounding could differ)
        e = rng.uniform(-40, 40, 20_000_000)
        x = rng.choice([-1.0, 1.0], e.size) * 2.0 ** e * rng.uniform(1, 2, e.size)
        codes = np.arange(1 << BITS[i], dtype=np.uint16)
        v = (codes.astype(np.uint8) if BITS[i] <= 8 else codes).view(DTYPES[i]).astype(np.float64)
        v = np.unique(v[np.isfinite(v)])
        mid = (v[1:] + v[:-1]) / 2
        extra = np.concatenate([mid, np.nextafter(mid, np.inf), np.nextafter(mid, -np.inf), v,
                                np.nextafter(v, np.inf), np.nextafter(v, -np.inf),
                                [0.0, -0.0, np.inf, -np.inf, np.nan, -np.nan, 5e-324, 1.7976931348623157e308]])
        if i == 11:   # NaNs with random payloads and signs: numpy carries the payload into the half NaN
            nan_bits = (rng.integers(1, 1 << 52, 200_000, dtype=np.uint64) | np.uint64(0x7FF0000000000000)) | (rng.integers(0, 2, 200_000, dtype=np.uint64) << np.uint64(63))
            extra = np.concatenate([extra, nan_bits.view(np.float64)])
        x = np.concatenate([x, extra])
        out = np.empty(x.size, dtype=np.uint16)
        lib.oracle_enc_f64(i, x.ctypes.data_as(ctypes.c_void_p), ctypes.c_uint64(x.size), out.ctypes.data_as(ctypes.c_void_p))
        ref = ref_codes(i, x) if i == 11 else ref_codes(i, x)  # numpy applies the format's own cast
        d = np.nonzero(out != ref)[0]
        for k in d[:3]:
            print(f"   {NAMES[i]} double {x[k]!r} ml_dtypes {hex(int(ref[k]))} mine {hex(int(out[k]))}")
        print(f"doubles {NAMES[i]:20s} {x.size} inputs  mismatches: {len(d)}", flush=True)
        bad += len(d)
    return bad


if __name__ == "__main__":
    cmd = sys.argv[1] if len(sys.argv) > 1 else "decode"
    if cmd == "ints":
        so = sys.argv[2] if len(sys.argv) > 2 else os.path.join(here, "../../modules/buffers.so")
        sys.exit(1 if ints(so) else 0)
    which = [NAMES.index(a) for a in sys.argv[2:]] or list(range(13))
    bad = {"decode": decode, "encode": lambda: encode(which), "doubles": lambda: doubles(which)}[cmd]()
    sys.exit(1 if bad else 0)
