#!/usr/bin/env python3
"""Runs sim/fuzz_c.c (mutation fuzzing of the container reader, the hardware driver, the version 0 reader, the folder index reader and the TGY decoder) under AddressSanitizer and UBSan on seed files written by the real converter.
Usage: python3 sim/test_fuzz_c.py [--iters N] (default 20,000 per mode; the nightly run uses 500,000). A failure prints the iteration and the generator state: the run is deterministic."""
import os, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools/gallery")); sys.path.insert(0, str(ROOT / "sim"))
import tgy, tgy_make as MK, test_jpegcoef as TJ

MUTANTS = [
    ("tgy_hw.c", "if (off > l->data_len || len > l->data_len - off || len >= (1u << 20)) return TH_RANGE;", ""),
    ("tgy_hw.c", "if (ilen != rows * cols * TH_IDX || ioff < TH_HEAD || ioff > f->n || ilen > f->n - ioff) return TH_RANGE;", "if (ilen != rows * cols * TH_IDX) return TH_RANGE;"),
    ("tgy_hw.c", "if (doff < TH_HEAD || doff > f->n || dlen > f->n - doff) return TH_RANGE;", ""),
    ("tgy_hw.c", "if (qy >= f->ntables || (fmt != 0u && qc >= f->ntables)) return TH_RANGE;", ""),
    ("tgy_hw.c", "if (n > size || (base & 1u)) return base & 1u ? TH_ODD : TH_RANGE;", "if (base & 1u) return TH_ODD;"),
    ("gallery_fmt.c", "if (len != GT_STRIDE * GT_CANVAS_H || off < 128u || off > fsize || len > fsize - off) return GF_RANGE;", "if (len != GT_STRIDE * GT_CANVAS_H) return GF_RANGE;"),
    ("tgy_hw.c", "lt > 4096u || nlev * TH_LEVEL > 4096u - lt", "lt + nlev * TH_LEVEL > 4096u"),
    ("tgy_hw.c", "tab > 4096u || tlen < 2u || tlen > 4096u - tab", "tlen < 2u || tab + tlen > 4096u"),
    ("gallery_fmt.c", "lt > n || nlev * 48u > n - lt", "lt + nlev * 48u > n"),
    ("gallery_fmt.c", "if (off < 128u || (off & 4095u) || off > fsize || len > fsize - off) return GF_RANGE;", "if (off < 128u || (off & 4095u)) return GF_RANGE;"),
    ("tgy_dec.c", "if (++e > TGY_MAX_EXP) { d->err = 1; return 0; }", "++e;"),
]
def mutants(iters):
    survived = 0
    for fn, a, b in MUTANTS:
        d = Path(tempfile.mkdtemp()); (d / "fw2/gallery").mkdir(parents=True); (d / "fw2/platform").mkdir(); (d / "sim").mkdir()
        for f in (ROOT / "fw2/gallery").iterdir():
            if f.suffix in (".c", ".h"): (d / "fw2/gallery" / f.name).write_text(f.read_text())
        t = (d / "fw2/gallery" / fn).read_text(); assert a in t, (fn, a); (d / "fw2/gallery" / fn).write_text(t.replace(a, b))
        (d / "fw2/platform/mmio.h").write_text((ROOT / "fw2/platform/mmio.h").read_text()); (d / "sim/fuzz_c.c").write_text((ROOT / "sim/fuzz_c.c").read_text())
        run(d, iters, label="mutant " + fn + ": " + a[:48], expect_fail=True) or (survived := survived + 1)
    return survived

def run(root, iters, label=None, expect_fail=False):
    out = Path(root) if expect_fail else Path(tempfile.mkdtemp())            # a real run builds beside nothing of the repository's
    env = dict(os.environ); sdk = subprocess.run([sys.executable, str(ROOT / "tools/toolchain.py"), "sdkroot"], capture_output=True, text=True).stdout.strip()
    if sdk: env["SDKROOT"] = sdk
    exe = out / "fuzz"
    r = subprocess.run(["cc", "-O1", "-g", "-std=gnu99", "-w", "-fsanitize=address,undefined", "-fno-sanitize-recover=undefined", "-o", str(exe), str(Path(root) / "sim/fuzz_c.c")], capture_output=True, text=True, env=env)
    if r.returncode: print(r.stderr[:2000]); return False
    seeds = out / "seeds"; seeds.mkdir(exist_ok=True); make_seeds(seeds)
    jobs = [("container", "v1"), ("container", "v1small"), ("v0", "v0"), ("stream", "v1")] if expect_fail else [("container", "v1"), ("container", "v1small"), ("v0", "v0"), ("index", "index"), ("stream", "v1"), ("stream", "v1small")]
    bad = False
    for mode, seed in jobs:
        try: r = subprocess.run([str(exe), mode, str(seeds / seed), str(iters)], capture_output=True, text=True, timeout=120); rc, out = r.returncode, (r.stdout + r.stderr).strip().splitlines()
        except subprocess.TimeoutExpired: rc, out = 124, ["timeout (a hang)"]
        line = next((l for l in out if l.startswith("FUZZ")), out[-1] if out else "no output")
        if expect_fail:
            if rc: print(f"killed   {label}  [{mode}: {line[:70]}]"); return True
        else:
            print(f"{mode:9s} {seed:8s} {line}")
            if rc or "FUZZ ok" not in line: bad = True; print("\n".join(out[:12]))
    if expect_fail: print(f"SURVIVED {label}"); return False
    return not bad

def make_seeds(d):
    img = TJ.picture(1000, 900, seed=3); img.save(d / "a.jpg", quality=90)
    data, thumb, size = MK.canvas_container(d / "a.jpg", tgy.prepare(img)); seeds = {"v1": data}
    prep = tgy.prepare(TJ.picture(300, 200, seed=4)); seeds["v1small"] = MK.canvas_container(None, prep)[0]
    prep = tgy.prepare(TJ.picture(1400, 1000, seed=5)); canvas, rect, th, sz = tgy.render_prepared(prep); seeds["v0"] = tgy.pack_tgy(canvas, rect, th, sz, tgy.zoom_levels(prep))
    ents = [dict(path=f"Assets/tau_gallery/common/photos/p{i:02d}.tgy", name=f"Picture {i}", size=1000, width=800, height=600) for i in range(5)]
    seeds["index"] = tgy.pack_index(ents, [th] * 5)
    for k, v in seeds.items(): (d / k).write_bytes(v)

def main():
    iters = 20000
    if "--iters" in sys.argv: iters = int(sys.argv[sys.argv.index("--iters") + 1])
    if "--iters" in sys.argv: iters = int(sys.argv[sys.argv.index("--iters") + 1])
    if "--mutants" in sys.argv: return 1 if mutants(int(sys.argv[sys.argv.index("--iters") + 1]) if "--iters" in sys.argv else 20000) else 0
    return 0 if run(ROOT, iters) else 1

if __name__ == "__main__": sys.exit(main())
