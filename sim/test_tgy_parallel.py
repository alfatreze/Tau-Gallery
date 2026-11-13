#!/usr/bin/env python3
"""Tests the parallel tile encoder of tools/gallery/tgy_container.py (encode_tiles, the --jobs option of the converter): coding the tiles of a picture on several processes must give the SAME file, byte for byte, as one process,
for a JPEG source (the lossless full level), a picture that is not a JPEG (encoded here), a picture with a partial last window of tiles, and when jobs is more than the tiles. Also the zoom level of a 4:4:4 JPEG (the core's decoder has no 4:4:4: it is encoded as 4:2:0). `--mutants` edits copies of the module: an unordered
result, a lost last window, a window that is never flushed and a tile handed to the wrong coder must each fail. Run: python3 sim/test_tgy_parallel.py [--mutants]"""
import io, sys, tempfile, importlib, shutil
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools/gallery")); sys.path.insert(0, str(ROOT / "sim"))
import numpy as np
from PIL import Image
fails = 0
def check(name, cond):
    global fails
    print(("ok   " if cond else "FAIL ") + name)
    fails += 0 if cond else 1

def picture(w, h, seed):
    rng = np.random.default_rng(seed); base = rng.integers(0, 255, (h // 8 + 1, w // 8 + 1, 3), dtype=np.uint8)
    img = np.kron(base, np.ones((8, 8, 1), dtype=np.uint8))[:h, :w]; img = (img.astype(np.int16) + rng.integers(-6, 6, img.shape)).clip(0, 255).astype(np.uint8)
    return Image.fromarray(img, "RGB")

def wide_profile():
    """An ICC profile that is not sRGB (Display P3's primaries, a name without 'srgb'), made from the built-in sRGB profile so the test needs no file from the machine."""
    import struct
    from PIL import ImageCms
    b = bytearray(ImageCms.ImageCmsProfile(ImageCms.createProfile("sRGB")).tobytes()); n = struct.unpack_from(">I", b, 128)[0]
    prim = {b"rXYZ": (0.5151, 0.2412, -0.0011), b"gXYZ": (0.2920, 0.6922, 0.0419), b"bXYZ": (0.1571, 0.0666, 0.7841)}
    for i in range(n):
        sig, off, size = struct.unpack_from(">4sII", b, 132 + 12 * i)
        if sig in prim: struct.pack_into(">iii", b, off + 8, *[int(round(v * 65536)) for v in prim[sig]])
    for old, new in (("sRGB built-in".encode("utf-16-be"), "WideGamutTest".encode("utf-16-be")), (b"sRGB built-in", b"WideGamutTest")):
        i = bytes(b).find(old)
        while i >= 0: b[i:i + len(old)] = new; i = bytes(b).find(old)
    return bytes(b)

def build(moddir=None):
    """The container and make modules from a directory (the real one, or a mutated copy), freshly imported."""
    for m in ("tgy_container", "tgy_make", "tgy", "tgy_encode", "tgy_recon", "tgy_v0", "tvcrc", "jpegcoef"): sys.modules.pop(m, None)
    sys.path.insert(0, str(moddir or ROOT / "tools/gallery"))
    try: return importlib.import_module("tgy_container"), importlib.import_module("tgy_make"), importlib.import_module("tgy")
    finally: sys.path.pop(0)

def suite(C, MK, tgy, tmp):
    jp = tmp / "p.jpg"; picture(1100, 900, 5).save(jp, quality=90, subsampling=2)               # 9 x 8 tiles in the full level: a partial last window at jobs 2 (32 tiles a window)
    prep = tgy.prepare(Image.open(jp)); serial, thumb, size = MK.canvas_container(jp, prep, 92, 90, False, jobs=1)
    for jobs in (2, 3, 0):
        par, th2, sz2 = MK.canvas_container(jp, prep, 92, 90, False, jobs=jobs)
        check(f"a JPEG: {jobs or 'automatic'} processes give the same file as one ({len(par)} bytes)", par == serial and th2 == thumb and sz2 == size)
    png = tmp / "p.png"; picture(900, 1000, 6).save(png)                                         # not a JPEG: the full level is encoded by the converter
    prep = tgy.prepare(Image.open(png)); serial, _, _ = MK.canvas_container(png, prep, 92, 90, False, jobs=1); par, _, _ = MK.canvas_container(png, prep, 92, 90, False, jobs=2)
    check("a picture that is not a JPEG: two processes give the same file as one", par == serial)
    big_jobs, _, _ = MK.canvas_container(jp, tgy.prepare(Image.open(jp)), 92, 90, False, jobs=40)
    check("more processes than tiles (40) still gives the same file", big_jobs == MK.canvas_container(jp, tgy.prepare(Image.open(jp)), 92, 90, False, jobs=1)[0])
    one = list(C.encode_tiles([], 3)); check("no tiles: no streams", one == [])
    seq = [C.V.encode_tile(*t) for t in [(g, s) for _, _, g, s in C.R.split_tiles(C.jpegcoef.read_jpeg(jp.read_bytes()))][:5]]
    got = list(C.encode_tiles(((g, s) for _, _, g, s in list(C.R.split_tiles(C.jpegcoef.read_jpeg(jp.read_bytes())))[:5]), 2))
    check("five tiles through two processes come back in order", got == seq)
    check("auto_jobs is between 1 and 8", 1 <= C.auto_jobs() <= 8)
    import inspect
    check("the library defaults to ONE process (a script without a main guard would crash when workers start); only the converter's command line asks for automatic",
          inspect.signature(C.write_container).parameters["jobs"].default == 1 and inspect.signature(C.encode_tiles).parameters["jobs"].default == 1 and inspect.signature(MK.canvas_container).parameters["jobs"].default == 1)
    def full_fmt(sub, grey=False, kind=1):
        f = tmp / f"s{sub}{grey}.jpg"; im = picture(1000, 900, 9); (im.convert("L") if grey else im).save(f, quality=90, **({} if grey else {"subsampling": sub}))
        data, _, _ = MK.canvas_container(f, tgy.prepare(Image.open(f)), 92, 90, False, jobs=1); c = C.Container(data); return [e["pixfmt"] for e in c.levels if e["kind"] == kind]
    check("a 4:4:4 JPEG's zoom level stays 4:4:4 (K6: the core's decoder has C444); 4:2:0 stays 4:2:0; grey stays grey; 4:2:2 is stored as 4:2:0",
          full_fmt(0) == [C.SUB_444] and full_fmt(2) == [C.SUB_420] and full_fmt(0, grey=True) == [C.SUB_GREY] and full_fmt(1) == [C.SUB_420])
    # colour: a JPEG whose ICC profile is not sRGB has its fit view converted to sRGB, so its zoom level must come from the converted picture (not the raw coefficients) or the colours jump on zoom
    from PIL import ImageEnhance, ImageCms
    wide = wide_profile(); srgb_icc = ImageCms.ImageCmsProfile(ImageCms.createProfile("sRGB")).tobytes()
    base = ImageEnhance.Color(picture(1000, 900, 13)).enhance(2.5)
    def zoom_vs_fit(icc, tag, sub=2):
        f = tmp / f"icc_{tag}.jpg"; base.save(f, "JPEG", quality=85, subsampling=sub, **({"icc_profile": icc} if icc else {}))
        im = Image.open(f); prep = tgy.prepare(im); data, _, _ = MK.canvas_container(f, prep, 92, 90, False, jobs=1); c = C.Container(data)
        fit = [k for k, e in enumerate(c.levels) if e["kind"] == 2][0]; full = [k for k, e in enumerate(c.levels) if e["kind"] == 1][0]
        x, y, w, h = tgy.fit_rect(prep.width, prep.height, 800, 720)
        a = c.pixels(fit).astype(float)[y:y + h, x:x + w].reshape(-1, 3).mean(0); b = c.pixels(full).astype(float).reshape(-1, 3).mean(0)
        lossless = sorted(c.tables[c.levels[full]["qy"]]) == sorted(im.quantization[0])                       # the JPEG's own quantisation table: its coefficients were kept
        raw = np.asarray(im.convert("RGB"), float).reshape(-1, 3).mean(0); conv = np.asarray(prep, float).reshape(-1, 3).mean(0)
        return float(np.abs(a - b).max()), lossless, float(np.abs(raw - conv).max())
    d_wide, ll_wide, shift = zoom_vs_fit(wide, "wide"); d_srgb, ll_srgb, _ = zoom_vs_fit(srgb_icc, "srgb"); d_none, ll_none, _ = zoom_vs_fit(None, "none")
    check(f"the wide-gamut profile test is not vacuous: the conversion to sRGB moves the mean colour by {shift:.1f} levels", shift > 2.0 and tgy.needs_srgb(Image.open(tmp / "icc_wide.jpg")))
    check(f"a 4:2:0 JPEG with a wide-gamut profile: the zoom level has the fit view's colours (difference {d_wide:.2f}) because it is encoded from the converted picture", d_wide < 1.5 and not ll_wide)
    check("a 4:2:0 JPEG with an sRGB profile, and one with none, keep their own coefficients (lossless) and the colours agree", ll_srgb and ll_none and d_srgb < 1.5 and d_none < 1.5 and not tgy.needs_srgb(Image.open(tmp / "icc_srgb.jpg")))
    # orientation: a phone's portrait photo is a landscape file with an EXIF tag. The core draws a level as stored, so the zoom level is written upright and the tag reset
    from PIL import Image as _I
    ex = _I.Exif(); ex[0x0112] = 6; fr = tmp / "rot6.jpg"; picture(1200, 800, 17).save(fr, "JPEG", quality=85, subsampling=2, exif=ex)
    rp = tgy.prepare(_I.open(fr)); rd, _, _ = MK.canvas_container(fr, rp, 92, 90, False, jobs=1); rc = C.Container(rd); rfull = [k for k, e in enumerate(rc.levels) if e["kind"] == 1][0]
    ref = np.asarray(rp, float); got = rc.pixels(rfull).astype(float)
    mse = float(((ref - got[:ref.shape[0], :ref.shape[1]]) ** 2).mean()); psnr = 99.0 if mse == 0 else 10 * np.log10(255 ** 2 / mse)
    check(f"an EXIF-rotated JPEG: the zoom level is upright ({rc.levels[rfull]['w']} x {rc.levels[rfull]['h']} for a 1200 x 800 file with tag 6), the header says orientation 1 and the picture matches the rotated source (PSNR {psnr:.1f} dB)",
          (rc.levels[rfull]["w"], rc.levels[rfull]["h"]) == (800, 1200) and rc.orientation == 1 and psnr > 27.0)
    for tag in (2, 3, 4, 5, 7, 8):
        ex = _I.Exif(); ex[0x0112] = tag; ft = tmp / f"rot{tag}.jpg"; picture(1100, 700, 18).save(ft, "JPEG", quality=85, subsampling=2, exif=ex); pt = tgy.prepare(_I.open(ft))
        ct = C.Container(MK.canvas_container(ft, pt, 92, 90, False, jobs=1)[0]); fl = [e for e in ct.levels if e["kind"] == 1][0]
        check(f"orientation {tag}: the zoom level has the upright size {pt.width} x {pt.height} and the header orientation 1", (fl["w"], fl["h"]) == (pt.width, pt.height) and ct.orientation == 1)
    # speed: the coefficient read is the slow part, and it is skipped for every case that is encoded again (4:2:2, rotated, wide-gamut, not a JPEG)
    calls = []; real_read = MK.jpegcoef.read_jpeg
    MK.jpegcoef.read_jpeg = lambda data: (calls.append(1), real_read(data))[1]
    try:
        for label, path_, want in (("4:2:0", tmp / "s2False.jpg", 1), ("4:4:4", tmp / "s0False.jpg", 1), ("4:2:2", tmp / "s1False.jpg", 0), ("rotated", fr, 0), ("wide-gamut", tmp / "icc_wide.jpg", 0), ("PNG", png, 0)):
            calls.clear(); MK.canvas_container(path_, tgy.prepare(Image.open(path_)), 92, 90, False, jobs=1); check(f"the JPEG coefficient read happens {'once' if want else 'not at all'} for a {label} picture (it is skipped when the level is encoded again)", len(calls) == want)
    finally: MK.jpegcoef.read_jpeg = real_read
    import multiprocessing
    real_pool = multiprocessing.Pool
    def no_pool(*a, **k): raise AssertionError("a pool was started")
    multiprocessing.Pool = no_pool
    try: ok = list(C.encode_tiles(((g, s) for _, _, g, s in list(C.R.split_tiles(C.jpegcoef.read_jpeg(jp.read_bytes())))[:3]), 1)) == seq[:3]
    except AssertionError: ok = False
    finally: multiprocessing.Pool = real_pool
    check("one process is serial: no pool is started", ok)

if __name__ == "__main__":
    tmp = Path(tempfile.mkdtemp())
    C, MK, tgy = build(); suite(C, MK, tgy, tmp)
    if "--mutants" in sys.argv:
        src = (ROOT / "tools/gallery/tgy_container.py").read_text(); mksrc = (ROOT / "tools/gallery/tgy_make.py").read_text(); tgysrc = (ROOT / "tools/gallery/tgy.py").read_text(); bad = 0
        for name, a, b in (("a wide-gamut profile keeps the raw coefficients", "if tgy.needs_srgb(src_img): raise ValueError(", "if False: raise ValueError("),
                           ("every profile counts as wide-gamut", 'return None if "srgb" in ImageCms.getProfileDescription(prof).lower() else prof', "return prof"),
                           ("no profile counts as wide-gamut", "needs_srgb(img):\n", "needs_srgb(img):\n    return True\n"),
                           ("a wide-gamut profile is never recognised", "return _icc_profile(img) is not None", "return False"),
                           ("a rotated JPEG keeps its coefficients (and the tag)", 'if orient != 1: raise ValueError("an EXIF orientation', 'if False: raise ValueError("an EXIF orientation'),
                           ("the subsampling is not told from the header (the coefficients are read first)", 'if src_img.mode != "L" and JpegImagePlugin.get_sampling(src_img) not in (0, 2): raise ValueError(', 'if False: raise ValueError('),
                           ("a 4:4:4 JPEG is encoded again as 4:2:0 (the pre-K6 rule)", 'JpegImagePlugin.get_sampling(src_img) not in (0, 2): raise ValueError(', 'JpegImagePlugin.get_sampling(src_img) != 2: raise ValueError('),
                           ("the windows come back reversed", "if len(window) >= jobs * 16: yield from pool.map(_encode_one, window, chunksize=4); window = []", "if len(window) >= jobs * 16: yield from reversed(pool.map(_encode_one, window, chunksize=4)); window = []"),
                           ("the last window is dropped", "        if window: yield from pool.map(_encode_one, window, chunksize=4)\n", ""),
                           ("a full window is dropped", "if len(window) >= jobs * 16: yield from pool.map(_encode_one, window, chunksize=4); window = []", "if len(window) >= jobs * 16: window = []"),
                           ("each tile is coded with the next tile's shapes", "def _encode_one(t): return V.encode_tile(t[0], t[1])", "def _encode_one(t): return V.encode_tile(t[0], t[1][::-1])"),
                           ("one process is not serial: it always uses a pool of 2", "    if jobs <= 1:\n        for t in tiles: yield _encode_one(t)\n        return", "    jobs = max(jobs, 2)")):
            assert a in src or a in mksrc or a in tgysrc, name
            d = Path(tempfile.mkdtemp()); [shutil.copy(p, d / p.name) for p in (ROOT / "tools/gallery").glob("*.py")]
            if a in tgysrc: (d / "tgy.py").write_text(tgysrc.replace(a, b, 1))
            elif a in mksrc: (d / "tgy_make.py").write_text(mksrc.replace(a, b, 1))
            else: (d / "tgy_container.py").write_text(src.replace(a, b, 1))
            before = fails; buf = io.StringIO(); old = sys.stdout; sys.stdout = buf
            try: C2, MK2, tgy2 = build(d); suite(C2, MK2, tgy2, tmp)
            except Exception as e: fails += 1; print("exception", repr(e)[:80])
            finally: sys.stdout = old
            killed = fails > before; fails = before; print("killed  " if killed else "SURVIVED", name); bad += not killed
        sys.exit(1 if bad or fails else 0)
    print("tgy-parallel: %d failure(s)" % fails); sys.exit(1 if fails else 0)
