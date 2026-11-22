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
def is_picture(p): return p.suffix.lower() in EXT and not p.name.startswith(".") and p.is_file()      # not the ._name files macOS writes beside every file on a FAT / exFAT volume, nor hidden files

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

def nested_folders(src, mode):
    """The library's folders under src: [(name, [pictures])] in name order. A folder with pictures is a folder of the library; one inside another is flattened into the first level folder it is under (mode 'flatten') or gets its own, named with the whole path joined by ' - ' (mode 'join'). Hidden folders (a name starting with a dot) are skipped at every depth."""
    def pics(d): return sorted(p for p in d.iterdir() if is_picture(p))
    def walk(d, names):
        yield names, pics(d)
        for q in sorted(x for x in d.iterdir() if x.is_dir() and not x.name.startswith(".")): yield from walk(q, names + [q.name])
    out = {}
    for q in sorted(x for x in src.iterdir() if x.is_dir() and not x.name.startswith(".")):
        for names, fl in walk(q, [q.name]):
            if fl: out.setdefault(names[0] if mode == "flatten" else " - ".join(names), []).extend(fl)
    return sorted(out.items(), key=lambda kv: kv[0])

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
    ap.add_argument("--nested", choices=("flatten", "join"), default="flatten", help="folders inside a folder: flatten (default) puts their pictures in the first level folder they are under, join makes a folder of its own named with the whole path, such as 'Trip - Day 1'")
    ap.add_argument("--index-format", choices=("v1", "v0"), default="v1", help="v1: the library index of Tau Gallery 1.0 (folders, details, 96 x 96 square thumbnails; docs/features/TGIX_V1_SPEC.md); v0: the old flat index with 96 x 72 thumbnails, for a gallery core and firmware before 1.0")
    ap.add_argument("--sort", choices=("name", "taken", "converted"), default="name", help="v1: the order of the pictures inside a folder: by file name, by the date taken (the EXIF date; pictures without one last), or as converted (the pictures a merge kept first)")
    a = ap.parse_args(argv)
    if hasattr(sys.stdout, "reconfigure"): sys.stdout.reconfigure(line_buffering=True)        # an event reaches a reader the moment it is printed, not when the run ends (the app shows progress from them)
    from PIL import Image
    if a.src.is_file():
        if a.src.suffix.lower() not in EXT: print(json.dumps({"event": "error", "error": "not a picture this tool reads: " + a.src.name})); return 2
        jobs = [(a.src, None)]
    elif a.src.is_dir():
        top = sorted(p for p in a.src.iterdir() if is_picture(p))
        subs = nested_folders(a.src, a.nested)                                                              # one level of folders in the library: a subfolder with pictures is a folder; deeper ones are flattened into it or joined to its name (--nested)
        if a.index_format == "v1" and subs: jobs = [(p, name) for name, fl in subs for p in fl] + [(p, a.src.name or tgy.ALL_PICTURES) for p in top]
        else: jobs = [(p, None) for p in top]                                                             # no subfolders (or the flat v0 index): one folder, ALL PICTURES
    else: print(json.dumps({"event": "error", "error": "no such file or folder: " + str(a.src)})); return 2
    card_dir = a.card_dir.strip("/")
    photos = a.out / card_dir if a.card_layout else a.out                                              # where the .tgy files go
    index_dir = a.out / str(Path(card_dir).parent) if a.card_layout else a.out                         # and the index (the data slot's file)
    photos.mkdir(parents=True, exist_ok=True); index_dir.mkdir(parents=True, exist_ok=True)
    v1 = a.index_format == "v1"
    items, used, skipped = [], set(), 0                                                                # items: one dict per picture of the new library (path, name, size, width, height, folder, taken, details, thumb)
    old = {}                                                                                           # --merge: the library already there, by picture file name
    if a.merge and (index_dir / a.index_name).is_file():
        try:
            full = tgy.parse_index_full((index_dir / a.index_name).read_bytes())
            if full["version"] == 1 and not v1: raise ValueError("the old index is version 1 and --index-format v0 cannot hold folders")
            for e in full["entries"]:
                if not (photos / Path(e["path"]).name).is_file(): continue                                 # a picture whose file is gone is dropped from the index
                fname = full["folders"][e["folder"]]["name"] if full["version"] == 1 else tgy.ALL_PICTURES
                old[Path(e["path"]).name.lower()] = dict(e, folder_name=fname, thumb=e["thumb"] if full["version"] == 1 else tgy.thumb72_to_square(e["thumb"]) if v1 else e["thumb"], order=len(old))
        except ValueError as e: print(json.dumps({"event": "warning", "error": "the old index is not readable and is rebuilt without it: " + str(e)}))
    print(json.dumps({"event": "plan", "files": len(jobs), "kept": len(old)}))
    for p, folder in jobs:
        base = ascii_stem(p.stem); stem = base; n = 1
        while stem.lower() in used: n += 1; stem = f"{base}_{n}"
        shown = p.stem if n == 1 else f"{p.stem}_{n}"                  # the display name keeps the original characters (Latin-1) and the same suffix
        path = f"{card_dir}/{stem}.tgy"
        if len(path.encode("utf-8")) >= tgy.PATH_BYTES:              # never cut a name (M1 decision 7): the file is skipped, said so, and the rest of the folder still converts
            print(json.dumps({"event": "skip", "file": p.name, "error": f"the card path is {len(path.encode('utf-8'))} bytes, the limit is {tgy.PATH_BYTES - 1}: shorten the file name or the card folder"})); skipped += 1; continue
        try:
            im = Image.open(p); fmt = (im.format or "").upper(); details = tgy.exif_details(im, fmt) if v1 else {}      # the EXIF and the format, before the orientation is applied
            prep = tgy.prepare(im); canvas, rect, thumb, size = tgy.render_prepared(prep, upscale=a.upscale)
            levels = tgy.zoom_levels(prep) if (a.levels and a.format == "v0") else []
            sq = tgy.square_thumb(prep) if v1 else None
        except Exception as e:                                       # a file that is not a readable picture is skipped, and said so
            print(json.dumps({"event": "skip", "file": p.name, "error": str(e)})); skipped += 1; continue
        used.add(stem.lower())
        if a.format == "v1": data, thumb, size = tgy_make.canvas_container(p, prep, a.quality, a.fit_quality, a.upscale, jobs=a.jobs)
        else: data = tgy.pack_tgy(canvas, rect, thumb, size, levels)
        write_atomic(photos / (stem + ".tgy"), data); old.pop((stem + ".tgy").lower(), None)           # (a kept picture of the same name is replaced)
        items.append(dict(path=path, name=name_of(shown), size=len(data), width=size[0], height=size[1], folder_name=folder or tgy.ALL_PICTURES, taken=tgy.taken_seconds(details.get("taken")), details=details, thumb=sq if v1 else thumb, order=len(old) + len(items)))
        print(json.dumps({"event": "converted", "file": p.name, "tgy": stem + ".tgy", "size": len(data), "width": size[0], "height": size[1], "rect": rect, "folder": items[-1]["folder_name"], "levels": [[l["w"], l["h"], "raster"] if isinstance(l, dict) else [l[0], l[1], "tiles"] for l in levels]}))
    allitems = list(old.values()) + items
    if v1:
        fnames = sorted({it["folder_name"] for it in allitems}, key=lambda x: x.lower())                  # the folders in name order; the entries of a folder in the chosen order
        key = {"name": lambda it: (it["name"].lower(), it["path"].lower()), "taken": lambda it: (it["taken"] == 0, it["taken"], it["path"].lower()), "converted": lambda it: it["order"]}[a.sort]
        ordered = [it for f in fnames for it in sorted((x for x in allitems if x["folder_name"] == f), key=key)]
        folders = [dict(name=f.encode("latin-1", "replace").decode("latin-1")[:tgy.FOLDER_NAME - 1]) for f in fnames]
        entries = [dict(path=it["path"], name=it["name"], size=it["size"], width=it["width"], height=it["height"], folder=fnames.index(it["folder_name"]), taken=it.get("taken", 0), details=it.get("details", {})) for it in ordered]
        write_atomic(index_dir / a.index_name, tgy.pack_index_v1(entries, [it["thumb"] for it in ordered], folders or None, {"name": tgy.SORT_NAME, "taken": tgy.SORT_TAKEN, "converted": tgy.SORT_CONVERTED}[a.sort]))
        nf = len(folders) or 1
    else:
        ordered = sorted(allitems, key=lambda it: it["path"].lower()) if old else allitems                # a merge: the kept pictures and the new ones in path order; a plain run keeps the conversion order (the source file names, sorted)
        write_atomic(index_dir / a.index_name, tgy.pack_index([dict(path=it["path"], name=it["name"], size=it["size"], width=it["width"], height=it["height"]) for it in ordered], [it["thumb"] for it in ordered])); nf = 1
    print(json.dumps({"event": "done", "pictures": len(ordered), "skipped": skipped, "converted": len(items), "folders": nf, "out": str(a.out)}))
    return 0

if __name__ == "__main__":
    sys.exit(main())
