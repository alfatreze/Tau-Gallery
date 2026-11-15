//! TGY symbol layer, version 0 (FROZEN; docs/TGY_SYMBOL_LAYER_V0.md): the quantised DCT coefficients of ONE tile as ONE range-coder stream.
//! A line-for-line port of `tools/gallery/tgy_v0.py`; the stream bytes must equal the Python ones. One generic function codes both
//! directions (`ENC` true: values are read from the block; false: they are decoded and the block argument is ignored).
use crate::tvcrc;

pub const N_CTX: usize = 1250;
const LIMIT: u8 = 30;

const fn rec_table() -> [u32; LIMIT as usize + 1] {
    let mut t = [0u32; LIMIT as usize + 1];
    let mut n = 0;
    while n <= LIMIT as usize {
        let v = (65536.0f64 / (n as f64 + 1.5)) as u32;
        t[n] = if v < 32768 { v } else { 32768 };
        n += 1;
    }
    t
}
const REC: [u32; LIMIT as usize + 1] = rec_table();

const fn kg_table() -> [usize; 64] {
    let edges = [2usize, 3, 4, 5, 7, 9, 12, 16, 21, 28, 37, 49];
    let mut t = [0usize; 64];
    let mut k = 1;
    while k < 64 {
        let mut c = 0;
        let mut i = 0;
        while i < edges.len() { if k >= edges[i] { c += 1; } i += 1; }
        t[k] = c;
        k += 1;
    }
    t
}
const KG: [usize; 64] = kg_table();

pub const ZZN: [usize; 64] = [0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5, 12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63];

const fn ln_table() -> [(i8, i8); 64] {
    let mut zzi = [0usize; 64];
    let mut i = 0;
    while i < 64 { zzi[ZZN[i]] = i; i += 1; }
    let mut t = [(-1i8, -1i8); 64];
    let mut k = 0;
    while k < 64 {
        let n = ZZN[k];
        let l = if n % 8 != 0 { zzi[n - 1] as i8 } else { -1 };
        let u = if n >= 8 { zzi[n - 8] as i8 } else { -1 };
        t[k] = (l, u);
        k += 1;
    }
    t
}
const LN: [(i8, i8); 64] = ln_table();

const O_D0: usize = 0;
const O_DS: usize = 2;
const O_DX: usize = 4;
const O_DM: usize = 24;
const O_LT: usize = 44;
const O_SG: usize = 170;
const O_G1: usize = 300;
const O_MX: usize = 430;
const O_MM: usize = 1230;

/// What the symbol coder needs from a range coder: a context-coded bin and a bypass bin. The decoder ignores the bit it is given.
pub trait Io {
    fn bit(&mut self, c: usize, b: u32) -> u32;
    fn bypass(&mut self, b: u32) -> u32;
}

struct Engine { p: [u16; N_CTX], n: [u8; N_CTX] }
impl Engine {
    fn new() -> Self { Engine { p: [16384; N_CTX], n: [0; N_CTX] } }
    #[inline]
    fn update(&mut self, c: usize, bit: u32) {
        let p = self.p[c] as u32;
        let r = REC[self.n[c] as usize];
        let p = if bit != 0 { p - ((p * r) >> 16) } else { p + (((32768 - p) * r) >> 16) };
        self.p[c] = p.clamp(32, 32768 - 32) as u16;
        if self.n[c] < LIMIT { self.n[c] += 1; }
    }
}

pub struct EncIo { e: Engine, enc: tvcrc::Encoder }
impl EncIo {
    pub fn new() -> Self { EncIo { e: Engine::new(), enc: tvcrc::Encoder::new() } }
    pub fn finish(self) -> Vec<u8> { self.enc.finish() }
}
impl Default for EncIo { fn default() -> Self { Self::new() } }
impl Io for EncIo {
    #[inline]
    fn bit(&mut self, c: usize, b: u32) -> u32 { self.enc.bit_p(self.e.p[c] as u32, b); self.e.update(c, b); b }
    #[inline]
    fn bypass(&mut self, b: u32) -> u32 { self.enc.bypass(b); b }
}

pub struct DecIo<'a> { e: Engine, dec: tvcrc::Decoder<'a> }
impl<'a> DecIo<'a> {
    pub fn new(data: &'a [u8]) -> Self { DecIo { e: Engine::new(), dec: tvcrc::Decoder::new(data) } }
}
impl Io for DecIo<'_> {
    #[inline]
    fn bit(&mut self, c: usize, _b: u32) -> u32 { let b = self.dec.bit_p(self.e.p[c] as u32); self.e.update(c, b); b }
    #[inline]
    fn bypass(&mut self, _b: u32) -> u32 { self.dec.bypass() }
}

pub type Block = [i16; 64];

#[inline]
fn lb(x: i32) -> usize { match x { 0 => 0, 1 => 1, 2 => 2, 3 | 4 => 3, _ => 4 } }

#[inline]
fn bitlen(x: u32) -> u32 { 32 - x.leading_zeros() }

