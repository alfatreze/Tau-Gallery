#!/usr/bin/env python3
"""Writes docs/conformance/tgy_v0_vectors.json: the conformance vectors of the FROZEN TGY symbol layer v0 (docs/TGY_SYMBOL_LAYER_V0.md). Each vector: the tile's shapes, its coefficient blocks (int16 little-endian, base64, components in
order Y, Cb, Cr, each row by row, each block 64 zigzag coefficients) and the stream tgy_v0.encode_tile produces (hex). An implementation (C, RTL) must decode every stream to exactly the blocks, and encode every set of blocks to exactly
the stream. Also the constants (the rate table, the position groups, the neighbour table). Re-running this script must reproduce the file byte for byte for the synthetic vectors; the real-photo tiles are read from
../test-images/unsplash when present, and kept as they are in the file otherwise."""
import base64, hashlib, json, struct, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/gallery")); sys.path.insert(0, str(ROOT / "sim"))
import tgy_v0 as V
OUT = ROOT / "docs/conformance/tgy_v0_vectors.json"

def pack(grids): return base64.b64encode(b"".join(struct.pack("<64h", *b) for g in grids for row in g for b in row)).decode()
def unpack(b64, shapes):
    raw = base64.b64decode(b64); vals = struct.unpack("<%dh" % (len(raw) // 2), raw); pos = 0; grids = []
    for rows, cols in shapes:
        g = []
        for _ in range(rows):
            row = []
            for _ in range(cols): row.append(list(vals[pos:pos + 64])); pos += 64
            g.append(row)
        grids.append(g)
    return grids

def synthetic():
    import test_tgymodel as T
    out = []
    z = [0] * 64; full = [5] + [1] * 63; big = [2047] + [1023 if k % 2 else -1023 for k in range(1, 64)]; one = [0] * 63 + [-1]
    out.append(("one_block_zero", [[[z]]], [(1, 1)]))
    out.append(("extremes_3x3", [[[z, full, big], [one, z, full], [big, one, z]]], [(3, 3)]))
    out.append(("all_zero_8x8", [[[z] * 8 for _ in range(8)]], [(8, 8)]))
    for seed, rows, cols in ((1, (4, 2, 2), (4, 2, 2)), (2, (3, 3), (5, 5)), (3, (6, 3, 3), (6, 3, 3)), (4, (2, 1, 1), (7, 3, 3))):
        grids, shapes = T.synth_tile(seed, rows=rows, cols=cols); out.append((f"synthetic_seed{seed}", grids, shapes))
    return out

def real_tiles():
    import model_measure as MM, jpegcoef, tgymodel as M
    d = Path(tempfile.mkdtemp()); out = []
    for name, idx in (("c6227db76b6e", 0), ("21bda4d32df4", 0)):
        p = next((ROOT.parent / "test-images/unsplash").glob(f"*{name}.jpg")); refs, _ = MM.crop_jpeg(p, 1024, d)
        j = jpegcoef.read_jpeg(refs["progressive"].read_bytes()); tiles = list(M.tiles_of(j)); grids, shapes = tiles[27 if idx == 0 else 36]
        out.append((f"real_{name}", grids, shapes))
    return out

def main():
    old = json.loads(OUT.read_text()) if OUT.exists() else {"vectors": []}
    keep = {v["name"]: v for v in old["vectors"] if v["name"].startswith("real_")}
    vectors = []
    for name, grids, shapes in synthetic() + (real_tiles() if (ROOT.parent / "test-images/unsplash").is_dir() else []):
        vectors.append({"name": name, "shapes": shapes, "blocks": pack(grids), "stream": V.encode_tile(grids, shapes).hex()})
    for name, v in keep.items():
        if not any(x["name"] == name for x in vectors): vectors.append(v)
    doc = {"format": "TGY symbol layer", "version": V.TGY_SYMBOL_VERSION, "contexts": V.N_CTX, "limit": V.LIMIT, "rate_table": V.REC, "position_groups": V.KG, "neighbour_table": V.LN, "vectors": vectors}
    OUT.write_text(json.dumps(doc, indent=1) + "\n"); print("wrote", OUT, len(vectors), "vectors")

if __name__ == "__main__":
    main()
