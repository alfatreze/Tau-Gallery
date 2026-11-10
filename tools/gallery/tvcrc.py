"""The TVC binary range coder as specified in Tau codecs (docs/TVC_BITSTREAM_V0.md section 3, `tau-video/src/tvc/range.rs`), reused for the TGY and TGX entropy coding (plan section 6.5, "reuse"). Bit-exact:
32-bit `range` and `code`, 15-bit adaptive probabilities (initial 16384, adaptation shift 5), bypass bins, normalise while range < 2^24, a 5-byte flush, and zero bytes past the end of the data on decode.
This is the HOST REFERENCE: sim/test_tvcrc.py checks it (round trips, carry runs, padding, mutants) and sim/cross_tvcrc.py compares it bit for bit with the Rust implementation. The hardware decoder and the
C decoder of the firmware must reproduce it exactly."""
PROB_INIT, PROB_ONE, SHIFT = 16384, 32768, 5
M32 = 0xFFFFFFFF

def update(p, bit):
    return p - (p >> SHIFT) if bit else p + ((PROB_ONE - p) >> SHIFT)

class Encoder:
    def __init__(self):
        self.range, self.low, self.cache, self.cache_size, self.out = M32, 0, 0, 1, bytearray()
    def _shift_low(self):
        if (self.low & M32) < 0xFF000000 or (self.low >> 32) != 0:
            carry = self.low >> 32; temp = self.cache
            while self.cache_size:
                self.out.append((temp + carry) & 0xFF); temp = 0xFF; self.cache_size -= 1
            self.cache = (self.low >> 24) & 0xFF
        self.cache_size += 1
        self.low = (self.low & 0x00FFFFFF) << 8
    def _normalize(self):
        while self.range < (1 << 24):
            self.range = (self.range << 8) & M32; self._shift_low()
    def bit(self, probs, ctx, bit):
        p = probs[ctx]; bound = (self.range >> 15) * p
        if bit: self.low += bound; self.range -= bound
        else: self.range = bound
        probs[ctx] = update(p, bit); self._normalize()
    def bit_p(self, p, bit):
        """Encode one bin with an explicit 15-bit probability of a 0 (no adaptation here: the caller's engine owns it). bit_p(probs[c], b) followed by the TVC update equals bit(probs, c, b)."""
        bound = (self.range >> 15) * p
        if bit: self.low += bound; self.range -= bound
        else: self.range = bound
        self._normalize()
    def bypass(self, bit):
        self.range >>= 1
        if bit: self.low += self.range
        self._normalize()
    def finish(self):
        for _ in range(5): self._shift_low()
        return bytes(self.out)

class Decoder:
    def __init__(self, data):
        self.data, self.pos, self.range, self.code = data, 0, M32, 0
        for _ in range(5): self.code = ((self.code << 8) | self._byte()) & M32
    def _byte(self):
        if self.pos < len(self.data): b = self.data[self.pos]; self.pos += 1; return b
        return 0
    def _normalize(self):
        while self.range < (1 << 24):
            self.range = (self.range << 8) & M32; self.code = ((self.code << 8) | self._byte()) & M32
    def bit(self, probs, ctx):
        p = probs[ctx]; bound = (self.range >> 15) * p
        if self.code >= bound: self.code -= bound; self.range -= bound; b = 1
        else: self.range = bound; b = 0
        probs[ctx] = update(p, b); self._normalize(); return b
    def bit_p(self, p):
        bound = (self.range >> 15) * p
        if self.code >= bound: self.code -= bound; self.range -= bound; b = 1
        else: self.range = bound; b = 0
        self._normalize(); return b
    def bypass(self):
        self.range >>= 1
        if self.code >= self.range: self.code -= self.range; b = 1
        else: b = 0
        self._normalize(); return b

def new_probs(n): return [PROB_INIT] * n
