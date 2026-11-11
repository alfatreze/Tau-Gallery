#!/usr/bin/env python3
"""Decision D4 (docs/M3_STREAMING_NOTE.md section 11): how much does upsampling the chroma of each 128 x 128 tile on its own (the chroma tile's edge samples replicated, no neighbouring tile needed) differ from libjpeg's
whole-plane fancy upsampling, which needs one chroma sample from the neighbouring tiles? Measured on lossless 1024 x 1024 crops of the test photos (4:2:0), comparing the two RGB results.
  python3 tools/gallery/measure_tile_upsampling.py PHOTO_DIR"""
import sys, tempfile
from pathlib import Path
import numpy as np
sys.path.insert(0, str(Path(__file__).resolve().parent))
import jpegcoef, tgy_recon as R, model_measure as MM

tile_local = R.upsample_h2v2_tile

def main():
    d = Path(tempfile.mkdtemp()); tot_err = tot_n = 0; print("| Photo crop | pixels differing | mean abs error on those | 99.9th percentile | max | PSNR of the whole crop |\n|---|---|---|---|---|---|")
    for p in sorted(Path(sys.argv[1]).glob("*.jp*g")):
        refs, _ = MM.crop_jpeg(p, 1024, d); j = jpegcoef.read_jpeg(refs["progressive"].read_bytes())
        a = R.decode_pixels(j).astype(np.int32); b = R.decode_pixels(j, upsample=tile_local).astype(np.int32); diff = np.abs(a - b).max(axis=2)
        n = int((diff > 0).sum()); mse = ((a - b) ** 2).mean(); psnr = 99.0 if mse == 0 else 10 * np.log10(255 ** 2 / mse)
        nz = diff[diff > 0]; print(f"| {p.stem[-12:]} | {n:,} of {diff.size:,} ({100 * n / diff.size:.2f}%) | {nz.mean():.2f} | {np.percentile(nz, 99.9):.0f} | {int(nz.max())} | {psnr:.1f} dB |", flush=True)

if __name__ == "__main__":
    main()
