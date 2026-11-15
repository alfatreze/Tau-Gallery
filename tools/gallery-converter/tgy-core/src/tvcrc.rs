//! The TVC binary range coder (docs/TVC_BITSTREAM_V0.md section 3), bit-exact with `tools/gallery/tvcrc.py`:
//! 32-bit `range` and `code`, 15-bit probabilities supplied by the caller, bypass bins, normalise while range < 2^24,
//! a 5-byte flush, and zero bytes past the end of the data on decode.

const M32: u64 = 0xFFFF_FFFF;

pub struct Encoder {
    range: u32,
    low: u64,
    cache: u8,
    cache_size: u64,
    pub out: Vec<u8>,
}

impl Default for Encoder {
    fn default() -> Self { Self::new() }
}

impl Encoder {
    pub fn new() -> Self { Encoder { range: M32 as u32, low: 0, cache: 0, cache_size: 1, out: Vec::new() } }

    #[inline]
    fn shift_low(&mut self) {
        if (self.low & M32) < 0xFF00_0000 || (self.low >> 32) != 0 {
            let carry = (self.low >> 32) as u8;
            let mut temp = self.cache;
            while self.cache_size > 0 {
                self.out.push(temp.wrapping_add(carry));
                temp = 0xFF;
                self.cache_size -= 1;
            }
            self.cache = ((self.low >> 24) & 0xFF) as u8;
        }
        self.cache_size += 1;
        self.low = (self.low & 0x00FF_FFFF) << 8;
    }

    #[inline]
    fn normalize(&mut self) {
        while self.range < (1 << 24) {
            self.range <<= 8;
            self.shift_low();
        }
    }

    /// One bin with an explicit 15-bit probability `p` of a zero.
    #[inline]
    pub fn bit_p(&mut self, p: u32, bit: u32) {
        let bound = (self.range >> 15) * p;
        if bit != 0 {
            self.low += bound as u64;
            self.range -= bound;
        } else {
            self.range = bound;
        }
        self.normalize();
    }

    #[inline]
    pub fn bypass(&mut self, bit: u32) {
        self.range >>= 1;
        if bit != 0 { self.low += self.range as u64; }
        self.normalize();
    }

    pub fn finish(mut self) -> Vec<u8> {
        for _ in 0..5 { self.shift_low(); }
        self.out
    }
}

pub struct Decoder<'a> {
    data: &'a [u8],
    pos: usize,
    range: u32,
    code: u32,
}

impl<'a> Decoder<'a> {
    pub fn new(data: &'a [u8]) -> Self {
        let mut d = Decoder { data, pos: 0, range: M32 as u32, code: 0 };
        for _ in 0..5 { d.code = (d.code << 8) | d.byte() as u32; }
        d
    }
    #[inline]
    fn byte(&mut self) -> u8 {
        if self.pos < self.data.len() { let b = self.data[self.pos]; self.pos += 1; b } else { 0 }
    }
    #[inline]
    fn normalize(&mut self) {
        while self.range < (1 << 24) {
            self.range <<= 8;
            self.code = (self.code << 8) | self.byte() as u32;
        }
    }
    #[inline]
    pub fn bit_p(&mut self, p: u32) -> u32 {
        let bound = (self.range >> 15) * p;
        let b;
        if self.code >= bound { self.code -= bound; self.range -= bound; b = 1; } else { self.range = bound; b = 0; }
        self.normalize();
        b
    }
    #[inline]
    pub fn bypass(&mut self) -> u32 {
        self.range >>= 1;
        let b;
        if self.code >= self.range { self.code -= self.range; b = 1; } else { b = 0; }
        self.normalize();
        b
    }
}
