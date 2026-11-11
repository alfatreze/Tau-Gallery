#!/usr/bin/env python3
"""Writes docs/conformance/tgy_container_v1_vectors.json: two small complete TGY container files (version 1) with the values a conforming reader must find in them: a two-level 4:2:0 file (an encoded fit level and a full level)
and a one-level grey file. Per file: the bytes (base64), the header fields, the level table, the quantisation tables and the SHA-256 of each level's RGB under the TGY profile. A reader conforms if it parses these files to
exactly those values; a writer conforms if, given the decoded coefficients, it writes exactly these bytes (tests: sim/test_tgy_container_vectors.py). The pictures come from tgy_encode (deterministic numpy arithmetic); the stored bytes are
the reference, regenerating on another platform may differ in a rounding of the encoder, which is why the test does not require regeneration."""
import base64, hashlib, json, sys
from pathlib import Path
import numpy as np
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/gallery")); sys.path.insert(0, str(ROOT / "sim"))
import tgy_container as C, tgy_encode as E, tgy_recon as R, test_jpegcoef as TJ
OUT = ROOT / "docs/conformance/tgy_container_v1_vectors.json"

def describe(name, blob):
    c = C.Container(blob)
    levels = [{k: (list(v) if isinstance(v, tuple) else v) for k, v in e.items()} for e in c.levels]
    pix = [hashlib.sha256(c.pixels(i).tobytes()).hexdigest() for i in range(len(c.levels))]
    return {"name": name, "file_b64": base64.b64encode(blob).decode(), "bytes": len(blob), "width": c.width, "height": c.height, "channels": c.channels, "orientation": c.orientation,
            "tables": c.tables, "thumbnail_sha256": hashlib.sha256(c.thumb).hexdigest(), "levels": levels, "level_rgb_sha256_tgy_profile": pix}

def build():
    thumb = bytes((i * 7) & 255 for i in range(C.THUMB_BYTES))
    fit = E.encode_rgb(np.asarray(TJ.picture(60, 40, seed=21, patch=False)), 85, 2); full = E.encode_rgb(np.asarray(TJ.picture(300, 200, seed=22)), 80, 2)
    a = C.write_container(300, 200, 1, thumb, [dict(kind=C.KIND_FIT, pic=fit, rect=(370, 340, 60, 40)), dict(kind=C.KIND_FULL, pic=full, rect=(0, 0, 300, 200))])
    g = E.encode_rgb(np.asarray(TJ.picture(64, 48, seed=23, patch=False).convert("L")), 90); b = C.write_container(64, 48, 6, thumb, [dict(kind=C.KIND_FULL, pic=g, rect=(368, 336, 64, 48))])
    return {"format": "TGY container", "version": C.VERSION, "files": [describe("two_levels_420", a), describe("one_level_grey", b)]}

def main():
    OUT.write_text(json.dumps(build(), indent=1) + "\n"); print("wrote", OUT, OUT.stat().st_size, "bytes")

if __name__ == "__main__":
    main()
