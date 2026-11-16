"""view() argument fuzz: hostile shapes, strides and offsets against an exact big-integer model.

Accept/reject must match the rules (including 0-d views and the packed float6/float4 rules), and for
accepted views the last element read through chained offsets must equal the bytes at the modelled
position (no overflow, no out-of-bounds access).

  python3 view_fuzz.py [cases=20000] [seed=1] [path/to/buffers.so]
"""
import json, os, random, struct, subprocess, sys, tempfile
import numpy as np, ml_dtypes as m
N = int(sys.argv[1]) if len(sys.argv) > 1 else 20000
seed = int(sys.argv[2]) if len(sys.argv) > 2 else 1
so = sys.argv[3] if len(sys.argv) > 3 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../modules/buffers.so")
php = os.environ.get("PHP", "php")
rnd = random.Random(seed)
I64 = 2**63 - 1
INT = {"Int8": ("b", 1), "Int16": ("<h", 2), "Int32": ("<i", 4), "Int64": ("<q", 8), "UInt8": ("B", 1), "UInt16": ("<H", 2), "UInt32": ("<I", 4), "UInt64": ("<Q", 8)}
PACKED = {"Float6E2M3FN": (m.float6_e2m3fn, 6), "Float6E3M2FN": (m.float6_e3m2fn, 6), "Float4E2M1FN": (m.float4_e2m1fn, 4),
          "Int2": (None, 2), "Int4": (None, 4), "UInt2": (None, 2), "UInt4": (None, 4)}
POOL = [0, 1, 1, 2, 3, 4, 5, 7, 8, 15, 16, 31, 33, 100, 255, 256, 1000, 2**16, 2**31 - 1, 2**31, 2**32, 2**32 + 1, 2**53, 2**62, 2**63 - 2, I64, -1, -2, -8]


def num():
    return rnd.choice(POOL) if rnd.random() < 0.6 else rnd.randint(0, 40)


cases = []
for _ in range(N):
    name = rnd.choice(list(INT) + list(PACKED) * 3)
    ln = rnd.choice([0, 1, 7, 8, 16, 24, 64, 100, 4096])
    esize = INT[name][1] if name in INT else 1
    ndim = rnd.choice([0, 1, 1, 2, 2, 3, 4, 8, 16, 17])
    shape = [num() for _ in range(ndim)]
    mode = rnd.random()
    if mode < 0.15: shape = rnd.choice([None, num()])
    strides = None if rnd.random() < 0.5 else [num() for _ in range(len(shape) if isinstance(shape, list) else 1)]
    if strides is not None and rnd.random() < 0.15 and strides: strides.pop()
    offset = rnd.choice([0, 0, 0, esize, 2 * esize, 1, 3, ln, ln + 1, -1, 2**62, I64]) if rnd.random() < 0.7 else 0
    cases.append(dict(dtype=name, len=ln, shape=shape, strides=strides, offset=offset))


