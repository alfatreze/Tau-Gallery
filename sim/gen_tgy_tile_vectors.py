#!/usr/bin/env python3
"""Vectors for sim/tb_tau_tgy_tile.v (the tile controller): whole tiles given as QUANTISED coefficient blocks in zigzag order plus the two quantisation tables, with the expected RGB computed by the Python reference
(tgy_recon.idct_islow, upsample_h2v2_fancy on the tile's chroma, ycc_to_rgb). Dequantised values are saturated to +-131071 (the IDCT unit's input range; the controller's defined behaviour for out-of-range products).
Whole pictures of the reconstruction conformance file that fit one tile are also checked against that file's SHA-256 here, which ties this assembly to libjpeg. Usage: gen_tgy_tile_vectors.py OUT.hex
Slot per test, 64-bit words (fixed size SLOT): word 0 = fmt << 16 | w << 8 | h, word 1 = number of blocks, words 2..33 the two tables (Y then Cb/Cr, 64 u16 each, 4 per word), then per block (up to 768, 4:4:4): a header word
{comp << 16 | bx << 8 | by << 4 | left << 1 | above} followed by 16 words of 64 i16 zigzag coefficients, then expected R, G, B planes (2048 words each, 128 rows x 16 words, byte k = column 8 * word + k)."""
import base64, hashlib, json, sys
from pathlib import Path
import numpy as np
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools/gallery")); sys.path.insert(0, str(ROOT / "sim"))
import tgy_recon as R, jpegcoef
OFF_EXP = 2 + 32 + 768 * 17; SLOT = OFF_EXP + 3 * 2048

