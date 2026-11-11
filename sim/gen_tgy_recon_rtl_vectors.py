#!/usr/bin/env python3
"""Vectors for sim/tb_tau_tgy_recon.v (the RTL colour + upsample stage): whole tiles (planes in, RGB out) computed by the Python reference tools/gallery/tgy_recon.py
(upsample_h2v2_fancy applied to the tile's chroma on its own = the TGY profile; ycc_to_rgb = libjpeg jdcolor). Usage: gen_tgy_recon_rtl_vectors.py OUT.hex
Slot per test, 64-bit words (16 hex digits): word 0 = fmt << 16 | w << 8 | h ... (fmt 0 grey, 1 4:4:4, 2 4:2:0; w and h 1..128); then Y, Cb, Cr (2048 words each: 128 rows x 16 words of 8 samples,
byte k = column 8 * word + k; unused samples hold random garbage so the unit must ignore them), then expected R, G, B (2048 words each, same layout, zero outside the tile)."""
import json, sys
from pathlib import Path
import numpy as np
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools/gallery")); sys.path.insert(0, str(ROOT / "sim"))
import tgy_recon as R, tgy_encode as E, jpegcoef, make_tgy_v0_vectors as G

def pad(plane, rs):
    out = rs.randint(0, 256, (128, 128)).astype(np.uint8); out[:plane.shape[0], :plane.shape[1]] = plane; return out

def real_planes():
    """Y (128 x 128) and chroma (64 x 64) planes of the real test tiles in the symbol layer vectors: dequantise, IDCT, assemble the blocks."""
    v = {x["name"]: x for x in json.loads((ROOT / "docs/conformance/tgy_v0_vectors.json").read_text())["vectors"]}
    qy, qc = E.scaled_table(E.STD_LUMA, 90), E.scaled_table(E.STD_CHROMA, 90); out = []
    for name in ("real_c6227db76b6e", "real_21bda4d32df4"):
        shapes = [tuple(s) for s in v[name]["shapes"]]; grids = G.unpack(v[name]["blocks"], shapes); planes = []
        for ci, g in enumerate(grids):
            q = np.zeros(64, dtype=np.int64); q[jpegcoef.ZZ] = qy if ci == 0 else qc
            rows = []
            for row in g:
                blks = []
                for blk in row: nat = np.zeros(64, dtype=np.int64); nat[jpegcoef.ZZ] = blk; blks.append(R.idct_islow((nat * q).reshape(1, 8, 8))[0].reshape(8, 8))
                rows.append(np.hstack(blks))
            planes.append(np.vstack(rows).astype(np.uint8))
        out.append(planes)
    return out

def tests():
    rs = np.random.RandomState(7); out = []
    def add(fmt, w, h, y, cb, cr): out.append((fmt, w, h, y, cb, cr))
    sizes = [(128, 128), (128, 1), (1, 128), (1, 1), (2, 2), (3, 3), (4, 5), (5, 4), (127, 127), (126, 100), (65, 33), (64, 64), (129 - 1, 64), (17, 17), (2, 100), (100, 2)]
    for fmt in (2, 0, 1):
        for (w, h) in sizes:
            for kind in ("noise", "smooth"):
                if kind == "noise": y = rs.randint(0, 256, (h, w)); cb = rs.randint(0, 256, (128, 128)); cr = rs.randint(0, 256, (128, 128))
                else:
                    gx, gy = np.meshgrid(np.arange(128), np.arange(128)); y = (gx + gy) % 256; cb = (128 + 100 * np.sin(gx / 9.0 + gy / 17.0)).astype(int) % 256; cr = (128 + 100 * np.cos(gx / 13.0 - gy / 7.0)).astype(int) % 256
                add(fmt, w, h, np.asarray(y)[:h, :w], np.asarray(cb), np.asarray(cr))
    for _ in range(30):                                                                                                                                          # random sizes, noise
        w, h = rs.randint(1, 129), rs.randint(1, 129)
        add(int(rs.choice([0, 1, 2, 2, 2])), w, h, rs.randint(0, 256, (h, w)), rs.randint(0, 256, (128, 128)), rs.randint(0, 256, (128, 128)))
    for _ in range(6):                                                                                                                                           # saturated chroma, extremes
        w, h = rs.randint(60, 129), rs.randint(60, 129)
        add(2, w, h, rs.choice([0, 255, 128], (h, w)), rs.choice([0, 255, 1, 254], (128, 128)), rs.choice([0, 255, 1, 254], (128, 128)))
    k = np.arange(65536).reshape(4, 128, 128)                                                                                                                    # every (Cb, Cr) pair, with Y mid grey and with random Y
    for q in range(4):
        add(1, 128, 128, np.full((128, 128), 128), k[q] % 256, k[q] // 256)
    for q in range(4):
        add(1, 128, 128, rs.randint(0, 256, (128, 128)), k[q] % 256, k[q] // 256)
    for planes in real_planes():                                                                                                                                 # real tiles
        y, cb, cr = planes
        add(2, 128, 128, y, cb, cr); add(2, 100, 77, y[:77, :100], cb, cr)
    return out

def pack_plane(p):
    words = []
    for r in range(128):
        for c in range(16): words.append(int.from_bytes(bytes(int(v) for v in p[r, 8 * c:8 * c + 8]), "little"))
    return words

def main(path):
    rs = np.random.RandomState(11); lines = []; ts = tests()
    for (fmt, w, h, y, cb, cr) in ts:
        y = np.asarray(y).astype(np.uint8); cb = np.asarray(cb).astype(np.uint8); cr = np.asarray(cr).astype(np.uint8)
        ycw, ch = (w + 1) // 2, (h + 1) // 2
        if fmt == 0: rgb = np.stack([y[:h, :w]] * 3, axis=-1)
        elif fmt == 1: rgb = R.ycc_to_rgb(y[:h, :w], cb[:h, :w], cr[:h, :w])
        else:
            u = [R.upsample_h2v2_fancy(c[:ch, :ycw])[:h, :w] for c in (cb, cr)]; rgb = R.ycc_to_rgb(y[:h, :w], u[0], u[1])
        planes_in = [pad(y[:h, :w], rs)]
        if fmt == 2: planes_in += [pad(cb[:ch, :ycw], rs), pad(cr[:ch, :ycw], rs)]
        elif fmt == 1: planes_in += [pad(cb[:h, :w], rs), pad(cr[:h, :w], rs)]
        else: planes_in += [pad(np.zeros((0, 0), np.uint8), rs)] * 2
        planes_out = [pad(np.zeros((0, 0), np.uint8), rs) * 0 for _ in range(3)]
        for k in range(3): planes_out[k][:h, :w] = rgb[:, :, k]
        for k in range(3): planes_out[k][h:, :] = 0; planes_out[k][:, w:] = 0
        lines.append("%016x" % ((fmt << 16) | (w << 8) | h))
        for p in planes_in + planes_out: lines += ["%016x" % x for x in pack_plane(p)]
    Path(path).write_text("\n".join(lines) + "\n"); print(len(ts), "tests,", len(lines), "words ->", path)

if __name__ == "__main__":
    main(sys.argv[1])
