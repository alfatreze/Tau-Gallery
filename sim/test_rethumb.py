#!/usr/bin/env python3
"""Host test of tools/gallery/rethumb.py: three pictures (wide, tall, square, bright and with a marker off centre) are packed as version 1 files with an index whose thumbnails have black bars (the old,
merged kind); after rethumb every thumbnail must be the picture scaled to COVER the square and centre-cropped (checked against a cover thumbnail made here from the SOURCE picture with its own arithmetic: PSNR,
and no bar rows or columns), while names, order, folders, details, sizes and times are unchanged. Mutants of rethumb.py must each fail. Run: python3 sim/test_rethumb.py [--mutants]"""
import io, sys, tempfile
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools/gallery"))
import tgy, tgy_make
fails = 0
def check(name, ok):
    global fails
    print(("ok   " if ok else "FAIL ") + name); fails += 0 if ok else 1

def source(w, h, seed):
    im = Image.new("RGB", (w, h)); a = np.zeros((h, w, 3), np.uint8)
    a[..., 0] = np.linspace(60, 230, w)[None, :]; a[..., 1] = np.linspace(230, 60, h)[:, None]; a[..., 2] = 120 + 20 * seed
    im = Image.fromarray(a); d = ImageDraw.Draw(im); d.ellipse([w * .2, h * .3, w * .35, h * .55], fill=(250, 250, 20)); return im

def cover(im):                                             # the expected thumbnail, with its own arithmetic (not tgy.square_thumb)
    s = max(96 / im.width, 96 / im.height); w, h = max(96, round(im.width * s)), max(96, round(im.height * s))
    r = im.resize((w, h), Image.LANCZOS); x, y = (w - 96) // 2, (h - 96) // 2
    a = np.asarray(r.crop((x, y, x + 96, y + 96)).convert("RGB")).astype(int)
    return (a[..., 0] >> 3 << 3, a[..., 1] >> 2 << 2, a[..., 2] >> 3 << 3)

def to_rgb(thumb):
    v = np.frombuffer(thumb, "<u2").reshape(96, 96).astype(int)
    return np.stack([(v >> 11 & 31) << 3, (v >> 5 & 63) << 2, (v & 31) << 3], -1)

def psnr(a, b):
    m = ((a.astype(float) - b.astype(float)) ** 2).mean(); return 99 if m == 0 else 10 * np.log10(255 ** 2 / m)

def build(d):
    lib = d / "lib"; (lib / "photos").mkdir(parents=True); srcs = {"wide": source(1500, 500, 1), "tall": source(500, 1500, 2), "square": source(900, 900, 3)}
    ents, thumbs = [], []
    for k, (name, im) in enumerate(sorted(srcs.items())):
        p = d / f"{name}.jpg"; im.save(p, quality=95); prep = tgy.prepare(Image.open(p)); data, t72, size = tgy_make.canvas_container(p, prep)
        (lib / "photos" / f"{name}.tgy").write_bytes(data)
        ents.append(dict(path=f"Assets/tau_gallery/common/photos/{name}.tgy", name=name.upper(), size=len(data), width=size[0], height=size[1], folder=k % 2, taken=1000 + k, details=dict(format="JPEG", camera="Test cam %d" % k)))
        thumbs.append(tgy.thumb72_to_square(t72))
    ents.sort(key=lambda e: (e["folder"], e["path"])); order = {e["name"].lower(): i for i, e in enumerate(ents)}
    ths = [None] * 3
    for e in ents: ths[ents.index(e)] = thumbs[sorted(srcs).index(e["name"].lower())]
    (lib / "tau-library.tdb").write_bytes(tgy.pack_index_v1(ents, ths, [dict(name="A"), dict(name="B")], tgy.SORT_NAME))
    return lib, srcs, ents

