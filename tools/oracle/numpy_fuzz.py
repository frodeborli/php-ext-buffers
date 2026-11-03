"""Differential fuzz of TypedView against NumPy (+ ml_dtypes).

Random dtype, buffer bytes, shape, strides and offset; PHP reads every element through chained
offsets and NumPy (as_strided over the same bytes) must see the same values. A second pass writes
random values through PHP and NumPy and compares the whole buffer afterwards.

  python3 numpy_fuzz.py [cases=3000] [seed=1] [path/to/dlpack.so]      (run from a clean directory; see run.sh)
"""
import json, os, random, struct, subprocess, sys, tempfile, warnings
import numpy as np, ml_dtypes as m
from numpy.lib.stride_tricks import as_strided

warnings.filterwarnings("ignore")
N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000
seed = int(sys.argv[2]) if len(sys.argv) > 2 else 1
so = sys.argv[3] if len(sys.argv) > 3 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../modules/dlpack.so")
php = os.environ.get("PHP", "php")
rnd = random.Random(seed)

# case name -> (numpy dtype of one component, components per element, size in bytes of the element, mask of valid bits)
T = {
    "Bool": (np.bool_, 1, 1), "Int8": (np.int8, 1, 1), "Int16": (np.int16, 1, 2), "Int32": (np.int32, 1, 4), "Int64": (np.int64, 1, 8),
    "UInt8": (np.uint8, 1, 1), "UInt16": (np.uint16, 1, 2), "UInt32": (np.uint32, 1, 4), "UInt64": (np.uint64, 1, 8),
    "Float16": (np.float16, 1, 2), "BFloat16": (m.bfloat16, 1, 2), "Float32": (np.float32, 1, 4), "Float64": (np.float64, 1, 8),
    "Complex32": (np.float16, 2, 4), "BComplex32": (m.bfloat16, 2, 4), "Complex64": (np.float32, 2, 8), "Complex128": (np.float64, 2, 16),
    "Float8E3M4": (m.float8_e3m4, 1, 1), "Float8E4M3": (m.float8_e4m3, 1, 1), "Float8E4M3B11FNUZ": (m.float8_e4m3b11fnuz, 1, 1),
    "Float8E4M3FN": (m.float8_e4m3fn, 1, 1), "Float8E4M3FNUZ": (m.float8_e4m3fnuz, 1, 1), "Float8E5M2": (m.float8_e5m2, 1, 1),
    "Float8E5M2FNUZ": (m.float8_e5m2fnuz, 1, 1), "Float8E8M0FNU": (m.float8_e8m0fnu, 1, 1),
    "Float6E2M3FN": (m.float6_e2m3fn, 1, 1), "Float6E3M2FN": (m.float6_e3m2fn, 1, 1), "Float4E2M1FN": (m.float4_e2m1fn, 1, 1),
}
UINT = {1: np.uint8, 2: np.uint16, 4: np.uint32, 8: np.uint64}
MASK = {"Float6E2M3FN": 0x3F, "Float6E3M2FN": 0x3F, "Float4E2M1FN": 0x0F, "Bool": 0x01}


def bits(x):
    return "nan" if x != x else struct.pack(">d", x).hex()


def make_case(name, writes):
    comp, ncomp, esize = T[name]
    ndim = rnd.randint(1, 4)
    shape = [rnd.choice([0, 1, 1, 2, 3, 4, 5, 7]) if rnd.random() < 0.9 else rnd.randint(1, 9) for _ in range(ndim)]
    if writes:   # a permuted C layout with gaps: elements never overlap
        order = list(range(ndim)); rnd.shuffle(order)
        strides = [0] * ndim; span = 1
        for ax in reversed(order):
            strides[ax] = span
            span = span * max(shape[ax], 1) + rnd.choice([0, 0, 1, 3])
    else:        # arbitrary strides, overlap allowed
        strides = [rnd.choice([1, 1, 2, 3, 4, 7, 16, 33]) for _ in range(ndim)]
    last = sum((s - 1) * st for s, st in zip(shape, strides)) + 1 if all(s > 0 for s in shape) else 0
    offset_el = rnd.randint(0, 5)
    nbytes = (offset_el + max(last, 1) + rnd.randint(0, 4)) * esize
    data = bytearray(rnd.getrandbits(8) for _ in range(nbytes))
    if name in MASK:
        for i in range(len(data)): data[i] &= MASK[name]
    case = dict(dtype=name, hex=bytes(data).hex(), shape=shape, strides=strides, offset=offset_el * esize)
    if writes:
        vals = []
        for _ in range(int(np.prod(shape))):
            def one():
                r = rnd.random()
                if name == "Bool": return rnd.random() < 0.5
                if name.startswith(("Int", "UInt")):
                    return rnd.choice([rnd.randint(-2**63, 2**63 - 1), rnd.randint(-300, 300), rnd.randint(-70000, 70000)])
                if r < 0.04: return rnd.choice(["nan", "inf", "-inf"])
                if r < 0.08: return bits(rnd.choice([0.0, -0.0, 5e-324, 1.7976931348623157e308]))
                return bits(rnd.choice([-1, 1]) * 2.0 ** rnd.uniform(-30, 30) * rnd.uniform(1, 2))
            vals.append([one(), one()] if ncomp == 2 else one())
        case["writes"] = vals
    return case