def model(c):
    """returns ('ok', last corner, strides in elements, offset) or 'reject'"""
    packed = c["dtype"] in PACKED
    size = INT[c["dtype"]][1] if not packed else 0; ln = c["len"]; off = c["offset"]
    nbits = PACKED[c["dtype"]][1] if packed else size * 8
    if off < 0 or off > ln: return "reject"   # any byte offset (views need not be aligned)
    sh = c["shape"]
    if sh is None:
        if packed:
            if (ln - off) * 8 % nbits: return "reject"
            shape = [(ln - off) * 8 // nbits]
        elif (ln - off) % size: return "reject"
        else: shape = [(ln - off) // size]
    elif isinstance(sh, int):
        if sh < 0: return "reject"
        shape = [sh]
    else:
        if len(sh) > 16 or any(x < 0 for x in sh): return "reject"
        shape = list(sh)
    nd = len(shape)
    st = c["strides"]
    if st is not None:
        if any(x < 0 for x in st): return "reject"
        if len(st) != nd: return "reject"
        elems = list(st)
    else:
        elems = [0] * nd; acc = 1
        for i in range(nd - 1, -1, -1):
            elems[i] = acc
            acc *= max(shape[i], 1)
            if acc > I64: return "reject"
    if packed:
        if nd and elems[-1] != 1: return "reject"
        for e in elems[:-1]:
            if e * nbits > I64 or e * nbits % 8: return "reject"
    elif any(e * size > I64 for e in elems): return "reject"
    if 0 in shape: return ("ok", "empty", elems, off)
    if nd == 0: return ("ok", "zero-d", elems, off) if off + max(size, 1) <= ln else "reject"
    if packed:
        if shape[-1] * nbits > I64: return "reject"
        rowb = (shape[-1] * nbits + 7) // 8
        last = sum((s - 1) * e * nbits // 8 for s, e in zip(shape[:-1], elems[:-1]))
        if last > I64 - rowb or off + last + rowb > ln: return "reject"
        bit = off * 8 + sum((s - 1) * e * nbits for s, e in zip(shape, elems))
        return ("ok", bit, elems, off)
    last = sum((s - 1) * e * size for s, e in zip(shape, elems))
    if last > I64 - size or off + last + size > ln: return "reject"
    return ("ok", (off + last), elems, off)


with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False) as f:
    json.dump(cases, f); path = f.name
res = []
for k in range(0, len(cases), 5000):
    with open(path, "w") as f: json.dump(cases[k:k + 5000], f)
    r = subprocess.run([php, "-n", "-d", "memory_limit=-1", "-d", "extension=" + so, os.path.join(os.path.dirname(os.path.abspath(__file__)), "view_fuzz.php"), path], capture_output=True, text=True)
    if r.returncode: print(r.stdout[-1500:], r.stderr[-1500:]); sys.exit(2)
    res += json.loads(r.stdout)
os.unlink(path)

bad = acc = rej = 0
for c, got in zip(cases, res):
    e = model(c)
    if e == "reject":
        rej += 1
        if got.startswith("ok"): bad += 1; print("ACCEPTED but should reject:", c, got)
        elif got not in ("ValueError", "TypeError"): bad += 1; print("unexpected", got, c)
        continue
    acc += 1
    if not got.startswith("ok"): bad += 1; print("REJECTED but should accept:", c, got); continue
    parts = got.split(" ", 2)
    if e[1] == "empty":
        want = f"ok empty {json.dumps(e[2], separators=(',', ':'))} {e[3]}"
        if got.replace(", ", ",") != want: bad += 1; print("empty view metadata differs:", c, got, want)
        continue
    if e[1] == "zero-d":
        want = f"ok zero-d {json.dumps(e[2], separators=(',', ':'))} {e[3]}"
        if got.replace(", ", ",") != want: bad += 1; print("0-d view metadata differs:", c, got, want)
        continue
    if c["dtype"] in PACKED:   # the buffer holds byte i = i % 251; decode the code at the last corner's bit position
        comp, nbits = PACKED[c["dtype"]]
        bitpos = e[1]
        w = ((bitpos >> 3) % 251) | (((bitpos >> 3) + 1) % 251) << 8
        code = (w >> (bitpos & 7)) & ((1 << nbits) - 1)
        if comp is None:   # packed integers: sign-extended for Int*
            x = code - (1 << nbits) if c["dtype"][0] == "I" and code >= 1 << (nbits - 1) else code
            shown = str(x)
        else:
            x = float(np.array([code], dtype=np.uint8).view(comp).astype(np.float64)[0])
            shown = struct.pack('>d', x).hex()
        want = f"ok {shown} {json.dumps(e[2], separators=(',', ':'))} {e[3]}"
        if got.replace(", ", ",") != want: bad += 1; print("packed read differs:", c, "got", got, "want", want)
        continue
    fmt, size = INT[c["dtype"]]
    # the buffer holds byte i = i % 251, so the last corner element can be computed here
    p = e[1]
    value = struct.unpack(fmt, bytes((p + j) % 251 for j in range(size)))[0]
    want = f"ok {value} {json.dumps(e[2], separators=(',', ':'))} {e[3]}"
    if got.replace(", ", ",") != want:
        # UInt64 reads come back as the signed 64-bit pattern
        if c["dtype"] == "UInt64" and value >= 2**63: want = f"ok {value - 2**64} {json.dumps(e[2], separators=(',', ':'))} {e[3]}"
        if got.replace(", ", ",") != want: bad += 1; print("differs:", c, "got", got, "want", want)
print(f"{len(cases)} hostile view() calls: {acc} accepted, {rej} rejected; mismatches against the model: {bad}")
sys.exit(1 if bad else 0)
