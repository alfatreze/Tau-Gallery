#!/usr/bin/env python3
"""Tests the C pixel pipeline (fw2/gallery/tgy_pix.c) against the Python reconstruction (tools/gallery/tgy_recon.py, the TGY profile): whole pictures decoded TILE BY TILE from their symbol-layer streams (grey, 4:4:4, 4:2:0, odd sizes,
partial tiles, saturating noise, a real photo crop) must equal the reference's RGB byte for byte, and the primitive vectors of docs/conformance/tgy_recon_v0_vectors.json (IDCT, colour, upsampling) must hold. Sanitizers on.
`--mutants` edits copies of the pipeline; each edit must fail. Also reports the rv32im code size."""
import base64, io, json, os, struct, subprocess, sys, tempfile
from pathlib import Path
import numpy as np
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools/gallery")); sys.path.insert(0, str(ROOT / "sim"))
import jpegcoef, tgy_container as C, tgy_encode as E, tgy_recon as R, tgy_v0 as V, test_jpegcoef as TJ
from PIL import Image

def picture_blob(j, sub):
    w, h = j.width, j.height; qy = np.array(j.quant[j.components[0].tq], dtype="<u2"); qc = np.array(j.quant[j.components[1].tq] if len(j.components) > 1 else j.quant[j.components[0].tq], dtype="<u2")
    tiles = R.split_tiles(j); out = struct.pack("<III", w, h, sub) + qy.tobytes() + qc.tobytes() + struct.pack("<I", len(tiles))
    for ty, tx, grids, shapes in tiles:
        s = V.encode_tile(grids, shapes); out += struct.pack("<III", ty, tx, len(shapes)) + b"".join(struct.pack("<II", r, c) for r, c in shapes) + struct.pack("<I", len(s)) + s
    return out + R.decode_pixels_tgy(j).tobytes()

def pictures():
    cases = []
    def add(img, **kw): j = jpegcoef.read_jpeg(TJ.encode(img, **kw)); cases.append((j, C.sub_of(j)))
    add(TJ.picture(300, 200, seed=4), quality=85, subsampling=2); add(TJ.picture(257, 131, seed=5), quality=90, subsampling=0); add(TJ.picture(130, 130, seed=6).convert("L"), quality=88)
    add(TJ.picture(17, 5, seed=7), quality=90, subsampling=2); add(TJ.picture(2, 2, seed=8), quality=90, subsampling=2); add(TJ.picture(129, 257, seed=9), quality=70, subsampling=2, progressive=True)
    r = np.random.RandomState(3); noise = Image.fromarray(r.randint(0, 256, (150, 200, 3)).astype(np.uint8)); add(noise, quality=100, subsampling=2); add(noise, quality=5, subsampling=0)
    for q in (95, 40):
        j = E.encode_rgb(np.asarray(TJ.picture(200, 150, seed=10, patch=False)), q, 2); cases.append((j, C.SUB_420))
    p = next((ROOT.parent / "test-images/unsplash").glob("*c6227db76b6e.jpg"), None) if (ROOT.parent / "test-images/unsplash").is_dir() else None
    if p:
        crop = Path(tempfile.mkdtemp()) / "c.jpg"; subprocess.run(["/opt/homebrew/bin/jpegtran", "-crop", "512x384+768+256", "-copy", "none", "-outfile", str(crop), str(p)], check=True)
        j = jpegcoef.read_jpeg(crop.read_bytes()); cases.append((j, C.sub_of(j)))
    return cases

def prims_blob():
    v = json.loads((ROOT / "docs/conformance/tgy_recon_v0_vectors.json").read_text()); out = struct.pack("<I", len(v["idct"]))
    for c in v["idct"]: out += struct.pack("<64i", *c["dequantised_natural"]) + bytes(c["samples"])
    out += struct.pack("<I", len(v["colour"]))
    for c in v["colour"]: out += struct.pack("<6I", c["y"], c["cb"], c["cr"], *c["rgb"])
    out += struct.pack("<I", len(v["upsample"]))
    for c in v["upsample"]: out += struct.pack("<II", c["height"], c["width"]) + base64.b64decode(c["plane_b64"]) + base64.b64decode(c["tile_b64"])
    return out

def compile_harness(work, src_text=None, sanitize=True):
    d = Path(work); (d / "fw2/gallery").mkdir(parents=True, exist_ok=True); (d / "sim").mkdir(exist_ok=True)
    for n in ("tgy_dec.h", "tgy_dec.c", "tgy_pix.h"): (d / "fw2/gallery" / n).write_text((ROOT / "fw2/gallery" / n).read_text())
    (d / "fw2/gallery/tgy_pix.c").write_text(src_text if src_text is not None else (ROOT / "fw2/gallery/tgy_pix.c").read_text()); (d / "sim/h.c").write_text((ROOT / "sim/tgy_pix_harness.c").read_text())
    env = dict(os.environ); sdk = subprocess.run([sys.executable, str(ROOT / "tools/toolchain.py"), "sdkroot"], capture_output=True, text=True).stdout.strip()
    if sdk: env["SDKROOT"] = sdk
    flags = ["-fsanitize=address,undefined", "-fno-sanitize-recover=undefined"] if sanitize else []
    r = subprocess.run(["cc", "-O1", "-g", "-std=c99", "-w", *flags, "-o", str(d / "h"), str(d / "sim/h.c")], capture_output=True, text=True, env=env)
    if r.returncode: print(r.stderr[:2000]); return None
    return d / "h"