def expected_reads(case):
    buf = bytearray.fromhex(case["hex"])
    comp, ncomp, esize = T[case["dtype"]]
    csize = esize // ncomp
    tail = np.frombuffer(bytes(buf), dtype=np.uint8)[case["offset"]:]
    usable = len(tail) // csize * csize
    typed = tail[:usable].view(UINT[csize])              # as_strided cannot take ml_dtypes types: strided uint view, then reinterpret
    shape = tuple(case["shape"]) + ((2,) if ncomp == 2 else ())
    strides = tuple(s * esize for s in case["strides"]) + ((csize,) if ncomp == 2 else ())
    if int(np.prod(shape)) == 0: return []
    view = as_strided(typed, shape=shape, strides=strides).view(comp)
    out = []
    flat = np.array(view).reshape(-1, ncomp) if ncomp == 2 else np.array(view).reshape(-1)
    name = case["dtype"]
    for x in flat:
        if ncomp == 2: out.append([bits(float(x[0])), bits(float(x[1]))])
        elif name == "Bool": out.append(bool(x))
        elif name.startswith(("Int", "UInt")):
            v = int(x)
            if name == "UInt64" and v >= 2**63: v -= 2**64
            out.append(v)
        else: out.append(bits(float(x)))
    return out


def expected_after(case):
    buf = bytearray.fromhex(case["hex"])
    comp, ncomp, esize = T[case["dtype"]]
    csize = esize // ncomp
    name = case["dtype"]
    if int(np.prod(case["shape"])) == 0: return bytes(buf).hex()
    arr = np.frombuffer(buf, dtype=np.uint8).copy()
    tail = arr[case["offset"]:]
    usable = len(tail) // csize * csize
    typed = tail[:usable].view(UINT[csize])
    shape = tuple(case["shape"]) + ((2,) if ncomp == 2 else ())
    strides = tuple(s * esize for s in case["strides"]) + ((csize,) if ncomp == 2 else ())
    view = as_strided(typed, shape=shape, strides=strides, writeable=True).view(comp)

    def val(w):
        if isinstance(w, bool): return w
        if isinstance(w, int): return w
        return {"nan": float("nan"), "inf": float("inf"), "-inf": float("-inf")}.get(w) if w in ("nan", "inf", "-inf") else struct.unpack(">d", bytes.fromhex(w))[0]
    ws = case["writes"]
    if ncomp == 2:
        src = np.array([[val(a), val(b)] for a, b in ws], dtype=np.float64).reshape(shape)
        view[...] = src.astype(comp)
    elif name == "Bool":
        view[...] = np.array([val(w) for w in ws], dtype=np.bool_).reshape(shape)
    elif name.startswith(("Int", "UInt")):
        vals = np.array([val(w) & 0xFFFFFFFFFFFFFFFF for w in ws], dtype=np.uint64)   # two's complement bits, then C truncation
        view[...] = vals.astype(np.int64, casting="unsafe").astype(comp).reshape(shape) if name != "UInt64" else vals.reshape(shape)
    else:
        view[...] = np.array([val(w) for w in ws], dtype=np.float64).astype(comp).reshape(shape)
    out = bytearray(bytes.fromhex(case["hex"]))
    out[:case["offset"]] = arr[:case["offset"]].tobytes()
    out[case["offset"]:case["offset"] + usable] = typed.view(np.uint8).tobytes()
    out[case["offset"] + usable:] = arr[case["offset"] + usable:].tobytes()
    return bytes(out).hex()


def run(cases):
    with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False) as f:
        json.dump(cases, f); path = f.name
    r = subprocess.run([php, "-n", "-d", "extension=" + so, os.path.join(os.path.dirname(os.path.abspath(__file__)), "numpy_fuzz.php"), path],
                       capture_output=True, text=True)
    os.unlink(path)
    if r.returncode: print(r.stdout[-2000:], r.stderr[-2000:]); sys.exit(2)
    return json.loads(r.stdout)


names = list(T)
cases, kinds = [], []
for i in range(N):
    name = names[i % len(names)]
    writes = i % 2 == 1
    cases.append(make_case(name, writes)); kinds.append(writes)
res = run(cases)
bad = 0; nread = nwrite = nel = 0
for case, r, writes in zip(cases, res, kinds):
    er = expected_reads(case)
    nel += len(er)
    if er != r["reads"]:
        bad += 1; print("READ MISMATCH", case["dtype"], case["shape"], case["strides"], case["offset"])
        for a, b in zip(er, r["reads"]):
            if a != b: print("   numpy", a, "php", b); break
    nread += 1
    if writes:
        nwrite += 1
        ea = expected_after(case)
        if ea != r["after"]:
            bad += 1; print("WRITE MISMATCH", case["dtype"], case["shape"], case["strides"], case["offset"])
            for k in range(0, len(ea), 2):
                if ea[k:k+2] != r["after"][k:k+2]: print("   byte", k // 2, "numpy", ea[k:k+2], "php", r["after"][k:k+2]); break
print(f"{len(cases)} random views, {nel} elements read through chained offsets, {nwrite} write passes; mismatches: {bad}")
sys.exit(1 if bad else 0)
