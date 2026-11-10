#!/usr/bin/env python3
"""The richer-context measurement (docs/M3_STREAMING_NOTE.md section 14): the same lossless 1024 x 1024 crops as model_measure.py, the count-based engine, and the options of tgymodel.code_block2 added one at a time and together.
  python3 tools/gallery/model_measure2.py PHOTO_DIR [--crop 1024]"""
import argparse, sys, tempfile
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
import jpegcoef, tgymodel as M, model_measure as MM

def cost(tiles, opt, nbr=False):
    bits = 0.0; nb = 0
    for grids, shapes in tiles:
        io = M.CostIO(M.Engine("count")); M.code_tile(io, grids, shapes, nbr, opt); bits += io.bits + 40
    return bits

VARIANTS = [("old model (last index)", None), ("+ local", dict(local=True)), ("+ dcmed", dict(dcmed=True)), ("nz count", dict(nz=True)), ("nz + nznb", dict(nz=True, nznb=True)),
            ("nz + local", dict(nz=True, local=True)), ("nz + local + nznb", dict(nz=True, local=True, nznb=True)), ("nz + local + dcmed", dict(nz=True, local=True, dcmed=True)),
            ("nz + local + dcmed + nznb", dict(nz=True, local=True, dcmed=True, nznb=True))]

def main():
    ap = argparse.ArgumentParser(); ap.add_argument("dir", type=Path); ap.add_argument("--crop", type=int, default=1024); a = ap.parse_args(); d = Path(tempfile.mkdtemp())
    photos = sorted(a.dir.glob("*.jp*g")); data = []
    for p in photos:
        refs, _ = MM.crop_jpeg(p, a.crop, d); j = jpegcoef.read_jpeg(refs["progressive"].read_bytes()); data.append((p, list(M.tiles_of(j)), refs["progressive"].stat().st_size))
    prog = sum(x[2] for x in data); print("| Variant | " + " | ".join(p.stem[-6:] for p, _, _ in data) + " | total bytes | ratio to progressive Huffman |\n|" + "---|" * (len(data) + 3))
    for name, opt in VARIANTS:
        row = [int(cost(t, opt) / 8) for _, t, _ in data]; print(f"| {name} | " + " | ".join(f"{v:,}" for v in row) + f" | {sum(row):,} | {sum(row) / prog:.3f} |", flush=True)

if __name__ == "__main__":
    main()