def grid(fmt, w, h):
    if fmt == 2: yc, yr, cc, cr = 2 * ((w + 15) // 16), 2 * ((h + 15) // 16), (w + 15) // 16, (h + 15) // 16
    else: yc = yr = cc = cr = None; yc, yr = (w + 7) // 8, (h + 7) // 8; cc, cr = yc, yr
    return [(yr, yc)] + ([(cr, cc), (cr, cc)] if fmt != 0 else [])

def plane(blocks, q, rows, cols):
    """blocks: (rows, cols, 64) zigzag coefficients -> sample plane (uint8) of rows*8 x cols*8."""
    qn = np.zeros(64, dtype=np.int64); qn[jpegcoef.ZZ] = q
    nat = np.zeros((rows * cols, 64), dtype=np.int64); nat[:, jpegcoef.ZZ] = blocks.reshape(-1, 64).astype(np.int64); nat = np.clip(nat * qn, -131071, 131071)
    px = R.idct_islow(nat.reshape(-1, 8, 8)).reshape(rows, cols, 8, 8)
    return px.transpose(0, 2, 1, 3).reshape(rows * 8, cols * 8).astype(np.uint8)

def expected(fmt, w, h, comps, qy, qc):
    planes = [plane(comps[i], qy if i == 0 else qc, *comps[i].shape[:2]) for i in range(len(comps))]
    y = planes[0][:h, :w]
    if fmt == 0: return np.stack([y] * 3, axis=-1)
    if fmt == 1: return R.ycc_to_rgb(y, planes[1][:h, :w], planes[2][:h, :w])
    cw, ch = (w + 1) // 2, (h + 1) // 2
    u = [R.upsample_h2v2_fancy(planes[i][:ch, :cw])[:h, :w] for i in (1, 2)]
    return R.ycc_to_rgb(y, u[0], u[1])

def rand_blocks(rs, rows, cols, kind, cc):
    b = np.zeros((rows, cols, 64), dtype=np.int64)
    if kind == "sparse":
        b[:, :, 0] = rs.randint(-100, 100, (rows, cols))
        for k in range(1, 64):
            m = rs.random_sample((rows, cols)) < (0.5 / (1 + k / 4)); b[:, :, k] = np.where(m, rs.randint(-6, 7, (rows, cols)), 0)
    elif kind == "dense": b = rs.randint(-30, 31, (rows, cols, 64)); b[:, :, 0] = rs.randint(-300, 300, (rows, cols))
    elif kind == "extreme": b = rs.choice([2047, -2047, 1023, -1023, 0, 0, 1, -1], (rows, cols, 64)).astype(np.int64)
    elif kind == "zero": pass
    return b

def tests():
    rs = np.random.RandomState(5); out = []
    stdq = lambda: np.array([max(1, int(v)) for v in rs.randint(1, 40, 64)])
    sizes = [(128, 128), (128, 128), (100, 77), (64, 64), (17, 9), (1, 1), (8, 8), (40, 30), (129 - 1, 16), (33, 130 - 2)]
    for fmt in (2, 0, 1):
        for i, (w, h) in enumerate(sizes):
            kind = ["sparse", "dense", "sparse", "dense", "sparse", "zero", "sparse", "sparse", "dense", "sparse"][i]
            g = grid(fmt, w, h); comps = [rand_blocks(rs, r, c, kind, ci) for ci, (r, c) in enumerate(g)]
            out.append((fmt, w, h, comps, stdq(), stdq()))
    for fmt in (2, 1, 0):                                                                                                                                   # saturation: huge coefficients times huge tables
        g = grid(fmt, 40, 24); comps = [rand_blocks(rs, r, c, "extreme", ci) for ci, (r, c) in enumerate(g)]
        out.append((fmt, 40, 24, comps, np.array(rs.choice([1, 100, 20000, 65535], 64)), np.array(rs.choice([1, 50, 30000, 65535], 64))))
    pics = {p["name"]: p for p in json.loads((ROOT / "docs/conformance/tgy_recon_v0_vectors.json").read_text())["pictures"]}
    for name, fmt in (("grey_40x30", 0), ("yuv444_64x48", 1), ("yuv420_200x150", 2)):
        p = pics[name]; comps, qs = [], []
        for c in p["components"]:
            a = np.frombuffer(base64.b64decode(c["coefficients_i16le_b64"]), dtype="<i2").reshape(c["blocks_h"], c["blocks_w"], 64).astype(np.int64); comps.append(a); qs.append(np.array(c["quant_zigzag"]))
        w, h = p["width"], p["height"]
        if fmt != 2:
            rgb = expected(fmt, w, h, comps, qs[0], qs[-1]); assert hashlib.sha256(rgb.tobytes()).hexdigest() == p["rgb_sha256_tgy_profile"], name
            out.append((fmt, w, h, comps, qs[0], qs[-1]))
        else: out.append((fmt, 128, 128, [comps[0][:16, :16], comps[1][:8, :8], comps[2][:8, :8]], qs[0], qs[1]))                                              # the picture's first tile
    return out

def pack_plane(p):
    return [int.from_bytes(bytes(int(v) for v in p[r, 8 * c:8 * c + 8]), "little") for r in range(128) for c in range(16)]

def main(path):
    lines = []; ts = tests()
    for (fmt, w, h, comps, qy, qc) in ts:
        words = [(fmt << 16) | (w << 8) | h, sum(c.shape[0] * c.shape[1] for c in comps)]
        for q in (qy, qc): words += [sum(int(q[4 * i + j]) << (16 * j) for j in range(4)) for i in range(16)]
        for ci, c in enumerate(comps):
            rows, cols = c.shape[:2]
            for by in range(rows):
                for bx in range(cols):
                    words.append((ci << 16) | (bx << 8) | (by << 4) | ((1 if bx else 0) << 1) | (1 if by else 0))
                    co = [int(x) & 0xFFFF for x in c[by, bx]]; words += [sum(co[4 * i + j] << (16 * j) for j in range(4)) for i in range(16)]
        words += [0] * (OFF_EXP - len(words))
        rgb = expected(fmt, w, h, comps, qy, qc)
        for k in range(3):
            p = np.zeros((128, 128), np.uint8); p[:h, :w] = rgb[:, :, k]; words += pack_plane(p)
        assert len(words) == SLOT
        lines += ["%016x" % x for x in words]
    Path(path).write_text("\n".join(lines) + "\n"); print(len(ts), "tests,", len(lines), "words ->", path)

if __name__ == "__main__":
    main(sys.argv[1])
