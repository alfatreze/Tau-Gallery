#!/usr/bin/env python3
"""Builds TGY container version 1 files (docs/TGY_CONTAINER_V1.md) from pictures: a JPEG (baseline or progressive Huffman, 4:4:4, 4:2:0 or grey) keeps its coefficients LOSSLESSLY as the full-resolution level;
any other picture is encoded by tgy_encode at the given quality. Both get an encoded fit level (the picture resampled to fit 800 x 720, never enlarged, placed centred in the canvas) written FIRST, so a viewer
shows something after reading the first few hundred kilobytes, and a 96 x 72 RGB565 thumbnail. A picture no larger than the fit size has only the full level.
  python3 tools/gallery/tgy_make.py SRC_DIR_OR_FILE OUT_DIR [--quality 92] [--fit-quality 90]"""
import argparse, io, json, sys
from pathlib import Path
import numpy as np
sys.path.insert(0, str(Path(__file__).resolve().parent))
import jpegcoef, tgy, tgy_container as C, tgy_encode as E, tgy_recon as R
from PIL import Image, ImageOps
Image.MAX_IMAGE_PIXELS = None

ORIENT = {2: Image.Transpose.FLIP_LEFT_RIGHT, 3: Image.Transpose.ROTATE_180, 4: Image.Transpose.FLIP_TOP_BOTTOM, 5: Image.Transpose.TRANSPOSE, 6: Image.Transpose.ROTATE_270, 7: Image.Transpose.TRANSVERSE, 8: Image.Transpose.ROTATE_90}   # EXIF orientation -> the transform that makes the picture upright

def fit_level(rgb, fit_quality):
    """(level dict or None, the fit-size RGB array) for a picture's pixels: None when the picture is not larger than the fit size."""
    h, w = rgb.shape[:2]; x, y, fw, fh = tgy.fit_rect(w, h)
    small = np.asarray(Image.fromarray(rgb).resize((fw, fh), Image.LANCZOS)) if (fw, fh) != (w, h) else rgb
    if (fw, fh) == (w, h): return None, small, (x, y, fw, fh)
    return dict(kind=C.KIND_FIT, pic=E.encode_rgb(small, fit_quality, 2), rect=(x, y, fw, fh)), small, (x, y, fw, fh)

