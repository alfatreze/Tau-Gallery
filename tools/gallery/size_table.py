#!/usr/bin/env python3
"""Whole-picture sizes and card load times for a folder of photos (docs/M3_STREAMING_NOTE.md): the original file, a q92 / q95 / q88 JPEG-class re-encode of the full-resolution picture (the proxy the plan uses for TGY: optimised
Huffman, so a range-coded TGY would be smaller), the raw RGB888 size, and the time to read each from the card at the rate MEASURED for large commands (1.55 MB/s, docs/M0_DESIGN.md 6e).
  python3 tools/gallery/size_table.py DIR [--rate 1.55]   # prints a Markdown table"""
import argparse, io, sys
from pathlib import Path
from PIL import Image, ImageOps
Image.MAX_IMAGE_PIXELS = None

def main():
    ap = argparse.ArgumentParser(); ap.add_argument("dir", type=Path); ap.add_argument("--rate", type=float, default=1.55, help="MB/s from the card for large commands (measured 1.5 to 1.6)")
    a = ap.parse_args(); rate = a.rate * 1e6
    print(f"| Photo | Pixels | Original JPEG | q95 4:4:4 | q92 4:4:4 | q88 4:2:0 | Raw RGB888 | Load q92 at {a.rate} MB/s | Load raw |\n|---|---|---|---|---|---|---|---|---|")
    for p in sorted(a.dir.glob("*.jp*g")):
        im = ImageOps.exif_transpose(Image.open(p)).convert("RGB"); w, h = im.size
        def enc(q, ss):
            b = io.BytesIO(); im.save(b, "JPEG", quality=q, subsampling=ss, optimize=True); return len(b.getvalue())
        o, s95, s92, s88, raw = p.stat().st_size, enc(95, 0), enc(92, 0), enc(88, 2), w * h * 3
        mb = lambda n: f"{n / 1e6:.1f} MB"
        print(f"| {p.stem[-12:]} | {w}x{h} ({w * h / 1e6:.1f} MP) | {mb(o)} | {mb(s95)} | {mb(s92)} | {mb(s88)} | {mb(raw)} | {s92 / rate:.1f} s | {raw / rate:.0f} s |")

if __name__ == "__main__":
    main()
