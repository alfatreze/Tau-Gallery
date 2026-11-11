#!/usr/bin/env python3
"""Tests the C decoder of the frozen TGY symbol layer (fw2/gallery/tgy_dec.c) against the conformance vectors (docs/conformance/tgy_v0_vectors.json), 60 more random tiles from the reference encoder, and fuzzed streams
under AddressSanitizer and UBSan (the decoder must terminate and stay in its buffers). `--mutants` edits copies of the decoder; each edit must fail the vectors. Also reports the RISC-V (rv32im) code size when the toolchain is found."""
import base64, io, json, os, struct, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools/gallery")); sys.path.insert(0, str(ROOT / "sim"))
import tgy_v0 as V, make_tgy_v0_vectors as G, test_tgymodel as T

def build_vectors(path):
    vecs = []
    for v in json.loads((ROOT / "docs/conformance/tgy_v0_vectors.json").read_text())["vectors"]:
        shapes = [tuple(s) for s in v["shapes"]]; vecs.append((shapes, bytes.fromhex(v["stream"]), G.unpack(v["blocks"], shapes)))
    for seed in range(60):
        grids, shapes = T.synth_tile(300 + seed, rows=(1 + seed % 5, 1 + seed % 3, 1 + seed % 3), cols=(1 + seed % 7, 1 + seed % 4, 1 + seed % 4)); vecs.append((shapes, V.encode_tile(grids, shapes), grids))
    out = bytearray(struct.pack("<I", len(vecs)))
    for shapes, stream, grids in vecs:
        out += struct.pack("<I", len(shapes)) + b"".join(struct.pack("<II", r, c) for r, c in shapes) + struct.pack("<I", len(stream)) + stream
        blocks = [b for g in grids for row in g for b in row]; out += struct.pack("<I", len(blocks)) + b"".join(struct.pack("<64h", *b) for b in blocks)
    path.write_bytes(bytes(out))

def tables_ok(src):
    """The constant tables and the context offsets of the C source equal the conformance file's constants (the rate table, the position groups, the neighbour table) and the reference's offsets: a changed entry is found directly,
    which random vectors only find by luck (a rate entry off by one changes a decision about once in thousands)."""
    import re
    doc = json.loads((ROOT / "docs/conformance/tgy_v0_vectors.json").read_text()); nums = lambda name: [int(x) for x in re.findall(r"-?\d+", re.search(name + r"[^=]*= \{(.*?)\};", src, re.S).group(1))]
    ln = nums("LN")
    ok = nums("REC") == doc["rate_table"] and nums("KG") == doc["position_groups"] and [tuple(ln[i:i + 2]) for i in range(0, len(ln), 2)] == [tuple(x) for x in doc["neighbour_table"]]
    offs = dict(re.findall(r"O_(\w+) = (\d+)", src)); ref = dict(D0=V.O_D0, DS=V.O_DS, DX=V.O_DX, DM=V.O_DM, LT=V.O_LT, SG=V.O_SG, G1=V.O_G1, MX=V.O_MX, MM=V.O_MM)
    return ok and {k: int(v) for k, v in offs.items()} == ref and "#define LIMIT %du" % doc["limit"] in src

def compile_harness(work, src_text=None, sanitize=True):
    d = Path(work); (d / "fw2/gallery").mkdir(parents=True, exist_ok=True); (d / "sim").mkdir(exist_ok=True)
    (d / "fw2/gallery/tgy_dec.h").write_text((ROOT / "fw2/gallery/tgy_dec.h").read_text()); (d / "fw2/gallery/tgy_dec.c").write_text(src_text if src_text is not None else (ROOT / "fw2/gallery/tgy_dec.c").read_text())
    (d / "sim/h.c").write_text((ROOT / "sim/tgy_dec_harness.c").read_text()); env = dict(os.environ); sdk = subprocess.run([sys.executable, str(ROOT / "tools/toolchain.py"), "sdkroot"], capture_output=True, text=True).stdout.strip()
    if sdk: env["SDKROOT"] = sdk
    flags = ["-fsanitize=address,undefined", "-fno-sanitize-recover=undefined"] if sanitize else []
    r = subprocess.run(["cc", "-O1", "-g", "-std=c99", "-w", *flags, "-o", str(d / "h"), str(d / "sim/h.c")], capture_output=True, text=True, env=env)
    if r.returncode: print(r.stderr[:2000]); return None
    return d / "h"

