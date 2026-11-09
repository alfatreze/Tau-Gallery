#!/usr/bin/env python3
"""Convert a folder of pictures into Tau Gallery files (M1, docs/M1_DESIGN.md): one .tgy per picture and the folder index tau-library.tdb (TGIX: entries plus a 96 x 72 thumbnail atlas; it is the file of data slot 5, the library slot).

  python3 tools/gallery/tgy_convert.py SRC_DIR OUT_DIR [--card-dir Assets/tau_gallery/common/photos] [--upscale]

OUT_DIR gets NAME.tgy files and tau-library.tdb (put the index in Assets/tau_gallery/common/ and the pictures in --card-dir). --card-dir is where the .tgy files will be on the SD card (the index stores that path: from the card root, no leading slash).
Outputs are written to a temporary name and renamed, so an interrupted run never leaves a half-written file. One JSON event per line on stdout."""
import argparse, json, os, sys, zlib
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
import tgy

EXT = {".jpg", ".jpeg", ".png", ".bmp", ".gif", ".tif", ".tiff", ".webp"}

def write_atomic(path, data):
    tmp = path.with_name(path.name + ".part")
    tmp.write_bytes(data); os.replace(tmp, path)

FAT_BAD = set('<>:"/\\|?*')

def ascii_stem(stem):
    """The file name on the card is ASCII only: the Pocket's core-issued open fails for any path with a byte of 0x80 or more (KB-041, hardware-validated on an exFAT card, NFC and NFD alike). Every character outside
    printable ASCII (and the characters FAT forbids) becomes '_', and when anything was replaced a short CRC of the original name is added so two different names never become one. The display name in the index keeps the Latin-1 text."""
    s = "".join(ch if 0x20 <= ord(ch) < 0x7F and ch not in FAT_BAD else "_" for ch in stem)
    if s != stem: s += "_%04x" % (zlib.crc32(stem.encode("utf-8")) & 0xFFFF)
    return s

def name_of(stem):
    s = "".join(ch if ord(ch) < 256 else "?" for ch in stem)
    return s[:tgy.NAME_BYTES - 1]

def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("src", type=Path); ap.add_argument("out", type=Path)
    ap.add_argument("--card-dir", default="Assets/tau_gallery/common/photos")
    ap.add_argument("--index-name", default="tau-library.tdb")
    ap.add_argument("--upscale", action="store_true", help="enlarge pictures smaller than the screen to fit (default: show them at 1:1)")
    ap.add_argument("--levels", action="store_true", help="also write the zoom levels (raw tiles, the half and the 1:1 stops of tgy.ladder) for pictures larger than the screen (docs/M2_DESIGN.md): about 45 MB for a 12-megapixel photo, off by default")
    a = ap.parse_args(argv)
    from PIL import Image
    a.out.mkdir(parents=True, exist_ok=True)
    files = sorted(p for p in a.src.iterdir() if p.suffix.lower() in EXT and p.is_file())
    entries, thumbs, used, skipped = [], [], set(), 0
    for p in files:
        base = ascii_stem(p.stem); stem = base; n = 1
        while stem.lower() in used: n += 1; stem = f"{base}_{n}"
        shown = p.stem if n == 1 else f"{p.stem}_{n}"                  # the display name keeps the original characters (Latin-1) and the same suffix
        path = f"{a.card_dir.strip('/')}/{stem}.tgy"
        if len(path.encode("utf-8")) >= tgy.PATH_BYTES:              # never cut a name (M1 decision 7): the file is skipped, said so, and the rest of the folder still converts
            print(json.dumps({"event": "skip", "file": p.name, "error": f"the card path is {len(path.encode('utf-8'))} bytes, the limit is {tgy.PATH_BYTES - 1}: shorten the file name or the card folder"})); skipped += 1; continue
        try:
            prep = tgy.prepare(Image.open(p)); canvas, rect, thumb, size = tgy.render_prepared(prep, upscale=a.upscale)
            levels = tgy.zoom_levels(prep) if a.levels else []
        except Exception as e:                                       # a file that is not a readable picture is skipped, and said so
            print(json.dumps({"event": "skip", "file": p.name, "error": str(e)})); skipped += 1; continue
        used.add(stem.lower())
        data = tgy.pack_tgy(canvas, rect, thumb, size, levels)
        write_atomic(a.out / (stem + ".tgy"), data)
        entries.append(dict(path=path, name=name_of(shown), size=len(data), width=size[0], height=size[1])); thumbs.append(thumb)
        print(json.dumps({"event": "converted", "file": p.name, "tgy": stem + ".tgy", "size": len(data), "width": size[0], "height": size[1], "rect": rect, "levels": [[lw, lh] for (lw, lh, _d) in levels]}))
    write_atomic(a.out / a.index_name, tgy.pack_index(entries, thumbs))
    print(json.dumps({"event": "done", "pictures": len(entries), "skipped": skipped}))
    return 0

if __name__ == "__main__":
    sys.exit(main())
