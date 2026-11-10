#!/usr/bin/env python3
"""The context-model measurement of decision D2 (docs/M3_STREAMING_NOTE.md section 11): for each photo a lossless 1024 x 1024 crop (jpegtran -crop, tile aligned, so the coefficients are exactly the original's) is coded
tile by tile with the symbol layer of tools/gallery/tgymodel.py in several variants, and compared with the SAME coefficients coded by jpegtran as baseline Huffman, progressive Huffman and arithmetic coding, and with the
static entropy limit of the model's own contexts. Costs are ideal code lengths (the real TVC stream is within a few bytes per tile of them, sim/test_tgymodel.py); 40 bits a tile are added for the 5-byte flush.
  python3 tools/gallery/model_measure.py PHOTO_DIR [--crop 1024] [--json out.json]"""
import argparse, json, math, subprocess, sys, tempfile
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
import jpegcoef, tgymodel as M

JT = "/opt/homebrew/bin/jpegtran"

def crop_jpeg(path, size, d):
    from PIL import Image
    Image.MAX_IMAGE_PIXELS = None
    w, h = Image.open(path).size
    x0 = max(0, (w - size) // 2) // 128 * 128; y0 = max(0, (h - size) // 2) // 128 * 128
    cw, ch = min(size, (w - x0) // 128 * 128), min(size, (h - y0) // 128 * 128)
    out = {}
    for name, flags in (("baseline", ["-optimize"]), ("progressive", ["-progressive"]), ("arithmetic", ["-arithmetic"])):
        f = d / f"{path.stem[-8:]}_{name}.jpg"
        subprocess.run([JT, "-crop", f"{cw}x{ch}+{x0}+{y0}", *flags, "-copy", "none", "-outfile", str(f), str(path)], check=True); out[name] = f
    return out, (cw, ch, x0, y0)

def run_variant(tiles, mode, nbr, priors=None):
    bits = 0.0; counts = {}; nby = 0
    for grids, shapes in tiles:
        io = M.CostIO(M.Engine(mode, priors)); M.code_tile(io, grids, shapes, nbr); bits += io.bits + 40; nby += io.nbypass
        for c, v in io.counts.items():
            t = counts.setdefault(c, [0, 0]); t[0] += v[0]; t[1] += v[1]
    return bits, counts, nby

def static_limit(counts, nby, ntiles):
    bits = float(nby)
    for n0, n1 in counts.values():
        n = n0 + n1
        for k in (n0, n1):
            if k: bits -= k * math.log2(k / n)
    return bits + 40 * ntiles

def main():
    ap = argparse.ArgumentParser(); ap.add_argument("dir", type=Path); ap.add_argument("--crop", type=int, default=1024); ap.add_argument("--json", type=Path)
    a = ap.parse_args(); d = Path(tempfile.mkdtemp()); res = {}
    photos = sorted(a.dir.glob("*.jp*g"))
    for p in photos:
        refs, geom = crop_jpeg(p, a.crop, d); j = jpegcoef.read_jpeg(refs["progressive"].read_bytes())
        tiles = list(M.tiles_of(j)); r = {"geom": geom, "tiles": len(tiles), "ref": {k: v.stat().st_size for k, v in refs.items()}, "tiles_data": tiles}
        r["fixed_plain"], _, _ = run_variant(tiles, "fixed", False); r["fixed_nbr"], _, _ = run_variant(tiles, "fixed", True)
        r["count_plain"], c0, nb0 = run_variant(tiles, "count", False); r["count_nbr"], c1, nb1 = run_variant(tiles, "count", True)
        r["counts_plain"], r["counts_nbr"] = c0, c1; r["static_plain"] = static_limit(c0, nb0, len(tiles)); r["static_nbr"] = static_limit(c1, nb1, len(tiles))
        res[p.name] = r; print("done", p.name[-16:], geom, flush=True)
    for p in photos:                                                                                     # trained priors, leave one photo out
        r = res[p.name]
        for key, nbr in (("plain", False), ("nbr", True)):
            tot = {}
            for q in photos:
                if q != p:
                    for c, v in res[q.name]["counts_" + key].items(): t = tot.setdefault(c, [0, 0]); t[0] += v[0]; t[1] += v[1]
            r["prior_" + key], _, _ = run_variant(r["tiles_data"], "count", nbr, M.train_priors(tot))
    cols = ["fixed_plain", "fixed_nbr", "count_plain", "count_nbr", "prior_plain", "prior_nbr", "static_nbr"]
    print("\n| Photo (crop) | tiles | JPEG baseline | JPEG progressive | JPEG arithmetic | " + " | ".join(cols) + " |\n|" + "---|" * (5 + len(cols)))
    tot = {k: 0 for k in ["baseline", "progressive", "arithmetic"] + cols}
    for p in photos:
        r = res[p.name]; row = [r["ref"]["baseline"], r["ref"]["progressive"], r["ref"]["arithmetic"]] + [int(r[c] / 8) for c in cols]
        for k, v in zip(["baseline", "progressive", "arithmetic"] + cols, row): tot[k] += v
        print(f"| {p.stem[-12:]} {r['geom'][0]}x{r['geom'][1]} | {r['tiles']} | " + " | ".join(f"{v:,}" for v in row) + " |")
    print("| **total** | | " + " | ".join(f"{v:,}" for v in tot.values()) + " |")
    pr = tot["progressive"]
    print("| **ratio to the progressive crop** | | " + " | ".join(f"{v / pr:.3f}" for v in tot.values()) + " |")
    if a.json:
        for r in res.values(): r.pop("tiles_data"); r.pop("counts_plain"); r.pop("counts_nbr")
        a.json.write_text(json.dumps(res, indent=1))

if __name__ == "__main__":
    main()
