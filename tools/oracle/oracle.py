"""Compare codec.h against ml_dtypes (the reference) - decode of every code, encode of every float32.

  python3 oracle.py decode                 all codes of every <=16-bit format
  python3 oracle.py encode [fmt ...]       exhaustive over all 2^32 float32 inputs (minutes per format)
  python3 oracle.py doubles [fmt ...]      random doubles, midpoints and neighbours (double-rounding probes)

Run from a directory that does not shadow the stdlib; build with run.sh first.
"""
import ctypes, os, sys, warnings
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
    which = [NAMES.index(a) for a in sys.argv[2:]] or list(range(13))
    bad = {"decode": decode, "encode": lambda: encode(which), "doubles": lambda: doubles(which)}[cmd]()
    sys.exit(1 if bad else 0)
