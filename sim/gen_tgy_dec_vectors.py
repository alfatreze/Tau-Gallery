#!/usr/bin/env python3
"""Vectors for sim/tb_tau_tgy_dec.v (the whole TGY tile decoder, stream in, RGB out): the tiles of the frozen symbol layer vectors (docs/conformance/tgy_v0_vectors.json: real photo tiles, synthetic tiles, grey and 4:4:4 shapes)
with the JPEG standard tables at quality 90, and the expected RGB from the Python reference (gen_tgy_tile_vectors.expected: libjpeg IDCT, tile-local fancy upsampling, libjpeg colour). Usage: gen_tgy_dec_vectors.py OUT.hex
Slot per test, 64-bit words: word 0 = fmt << 16 | w << 8 | h, word 1 = stream length in bytes, words 2..33 the tables (Y then Cb/Cr, 4 u16 per word), STREAM_WORDS words of stream (8 bytes per word, zero padded), then expected R, G, B planes (2048 words each)."""
import json, sys
from pathlib import Path
import numpy as np
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools/gallery")); sys.path.insert(0, str(ROOT / "sim"))
import tgy_v0 as V, tgy_encode as E, make_tgy_v0_vectors as G, gen_tgy_tile_vectors as T
STREAM_WORDS = 3300; SLOT = 34 + STREAM_WORDS + 3 * 2048

def cases():
    out = []; qy, qc = E.scaled_table(E.STD_LUMA, 90), E.scaled_table(E.STD_CHROMA, 90)
    items = [(x["name"], G.unpack(x["blocks"], [tuple(s) for s in x["shapes"]]), [tuple(s) for s in x["shapes"]], bytes.fromhex(x["stream"])) for x in json.loads((ROOT / "docs/conformance/tgy_v0_vectors.json").read_text())["vectors"]]
    import test_tgymodel as TM
    for seed, rows, cols in ((12, (3, 3, 3), (3, 3, 3)), (16, (6, 6, 6), (6, 6, 6)), (18, (4, 2, 2), (6, 3, 3)), (19, (2, 1, 1), (4, 2, 2))):
        grids, shapes = TM.synth_tile(seed, rows=rows, cols=cols); items.append((f"extra_seed{seed}", grids, shapes, V.encode_tile(grids, shapes)))
    for name, grids, shapes, stream in items:
        n = len(shapes)
        if n == 2: continue
        if n == 3 and shapes[1] != shapes[0]:                                                                                                              # 4:2:0 needs Y = 2 x chroma and even Y dimensions
            if shapes[0][0] != 2 * shapes[1][0] or shapes[0][1] != 2 * shapes[1][1]: continue
            fmt = 2; w, h = shapes[0][1] * 8, shapes[0][0] * 8
        elif n == 3: fmt = 1; w, h = shapes[0][1] * 8, shapes[0][0] * 8
        else: fmt = 0; w, h = shapes[0][1] * 8, shapes[0][0] * 8
        comps = [np.array(g, dtype=np.int64).reshape(shapes[i][0], shapes[i][1], 64) for i, g in enumerate(grids)]
        out.append((name, fmt, w, h, comps, qy, qc, stream))
    return out

def main(path):
    lines = []; cs = cases()
    for (name, fmt, w, h, comps, qy, qc, stream) in cs:
        assert len(stream) <= STREAM_WORDS * 8, (name, len(stream))
        words = [(fmt << 16) | (w << 8) | h, len(stream)]
        for q in (qy, qc): words += [sum(int(q[4 * i + j]) << (16 * j) for j in range(4)) for i in range(16)]
        pad = stream + bytes(STREAM_WORDS * 8 - len(stream)); words += [int.from_bytes(pad[8 * i:8 * i + 8], "little") for i in range(STREAM_WORDS)]
        rgb = T.expected(fmt, w, h, comps, qy, qc)
        for k in range(3):
            p = np.zeros((128, 128), np.uint8); p[:h, :w] = rgb[:, :, k]; words += T.pack_plane(p)
        assert len(words) == SLOT; lines += ["%016x" % x for x in words]
        print(name, "fmt", fmt, w, "x", h, len(stream), "bytes")
    Path(path).write_text("\n".join(lines) + "\n"); print(len(cs), "tests ->", path)

if __name__ == "__main__":
    main(sys.argv[1])
