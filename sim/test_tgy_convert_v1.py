#!/usr/bin/env python3
"""Tests the converter's TGIX version 1 output (tools/gallery/tgy_convert.py): one level of folders from the source's subfolders, EXIF details only for the fields a picture has, 96 x 96 square thumbnails, the sort modes, the merge
(an old version 0 or version 1 index kept, its thumbnails padded or untouched), the version 0 fallback. `--mutants` edits a copy of the converter; each edit must fail this test. Run: python3 sim/test_tgy_convert_v1.py [--mutants]"""
import io, json, os, shutil, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools/gallery"))
import tgy, tgy_convert
from PIL import Image
fails = 0
def check(name, ok):
    global fails
    print(("ok   " if ok else "FAIL ") + name); fails += 0 if ok else 1

def pic(path, color, size=(120, 90), exif=None, fmt="JPEG"):
    im = Image.new("RGB", size, color); im.paste((255 - color[0], 255 - color[1], 40), (size[0] // 3, 0, size[0] // 2, size[1] // 2))
    kw = {"exif": exif} if exif is not None else {}
    im.save(path, fmt, **kw)
def exif(make="Canon", model="Canon EOS R5", taken=None, iso=None):
    ex = Image.Exif(); ex[271] = make; ex[272] = model; ifd = ex.get_ifd(0x8769)
    if taken: ifd[0x9003] = taken
    if iso: ifd[0x8827] = iso
    ifd[0x829A] = 0.004; ifd[0x829D] = 2.8; ifd[0x920A] = 35.0; ifd[0xA434] = "RF24-70mm F2.8"
    return ex
def run(mod, src, out, *args):
    old = sys.stdout; sys.stdout = io.StringIO()
    try: rc = mod.main([str(src), str(out), "--card-layout", *args])
    finally: ev = [json.loads(l) for l in sys.stdout.getvalue().splitlines() if l.startswith("{")]; sys.stdout = old
    return rc, ev
def load(out): return tgy.parse_index_full((Path(out) / "Assets/tau_gallery/common/tau-library.tdb").read_bytes())

def suite(mod):
    global fails
    fails = 0; d = Path(tempfile.mkdtemp()); src = d / "My Pictures"; (src / "Beach").mkdir(parents=True); (src / "Alps").mkdir(); (src / "empty").mkdir()
    pic(src / "Beach" / "b2.jpg", (200, 100, 50), exif=exif(taken="2024:07:02 10:00:00")); pic(src / "Beach" / "b1.jpg", (10, 120, 200), exif=exif(taken="2024:07:03 09:00:00", iso=800))
    pic(src / "Alps" / "a1.jpg", (30, 30, 200), size=(90, 150)); pic(src / "top.png", (90, 90, 90), fmt="PNG")
    out = d / "o1"; rc, ev = run(tgy_convert, src, out) if mod is None else run(mod, src, out)
    check("the run succeeds and says how many folders it made (empty subfolders do not count, the top pictures go to a folder named after the source)", rc == 0 and ev[-1]["event"] == "done" and ev[-1]["folders"] == 3 and ev[-1]["pictures"] == 4)
    lib = load(out)
    check("version 1 index, folders in name order: Alps, Beach, My Pictures", lib["version"] == 1 and [f["name"] for f in lib["folders"]] == ["Alps", "Beach", "My Pictures"] and [(f["first"], f["count"]) for f in lib["folders"]] == [(0, 1), (1, 2), (3, 1)])
    check("the entries of a folder are in file name order (b1 before b2)", [e["name"] for e in lib["entries"]] == ["a1", "b1", "b2", "top"])
    check("every thumbnail is a 96 x 96 square", all(len(e["thumb"]) == tgy.THUMB1_BYTES for e in lib["entries"]) and (lib["thumb_w"], lib["thumb_h"]) == (96, 96))
    prep = tgy.prepare(Image.open(src / "Beach" / "b1.jpg")); check("the thumbnail is the picture scaled to cover and cropped to the square", lib["entries"][1]["thumb"] == tgy.square_thumb(prep))
    b1 = lib["entries"][1]["details"]
    check("EXIF details are stored for the fields the picture has: camera, lens, taken, shutter, aperture, ISO, focal length", b1 == dict(format="JPEG", camera="Canon EOS R5", lens="RF24-70mm F2.8", taken=(2024, 7, 3, 9, 0, 0), shutter=(1, 250), aperture=28, iso=800, focal=350))
    check("a picture without EXIF has only its format (no empty fields)", lib["entries"][0]["details"] == {"format": "JPEG"} and lib["entries"][3]["details"] == {"format": "PNG"})
    check("the taken time is stored as seconds since 2000", lib["entries"][1]["taken"] == tgy.taken_seconds((2024, 7, 3, 9, 0, 0)) and lib["entries"][0]["taken"] == 0)
    # the sort by date taken
    out2 = d / "o2"; rc, ev = run(mod or tgy_convert, src / "Beach", out2, "--sort", "taken"); lib2 = load(out2)
    check("--sort taken: the earlier picture first (b2 was taken before b1), one folder ALL PICTURES", [e["name"] for e in lib2["entries"]] == ["b2", "b1"] and [f["name"] for f in lib2["folders"]] == ["ALL PICTURES"] and lib2["sort"] == tgy.SORT_TAKEN)
    shutil.copy(src / "top.png", src / "Beach" / "c0.png"); out3 = d / "o3"; rc, ev = run(mod or tgy_convert, src / "Beach", out3, "--sort", "taken")
    check("--sort taken: a picture without a date comes last", [e["name"] for e in load(out3)["entries"]] == ["b2", "b1", "c0"]); (src / "Beach" / "c0.png").unlink()
    # a flat source: one folder; and the version 0 fallback
    out4 = d / "o4"; rc, ev = run(mod or tgy_convert, src / "Alps", out4, "--index-format", "v0"); data = (out4 / "Assets/tau_gallery/common/tau-library.tdb").read_bytes()
    check("--index-format v0 writes the old flat index (version 0, 96 x 72 thumbnails)", data[4:6] == b"\x00\x00" and tgy.parse_index_full(data)["thumb_h"] == 72 and len(tgy.parse_index(data)) == 1)
    # merge: a new picture and a new folder into the first library; the old ones keep thumbnails and details
    new = d / "new"; (new / "Zoo").mkdir(parents=True); pic(new / "Zoo" / "z1.jpg", (5, 5, 5)); pic(new / "n1.jpg", (60, 160, 60))
    rc, ev = run(mod or tgy_convert, new, out, "--merge"); lib3 = load(out)
    check("--merge keeps the old pictures (thumbnails, details, taken) and adds the new in their folders: folders Alps, Beach, My Pictures, new, Zoo", [f["name"] for f in lib3["folders"]] == ["Alps", "Beach", "My Pictures", "new", "Zoo"] and len(lib3["entries"]) == 6)
    old_b1 = [e for e in lib3["entries"] if e["name"] == "b1"][0]; check("a kept picture keeps its details, taken time and thumbnail", old_b1["details"] == b1 and old_b1["thumb"] == lib["entries"][1]["thumb"] and old_b1["taken"] == lib["entries"][1]["taken"])
    # a version 0 library merged into version 1: the old thumbnails are padded to squares
    out5 = d / "o5"; run(mod or tgy_convert, src / "Alps", out5, "--index-format", "v0"); rc, ev = run(mod or tgy_convert, new, out5, "--merge"); lib5 = load(out5)
    a1 = [e for e in lib5["entries"] if e["name"] == "a1"][0]; v0 = tgy.parse_index_full((out4 / "Assets/tau_gallery/common/tau-library.tdb").read_bytes())["entries"][0]["thumb"]
    check("a version 0 index merged into version 1: its thumbnails are padded to 96 x 96 with black bars, its entries sit in ALL PICTURES", a1["thumb"] == tgy.thumb72_to_square(v0) and any(f["name"] == "ALL PICTURES" for f in lib5["folders"]))
    # an empty subfolder alone is not a folder: the top pictures stay in ALL PICTURES
    src7 = d / "flat7"; (src7 / "empty").mkdir(parents=True); pic(src7 / "t.jpg", (1, 2, 3)); out7 = d / "o7"; rc, ev = run(mod or tgy_convert, src7, out7)
    check("a source whose only subfolder is empty gives one folder, ALL PICTURES", [f["name"] for f in load(out7)["folders"]] == ["ALL PICTURES"])
    # a path that does not fit is skipped, the rest converts
    out6 = d / "o6"; shutil.copy(src / "Alps" / "a1.jpg", src / "Alps" / ("x" * 90 + ".jpg")); rc, ev = run(mod or tgy_convert, src / "Alps", out6)
    check("a picture whose card path does not fit is skipped and said so; the rest still converts", rc == 0 and any(e["event"] == "skip" for e in ev) and len(load(out6)["entries"]) == 1)
    return fails

MUTANTS = [
    ("the subfolders are not folders", "if a.index_format == \"v1\" and subs: jobs =", "if False: jobs ="),
    ("an empty subfolder counts", "subs = [(d, fl) for d, fl in subs if fl]", "subs = [(d, fl) for d, fl in subs]"),
    ("the top pictures go to ALL PICTURES", "[(p, a.src.name or tgy.ALL_PICTURES) for p in top]", "[(p, None) for p in top]"),
    ("the thumbnail is the old 96 x 72 one", "thumb=sq if v1 else thumb", "thumb=thumb"),
    ("the EXIF is read after the orientation", "details = tgy.exif_details(im, fmt) if v1 else {}", "details = {}"),
    ("the format name is dropped", "details = tgy.exif_details(im, fmt)", "details = tgy.exif_details(im, None)"),
    ("the taken time is not stored", "taken=tgy.taken_seconds(details.get(\"taken\"))", "taken=0"),
    ("the date sort ignores the date", "\"taken\": lambda it: (it[\"taken\"] == 0, it[\"taken\"], it[\"path\"].lower())", "\"taken\": lambda it: it[\"path\"].lower()"),
    ("the date sort puts undated first", "(it[\"taken\"] == 0, it[\"taken\"],", "(it[\"taken\"] != 0, it[\"taken\"],"),
    ("the folders are not in name order", "fnames = sorted({it[\"folder_name\"] for it in allitems}, key=lambda x: x.lower())", "fnames = list(dict.fromkeys(it[\"folder_name\"] for it in allitems))[::-1]"),
    ("a version 0 thumbnail is not padded", "tgy.thumb72_to_square(e[\"thumb\"]) if v1 else e[\"thumb\"]", "e[\"thumb\"]"),
    ("a kept picture loses its details", "details=it.get(\"details\", {}))", "details={})"),
    ("a kept picture loses its taken time", "taken=it.get(\"taken\", 0)", "taken=0"),
    ("the sort mode is not recorded", "{\"name\": tgy.SORT_NAME, \"taken\": tgy.SORT_TAKEN, \"converted\": tgy.SORT_CONVERTED}[a.sort]", "tgy.SORT_NAME"),
    ("the v0 fallback writes version 1", "write_atomic(index_dir / a.index_name, tgy.pack_index([dict(", "write_atomic(index_dir / a.index_name, tgy.pack_index_v1([dict("),
]
def mutants():
    src = (ROOT / "tools/gallery/tgy_convert.py").read_text(); surv = 0
    for name, a, b in MUTANTS:
        assert a in src, name
        w = Path(tempfile.mkdtemp()); (w / "tgy_convert.py").write_text(src.replace(a, b, 1))
        for f in (ROOT / "tools/gallery").glob("*.py"):
            if f.name != "tgy_convert.py": shutil.copy(f, w / f.name)
        import importlib.util
        spec = importlib.util.spec_from_file_location("tgy_convert_mut", w / "tgy_convert.py"); m = importlib.util.module_from_spec(spec)
        sys.path.insert(0, str(w))
        try:
            spec.loader.exec_module(m); r = suite(m)
        except Exception: r = 1
        finally: sys.path.remove(str(w))
        print("SURVIVED" if r == 0 else "killed  ", name); surv += r == 0
    return surv
if __name__ == "__main__":
    import contextlib
    if "--mutants" in sys.argv:
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf) if False else contextlib.nullcontext(): pass
        sys.exit(1 if mutants() else 0)
    f = suite(None); print("tgy-convert-v1: %d failure(s)" % f); sys.exit(1 if f else 0)
