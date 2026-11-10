"""TGY symbol layer, measurement version (docs/M3_STREAMING_NOTE.md section 11, decision D2): how the quantised DCT coefficients of one 128 x 128 tile are binarised and which contexts code them, over the TVC range coder.
NOT the frozen format: this is the model the measurement (tools/gallery/model_measure.py) compares variants of. One tile = one stream, every context starts fresh (or from trained priors): tiles are independent.

The binarisation is written ONCE, as functions that take the value to encode or None to decode and return the value: the same code runs as a cost counter (ideal code length), as the real TVC encoder and as the decoder,
so an asymmetry cannot hide. Per block (zigzag order): the DC difference to the left (else above) block of the tile (zero flag, sign, Exp-Golomb with a context per unary step), the index of the last nonzero AC
(a 6-bit binary tree, context per node), then for each position before it a significance flag, and for each nonzero a greater-than-1 flag, an Exp-Golomb remainder and a bypass sign. Options: neighbour contexts (the same
position in the left and above blocks of the tile), the engine (fixed shift-5 as TVC, or a count-based rate), trained priors."""
import math
import tvcrc

KG = [0] * 64                                                                                      # zigzag position 1..63 to one of 13 groups
for _k in range(1, 64): KG[_k] = sum(1 for e in (2, 3, 4, 5, 7, 9, 12, 16, 21, 28, 37, 49) if _k >= e)
REC = [min(65536 // 2, int(65536 / (n + 1.5))) for n in range(64)]                                  # count-based rate: 1/(n+1.5), at most 1/2, falling to the floor below
LIMIT = 30                                                                                         # the rate stops falling at 1/(LIMIT + 1.5), about 1/32

class Engine:
    """Adaptive probabilities of a 0 (15 bit, as TVC): mode 'fixed' = the TVC update (shift 5); 'count' = rate 1/(n+1.5) falling to about 1/32. priors: ctx -> (p, n) start values (the count seeds the rate)."""
    def __init__(self, mode="fixed", priors=None):
        self.mode, self.priors, self.p, self.n = mode, priors or {}, {}, {}
    def prob(self, ctx):
        p = self.p.get(ctx)
        if p is None:
            pr = self.priors.get(ctx); p = pr[0] if pr else 16384; self.p[ctx] = p; self.n[ctx] = pr[1] if pr else 0
        return p
    def update(self, ctx, bit):
        p = self.p[ctx]
        if self.mode == "fixed":
            p = p - (p >> 5) if bit else p + ((32768 - p) >> 5)
        else:
            n = self.n[ctx]; r = REC[min(n, LIMIT)]
            p = p - ((p * r) >> 16) if bit else p + (((32768 - p) * r) >> 16)
            p = min(32768 - 32, max(32, p)); self.n[ctx] = n + 1
        self.p[ctx] = p

class CostIO:
    """Ideal code length: bits = -log2(p), plus per-context decision counts (the static entropy limit and the training data of priors)."""
    def __init__(self, engine): self.e, self.bits, self.counts, self.nbypass = engine, 0.0, {}, 0
    def bit(self, ctx, b):
        p = self.e.prob(ctx); self.bits -= math.log2((p if b == 0 else 32768 - p) / 32768.0); self.e.update(ctx, b)
        c = self.counts.setdefault(ctx, [0, 0]); c[b] += 1; return b
    def bypass(self, b): self.bits += 1.0; self.nbypass += 1; return b

class EncIO:
    def __init__(self, engine): self.e, self.enc = engine, tvcrc.Encoder()
    def bit(self, ctx, b): p = self.e.prob(ctx); self.enc.bit_p(p, b); self.e.update(ctx, b); return b
    def bypass(self, b): self.enc.bypass(b); return b
    def finish(self): return self.enc.finish()

class DecIO:
    def __init__(self, engine, data): self.e, self.dec = engine, tvcrc.Decoder(data)
    def bit(self, ctx, b=None): p = self.e.prob(ctx); b = self.dec.bit_p(p); self.e.update(ctx, b); return b
    def bypass(self, b=None): return self.dec.bypass()

def _eg0(io, ctxp, topctx, m):
    """Exp-Golomb order 0 of m >= 0: unary exponent with a context per step, the top mantissa bit in a context, the rest bypass. m is None when decoding."""
    enc = m is not None
    e = (m + 1).bit_length() - 1 if enc else 0; i = 0
    while True:
        b = io.bit(ctxp + (min(i, 9),), (1 if i < e else 0) if enc else None)
        if not b: break
        i += 1
    e = i; v = 1
    for j in range(e - 1, -1, -1):
        bit = ((m + 1) >> j) & 1 if enc else None
        v = (v << 1) | (io.bit(topctx + (min(e, 9),), bit) if j == e - 1 else io.bypass(bit))
    return v - 1

def _bucket(x): return 0 if x == 0 else (1 if x <= 2 else (2 if x <= 5 else 3))
def _lastb(x): return 0 if x == 0 else (1 if x < 8 else (2 if x < 20 else 3))

def code_block(io, cc, blk, pred, left, above, nbr):
    """One block (64 zigzag coefficients). blk is None when decoding. left, above: the neighbours' coefficient lists or None; nbr: use neighbour contexts. Returns the block."""
    enc = blk is not None; out = [0] * 64
    d = (blk[0] - pred) if enc else None
    nz = io.bit(("d0", cc), (1 if d != 0 else 0) if enc else None)
    if nz:
        sg = io.bit(("ds", cc), (1 if d < 0 else 0) if enc else None)
        a = _eg0(io, ("dx", cc), ("dm", cc), abs(d) - 1 if enc else None) + 1; d = -a if sg else a
    else: d = 0
    out[0] = pred + d
    ll = _last_of(left) if nbr else 0; la = _last_of(above) if nbr else 0; nbl = (_lastb(ll) + _lastb(la) + 1) // 2
    last = 0
    if enc:
        for k in range(63, 0, -1):
            if blk[k]: last = k; break
    node = 1
    for i in range(5, -1, -1):
        node = node * 2 + io.bit(("lt", cc, nbl, node), ((last >> i) & 1) if enc else None)
    last = node - 64
    for k in range(1, last + 1):
        lk = left[k] if (nbr and left is not None) else 0; ak = above[k] if (nbr and above is not None) else 0
        if k < last:
            nb = min(2, (lk != 0) + (ak != 0))
            if not io.bit(("sg", cc, KG[k], nb), (1 if blk[k] else 0) if enc else None): continue
        a = abs(blk[k]) if enc else None
        nbm = _bucket(abs(lk) + abs(ak))
        if io.bit(("g1", cc, KG[k], nbm), (1 if a > 1 else 0) if enc else None): a = _eg0(io, ("mx", cc, min(KG[k], 7)), ("mm", cc), a - 2 if enc else None) + 2
        else: a = 1
        neg = io.bypass((1 if blk[k] < 0 else 0) if enc else None)
        out[k] = -a if neg else a
    return out

def _last_of(blk):
    if blk is None: return 0
    for k in range(63, 0, -1):
        if blk[k]: return k
    return 0

def code_tile(io, grids, shapes, nbr):
    """One tile: grids = list of [rows][cols] lists of 64-coefficient lists per component (None when decoding), shapes = [(rows, cols)] per component. Returns the grids."""
    out = []
    for ci, (rows, cols) in enumerate(shapes):
        cc = 0 if ci == 0 else 1; g = [[None] * cols for _ in range(rows)]
        for y in range(rows):
            for x in range(cols):
                left = g[y][x - 1] if x else None; above = g[y - 1][x] if y else None
                pred = (left[0] if left is not None else (above[0] if above is not None else 0))
                g[y][x] = code_block(io, cc, grids[ci][y][x] if grids is not None else None, pred, left, above, nbr)
        out.append(g)
    return out

def tiles_of(j, tile_px=128):
    """Split a tvcrc-style Jpeg (tools/gallery/jpegcoef.py) into tiles: yields (grids, shapes) with the blocks as lists. Whole tiles only (the caller crops to a multiple of the tile)."""
    hmax = max(c.h for c in j.components); vmax = max(c.v for c in j.components)
    per = [(tile_px // 8 * c.v // vmax, tile_px // 8 * c.h // hmax) for c in j.components]                 # blocks per tile side, rows then cols
    tys = j.components[0].blocks_h // per[0][0]; txs = j.components[0].blocks_w // per[0][1]
    for ty in range(tys):
        for tx in range(txs):
            grids = [[[c.coef[ty * pr + y, tx * pc + x].tolist() for x in range(pc)] for y in range(pr)] for c, (pr, pc) in zip(j.components, per)]
            yield grids, per

def train_priors(counts, strength=6, floor=0.002):
    """ctx -> (p15, n) from accumulated per-context [n0, n1] counts: a smoothed frequency and a starting confidence of `strength` observations."""
    out = {}
    for ctx, (n0, n1) in counts.items():
        p = (n0 + 0.4) / (n0 + n1 + 0.8); p = min(1 - floor, max(floor, p)); out[ctx] = (int(p * 32768), strength)
    return out
