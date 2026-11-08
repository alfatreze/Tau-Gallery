#!/usr/bin/env python3
"""Convert a folder of pictures into Tau Gallery files (M1, docs/M1_DESIGN.md): one .tgy per picture and the folder index tau-library.tdb (TGIX: entries plus a 96 x 72 thumbnail atlas; it is the file of data slot 5, the library slot).

  python3 tools/gallery/tgy_convert.py SRC_DIR OUT_DIR [--card-dir Assets/tau_gallery/common/photos] [--upscale]

OUT_DIR gets NAME.tgy files and tau-library.tdb (put the index in Assets/tau_gallery/common/ and the pictures in --card-dir). --card-dir is where the .tgy files will be on the SD card (the index stores that path: from the card root, no leading slash).
Outputs are written to a temporary name and renamed, so an interrupted run never leaves a half-written file. One JSON event per line on stdout."""
import argparse, json, os, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
import tgy

EXT = {".jpg", ".jpeg", ".png", ".bmp", ".gif", ".tif", ".tiff", ".webp"}

def write_atomic(path, data):
    tmp = path.with_name(path.name + ".part")
    tmp.write_bytes(data); os.replace(tmp, path)

def name_of(stem):
    s = "".join(ch if ord(ch) < 256 else "?" for ch in stem)
    return s[:tgy.NAME_BYTES - 1]

def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("src", type=Path); ap.add_argument("out", type=Path)
    ap.add_argument("--card-dir", default="Assets/tau_gallery/common/photos")
    ap.add_argument("--index-name", default="tau-library.tdb")
    ap.add_argument("--upscale", action="store_true", help="enlarge pictures smaller than the screen to fit (default: show them at 1:1)")
    a = ap.parse_args(argv)
    from PIL import Image
    a.out.mkdir(parents=True, exist_ok=True)
    files = sorted(p for p in a.src.iterdir() if p.suffix.lower() in EXT and p.is_file())
    entries, thumbs, used = [], [], set()
    for p in files:
        try:
            canvas, rect, thumb, size = tgy.render(Image.open(p), upscale=a.upscale)
        except Exception as e:                                       # a file that is not a readable picture is skipped, and said so
            print(json.dumps({"event": "skip", "file": p.name, "error": str(e)})); continue
        stem = p.stem; n = 1
        while stem.lower() in used: n += 1; stem = f"{p.stem}_{n}"
        used.add(stem.lower())
        data = tgy.pack_tgy(canvas, rect, thumb, size)
        write_atomic(a.out / (stem + ".tgy"), data)
        entries.append(dict(path=f"{a.card_dir.strip('/')}/{stem}.tgy", name=name_of(stem), size=len(data), width=size[0], height=size[1])); thumbs.append(thumb)
        print(json.dumps({"event": "converted", "file": p.name, "tgy": stem + ".tgy", "size": len(data), "width": size[0], "height": size[1], "rect": rect}))
    write_atomic(a.out / a.index_name, tgy.pack_index(entries, thumbs))
    print(json.dumps({"event": "done", "pictures": len(entries)}))
    return 0

if __name__ == "__main__":
    sys.exit(main())
