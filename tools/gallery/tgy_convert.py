#!/usr/bin/env python3
"""Convert a folder of pictures into Tau Gallery files (M1, docs/M1_DESIGN.md): one .tgy per picture and the folder index tau-library.tdb (TGIX: entries plus a 96 x 72 thumbnail atlas; it is the file of data slot 5, the library slot).

  python3 tools/gallery/tgy_convert.py SRC OUT [--card-dir Assets/tau_gallery/common/photos] [--upscale] [--levels] [--card-layout] [--merge]

SRC is a folder of pictures or one picture file. --card-layout writes OUT as the card's own tree (OUT/Assets/tau_gallery/common/photos/*.tgy and OUT/Assets/tau_gallery/common/tau-library.tdb: copy the Assets folder onto the card). --merge keeps the pictures the OUT library already holds (a picture with the same name is replaced), so one picture can be added to a library built earlier.

OUT_DIR gets NAME.tgy files and tau-library.tdb (put the index in Assets/tau_gallery/common/ and the pictures in --card-dir). --card-dir is where the .tgy files will be on the SD card (the index stores that path: from the card root, no leading slash).
Outputs are written to a temporary name and renamed, so an interrupted run never leaves a half-written file. One JSON event per line on stdout."""
import argparse, json, os, sys, zlib
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
import tgy, tgy_make

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
    ap.add_argument("src", type=Path, help="a folder of pictures, or one picture file"); ap.add_argument("out", type=Path)
    ap.add_argument("--card-dir", default="Assets/tau_gallery/common/photos")
    ap.add_argument("--index-name", default="tau-library.tdb")
    ap.add_argument("--upscale", action="store_true", help="enlarge pictures smaller than the screen to fit (default: show them at 1:1)")
    ap.add_argument("--levels", action="store_true", help="also write the zoom levels (raw tiles, the half and the 1:1 stops of tgy.ladder) for pictures larger than the screen (docs/M2_DESIGN.md): about 45 MB for a 12-megapixel photo, off by default")
    ap.add_argument("--format", choices=("v0", "v1"), default="v0", help="v0: the RAW canvas files (every Tau Gallery core); v1: the compressed container the hardware TGY decoder draws (core rev 29, TAU_TGY; docs/TGY_CONTAINER_V1.md, the fit level is the whole canvas)")
    ap.add_argument("--quality", type=int, default=92, help="v1: the quality of a full-resolution level encoded from a non-JPEG picture")
    ap.add_argument("--fit-quality", type=int, default=90, help="v1: the quality of the fit level")
    ap.add_argument("--card-layout", action="store_true", help="write OUT as the card's tree: the pictures in OUT/<card-dir>, the index in the folder above it")
    ap.add_argument("--jobs", type=int, default=0, help="v1: processes that code the tiles (0: automatic, the machine's cores up to 8; 1: one). The file is the same whatever the number; a dense 100 megapixel photo takes about 18 minutes on one process")
    ap.add_argument("--merge", action="store_true", help="keep the pictures the output library already has; a picture with the same name is replaced")
    a = ap.parse_args(argv)
    if hasattr(sys.stdout, "reconfigure"): sys.stdout.reconfigure(line_buffering=True)        # an event reaches a reader the moment it is printed, not when the run ends (the app shows progress from them)
    from PIL import Image
    if a.src.is_file():
        if a.src.suffix.lower() not in EXT: print(json.dumps({"event": "error", "error": "not a picture this tool reads: " + a.src.name})); return 2
        files = [a.src]
    elif a.src.is_dir(): files = sorted(p for p in a.src.iterdir() if p.suffix.lower() in EXT and p.is_file())
    else: print(json.dumps({"event": "error", "error": "no such file or folder: " + str(a.src)})); return 2
    card_dir = a.card_dir.strip("/")
    photos = a.out / card_dir if a.card_layout else a.out                                              # where the .tgy files go
    index_dir = a.out / str(Path(card_dir).parent) if a.card_layout else a.out                         # and the index (the data slot's file)
    photos.mkdir(parents=True, exist_ok=True); index_dir.mkdir(parents=True, exist_ok=True)
    entries, thumbs, used, skipped = [], [], set(), 0
    old = {}                                                                                           # --merge: the library already there, by picture file name
    if a.merge and (index_dir / a.index_name).is_file():
        try:
            for e in tgy.parse_index((index_dir / a.index_name).read_bytes()):
                if (photos / Path(e["path"]).name).is_file(): old[Path(e["path"]).name.lower()] = e         # a picture whose file is gone is dropped from the index
        except ValueError as e: print(json.dumps({"event": "warning", "error": "the old index is not readable and is rebuilt without it: " + str(e)}))
    print(json.dumps({"event": "plan", "files": len(files), "kept": len(old)}))
    for p in files:
        base = ascii_stem(p.stem); stem = base; n = 1
        while stem.lower() in used: n += 1; stem = f"{base}_{n}"
        shown = p.stem if n == 1 else f"{p.stem}_{n}"                  # the display name keeps the original characters (Latin-1) and the same suffix
        path = f"{card_dir}/{stem}.tgy"
        if len(path.encode("utf-8")) >= tgy.PATH_BYTES:              # never cut a name (M1 decision 7): the file is skipped, said so, and the rest of the folder still converts
            print(json.dumps({"event": "skip", "file": p.name, "error": f"the card path is {len(path.encode('utf-8'))} bytes, the limit is {tgy.PATH_BYTES - 1}: shorten the file name or the card folder"})); skipped += 1; continue
        try:
            prep = tgy.prepare(Image.open(p)); canvas, rect, thumb, size = tgy.render_prepared(prep, upscale=a.upscale)
            levels = tgy.zoom_levels(prep) if (a.levels and a.format == "v0") else []
        except Exception as e:                                       # a file that is not a readable picture is skipped, and said so
            print(json.dumps({"event": "skip", "file": p.name, "error": str(e)})); skipped += 1; continue
        used.add(stem.lower())
        if a.format == "v1": data, thumb, size = tgy_make.canvas_container(p, prep, a.quality, a.fit_quality, a.upscale, jobs=a.jobs)
        else: data = tgy.pack_tgy(canvas, rect, thumb, size, levels)
        write_atomic(photos / (stem + ".tgy"), data); old.pop((stem + ".tgy").lower(), None)           # (a kept picture of the same name is replaced)
        entries.append(dict(path=path, name=name_of(shown), size=len(data), width=size[0], height=size[1])); thumbs.append(thumb)
        print(json.dumps({"event": "converted", "file": p.name, "tgy": stem + ".tgy", "size": len(data), "width": size[0], "height": size[1], "rect": rect, "levels": [[l["w"], l["h"], "raster"] if isinstance(l, dict) else [l[0], l[1], "tiles"] for l in levels]}))
    if old:                                                                                            # the kept pictures and the new ones, in name order
        both = sorted([(e["path"].lower(), e, e["thumb"]) for e in old.values()] + [(e["path"].lower(), e, th) for e, th in zip(entries, thumbs)], key=lambda x: x[0])
        entries = [dict(path=e["path"], name=e["name"], size=e["size"], width=e["width"], height=e["height"]) for _, e, _ in both]; thumbs = [th for _, _, th in both]
    write_atomic(index_dir / a.index_name, tgy.pack_index(entries, thumbs))
    print(json.dumps({"event": "done", "pictures": len(entries), "skipped": skipped, "converted": len(entries) - len(old), "out": str(a.out)}))
    return 0

if __name__ == "__main__":
    sys.exit(main())