def evaluate(mod):
    global fails
    fails = 0; d = Path(tempfile.mkdtemp()); lib, srcs, ents = build(d)
    old = tgy.parse_index_full((lib / "tau-library.tdb").read_bytes())
    check("the starting library has bars: the first row of every old thumbnail is black", all(not any(e["thumb"][:192]) for e in old["entries"] if e["name"] != "SQUARE") and any(e["name"] == "WIDE" for e in old["entries"]))
    new = tgy.parse_index_full(mod.rethumb((lib / "tau-library.tdb").read_bytes(), lib / "photos"))
    check("same entries, names, order, folders, sizes, details and times", [(e["path"], e["name"], e["size"], e["width"], e["height"], e["folder"], e["taken"], e["details"]) for e in new["entries"]] == [(e["path"], e["name"], e["size"], e["width"], e["height"], e["folder"], e["taken"], e["details"]) for e in old["entries"]]
          and [f["name"] for f in new["folders"]] == ["A", "B"] and new["sort"] == old["sort"] and new["version"] == 1)
    for e in new["entries"]:
        rgb = to_rgb(e["thumb"]); ref = np.stack(cover(srcs[e["name"].lower()]), -1)
        check(f"{e['name']}: the thumbnail is the picture covering the square (PSNR {psnr(rgb, ref):.1f} dB against a cover thumbnail of the source)", psnr(rgb, ref) > 26)
        edge = [rgb[0].mean(), rgb[-1].mean(), rgb[:, 0].mean(), rgb[:, -1].mean()]
        check(f"{e['name']}: no bar at any edge (the mean of each edge row and column is not near black)", min(edge) > 45)
    marker = to_rgb(next(e for e in new["entries"] if e["name"] == "TALL")["thumb"])
    yel = np.argwhere((marker[..., 0] > 200) & (marker[..., 1] > 200) & (marker[..., 2] < 80)); ref_y = np.argwhere((np.stack(cover(srcs["tall"]), -1)[..., 0] > 200) & (np.stack(cover(srcs["tall"]), -1)[..., 2] < 80))
    check("the marker of the tall picture is where the centre crop puts it", len(yel) > 5 and abs(yel[:, 0].mean() - ref_y[:, 0].mean()) < 4 and abs(yel[:, 1].mean() - ref_y[:, 1].mean()) < 4)
    try: mod.rethumb(tgy.pack_index((lambda e: [dict(path="p/x.tgy", name="x", size=1, width=1, height=1, thumb=bytes(13824))])(0), [bytes(13824)]), lib / "photos"); ok = False
    except ValueError: ok = True
    check("a version 0 index is refused (it has no 96 x 96 thumbnails)", ok)
    try: mod.rethumb((lib / "tau-library.tdb").read_bytes(), d / "nowhere"); ok = False
    except OSError: ok = True
    check("a missing picture file is an error, not a silent skip", ok)
    return fails

MUTANTS = [
    ("the whole canvas is used (the bars come along)", "px[y + iy:y + h - iy, x + ix:x + w - ix]", "px"),
    # not a mutant: dropping the one-pixel inset changes the outer row of a thumbnail by at most 3 levels (measured on these pictures), below anything a test can tell from the resampling
    ("the crop starts at the canvas origin", "px[y + iy:y + h - iy, x + ix:x + w - ix]", "px[iy:h - iy, ix:w - ix]"),
    ("the rectangle is taken for another picture size", "tgy.fit_rect(c.width, c.height, cw, ch)", "tgy.fit_rect(c.height, c.width, cw, ch)"),
    ("the folders are dropped", "[dict(name=f[\"name\"], cover=f.get(\"cover\")) for f in d[\"folders\"]], d[\"sort\"])", "None, d[\"sort\"])"),
    ("the details are dropped", "details=e.get(\"details\", {}))", "details={})"),
    ("the taken time is dropped", "taken=e.get(\"taken\", 0)", "taken=0"),
    ("a version 0 index is accepted", "    if d[\"version\"] != 1: raise ValueError", "    if False: raise ValueError"),
    ("a missing picture is skipped", "thumbs.append(cover_thumb(f.read_bytes())); log(e[\"name\"])", "thumbs.append(cover_thumb(f.read_bytes()) if f.exists() else bytes(18432)); log(e[\"name\"])"),
]

if __name__ == "__main__":
    import importlib.util
    f = evaluate(importlib.import_module("rethumb")); print("rethumb: %d failure(s)" % f)
    if f: sys.exit(1)
    if "--mutants" in sys.argv:
        src = (ROOT / "tools/gallery/rethumb.py").read_text(); surv = 0
        for name, a, b in MUTANTS:
            assert a in src, name
            w = Path(tempfile.mkdtemp()); (w / "rethumb_mut.py").write_text(src.replace(a, b, 1)); sys.path.insert(0, str(w))
            try:
                import contextlib; spec = importlib.util.spec_from_file_location("rethumb_mut", w / "rethumb_mut.py"); m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
                with contextlib.redirect_stdout(io.StringIO()): n = evaluate(m)
            except Exception: n = 1
            finally: sys.path.remove(str(w))
            print("killed  " if n else "SURVIVED", name); surv += not n
        sys.exit(1 if surv else 0)
