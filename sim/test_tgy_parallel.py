#!/usr/bin/env python3
"""Tests the parallel tile encoder of tools/gallery/tgy_container.py (encode_tiles, the --jobs option of the converter): coding the tiles of a picture on several processes must give the SAME file, byte for byte, as one process,
for a JPEG source (the lossless full level), a picture that is not a JPEG (encoded here), a picture with a partial last window of tiles, and when jobs is more than the tiles. `--mutants` edits copies of the module: an unordered
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
        src = (ROOT / "tools/gallery/tgy_container.py").read_text(); bad = 0
        for name, a, b in (("the windows come back reversed", "if len(window) >= jobs * 16: yield from pool.map(_encode_one, window, chunksize=4); window = []", "if len(window) >= jobs * 16: yield from reversed(pool.map(_encode_one, window, chunksize=4)); window = []"),
                           ("the last window is dropped", "        if window: yield from pool.map(_encode_one, window, chunksize=4)\n", ""),
                           ("a full window is dropped", "if len(window) >= jobs * 16: yield from pool.map(_encode_one, window, chunksize=4); window = []", "if len(window) >= jobs * 16: window = []"),
                           ("each tile is coded with the next tile's shapes", "def _encode_one(t): return V.encode_tile(t[0], t[1])", "def _encode_one(t): return V.encode_tile(t[0], t[1][::-1])"),
                           ("one process is not serial: it always uses a pool of 2", "    if jobs <= 1:\n        for t in tiles: yield _encode_one(t)\n        return", "    jobs = max(jobs, 2)")):
            assert a in src, name
            d = Path(tempfile.mkdtemp()); [shutil.copy(p, d / p.name) for p in (ROOT / "tools/gallery").glob("*.py")]; (d / "tgy_container.py").write_text(src.replace(a, b, 1))
            before = fails; buf = io.StringIO(); old = sys.stdout; sys.stdout = buf
            try: C2, MK2, tgy2 = build(d); suite(C2, MK2, tgy2, tmp)
            except Exception as e: fails += 1; print("exception", repr(e)[:80])
            finally: sys.stdout = old
            killed = fails > before; fails = before; print("killed  " if killed else "SURVIVED", name); bad += not killed
        sys.exit(1 if bad or fails else 0)
    print("tgy-parallel: %d failure(s)" % fails); sys.exit(1 if fails else 0)
