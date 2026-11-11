#!/usr/bin/env python3
"""Writes the test vectors of the RTL IDCT (sim/tb_tau_tgy_idct.v): per test 64 dequantised natural-order coefficients (18-bit two's complement, one 8-digit hex word each) then the 64 expected samples (one word each),
computed by the Python reference (tools/gallery/tgy_recon.py: libjpeg islow, arbitrary precision). Usage: gen_tgy_idct_vectors.py OUT.hex"""
import json, sys
from pathlib import Path
import numpy as np
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools/gallery")); sys.path.insert(0, str(ROOT / "sim"))
import tgy_recon as R, tgy_encode as E, jpegcoef, make_tgy_v0_vectors as G

def tests():
    rs = np.random.RandomState(2026); out = []; z = np.zeros(64, dtype=np.int64)
    for c in json.loads((ROOT / "docs/conformance/tgy_recon_v0_vectors.json").read_text())["idct"]: out.append(np.array(c["dequantised_natural"], dtype=np.int64))        # the conformance cases
    for pos in range(64):
        for v in (1, -1, 200, -200, 131071, -131071): b = z.copy(); b[pos] = v; out.append(b)                                                                         # every impulse
    for _ in range(600): out.append(rs.randint(-300, 301, 64) * (rs.random_sample(64) < rs.choice([0.1, 0.3, 1.0])))                                                  # dense and sparse small
    for _ in range(300): b = z.copy(); idx = rs.choice(64, rs.randint(1, 5), replace=False); b[idx] = rs.randint(-5000, 5001, len(idx)); out.append(b)                  # a few large
    for _ in range(600): out.append(rs.choice([131071, -131071, 65535, -65535, 1, -1, 0, 0], 64).astype(np.int64))                                                        # extremes of the input range
    for _ in range(200): out.append(rs.randint(-131071, 131072, 64).astype(np.int64))                                                                                   # full range noise
    v = {x["name"]: x for x in json.loads((ROOT / "docs/conformance/tgy_v0_vectors.json").read_text())["vectors"]}                                                       # real blocks, dequantised with a real table
    qy, qc = E.scaled_table(E.STD_LUMA, 90), E.scaled_table(E.STD_CHROMA, 90)
    for name in ("real_c6227db76b6e", "real_21bda4d32df4"):
        shapes = [tuple(s) for s in v[name]["shapes"]]; grids = G.unpack(v[name]["blocks"], shapes)
        for ci, g in enumerate(grids):
            q = np.zeros(64, dtype=np.int64); q[jpegcoef.ZZ] = qy if ci == 0 else qc
            for row in g:
                for blk in row: nat = np.zeros(64, dtype=np.int64); nat[jpegcoef.ZZ] = blk; out.append(nat * q)
    return out

def main(path):
    ts = tests(); lines = []
    for t in ts:
        assert np.abs(t).max() < 2 ** 17
        samples = R.idct_islow(t.reshape(1, 8, 8).astype(np.int64)).reshape(-1)
        lines += ["%08x" % (int(x) & 0xFFFFFFFF) for x in t] + ["%08x" % int(s) for s in samples]
    Path(path).write_text("\n".join(lines) + "\n"); print(len(ts), "tests,", len(lines), "words ->", path)

if __name__ == "__main__":
    main(sys.argv[1])