def write_inputs(w):
    cases = pictures(); blob = struct.pack("<I", len(cases)) + b"".join(picture_blob(j, s) for j, s in cases); (w / "pics.bin").write_bytes(blob); (w / "prims.bin").write_bytes(prims_blob())

def main():
    w = Path(tempfile.mkdtemp()); write_inputs(w); exe = compile_harness(w); ok = True
    for mode, f in (("prims", "prims.bin"), ("pictures", "pics.bin")):
        r = subprocess.run([str(exe), mode, str(w / f)], capture_output=True, text=True, timeout=600); print(r.stdout.strip()); print(r.stderr.strip()[:500]); ok = ok and r.returncode == 0
    cand = sorted((ROOT / "toolchain").glob("*/bin/riscv-none-elf-gcc"))
    if cand:
        o = w / "p.o"; subprocess.run([str(cand[0]), "-march=rv32im", "-mabi=ilp32", "-Os", "-ffreestanding", "-c", str(ROOT / "fw2/gallery/tgy_pix.c"), "-o", str(o)], check=True)
        sz = subprocess.run([str(cand[0].with_name("riscv-none-elf-size")), str(o)], capture_output=True, text=True).stdout.splitlines()[-1].split(); print("rv32im -Os: text %s bytes (tgy_planes_t is %d bytes, tgy_dec_t 3,766)" % (sz[0], 128 * 128 * 3))
    return 0 if ok else 1

# A colour constant changed by 1 (91880 for 91881) changes no output for any of the 256 table inputs (the products stay on the same side of every rounding boundary): such a deviation is not observable, so these mutants change the constants by 100 to 120.
def mutants():
    src = (ROOT / "fw2/gallery/tgy_pix.c").read_text()
    muts = [("zigzag", "{ 0, 1, 8, 16, 9, 2, 3,", "{ 0, 1, 8, 9, 16, 2, 3,"), ("f4433", "(i[2] + i[6]) * 4433", "(i[2] + i[6]) * 4432"), ("f9633", "z5 = (z3 + z4) * 9633", "z5 = (z3 + z4) * 9632"), ("f20995", "z2 *= -20995", "z2 *= -20994"),
            ("pass1 shift", "descale(o[r], 11)", "descale(o[r], 10)"), ("pass2 shift", "descale(o[k], 18)", "descale(o[k], 17)"), ("rounding", "return (x + ((int64_t)1 << (n - 1))) >> n;", "return x >> n;"),
            ("range limit", "(j < 512u ? 255u :", "(j < 511u ? 255u :"), ("wrap", "unsigned j = (unsigned)(d & 1023);", "unsigned j = (unsigned)(d & 2047);"), ("cr_r", "91881 * xr + 32768", "92000 * xr + 32768"),
            ("g shift", "(-22554 * xb + 32768 + -46802 * xr) >> 16", "(-22554 * xb + 32768 + -46802 * xr) >> 15"), ("b", "116130 * xb", "116140 * xb"), ("box", "if (cw <= 2)", "if (cw <= 1)"),
            ("other row", "(cr + 1 < ch ? cr + 1 : ch - 1) : (cr ? cr - 1 : 0)", "(cr + 1 < ch ? cr + 1 : ch - 1) : (cr ? cr : 0)"), ("first col", "(s * 4 + 8) >> 4", "(s * 4 + 7) >> 4"), ("last col", "(s * 4 + 7) >> 4", "(s * 4 + 8) >> 4"),
            ("mid even", "(3 * t[c - 1] + o[c - 1]) + 8) >> 4", "(3 * t[c - 1] + o[c - 1]) + 7) >> 4"), ("mid odd", "(3 * t[c + 1] + o[c + 1]) + 7) >> 4", "(3 * t[c + 1] + o[c + 1]) + 8) >> 4"),
            ("dc pred", "int32_t pred = x ? dcleft : (y ? dcrow[x] : 0);", "int32_t pred = y ? dcrow[x] : (x ? dcleft : 0);"), ("chroma stride", "stride = ci == 0 || sub == TGY_444 ? 128u : 64u;", "stride = 128u;"),
            ("chroma table", "tgy_dequant_idct(blk, ci ? qc : qy, px);", "tgy_dequant_idct(blk, qy, px);"), ("grey", "row[3 * x] = row[3 * x + 1] = row[3 * x + 2] = pl->y[r * 128 + x];", "row[3 * x] = row[3 * x + 1] = pl->y[r * 128 + x]; row[3 * x + 2] = 0;"),
            ("tile chroma width", "unsigned cw = (w + 1) / 2, chh = (h + 1) / 2;", "unsigned cw = w / 2, chh = (h + 1) / 2;")]
    w = Path(tempfile.mkdtemp()); write_inputs(w); bad = 0
    for name, a, b in muts:
        assert a in src, name
        exe = compile_harness(w / name, src.replace(a, b, 1), sanitize=False)
        if exe is None: print("killed   (does not build)", name); continue
        ok = True
        for mode, f in (("prims", "prims.bin"), ("pictures", "pics.bin")):
            try: r = subprocess.run([str(exe), mode, str(w / f)], capture_output=True, text=True, timeout=120); ok = ok and r.returncode == 0
            except subprocess.TimeoutExpired: ok = False
        print("SURVIVED" if ok else "killed  ", name); bad += ok
    return bad

if __name__ == "__main__":
    sys.exit((1 if mutants() else 0) if "--mutants" in sys.argv else main())
