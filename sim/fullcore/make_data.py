#!/usr/bin/env python3
"""Writes the data directory of the whole-core simulation: python3 sim/fullcore/make_data.py DIR. It is `make_v1_set` of sim/test_gallery_app.py (six pictures in the hardware decoder's container, fixed seeds, and their
index tau-library.tdb), so the golden frame does not depend on an anonymous folder: tests of the same files, same bytes. `run.py --dir DIR` reads it; the golden frame records the SHA-256 of every file in it."""
import sys
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "sim")); sys.path.insert(0, str(ROOT / "tools/gallery"))
import test_gallery_app as T

if __name__ == "__main__":
    d = Path(sys.argv[1]); d.mkdir(parents=True, exist_ok=True); T.make_v1_set(d); print("wrote", len(list(d.iterdir())), "files to", d)
