#!/usr/bin/env python3
"""Placeholder artwork for the Tau Gallery core (replace with real art later; same file formats).

Writes, from one drawn mark (a picture frame with a mountain and a sun):
  dist/Cores/alfatreze.Tau Gallery/icon.bin     36x36, 16-bit LE pixels, brightness in the low byte, stored rotated 90 deg CCW
  dist/Platforms/_images/tau_gallery.bin        521x165, same format (Analogue platform image)
  assets/brand/gallery-splash-source.png        400x360 boot-screen source for tools/gen_splash_asset.py
  assets/brand/placeholder-icon.png, placeholder-platform.png   previews (logical orientation)

    python3 tools/gen_gallery_art.py
    python3 tools/gen_splash_asset.py assets/brand/gallery-splash-source.png \
        "dist/Assets/tau_gallery/alfatreze.Tau Gallery/tau-loading.bin" --preview work/previews/gallery-loading-device.png
"""
import struct
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
CORE = ROOT / "dist/Cores/alfatreze.Tau Gallery"
FONTS = ["/System/Library/Fonts/Helvetica.ttc", "/System/Library/Fonts/Supplemental/Arial Bold.ttf",
         "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"]


def font(size):
    for f in FONTS:
        try:
            return ImageFont.truetype(f, size, index=1) if f.endswith(".ttc") else ImageFont.truetype(f, size)
        except OSError:
            pass
    return ImageFont.load_default(size)


def mark(size, fg=255, bg=0):
    """The mark at `size` px square, drawn 4x and reduced: frame, mountains, sun."""
    k = 4; n = size * k
    im = Image.new("L", (n, n), bg); d = ImageDraw.Draw(im)
    m = n * 0.06; t = max(2, int(n * 0.07))
    d.rounded_rectangle([m, m, n - m, n - m], radius=n * 0.09, outline=fg, width=t)
    ix0, iy0, ix1, iy1 = m + t * 2, m + t * 2, n - m - t * 2, n - m - t * 2
    d.ellipse([ix0 + (ix1 - ix0) * 0.62, iy0 + (iy1 - iy0) * 0.12, ix0 + (ix1 - ix0) * 0.84, iy0 + (iy1 - iy0) * 0.34], fill=fg)
    w, h = ix1 - ix0, iy1 - iy0
    d.polygon([(ix0, iy1), (ix0 + w * 0.34, iy0 + h * 0.42), (ix0 + w * 0.52, iy0 + h * 0.66), (ix0 + w * 0.70, iy0 + h * 0.46), (ix1, iy1)], fill=fg)
    return im.resize((size, size), Image.LANCZOS)


def store(im, path):
    """Logical image -> Pocket file: rotate 90 deg CCW, 16-bit LE brightness in the low byte."""
    r = im.convert("L").rotate(90, expand=True)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(struct.pack("<%dH" % (r.width * r.height), *r.tobytes()))


def wordmark(im, cx, cy, text, size, fill):
    d = ImageDraw.Draw(im); f = font(size)
    w = d.textlength(text, font=f); d.text((cx - w / 2, cy - size * 0.6), text, font=f, fill=fill)


def main():
    icon = mark(36); store(icon, CORE / "icon.bin")
    plat = Image.new("L", (521, 165), 8)
    plat.paste(mark(92), (74, 36))
    d = ImageDraw.Draw(plat); f = font(52)
    d.text((190, 34), "TAU", font=f, fill=255); d.text((190, 84), "GALLERY", font=font(46), fill=255)
    store(plat, ROOT / "dist/Platforms/_images/tau_gallery.bin")
    out = ROOT / "assets/brand"; out.mkdir(parents=True, exist_ok=True)
    icon.resize((144, 144), Image.NEAREST).save(out / "placeholder-icon.png")
    plat.save(out / "placeholder-platform.png")
    sp = Image.new("RGB", (400, 360), (10, 10, 12))
    g = mark(150, fg=255, bg=10).convert("RGB"); sp.paste(g, (125, 70))
    wordmark(sp, 200, 262, "TAU GALLERY", 34, (255, 255, 255)); wordmark(sp, 200, 306, "LOADING", 16, (150, 150, 160))
    sp.save(out / "gallery-splash-source.png")
    print("icon.bin", (CORE / "icon.bin").stat().st_size, "platform", (ROOT / "dist/Platforms/_images/tau_gallery.bin").stat().st_size)


if __name__ == "__main__":
    main()
