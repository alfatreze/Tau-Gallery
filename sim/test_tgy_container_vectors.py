#!/usr/bin/env python3
"""Checks docs/conformance/tgy_container_v1_vectors.json: every stored file parses to exactly the recorded header, tables, level table and pixels (SHA-256 under the TGY profile), and the writer, given the decoded coefficients,
writes exactly the stored bytes; and the numbers printed in docs/TGY_CONTAINER_V1.md agree with the code."""
import base64, hashlib, json, sys, unittest
from pathlib import Path
import numpy as np
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools/gallery"))
import tgy_container as C
DOC = json.loads((ROOT / "docs/conformance/tgy_container_v1_vectors.json").read_text()); TXT = (ROOT / "docs/TGY_CONTAINER_V1.md").read_text()

class T(unittest.TestCase):
    def test_files_parse_to_the_recorded_values(self):
        self.assertEqual([f["name"] for f in DOC["files"]], ["two_levels_420", "one_level_grey"])
        for f in DOC["files"]:
            blob = base64.b64decode(f["file_b64"]); self.assertEqual(len(blob), f["bytes"]); c = C.Container(blob)
            self.assertEqual((c.width, c.height, c.channels, c.orientation), (f["width"], f["height"], f["channels"], f["orientation"])); self.assertEqual(c.tables, f["tables"])
            self.assertEqual(hashlib.sha256(c.thumb).hexdigest(), f["thumbnail_sha256"])
            for i, (e, want) in enumerate(zip(c.levels, f["levels"])):
                for k, v in want.items(): self.assertEqual(list(e[k]) if isinstance(e[k], tuple) else e[k], v, (f["name"], i, k))
                self.assertEqual(hashlib.sha256(c.pixels(i).tobytes()).hexdigest(), f["level_rgb_sha256_tgy_profile"][i])
    def test_the_writer_reproduces_the_stored_bytes(self):
        for f in DOC["files"]:
            blob = base64.b64decode(f["file_b64"]); c = C.Container(blob)
            levels = [dict(kind=e["kind"], pic=c.picture(i), rect=tuple(e["rect"])) for i, e in enumerate(c.levels)]
            self.assertEqual(C.write_container(c.width, c.height, c.orientation, c.thumb, levels), blob, f["name"])
    def test_alignment_in_the_vectors(self):
        for f in DOC["files"]:
            blob = base64.b64decode(f["file_b64"]); self.assertEqual(len(blob) % 4096, 0)
            for e in C.Container(blob).levels: self.assertEqual(e["data_off"] % 4096, 0); self.assertEqual(e["index_off"] % 4096, 0)
    def test_the_document_matches_the_code(self):
        for token in ("version 1", "64 bytes", "48 bytes", "12 bytes", "4,096", "MAX_LEVELS = 16", "MAX_TABLES = 8", "<4sHHIIBBBBIIIIHHIII", "<IIHHHHBBBBBBBBIIIIII"): self.assertIn(token, TXT)
        self.assertEqual((C.HEADER, C.LEVEL, C.TILE_INDEX, C.ALIGN, C.MAX_LEVELS, C.MAX_TABLES, C.VERSION), (64, 48, 12, 4096, 16, 8, 1))
        import struct; self.assertEqual(struct.calcsize("<4sHHIIBBBBIIIIHHIII"), 52); self.assertEqual(struct.calcsize("<IIHHHHBBBBBBBBIIIIII"), 48)

if __name__ == "__main__":
    unittest.main()