/// Exp-Golomb order 0 of m >= 0 with the contexts of `unary` (exponent steps) and `mant` (the first mantissa bit).
fn eg<I: Io>(io: &mut I, m: u32, unary: impl Fn(usize) -> usize, mant: impl Fn(usize) -> usize) -> u32 {
    let e = bitlen(m + 1) - 1;
    let mut i = 0u32;
    loop {
        if io.bit(unary(i.min(9) as usize), (i < e) as u32) == 0 { break; }
        i += 1;
    }
    let e = i;
    let mut v = 1u32;
    for j in (0..e).rev() {
        let bit = ((m + 1) >> j) & 1;
        let got = if j == e - 1 { io.bit(mant(e.min(9) as usize), bit) } else { io.bypass(bit) };
        v = (v << 1) | got;
    }
    v - 1
}

/// One block of 64 zigzag-ordered coefficients. `blk` is read only when ENC; `left` and `above` are the already coded neighbours of the same component.
pub fn code_block<const ENC: bool, I: Io>(io: &mut I, cc: usize, blk: &Block, left: Option<&Block>, above: Option<&Block>) -> Block {
    let mut out = [0i16; 64];
    let pred: i32 = match (left, above) { (Some(l), _) => l[0] as i32, (None, Some(a)) => a[0] as i32, _ => 0 };
    let d: i32 = if ENC { blk[0] as i32 - pred } else { 0 };
    let dv = if io.bit(O_D0 + cc, (d != 0) as u32) != 0 {
        let neg = io.bit(O_DS + cc, (d < 0) as u32) != 0;
        let m = if ENC { d.unsigned_abs() - 1 } else { 0 };
        let a = eg(io, m, |i| O_DX + cc * 10 + i, |e| O_DM + cc * 10 + e) as i32 + 1;
        if neg { -a } else { a }
    } else { 0 };
    out[0] = (pred + dv) as i16;
    let mut last = 0usize;
    if ENC { for k in (1..64).rev() { if blk[k] != 0 { last = k; break; } } }
    let mut node = 1usize;
    for i in (0..6).rev() { node = node * 2 + io.bit(O_LT + cc * 63 + node - 1, ((last >> i) & 1) as u32) as usize; }
    let last = node - 64;
    for k in 1..=last {
        let (li, ui) = LN[k];
        let lc = if li >= 0 && ui >= 0 { lb((out[li as usize] as i32).abs() + (out[ui as usize] as i32).abs()) }
                 else if li >= 0 { lb((out[li as usize] as i32).abs()) }
                 else if ui >= 0 { lb((out[ui as usize] as i32).abs()) } else { 0 };
        let g = (cc * 13 + KG[k]) * 5 + lc;
        if k < last && io.bit(O_SG + g, (blk[k] != 0) as u32) == 0 { continue; }
        let a0 = if ENC { (blk[k] as i32).unsigned_abs() } else { 0 };
        let a: i32 = if io.bit(O_G1 + g, (a0 > 1) as u32) != 0 {
            let kg = KG[k].min(7);
            let m = if ENC { a0 - 2 } else { 0 };
            eg(io, m, |i| O_MX + (((cc * 8 + kg) * 5 + lc) * 10) + i, |e| O_MM + cc * 10 + e) as i32 + 2
        } else { 1 };
        let neg = io.bypass((blk[k] < 0) as u32) != 0;
        out[k] = if neg { -a } else { a } as i16;
    }
    out
}

/// The blocks of one component of a tile, row-major.
#[derive(Clone, Debug, PartialEq)]
pub struct Grid { pub rows: usize, pub cols: usize, pub blocks: Vec<Block> }

impl Grid {
    pub fn zeros(rows: usize, cols: usize) -> Grid { Grid { rows, cols, blocks: vec![[0; 64]; rows * cols] } }
}

fn code_tile<const ENC: bool, I: Io>(io: &mut I, grids: &[Grid]) -> Vec<Grid> {
    let mut out = Vec::with_capacity(grids.len());
    for (ci, gr) in grids.iter().enumerate() {
        let cc = if ci == 0 { 0 } else { 1 };
        let mut g = Grid::zeros(gr.rows, gr.cols);
        for y in 0..gr.rows {
            for x in 0..gr.cols {
                let b = {
                    let left = if x > 0 { Some(&g.blocks[y * gr.cols + x - 1]) } else { None };
                    let above = if y > 0 { Some(&g.blocks[(y - 1) * gr.cols + x]) } else { None };
                    code_block::<ENC, I>(io, cc, &gr.blocks[y * gr.cols + x], left, above)
                };
                g.blocks[y * gr.cols + x] = b;
            }
        }
        out.push(g);
    }
    out
}

/// The stream of one tile (grids per component in order Y, Cb, Cr).
pub fn encode_tile(grids: &[Grid]) -> Vec<u8> {
    let mut io = EncIo::new();
    code_tile::<true, _>(&mut io, grids);
    io.finish()
}

/// Decodes a tile of the given shapes (rows, cols) per component.
pub fn decode_tile(data: &[u8], shapes: &[(usize, usize)]) -> Vec<Grid> {
    let mut io = DecIo::new(data);
    let empty: Vec<Grid> = shapes.iter().map(|&(r, c)| Grid::zeros(r, c)).collect();
    code_tile::<false, _>(&mut io, &empty)
}
