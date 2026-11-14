#!/usr/bin/env python3
"""Rebuild the thumbnails of a version 1 library from its own pictures: each thumbnail becomes the picture scaled to COVER the 96 x 96 square and centre-cropped (the grid is then always full squares), taken from the
picture's fit level (the 800 x 720 canvas inside the .tgy, cropped to the picture's own rectangle: the black bars of the canvas are not the picture). For a library whose thumbnails were made with bars (a version 0 index
padded to 96 x 96 by `merge_index.py`, or an index written by an older converter). Names, order, folders, sizes, details and times are kept. The pictures are only read.

  rethumb.py LIBRARY_DIR NEW_INDEX [--index tau-library.tdb] [--photos photos]
  LIBRARY_DIR is the card's Assets/tau_gallery/common (it holds tau-library.tdb and photos/); NEW_INDEX is the file written (the library is not changed).
One line a picture on stdout; exit 1 when a picture could not be read (the index is not written then)."""
import argparse, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
import tgy, tgy_container as C

def cover_thumb(tgy_bytes):
    """The cover thumbnail (96 x 96 RGB565) of one picture file, from its fit level."""
    import numpy as np
    from PIL import Image
    c = C.Container(tgy_bytes)
    fit = next(k for k, e in enumerate(c.levels) if e["kind"] == C.KIND_FIT)
    px = c.pixels(fit); ch, cw = px.shape[:2]
    x, y, w, h = tgy.fit_rect(c.width, c.height, cw, ch)
    ix, iy = (1 if w < cw and w > 2 else 0), (1 if h < ch and h > 2 else 0)      # one pixel in from each edge that borders a bar: the resampling blends the edge with the bar
    return tgy.square_thumb(Image.fromarray(px[y + iy:y + h - iy, x + ix:x + w - ix]))

def rethumb(index_bytes, photos_dir, log=lambda *a: None):
    d = tgy.parse_index_full(index_bytes)
    if d["version"] != 1: raise ValueError("a version 0 index has no 96 x 96 thumbnails to replace: upgrade it first")
    ents, thumbs = [], []
    for e in d["entries"]:
        f = Path(photos_dir) / Path(e["path"]).name
        thumbs.append(cover_thumb(f.read_bytes())); log(e["name"])
        ents.append(dict(path=e["path"], name=e["name"], size=e["size"], width=e["width"], height=e["height"], folder=e["folder"], taken=e.get("taken", 0), details=e.get("details", {})))
    return tgy.pack_index_v1(ents, thumbs, [dict(name=f["name"], cover=f.get("cover")) for f in d["folders"]], d["sort"])

if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("library", type=Path); ap.add_argument("out", type=Path); ap.add_argument("--index", default="tau-library.tdb"); ap.add_argument("--photos", default="photos")
    a = ap.parse_args()
    try: data = rethumb((a.library / a.index).read_bytes(), a.library / a.photos, lambda n: print(n, flush=True))
    except (OSError, ValueError, StopIteration) as e: sys.exit(f"rethumb: {e}")
    a.out.write_bytes(data); print(f"wrote {a.out} ({len(data):,} bytes)")
