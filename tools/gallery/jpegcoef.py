"""A JPEG coefficient reader: the quantised DCT coefficients of a baseline, extended-sequential or progressive Huffman JPEG, exactly as stored (no IDCT, no colour conversion). The entropy model of TGY is measured on
THESE numbers (docs/M3_STREAMING_NOTE.md section 11, D2), not on pixels decoded and re-quantised, whose statistics differ. Pure Python and numpy; slow (about a minute for a 20 MP progressive photo), correct first.

    j = read_jpeg(open(path, "rb").read())
    j.width, j.height, j.progressive, j.restart_interval
    for c in j.components: c.id, c.h, c.v, c.tq, c.blocks_w, c.blocks_h, c.coef   # coef: int16 array [blocks_h, blocks_w, 64], ZIGZAG order
    j.quant[tq]                                                                      # 64 entries, zigzag order
    plane = idct_plane(c, j.quant[c.tq])                                             # float plane (level shifted by +128, not clamped) for checks

Only Huffman-coded, 8-bit, 1 or 3 component files; arithmetic-coded, 12-bit, lossless and hierarchical JPEG raise ValueError."""
import numpy as np

ZZ = np.array([0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5, 12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63])
# ZZ[k] = natural (row-major) index of the k-th zigzag coefficient

def _cdiv(a, b): return -(-a // b)

class Comp:
    def __init__(self, cid, h, v, tq): self.id, self.h, self.v, self.tq = cid, h, v, tq
class Jpeg: pass

class Huff:
    def __init__(self, counts, symbols):
        self.maxcode = [-1] * 18; self.valptr = [0] * 18; self.mincode = [0] * 18; self.symbols = symbols
        code = 0; k = 0
        for l in range(1, 17):
            self.valptr[l] = k; self.mincode[l] = code; code += counts[l - 1]; k += counts[l - 1]
            self.maxcode[l] = code - 1 if counts[l - 1] else -1
            code <<= 1
        self.lut = [None] * 512                                                                    # 9-bit lookahead: (length, symbol)
        code = 0; k = 0
        for l in range(1, 10):
            for _ in range(counts[l - 1]):
                base = code << (9 - l)
                for f in range(1 << (9 - l)): self.lut[base + f] = (l, symbols[k])
                code += 1; k += 1
            code <<= 1

class Bits:
    """MSB-first bit reader over one restart interval of already unstuffed bytes; bytes past the end read as zero."""
    def __init__(self, data): self.d, self.pos, self.buf, self.cnt = data, 0, 0, 0
    def _fill(self, n):
        while self.cnt < n:
            b = self.d[self.pos] if self.pos < len(self.d) else 0; self.pos += 1
            self.buf = (self.buf << 8) | b; self.cnt += 8
    def get(self, n):
        if n == 0: return 0
        self._fill(n); self.cnt -= n; v = (self.buf >> self.cnt) & ((1 << n) - 1); self.buf &= (1 << self.cnt) - 1; return v
    def bit(self):
        if self.cnt == 0: self._fill(1)
        self.cnt -= 1; v = (self.buf >> self.cnt) & 1; self.buf &= (1 << self.cnt) - 1; return v
    def huff(self, h):
        self._fill(16); peek = (self.buf >> (self.cnt - 9)) & 511; e = h.lut[peek]
        if e is not None:
            self.cnt -= e[0]; self.buf &= (1 << self.cnt) - 1; return e[1]
        code = 0
        for l in range(1, 17):
            code = (code << 1) | self.bit()
            if h.maxcode[l] >= 0 and code <= h.maxcode[l] and code >= h.mincode[l]: return h.symbols[h.valptr[l] + code - h.mincode[l]]
        raise ValueError("bad Huffman code")
    def extend(self, s):
        v = self.get(s)
        return v if v >= (1 << (s - 1)) else v - (1 << s) + 1

def _split_restarts(seg):
    """The entropy-coded segment as a list of intervals: split at RSTn markers, FF00 unstuffed to FF."""
    out, start, i, n = [], 0, 0, len(seg)
    while i < n - 1:
        if seg[i] == 0xFF and 0xD0 <= seg[i + 1] <= 0xD7:
            out.append(seg[start:i].replace(b"\xff\x00", b"\xff")); start = i + 2; i += 2
        else: i += 1
    out.append(seg[start:].replace(b"\xff\x00", b"\xff")); return out

def read_jpeg(data):
    if data[:2] != b"\xff\xd8": raise ValueError("not a JPEG")
    j = Jpeg(); j.quant = {}; j.components = []; j.progressive = False; j.restart_interval = 0; dc = {}; ac = {}; pos = 2; frame = False
    while pos < len(data):
        if data[pos] != 0xFF: pos += 1; continue
        while data[pos] == 0xFF: pos += 1
        m = data[pos]; pos += 1
        if m == 0xD9: break
        if m in (0x01,) or 0xD0 <= m <= 0xD8: continue
        ln = (data[pos] << 8) | data[pos + 1]; body = data[pos + 2:pos + ln]; pos += ln
        if m == 0xDB:
            i = 0
            while i < len(body):
                pq, tq = body[i] >> 4, body[i] & 15; i += 1; n = 64 * (2 if pq else 1)
                vals = [(body[i + 2 * k] << 8) | body[i + 2 * k + 1] for k in range(64)] if pq else list(body[i:i + 64]); j.quant[tq] = np.array(vals); i += n
        elif m in (0xC0, 0xC1, 0xC2):
            if body[0] != 8: raise ValueError("only 8-bit JPEG")
            j.progressive = m == 0xC2; j.height = (body[1] << 8) | body[2]; j.width = (body[3] << 8) | body[4]; nc = body[5]
            if nc not in (1, 3): raise ValueError("1 or 3 components only")
            j.components = [Comp(body[6 + 3 * k], body[7 + 3 * k] >> 4, body[7 + 3 * k] & 15, body[8 + 3 * k]) for k in range(nc)]
            hmax = max(c.h for c in j.components); vmax = max(c.v for c in j.components)
            j.mcus_x = -(-j.width // (8 * hmax)); j.mcus_y = -(-j.height // (8 * vmax))
            for c in j.components:
                c.blocks_w = j.mcus_x * c.h; c.blocks_h = j.mcus_y * c.v                                  # padded to whole MCUs
                c.true_w = _cdiv(_cdiv(j.width * c.h, hmax), 8); c.true_h = _cdiv(_cdiv(j.height * c.v, vmax), 8)   # blocks that hold picture samples
                c.coef = np.zeros((c.blocks_h, c.blocks_w, 64), dtype=np.int16)
            frame = True
        elif m in (0xC3, 0xC5, 0xC6, 0xC7, 0xC9, 0xCA, 0xCB, 0xCD, 0xCE, 0xCF): raise ValueError("unsupported JPEG process (arithmetic, lossless or hierarchical)")
        elif m == 0xC4:
            i = 0
            while i < len(body):
                tc, th = body[i] >> 4, body[i] & 15; counts = list(body[i + 1:i + 17]); n = sum(counts); syms = list(body[i + 17:i + 17 + n]); i += 17 + n
                (ac if tc else dc)[th] = Huff(counts, syms)
        elif m == 0xDD: j.restart_interval = (body[0] << 8) | body[1]
        elif m == 0xDA:
            if not frame: raise ValueError("scan before frame")
            ns = body[0]; sc = []
            for k in range(ns):
                cid, t = body[1 + 2 * k], body[2 + 2 * k]; comp = next(c for c in j.components if c.id == cid); sc.append((comp, t >> 4, t & 15))
            ss, se, a = body[1 + 2 * ns], body[2 + 2 * ns], body[3 + 2 * ns]; ah, al = a >> 4, a & 15
            end = pos
            while True:                                                                                 # the entropy-coded segment ends at the next marker that is not FF00 or RSTn
                end = data.index(b"\xff", end)
                if end + 1 >= len(data): end = len(data); break
                nb = data[end + 1]
                if nb == 0 or 0xD0 <= nb <= 0xD7: end += 2; continue
                break
            _decode_scan(j, sc, ss, se, ah, al, _split_restarts(data[pos:end]), dc, ac); pos = end
    if not frame: raise ValueError("no frame")
    return j

def _decode_scan(j, sc, ss, se, ah, al, intervals, dc, ac):
    prog = j.progressive
    if not prog: ss, se, ah, al = 0, 63, 0, 0
    if len(sc) == 1:                                                                                  # non-interleaved: the component's own blocks in raster order, one block per MCU
        comp = sc[0][0]; order = [(comp, y, x) for y in range(comp.true_h) for x in range(comp.true_w)]; per = [[o] for o in order]
        mcu_blocks = lambda idx: per[idx]; total = len(order)
    else:
        total = j.mcus_x * j.mcus_y
        def mcu_blocks(idx):
            my, mx = divmod(idx, j.mcus_x)
            return [(comp, my * comp.v + v, mx * comp.h + h) for comp, _, _ in sc for v in range(comp.v) for h in range(comp.h)]
    ri = j.restart_interval or total; idx = 0
    dcpred = {id(c): 0 for c, _, _ in sc}; eobrun = 0
    tabs = {id(c): (dc.get(td), ac.get(ta)) for c, td, ta in sc}
    for piece in intervals:
        if idx >= total: break
        br = Bits(piece); dcpred = {k: 0 for k in dcpred}; eobrun = 0; n = min(ri, total - idx)
        for _ in range(n):
            for comp, by, bx in mcu_blocks(idx):
                blk = comp.coef[by, bx]; dct, act = tabs[id(comp)]
                if not prog:
                    s = br.huff(dct); diff = br.extend(s) if s else 0; dcpred[id(comp)] += diff; blk[0] = dcpred[id(comp)]
                    k = 1
                    while k < 64:
                        rs = br.huff(act); r, s = rs >> 4, rs & 15
                        if s == 0:
                            if r == 15: k += 16; continue
                            break
                        k += r
                        if k > 63: raise ValueError("coefficient index past 63")
                        blk[k] = br.extend(s); k += 1
                elif ss == 0:
                    if ah == 0:
                        s = br.huff(dct); diff = br.extend(s) if s else 0; dcpred[id(comp)] += diff; blk[0] = dcpred[id(comp)] << al
                    elif br.bit(): blk[0] |= (1 << al)
                elif ah == 0:                                                                          # AC first pass
                    if eobrun > 0: eobrun -= 1; continue
                    k = ss
                    while k <= se:
                        rs = br.huff(act); r, s = rs >> 4, rs & 15
                        if s:
                            k += r
                            if k > 63: raise ValueError("coefficient index past 63")
                            blk[k] = br.extend(s) << al; k += 1
                        elif r == 15: k += 16
                        else:
                            eobrun = (1 << r) + (br.get(r) if r else 0) - 1; break
                else:                                                                                  # AC refinement pass
                    p1 = 1 << al; m1 = -1 << al; k = ss
                    if eobrun == 0:
                        while k <= se:
                            rs = br.huff(act); r, s = rs >> 4, rs & 15; newv = 0
                            if s:
                                newv = p1 if br.bit() else m1
                            elif r != 15:
                                eobrun = (1 << r) + (br.get(r) if r else 0); break
                            while k <= se:
                                cv = int(blk[k])
                                if cv != 0:
                                    if br.bit() and (cv & p1) == 0: blk[k] = cv + (p1 if cv >= 0 else m1)
                                else:
                                    r -= 1
                                    if r < 0: break
                                k += 1
                            if newv and k <= 63: blk[k] = newv
                            k += 1
                    if eobrun > 0:
                        while k <= se:
                            cv = int(blk[k])
                            if cv != 0 and br.bit() and (cv & p1) == 0: blk[k] = cv + (p1 if cv >= 0 else m1)
                            k += 1
                        eobrun -= 1
            idx += 1
    return

def natural(coef_zz):
    """[..., 64] zigzag order to natural (row-major 8 x 8) order."""
    out = np.zeros_like(coef_zz); out[..., ZZ] = coef_zz; return out

_C = np.array([[np.sqrt((1 if u == 0 else 2) / 8) * np.cos((2 * x + 1) * u * np.pi / 16) for x in range(8)] for u in range(8)])
def idct_plane(comp, qzz):
    """Dequantise and inverse-DCT (float) one component into a plane of size (blocks_h * 8, blocks_w * 8), level shifted by +128, not clamped."""
    q = np.zeros(64); q[ZZ] = qzz
    f = natural(comp.coef.astype(np.float64)) * q                                                    # [bh, bw, 64] natural order, dequantised
    f = f.reshape(comp.blocks_h, comp.blocks_w, 8, 8)
    px = np.einsum("xu,hwuv,vy->hwxy", _C.T, f, _C)                                                   # C^T F C, per block
    return px.transpose(0, 2, 1, 3).reshape(comp.blocks_h * 8, comp.blocks_w * 8) + 128.0
