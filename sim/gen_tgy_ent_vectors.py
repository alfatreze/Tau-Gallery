#!/usr/bin/env python3
"""Vectors for sim/tb_tau_tgy_ent.v (the RTL entropy decoder): every vector of docs/conformance/tgy_v0_vectors.json (stream and the blocks it must decode to) plus extra synthetic tiles encoded by the frozen reference
(tools/gallery/tgy_v0.py: 4:4:4 shapes, many seeds, a tile whose stream is cut short so the decoder reads zeros past the end). Usage: gen_tgy_ent_vectors.py OUT.hex
Slot per test, 64-bit words (fixed size SLOT): word 0 = stream length in bytes, word 1 = number of blocks, word 2 = number of components, words 3..5 = rows << 8 | cols of each component (0 if absent), then the stream (8 bytes per
word, zero padded, STREAM_WORDS words), then per block in coding order a header word {comp << 16 | bx << 8 | by << 4 | left << 1 | above} and 16 words of 64 i16 zigzag coefficients."""
import base64, json, sys
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools/gallery")); sys.path.insert(0, str(ROOT / "sim"))
import tgy_v0 as V, make_tgy_v0_vectors as G
STREAM_WORDS = 3300; SLOT = 6 + STREAM_WORDS + 384 * 17

def vectors():
    out = []
    for x in json.loads((ROOT / "docs/conformance/tgy_v0_vectors.json").read_text())["vectors"]:
        shapes = [tuple(s) for s in x["shapes"]]; out.append((x["name"], G.unpack(x["blocks"], shapes), shapes, bytes.fromhex(x["stream"])))
    import test_tgymodel as T
    for seed, rows, cols in ((11, (2, 1, 1), (2, 1, 1)), (12, (3, 3, 3), (3, 3, 3)), (13, (5,), (5,)), (14, (4, 2, 2), (3, 2, 2)), (15, (1, 1, 1), (8, 4, 4)), (16, (6, 6, 6), (6, 6, 6)), (17, (8, 4, 4), (8, 4, 4))):
        grids, shapes = T.synth_tile(seed, rows=rows, cols=cols); out.append((f"extra_seed{seed}", grids, shapes, V.encode_tile(grids, shapes)))
    name, grids, shapes, stream = out[3]                                                                                                                   # a stream cut short: decoded by the reference with zeros past the end
    cut = stream[:len(stream) // 2]; out.append(("cut_short", V.decode_tile(cut, shapes), shapes, cut))
    return out

def main(path):
    lines = []; vs = vectors()
    for (name, grids, shapes, stream) in vs:
        assert len(stream) <= STREAM_WORDS * 8, (name, len(stream))
        words = [len(stream), sum(r * c for r, c in shapes), len(shapes)] + [(r << 8) | c for r, c in shapes] + [0] * (3 - len(shapes))
        pad = stream + bytes(STREAM_WORDS * 8 - len(stream)); words += [int.from_bytes(pad[8 * i:8 * i + 8], "little") for i in range(STREAM_WORDS)]
        for ci, g in enumerate(grids):
            for by, row in enumerate(g):
                for bx, blk in enumerate(row):
                    words.append((ci << 16) | (bx << 8) | (by << 4) | ((1 if bx else 0) << 1) | (1 if by else 0))
                    co = [int(v) & 0xFFFF for v in blk]; words += [sum(co[4 * i + j] << (16 * j) for j in range(4)) for i in range(16)]
        words += [0] * (SLOT - len(words)); lines += ["%016x" % w for w in words]
    Path(path).write_text("\n".join(lines) + "\n"); print(len(vs), "tests,", len(lines), "words ->", path)

if __name__ == "__main__":
    main(sys.argv[1])
