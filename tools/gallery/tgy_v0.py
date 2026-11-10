"""TGY symbol layer, version 0: FROZEN (docs/TGY_SYMBOL_LAYER_V0.md is the normative text; this module is its reference implementation, and docs/conformance/tgy_v0_vectors.json its test vectors).
It codes the quantised DCT coefficients of ONE tile (128 x 128 luma samples; the blocks of the components in order Y, Cb, Cr, each in raster order) as ONE TVC range-coder stream with all contexts freshly initialised:
independent tiles. The model is the `+ local` variant measured in docs/M3_STREAMING_NOTE.md section 14 (0.933 of progressive Huffman): a count-based adaptation rate, the DC difference to the left (else the upper) block,
a 6-bit tree for the index of the last nonzero AC coefficient, significance and greater-than-1 flags and Exp-Golomb remainders with the magnitudes of the two already coded neighbouring coefficients of the block as
context, and bypass signs. 1,250 contexts, each a 15-bit probability and a 5-bit count.
A change of anything that affects the stream bits is a new version (TGY_SYMBOL_VERSION), never an edit of this file. One function codes both directions (a value to encode, None to decode)."""
import tvcrc

TGY_SYMBOL_VERSION = 0
N_CTX = 1250
LIMIT = 30                                                                                        # the count saturates here: the rate stops falling at 1/(30 + 1.5)
REC = [min(32768, int(65536 / (n + 1.5))) for n in range(LIMIT + 1)]                              # 16-bit rates 1/(n+1.5), at most 1/2
KG = [0] * 64                                                                                     # zigzag position 1..63 -> position group 0..12
for _k in range(1, 64): KG[_k] = sum(1 for e in (2, 3, 4, 5, 7, 9, 12, 16, 21, 28, 37, 49) if _k >= e)
ZZN = [0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5, 12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63]
_ZZI = [0] * 64
for _i, _n in enumerate(ZZN): _ZZI[_n] = _i
LN = [(_ZZI[ZZN[k] - 1] if ZZN[k] % 8 else -1, _ZZI[ZZN[k] - 8] if ZZN[k] >= 8 else -1) for k in range(64)]   # zigzag index of the left and of the upper coefficient (natural order) of the same block, or -1

# context layout (flat index): the offsets below are part of the format only as a numbering for implementations and vectors
O_D0, O_DS, O_DX, O_DM, O_LT, O_SG, O_G1, O_MX, O_MM = 0, 2, 4, 24, 44, 170, 300, 430, 1230

class Engine:
    def __init__(self): self.p = [16384] * N_CTX; self.n = [0] * N_CTX
    def update(self, c, bit):
        p = self.p[c]; r = REC[self.n[c]]
        p = p - ((p * r) >> 16) if bit else p + (((32768 - p) * r) >> 16)
        self.p[c] = min(32768 - 32, max(32, p))
        if self.n[c] < LIMIT: self.n[c] += 1

class EncIO:
    def __init__(self): self.e, self.enc = Engine(), tvcrc.Encoder()
    def bit(self, c, b): self.enc.bit_p(self.e.p[c], b); self.e.update(c, b); return b
    def bypass(self, b): self.enc.bypass(b); return b
    def finish(self): return self.enc.finish()

class DecIO:
    def __init__(self, data): self.e, self.dec = Engine(), tvcrc.Decoder(data)
    def bit(self, c, b=None): b = self.dec.bit_p(self.e.p[c]); self.e.update(c, b); return b
    def bypass(self, b=None): return self.dec.bypass()

def _lb(x): return 0 if x == 0 else (1 if x == 1 else (2 if x == 2 else (3 if x <= 4 else 4)))

