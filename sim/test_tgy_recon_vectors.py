#!/usr/bin/env python3
"""Checks docs/conformance/tgy_recon_v0_vectors.json against the reference (tools/gallery/tgy_recon.py) and the numbers printed in docs/TGY_RECONSTRUCTION_V0.md against the code: the file is what the reference produces, every
picture's SHA-256 is reproduced from the coefficients stored in the file, and the document cannot drift from the constants."""
import base64, hashlib, json, re, sys, unittest
from pathlib import Path
import numpy as np
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools/gallery"))
import jpegcoef, tgy_recon as R, make_tgy_recon_vectors as G
DOC = json.loads((ROOT / "docs/conformance/tgy_recon_v0_vectors.json").read_text()); TXT = (ROOT / "docs/TGY_RECONSTRUCTION_V0.md").read_text()

def jpeg_of(pic):
    j = jpegcoef.Jpeg(); j.width, j.height, j.quant, j.components = pic["width"], pic["height"], {}, []
    for k, c in enumerate(pic["components"]):
        comp = jpegcoef.Comp(c["id"], c["h"], c["v"], k); comp.blocks_w, comp.blocks_h = c["blocks_w"], c["blocks_h"]
        comp.coef = np.frombuffer(base64.b64decode(c["coefficients_i16le_b64"]), dtype="<i2").reshape(comp.blocks_h, comp.blocks_w, 64).astype(np.int16); j.quant[k] = np.array(c["quant_zigzag"]); j.components.append(comp)
    return j

class T(unittest.TestCase):
    def test_the_file_is_what_the_reference_produces(self):
        self.assertEqual(DOC, G.build())                                                                   # the file is exactly what the generator and the reference produce now
        for v in DOC["idct"]: self.assertEqual(R.idct_islow(np.array(v["dequantised_natural"], dtype=np.int64).reshape(1, 8, 8)).reshape(-1).tolist(), v["samples"], v["name"])
        for v in DOC["colour"]: self.assertEqual(R.ycc_to_rgb(np.array([v["y"]]), np.array([v["cb"]]), np.array([v["cr"]]))[0].tolist(), v["rgb"])
        for v in DOC["upsample"]:
            p = np.frombuffer(base64.b64decode(v["plane_b64"]), dtype=np.uint8).reshape(v["height"], v["width"])
            self.assertEqual(base64.b64encode(R.upsample_h2v2_fancy(p).tobytes()).decode(), v["libjpeg_b64"], v["name"]); self.assertEqual(base64.b64encode(R.upsample_h2v2_tile(p).tobytes()).decode(), v["tile_b64"], v["name"])
    def test_pictures_hashes_from_the_stored_coefficients(self):
        self.assertEqual([p["name"] for p in DOC["pictures"]], ["grey_40x30", "yuv444_64x48", "yuv420_200x150"])
        for pic in DOC["pictures"]:
            j = jpeg_of(pic); self.assertEqual(hashlib.sha256(R.decode_pixels(j).tobytes()).hexdigest(), pic["rgb_sha256_libjpeg_profile"], pic["name"]); self.assertEqual(hashlib.sha256(R.decode_pixels_tgy(j).tobytes()).hexdigest(), pic["rgb_sha256_tgy_profile"], pic["name"])
        p420 = DOC["pictures"][2]; self.assertNotEqual(p420["rgb_sha256_libjpeg_profile"], p420["rgb_sha256_tgy_profile"])      # 200 x 150 has tile seams: the profiles differ there
        for p in DOC["pictures"][:2]: self.assertEqual(p["rgb_sha256_libjpeg_profile"], p["rgb_sha256_tgy_profile"])             # grey and 4:4:4 are exact in both
    def test_cases_cover_saturation_and_wrap(self):
        names = [v["name"] for v in DOC["idct"]]; self.assertIn("dc_saturate_high", names); self.assertIn("dc_range_wrap", names)
        d = {v["name"]: v for v in DOC["idct"]}; self.assertTrue(all(s == 255 for s in d["dc_saturate_high"]["samples"])); self.assertTrue(all(s == 0 for s in d["dc_saturate_low"]["samples"]))
    def test_the_document_matches_the_code(self):
        for val in R.F.values(): self.assertIn(str(val), TXT)
        self.assertIn("91881, 116130, 46802, 22554", TXT); self.assertIn("CONST_BITS = 13 and PASS1_BITS = 2", TXT); self.assertIn("18 plausible one-line deviations", TXT)
        self.assertEqual((R.CR_R[255], R.CB_B[255], R.CR_G[255], R.CB_G[255]), ((91881 * 127 + 32768) >> 16, (116130 * 127 + 32768) >> 16, -46802 * 127, -22554 * 127 + 32768))

if __name__ == "__main__":
    unittest.main()
