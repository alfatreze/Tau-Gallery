#!/usr/bin/env python3
"""Writes docs/conformance/tgy_recon_v0_vectors.json: conformance vectors of the TGY reconstruction (docs/TGY_RECONSTRUCTION_V0.md): single-block IDCT cases (dequantised natural-order coefficients to 64 samples, including
saturation and the range-limit wrap), colour conversion triples, small plane upsampling cases (both the libjpeg whole-plane filter and the tile profile), and whole small pictures (quantisation tables, coefficient blocks,
SHA-256 of the RGB bytes under both profiles; the libjpeg profile equals Pillow's decode of the same JPEG, which test_tgy_recon.py checks). Deterministic."""
import base64, hashlib, io, json, struct, sys
from pathlib import Path
import numpy as np
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/gallery")); sys.path.insert(0, str(ROOT / "sim"))
import jpegcoef, tgy_recon as R, test_jpegcoef as TJ
OUT = ROOT / "docs/conformance/tgy_recon_v0_vectors.json"
b64 = lambda a, fmt: base64.b64encode(np.asarray(a).astype(fmt).tobytes()).decode()

def idct_cases():
    rs = np.random.RandomState(5); cases = []
    z = np.zeros((8, 8), dtype=np.int64)
    dc = z.copy(); dc[0, 0] = 80; cases.append(("dc_80", dc))
    big = z.copy(); big[0, 0] = 100000; cases.append(("dc_saturate_high", big)); neg = z.copy(); neg[0, 0] = -100000; cases.append(("dc_saturate_low", neg))
    wrap = z.copy(); wrap[0, 0] = 5000; cases.append(("dc_range_wrap", wrap))
    for k in range(5): cases.append((f"random_{k}", rs.randint(-300, 300, (8, 8)) * (rs.random_sample((8, 8)) < 0.3)))
    cases.append(("single_ac", np.where(np.arange(64).reshape(8, 8) == 10, 200, 0)))
    for name, dcv in (("dc_127", 1016), ("dc_128", 1024), ("dc_511", 4088), ("dc_512", 4096), ("dc_neg_129", -1032), ("dc_neg_128", -1024), ("dc_neg_512", -4096), ("dc_895", 7160)):    # the corners of the post-IDCT range limit (descaled value d = dc / 8)
        b = z.copy(); b[0, 0] = dcv; cases.append((name, b))
    return cases

def picture_cases():
    out = []
    for name, w, h, kw, grey in (("grey_40x30", 40, 30, dict(quality=85), True), ("yuv444_64x48", 64, 48, dict(quality=90, subsampling=0), False), ("yuv420_200x150", 200, 150, dict(quality=80, subsampling=2, progressive=True), False)):
        img = TJ.picture(w, h, seed=11); img = img.convert("L") if grey else img; j = jpegcoef.read_jpeg(TJ.encode(img, **kw))
        comps = [{"id": c.id, "h": c.h, "v": c.v, "quant_zigzag": [int(x) for x in j.quant[c.tq]], "blocks_w": c.blocks_w, "blocks_h": c.blocks_h, "coefficients_i16le_b64": b64(c.coef, "<i2")} for c in j.components]
        out.append({"name": name, "width": w, "height": h, "components": comps, "rgb_sha256_libjpeg_profile": hashlib.sha256(R.decode_pixels(j).tobytes()).hexdigest(), "rgb_sha256_tgy_profile": hashlib.sha256(R.decode_pixels_tgy(j).tobytes()).hexdigest()})
    return out

def build():
    doc = {"format": "TGY reconstruction", "version": 0, "idct": [], "colour": [], "upsample": [], "pictures": picture_cases()}
    for name, c in idct_cases(): doc["idct"].append({"name": name, "dequantised_natural": c.reshape(-1).tolist(), "samples": R.idct_islow(c.reshape(1, 8, 8).astype(np.int64)).reshape(-1).tolist()})
    for y, cb, cr in ((128, 128, 128), (255, 255, 255), (0, 0, 0), (76, 85, 255), (150, 44, 21), (29, 255, 107), (226, 1, 149), (255, 0, 0), (0, 255, 255), (100, 90, 160), (200, 200, 60), (50, 128, 128), (255, 128, 255), (0, 128, 0), (128, 0, 255), (128, 255, 0)):
        doc["colour"].append({"y": y, "cb": cb, "cr": cr, "rgb": R.ycc_to_rgb(np.array([y]), np.array([cb]), np.array([cr]))[0].tolist()})
    for i in range(256):                                                                                   # full sweeps of every colour table entry: Cr alone, Cb alone, both together
        for y, cb, cr in ((100, 128, i), (100, i, 128), (60 + i // 2, i, 255 - i)): doc["colour"].append({"y": y, "cb": cb, "cr": cr, "rgb": R.ycc_to_rgb(np.array([y]), np.array([cb]), np.array([cr]))[0].tolist()})
    rs = np.random.RandomState(9)
    for name, h, w in (("plane_5x7", 5, 7), ("plane_2x2", 2, 2), ("plane_3x3", 3, 3), ("plane_70x66", 66, 70)):
        p = rs.randint(0, 256, (h, w)).astype(np.uint8)
        doc["upsample"].append({"name": name, "height": h, "width": w, "plane_b64": b64(p, "u1"), "libjpeg_b64": b64(R.upsample_h2v2_fancy(p), "u1"), "tile_b64": b64(R.upsample_h2v2_tile(p), "u1")})
    return doc

def main():
    OUT.write_text(json.dumps(build(), indent=1) + "\n"); print("wrote", OUT, OUT.stat().st_size, "bytes")

if __name__ == "__main__":
    main()