def code_block(io, cc, blk, left, above):
    """One block of 64 zigzag-ordered coefficients (None when decoding); left and above are the already coded neighbouring blocks of the same component inside the tile (or None). Returns the block."""
    enc = blk is not None; out = [0] * 64
    pred = left[0] if left is not None else (above[0] if above is not None else 0)
    d = (blk[0] - pred) if enc else None
    if io.bit(O_D0 + cc, (1 if d != 0 else 0) if enc else None):
        neg = io.bit(O_DS + cc, (1 if d < 0 else 0) if enc else None)
        a = _eg_dc(io, cc, abs(d) - 1 if enc else None) + 1; d = -a if neg else a
    else: d = 0
    out[0] = pred + d
    last = 0
    if enc:
        for k in range(63, 0, -1):
            if blk[k]: last = k; break
    node = 1
    for i in range(5, -1, -1): node = node * 2 + io.bit(O_LT + cc * 63 + node - 1, ((last >> i) & 1) if enc else None)
    last = node - 64
    for k in range(1, last + 1):
        li, ui = LN[k]
        if li >= 0 and ui >= 0: lc = _lb(abs(out[li]) + abs(out[ui]))
        elif li >= 0: lc = _lb(abs(out[li]))
        elif ui >= 0: lc = _lb(abs(out[ui]))
        else: lc = 0
        if k < last and not io.bit(O_SG + (cc * 13 + KG[k]) * 5 + lc, (1 if blk[k] else 0) if enc else None): continue
        a = abs(blk[k]) if enc else None
        if io.bit(O_G1 + (cc * 13 + KG[k]) * 5 + lc, (1 if a > 1 else 0) if enc else None):
            kg = min(KG[k], 7)
            a = _eg_mag(io, cc, kg, lc, a - 2 if enc else None) + 2
        else: a = 1
        neg = io.bypass((1 if blk[k] < 0 else 0) if enc else None); out[k] = -a if neg else a
    return out

def _eg_mag(io, cc, kg, lc, m):
    """Exp-Golomb order 0 of m >= 0 (None when decoding): the exponent in unary with one context per step (step index capped at 9), then the mantissa bits MSB first, the first in a context (exponent capped at 9), the rest bypass."""
    enc = m is not None; e = (m + 1).bit_length() - 1 if enc else 0; i = 0
    while True:
        if not io.bit(O_MX + (((cc * 8 + kg) * 5 + lc) * 10) + min(i, 9), (1 if i < e else 0) if enc else None): break
        i += 1
    e = i; v = 1
    for j in range(e - 1, -1, -1):
        bit = ((m + 1) >> j) & 1 if enc else None
        v = (v << 1) | (io.bit(O_MM + cc * 10 + min(e, 9), bit) if j == e - 1 else io.bypass(bit))
    return v - 1

def _eg_dc(io, cc, m):
    """The same binarisation for the DC difference magnitude minus one, with its own contexts."""
    enc = m is not None; e = (m + 1).bit_length() - 1 if enc else 0; i = 0
    while True:
        if not io.bit(O_DX + cc * 10 + min(i, 9), (1 if i < e else 0) if enc else None): break
        i += 1
    e = i; v = 1
    for j in range(e - 1, -1, -1):
        bit = ((m + 1) >> j) & 1 if enc else None
        v = (v << 1) | (io.bit(O_DM + cc * 10 + min(e, 9), bit) if j == e - 1 else io.bypass(bit))
    return v - 1

def code_tile(io, grids, shapes):
    """grids: per component a [rows][cols] list of blocks (None when decoding); shapes: [(rows, cols)] per component. Components in order Y, Cb, Cr; class 0 for Y, 1 for Cb and Cr. Returns the grids."""
    out = []
    for ci, (rows, cols) in enumerate(shapes):
        cc = 0 if ci == 0 else 1; g = [[None] * cols for _ in range(rows)]
        for y in range(rows):
            for x in range(cols): g[y][x] = code_block(io, cc, grids[ci][y][x] if grids is not None else None, g[y][x - 1] if x else None, g[y - 1][x] if y else None)
        out.append(g)
    return out

def encode_tile(grids, shapes): io = EncIO(); code_tile(io, grids, shapes); return io.finish()
def decode_tile(data, shapes): return code_tile(DecIO(data), None, shapes)