def thumb_of(rgb):
    h, w = rgb.shape[:2]; tx, ty, tw, th = tgy.fit_rect(w, h, tgy.THUMB_W, tgy.THUMB_H)
    t = Image.new("RGB", (tgy.THUMB_W, tgy.THUMB_H), (0, 0, 0)); t.paste(Image.fromarray(rgb).resize((tw, th), Image.LANCZOS), ((tgy.THUMB_W - tw) // 2, (tgy.THUMB_H - th) // 2)); return tgy.rgb565_le(t.tobytes())

def from_jpeg(data, fit_quality=90):
    """Container bytes from a JPEG file's bytes (lossless full level). The EXIF orientation is recorded in the header; the fit level and the thumbnail are upright."""
    j = jpegcoef.read_jpeg(data); im = Image.open(io.BytesIO(data)); orient = int(im.getexif().get(0x0112, 1) or 1)
    rgb = R.decode_pixels_tgy(j); up = np.asarray(Image.fromarray(rgb).transpose(ORIENT[orient])) if orient in ORIENT else rgb
    lvl, small, rect = fit_level(up, fit_quality); levels = ([lvl] if lvl else []) + [dict(kind=C.KIND_FULL, pic=j, rect=rect if not lvl else (0, 0, j.width, j.height))]
    return C.write_container(up.shape[1], up.shape[0], orient, thumb_of(small), levels)

def from_image(img, quality=92, fit_quality=90):
    """Container bytes from any PIL image: oriented upright, encoded at `quality` (4:2:0 for colour)."""
    img = ImageOps.exif_transpose(img).convert("RGB"); rgb = np.asarray(img)
    lvl, small, rect = fit_level(rgb, fit_quality); full = E.encode_rgb(rgb, quality, 2)
    levels = ([lvl] if lvl else []) + [dict(kind=C.KIND_FULL, pic=full, rect=rect if not lvl else (0, 0, rgb.shape[1], rgb.shape[0]))]
    return C.write_container(rgb.shape[1], rgb.shape[0], 1, thumb_of(small), levels)

def canvas_container(path, prep, quality=92, fit_quality=90, upscale=False, full=True, jobs=0):
    """The container for the gallery APP (docs/TGY_HW_DMA.md): the fit level is the WHOLE 800 x 720 canvas (the picture fitted and centred on black, the black bars are part of the encoded picture), so the hardware decoder writes every
    byte of the viewer plane and the firmware never clears it. `prep` = tgy.prepare(Image) (oriented, sRGB). A picture larger than the canvas also gets its full-resolution level (a JPEG file losslessly, anything else encoded at
    `quality`) for later zoom; `full=False` omits it. A 4:4:4 JPEG is the exception: its full level is encoded again (4:2:0, `quality`), because the core's decoder has no 4:4:4. Returns (container bytes, thumbnail RGB565 bytes, (width, height))."""
    canvas, rect, thumb, size = tgy.render_prepared(prep, upscale=upscale)
    rgb = np.frombuffer(canvas, np.uint8).reshape(tgy.CANVAS_H, tgy.CANVAS_W, 3)
    levels = [dict(kind=C.KIND_FIT, pic=E.encode_rgb(rgb, fit_quality, 2), rect=(0, 0, tgy.CANVAS_W, tgy.CANVAS_H))]; orient = 1
    if full and (prep.width > tgy.CANVAS_W or prep.height > tgy.CANVAS_H):
        pic = None; data = Path(path).read_bytes() if path is not None else b""
        try:
            j = jpegcoef.read_jpeg(data); orient = int(Image.open(io.BytesIO(data)).getexif().get(0x0112, 1) or 1)
            if orient in ORIENT: assert (j.width, j.height) == (prep.height, prep.width) or orient in (2, 3, 4)
            elif orient != 1: orient = 1
            pic = j
            if (j.width, j.height) not in ((prep.width, prep.height), (prep.height, prep.width)): pic = None
            if pic is not None and C.sub_of(pic) == C.SUB_444: pic = None      # the hardware decoder of this core draws 4:2:0 and grey only: a 4:4:4 JPEG's zoom level is encoded again as 4:2:0 (from the sRGB picture) so it zooms at all; its chroma resolution is halved
        except (ValueError, IndexError, StopIteration, KeyError, AssertionError, OSError): pic = None
        if pic is None: orient = 1; pic = E.encode_rgb(np.asarray(prep), quality, 2)
        levels.append(dict(kind=C.KIND_FULL, pic=pic, rect=(0, 0, prep.width, prep.height)))
    return C.write_container(prep.width, prep.height, orient, thumb, levels, jobs), thumb, (prep.width, prep.height)

def convert(path, quality=92, fit_quality=90):
    data = Path(path).read_bytes()
    try: return from_jpeg(data, fit_quality), "jpeg-lossless"
    except (ValueError, IndexError, StopIteration, KeyError): return from_image(Image.open(path), quality, fit_quality), "encoded"

def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter); ap.add_argument("src", type=Path); ap.add_argument("out", type=Path)
    ap.add_argument("--quality", type=int, default=92); ap.add_argument("--fit-quality", type=int, default=90); a = ap.parse_args(argv); a.out.mkdir(parents=True, exist_ok=True)
    files = [a.src] if a.src.is_file() else sorted(p for p in a.src.iterdir() if p.suffix.lower() in {".jpg", ".jpeg", ".png", ".bmp", ".gif", ".tif", ".tiff", ".webp"})
    for p in files:
        try: blob, how = convert(p, a.quality, a.fit_quality)
        except Exception as e: print(json.dumps({"event": "skip", "file": p.name, "error": str(e)})); continue
        out = a.out / (p.stem + ".tgy"); tmp = out.with_name(out.name + ".part"); tmp.write_bytes(blob); tmp.replace(out)
        print(json.dumps({"event": "converted", "file": p.name, "how": how, "bytes": len(blob), "source_bytes": p.stat().st_size}))
    return 0

if __name__ == "__main__":
    sys.exit(main())