def main(argv):
    w = Path(tempfile.mkdtemp()); vec = w / "vectors.bin"; build_vectors(vec); exe = compile_harness(w); print("tables and offsets equal the conformance constants:", tables_ok((ROOT / "fw2/gallery/tgy_dec.c").read_text()))
    r = subprocess.run([str(exe), "vectors", str(vec)], capture_output=True, text=True); print("\n".join(l for l in r.stdout.splitlines() if "FAIL" in l or "PASSED" in l)); ok = r.returncode == 0
    en = subprocess.run([str(exe), "engine"], capture_output=True, text=True).stdout.split("\n")[:3]; e = V.Engine(); ref = []
    for ph in range(3):
        for _ in range(300): e.update(7, 1 if ph == 1 else 0)
        ref.append("%d %d" % (e.p[7], e.n[7]))
    print("engine saturation equals the reference:", en == ref); ok = ok and en == ref
    f = subprocess.run([str(exe), "fuzz", "1", "4000"], capture_output=True, text=True, timeout=300); print(f.stdout.strip(), f.stderr.strip()[:300]); ok = ok and f.returncode == 0 and "fuzz done" in f.stdout and True
    gcc = ROOT / "toolchain"; cand = sorted(gcc.glob("*/bin/riscv-none-elf-gcc"))
    if cand:
        o = w / "d.o"; subprocess.run([str(cand[0]), "-march=rv32im", "-mabi=ilp32", "-Os", "-ffreestanding", "-c", str(ROOT / "fw2/gallery/tgy_dec.c"), "-o", str(o)], check=True)
        sz = subprocess.run([str(cand[0].with_name("riscv-none-elf-size")), str(o)], capture_output=True, text=True).stdout.splitlines()[-1].split(); print("rv32im -Os: text %s, data %s, bss %s bytes (the decoder state, %d bytes, is in the caller's tgy_dec_t)" % (sz[0], sz[1], sz[2], 1250 * 3 + 16))
    return 0 if ok else 1

def mutants():
    src = (ROOT / "fw2/gallery/tgy_dec.c").read_text()
    muts = [("rec", "{ 32768, 26214,", "{ 32768, 26215,"), ("limit", "#define LIMIT 30u", "#define LIMIT 31u"), ("kg", "static const uint8_t KG[64] = { 0, 0, 1, 2,", "static const uint8_t KG[64] = { 0, 0, 1, 3,"), ("ln", "{-1, -1}, {0, -1}, {-1, 0}", "{-1, -1}, {0, -1}, {0, 0}"),
            ("offset sg", "O_SG = 170", "O_SG = 171"), ("offset mx", "O_MX = 430", "O_MX = 431"), ("clamp", "(p > 32768u - 32u ? 32768u - 32u : p)", "(p > 32768u - 31u ? 32768u - 31u : p)"), ("clamp low", "(p < 32u ? 32u :", "(p < 33u ? 33u :"),
            ("bound shift", "(d->range >> 15) * d->prob[c]", "(d->range >> 14) * d->prob[c]"), ("normalise", "while (d->range < (1u << 24))", "while (d->range < (1u << 23))"), ("init bytes", "for (i = 0; i < 5; i++) d->code", "for (i = 0; i < 4; i++) d->code"),
            ("pad", "return d->p < d->end ? *d->p++ : 0u;", "return d->p < d->end ? *d->p++ : 1u;"), ("bypass", "if (d->code >= d->range) { d->code -= d->range; b = 1; }", "if (d->code > d->range) { d->code -= d->range; b = 1; }"),
            ("top mantissa", "(j == e ? ctx_bit(d, mbase + (e < 9u ? e : 9u)) : bypass_bit(d))", "bypass_bit(d)"), ("exp cap", "xbase + (e < 9u ? e : 9u)", "xbase + (e < 8u ? e : 8u)"), ("dc pred", "int32_t pred = left ? left[0] : (above ? above[0] : 0)", "int32_t pred = above ? above[0] : (left ? left[0] : 0)"),
            ("sign", "out[k] = sat(bypass_bit(d) ? -v : v);", "out[k] = sat(bypass_bit(d) ? v : -v);"), ("lbk", "(x <= 4 ? 3u : 4u)", "(x <= 5 ? 3u : 4u)"), ("last tree", "O_LT + cc * 63u + node - 1u", "O_LT + node - 1u"), ("implicit last", "if (k < last && !ctx_bit", "if (!ctx_bit"),
            ("plus two", "O_MM + cc * 10u) + 2u;", "O_MM + cc * 10u) + 1u;"), ("component class", "unsigned cc = ci ? 1u : 0u,", "unsigned cc = 0u,"), ("above stride", "y ? blk - (int32_t)c * 64 : 0", "y ? blk - (int32_t)c * 64 + 64 : 0")]
    w = Path(tempfile.mkdtemp()); vec = w / "vectors.bin"; build_vectors(vec); bad = 0
    for name, a, b in muts:
        assert a in src, name
        mut = src.replace(a, b, 1)
        if not tables_ok(mut): print("killed   (tables check)", name); bad += 0; continue
        exe = compile_harness(w / name, mut, sanitize=False)
        if exe is None: print("killed   (does not build)", name); continue
        try:
            r = subprocess.run([str(exe), "vectors", str(vec)], capture_output=True, text=True, timeout=60); ok = r.returncode == 0
            if ok:
                en = subprocess.run([str(exe), "engine"], capture_output=True, text=True, timeout=60).stdout.split("\n")[:3]; e = V.Engine(); ref = []
                for ph in range(3):
                    for _ in range(300): e.update(7, 1 if ph == 1 else 0)
                    ref.append("%d %d" % (e.p[7], e.n[7]))
                ok = en == ref
        except subprocess.TimeoutExpired: ok = False
        print("SURVIVED" if ok else "killed  ", name); bad += ok
    return bad

if __name__ == "__main__":
    sys.exit((1 if mutants() else 0) if "--mutants" in sys.argv else main(sys.argv))
