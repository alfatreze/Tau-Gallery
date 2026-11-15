//! A JPEG coefficient reader: the quantised DCT coefficients of a baseline, extended-sequential or progressive Huffman JPEG exactly as stored
//! (no IDCT, no colour conversion). A port of `tools/gallery/jpegcoef.py`; coefficients are in ZIGZAG order. Only Huffman-coded, 8-bit, 1 or 3 component files.

pub struct Comp {
    pub id: u8,
    pub h: usize,
    pub v: usize,
    pub tq: usize,
    pub blocks_w: usize,
    pub blocks_h: usize,
    pub true_w: usize,
    pub true_h: usize,
    /// [blocks_h][blocks_w][64], zigzag order.
    pub coef: Vec<i16>,
}

pub struct Jpeg {
    pub width: usize,
    pub height: usize,
    pub progressive: bool,
    pub restart_interval: usize,
    pub quant: [Option<[u16; 64]>; 4],
    pub components: Vec<Comp>,
    pub mcus_x: usize,
    pub mcus_y: usize,
}

type R<T> = Result<T, String>;

struct Huff {
    maxcode: [i32; 18],
    valptr: [usize; 18],
    mincode: [i32; 18],
    symbols: Vec<u8>,
    lut: [u16; 512], // (length << 8) | symbol, 0 = not in the lookahead
}

impl Huff {
    fn new(counts: &[u8], symbols: Vec<u8>) -> Huff {
        let mut h = Huff { maxcode: [-1; 18], valptr: [0; 18], mincode: [0; 18], symbols, lut: [0; 512] };
        let (mut code, mut k) = (0i32, 0usize);
        for l in 1..=16 {
            h.valptr[l] = k;
            h.mincode[l] = code;
            code += counts[l - 1] as i32;
            k += counts[l - 1] as usize;
            h.maxcode[l] = if counts[l - 1] != 0 { code - 1 } else { -1 };
            code <<= 1;
        }
        let (mut code, mut k) = (0usize, 0usize);
        for l in 1..=9usize {
            for _ in 0..counts[l - 1] {
                let base = code << (9 - l);
                let sym = h.symbols.get(k).copied().unwrap_or(0);
                for f in 0..(1usize << (9 - l)) { if base + f < 512 { h.lut[base + f] = ((l as u16) << 8) | sym as u16; } }
                code += 1;
                k += 1;
            }
            code <<= 1;
        }
        h
    }
}

/// MSB-first bit reader over one restart interval of already unstuffed bytes; bytes past the end read as zero.
struct Bits<'a> { d: &'a [u8], pos: usize, buf: u64, cnt: u32 }

impl<'a> Bits<'a> {
    fn new(d: &'a [u8]) -> Self { Bits { d, pos: 0, buf: 0, cnt: 0 } }
    #[inline]
    fn fill(&mut self) {
        while self.cnt <= 56 {
            let b = if self.pos < self.d.len() { self.d[self.pos] } else { 0 };
            self.pos += 1;
            self.buf = (self.buf << 8) | b as u64;
            self.cnt += 8;
        }
    }
    #[inline]
    fn get(&mut self, n: u32) -> u32 {
        if n == 0 { return 0; }
        if self.cnt < n { self.fill(); }
        self.cnt -= n;
        ((self.buf >> self.cnt) & ((1u64 << n) - 1)) as u32
    }
    #[inline]
    fn bit(&mut self) -> u32 { self.get(1) }
    #[inline]
    fn huff(&mut self, h: &Huff) -> R<u8> {
        if self.cnt < 16 { self.fill(); }
        let peek = ((self.buf >> (self.cnt - 9)) & 511) as usize;
        let e = h.lut[peek];
        if e != 0 {
            self.cnt -= (e >> 8) as u32;
            return Ok((e & 255) as u8);
        }
        let mut code = 0i32;
        for l in 1..=16usize {
            code = (code << 1) | self.bit() as i32;
            if h.maxcode[l] >= 0 && code <= h.maxcode[l] && code >= h.mincode[l] {
                return h.symbols.get(h.valptr[l] + (code - h.mincode[l]) as usize).copied().ok_or_else(|| "bad Huffman table".to_string());
            }
        }
        Err("bad Huffman code".into())
    }
    #[inline]
    fn extend(&mut self, s: u32) -> i32 {
        let v = self.get(s) as i32;
        if v >= (1 << (s - 1)) { v } else { v - (1 << s) + 1 }
    }
}

/// The entropy-coded segment as intervals split at RSTn markers, FF00 unstuffed to FF.
fn split_restarts(seg: &[u8]) -> Vec<Vec<u8>> {
    fn unstuff(s: &[u8]) -> Vec<u8> {
        let mut o = Vec::with_capacity(s.len());
        let mut i = 0;
        while i < s.len() { o.push(s[i]); if s[i] == 0xFF && i + 1 < s.len() && s[i + 1] == 0 { i += 2; } else { i += 1; } }
        o
    }
    let mut out = Vec::new();
    let (mut start, mut i, n) = (0usize, 0usize, seg.len());
    while i + 1 < n {
        if seg[i] == 0xFF && (0xD0..=0xD7).contains(&seg[i + 1]) { out.push(unstuff(&seg[start..i])); start = i + 2; i += 2; } else { i += 1; }
    }
    out.push(unstuff(&seg[start..]));
    out
}

fn cdiv(a: usize, b: usize) -> usize { a.div_ceil(b) }

pub fn read_jpeg(data: &[u8]) -> R<Jpeg> {
    if data.len() < 2 || data[0] != 0xFF || data[1] != 0xD8 { return Err("not a JPEG".into()); }
    let mut j = Jpeg { width: 0, height: 0, progressive: false, restart_interval: 0, quant: [None; 4], components: vec![], mcus_x: 0, mcus_y: 0 };
    let mut dc: [Option<Huff>; 4] = [None, None, None, None];
    let mut ac: [Option<Huff>; 4] = [None, None, None, None];
    let (mut pos, mut frame) = (2usize, false);
    let n = data.len();
    while pos < n {
        if data[pos] != 0xFF { pos += 1; continue; }
        while pos < n && data[pos] == 0xFF { pos += 1; }
        if pos >= n { break; }
        let m = data[pos];
        pos += 1;
        if m == 0xD9 { break; }
        if m == 0x01 || (0xD0..=0xD8).contains(&m) { continue; }
        if pos + 2 > n { return Err("truncated marker".into()); }
        let ln = ((data[pos] as usize) << 8) | data[pos + 1] as usize;
        if ln < 2 || pos + ln > n { return Err("marker length out of range".into()); }
        let body = &data[pos + 2..pos + ln];
        pos += ln;
        match m {
            0xDB => {
                let mut i = 0;
                while i < body.len() {
                    let (pq, tq) = ((body[i] >> 4) as usize, (body[i] & 15) as usize);
                    i += 1;
                    let need = 64 * if pq != 0 { 2 } else { 1 };
                    if tq > 3 || i + need > body.len() { return Err("bad quantisation table".into()); }
                    let mut t = [0u16; 64];
                    for k in 0..64 { t[k] = if pq != 0 { ((body[i + 2 * k] as u16) << 8) | body[i + 2 * k + 1] as u16 } else { body[i + k] as u16 }; }
                    j.quant[tq] = Some(t);
                    i += need;
                }
            }
            0xC0..=0xC2 => {
                if body.len() < 6 || body[0] != 8 { return Err("only 8-bit JPEG".into()); }
                j.progressive = m == 0xC2;
                j.height = ((body[1] as usize) << 8) | body[2] as usize;
                j.width = ((body[3] as usize) << 8) | body[4] as usize;
                let nc = body[5] as usize;
                if nc != 1 && nc != 3 { return Err("1 or 3 components only".into()); }
                if body.len() < 6 + 3 * nc || j.width == 0 || j.height == 0 { return Err("bad frame header".into()); }
                j.components = (0..nc).map(|k| Comp { id: body[6 + 3 * k], h: (body[7 + 3 * k] >> 4) as usize, v: (body[7 + 3 * k] & 15) as usize, tq: (body[8 + 3 * k] & 3) as usize, blocks_w: 0, blocks_h: 0, true_w: 0, true_h: 0, coef: vec![] }).collect();
                if j.components.iter().any(|c| c.h == 0 || c.v == 0 || c.h > 4 || c.v > 4) { return Err("bad sampling factors".into()); }
                let hmax = j.components.iter().map(|c| c.h).max().unwrap();
                let vmax = j.components.iter().map(|c| c.v).max().unwrap();
                j.mcus_x = cdiv(j.width, 8 * hmax);
                j.mcus_y = cdiv(j.height, 8 * vmax);
                let (w, h, mx, my) = (j.width, j.height, j.mcus_x, j.mcus_y);
                for c in j.components.iter_mut() {
                    c.blocks_w = mx * c.h;
                    c.blocks_h = my * c.v;
                    c.true_w = cdiv(cdiv(w * c.h, hmax), 8);
                    c.true_h = cdiv(cdiv(h * c.v, vmax), 8);
                    c.coef = vec![0i16; c.blocks_w * c.blocks_h * 64];
                }
                frame = true;
            }
            0xC3 | 0xC5..=0xC7 | 0xC9..=0xCB | 0xCD..=0xCF => return Err("unsupported JPEG process (arithmetic, lossless or hierarchical)".into()),
            0xC4 => {
                let mut i = 0;
                while i < body.len() {
                    if i + 17 > body.len() { return Err("bad Huffman table".into()); }
                    let (tc, th) = ((body[i] >> 4) as usize, (body[i] & 15) as usize);
                    let counts = &body[i + 1..i + 17];
                    let ns: usize = counts.iter().map(|&c| c as usize).sum();
                    if th > 3 || i + 17 + ns > body.len() { return Err("bad Huffman table".into()); }
                    let h = Huff::new(counts, body[i + 17..i + 17 + ns].to_vec());
                    if tc != 0 { ac[th] = Some(h) } else { dc[th] = Some(h) }
                    i += 17 + ns;
                }
            }
            0xDD => { if body.len() < 2 { return Err("bad restart interval".into()); } j.restart_interval = ((body[0] as usize) << 8) | body[1] as usize; }
            0xDA => {
                if !frame { return Err("scan before frame".into()); }
                if body.is_empty() { return Err("bad scan header".into()); }
                let ns = body[0] as usize;
                if ns == 0 || body.len() < 4 + 2 * ns { return Err("bad scan header".into()); }
                let mut sc: Vec<(usize, usize, usize)> = Vec::new();
                for k in 0..ns {
                    let (cid, t) = (body[1 + 2 * k], body[2 + 2 * k]);
                    let ci = j.components.iter().position(|c| c.id == cid).ok_or("scan names an unknown component")?;
                    sc.push((ci, (t >> 4) as usize & 3, (t & 15) as usize & 3));
                }
                let (ss, se, a) = (body[1 + 2 * ns] as usize, body[2 + 2 * ns] as usize, body[3 + 2 * ns]);
                let (ah, al) = ((a >> 4) as u32, (a & 15) as u32);
                let mut end = pos;
                loop {
                    end = data[end..].iter().position(|&b| b == 0xFF).map(|p| p + end).ok_or("no marker after the scan")?;
                    if end + 1 >= n { end = n; break; }
                    let nb = data[end + 1];
                    if nb == 0 || (0xD0..=0xD7).contains(&nb) { end += 2; continue; }
                    break;
                }
                decode_scan(&mut j, &sc, ss, se, ah, al, &split_restarts(&data[pos..end]), &dc, &ac)?;
                pos = end;
            }
            _ => {}
        }
    }
    if !frame { return Err("no frame".into()); }
    Ok(j)
}

fn decode_scan(j: &mut Jpeg, sc: &[(usize, usize, usize)], ss: usize, se: usize, ah: u32, al: u32, intervals: &[Vec<u8>], dc: &[Option<Huff>; 4], ac: &[Option<Huff>; 4]) -> R<()> {
    let prog = j.progressive;
    let (ss, se, ah, al) = if prog { (ss, se.min(63), ah, al) } else { (0, 63, 0, 0) };
    // the order of the blocks of the scan: (scan slot, block row, block column) per MCU
    let single = sc.len() == 1;
    let total = if single { let c = &j.components[sc[0].0]; c.true_w * c.true_h } else { j.mcus_x * j.mcus_y };
    let ri = if j.restart_interval != 0 { j.restart_interval } else { total };
    let mut idx = 0usize;
    let mut dcpred = vec![0i32; sc.len()];
    let mut eobrun: u32 = 0;
    let tabs: Vec<(Option<&Huff>, Option<&Huff>)> = sc.iter().map(|&(_, td, ta)| (dc[td].as_ref(), ac[ta].as_ref())).collect();
    let (mcus_x, ncomp_geom): (usize, Vec<(usize, usize, usize, usize)>) = (j.mcus_x, j.components.iter().map(|c| (c.h, c.v, c.blocks_w, c.true_w)).collect());
    let pattern: Vec<(usize, usize, usize, usize, usize)> = if single { vec![(0, 0, 0, 1, 1)] } else {
        let mut p = Vec::new();
        for (slot, &(ci, _, _)) in sc.iter().enumerate() { let (ch, cv, _, _) = ncomp_geom[ci]; for v in 0..cv { for h in 0..ch { p.push((slot, v, h, ch, cv)); } } }
        p
    };
    for piece in intervals {
        if idx >= total { break; }
        let mut br = Bits::new(piece);
        for d in dcpred.iter_mut() { *d = 0; }
        eobrun = 0;
        let cnt = ri.min(total - idx);
        for _ in 0..cnt {
            let (my, mx) = (idx / mcus_x, idx % mcus_x);
            for &(slot, v, h, ch, cv) in &pattern {
                let (by, bx) = if single { (idx / ncomp_geom[sc[0].0].3, idx % ncomp_geom[sc[0].0].3) } else { (my * cv + v, mx * ch + h) };
                let ci = sc[slot].0;
                let bw = ncomp_geom[ci].2;
                let off = (by * bw + bx) * 64;
                let comp = &mut j.components[ci];
                if off + 64 > comp.coef.len() { return Err("block outside the component".into()); }
                let blk = &mut comp.coef[off..off + 64];
                let (dct, act) = tabs[slot];
                if !prog {
                    let dct = dct.ok_or("missing DC Huffman table")?;
                    let act = act.ok_or("missing AC Huffman table")?;
                    let s = br.huff(dct)? as u32;
                    let diff = if s != 0 { br.extend(s.min(16)) } else { 0 };
                    dcpred[slot] += diff;
                    blk[0] = dcpred[slot] as i16;
                    let mut k = 1usize;
                    while k < 64 {
                        let rs = br.huff(act)?;
                        let (r, s) = ((rs >> 4) as usize, (rs & 15) as u32);
                        if s == 0 {
                            if r == 15 { k += 16; continue; }
                            break;
                        }
                        k += r;
                        if k > 63 { return Err("coefficient index past 63".into()); }
                        blk[k] = br.extend(s) as i16;
                        k += 1;
                    }
                } else if ss == 0 {
                    if ah == 0 {
                        let dct = dct.ok_or("missing DC Huffman table")?;
                        let s = br.huff(dct)? as u32;
                        let diff = if s != 0 { br.extend(s.min(16)) } else { 0 };
                        dcpred[slot] += diff;
                        blk[0] = (dcpred[slot] << al) as i16;
                    } else if br.bit() != 0 { blk[0] |= (1i32 << al) as i16; }
                } else if ah == 0 {
                    let act = act.ok_or("missing AC Huffman table")?;
                    if eobrun > 0 { eobrun -= 1; continue; }
                    let mut k = ss;
                    while k <= se {
                        let rs = br.huff(act)?;
                        let (r, s) = ((rs >> 4) as usize, (rs & 15) as u32);
                        if s != 0 {
                            k += r;
                            if k > 63 { return Err("coefficient index past 63".into()); }
                            blk[k] = (br.extend(s) << al) as i16;
                            k += 1;
                        } else if r == 15 { k += 16; } else {
                            eobrun = (1u32 << r) + if r != 0 { br.get(r as u32) } else { 0 } - 1;
                            break;
                        }
                    }
                } else {
                    let act = act.ok_or("missing AC Huffman table")?;
                    let p1 = 1i32 << al;
                    let m1 = -1i32 << al;
                    let mut k = ss;
                    if eobrun == 0 {
                        while k <= se {
                            let rs = br.huff(act)?;
                            let (mut r, s) = ((rs >> 4) as i32, (rs & 15) as u32);
                            let mut newv = 0i32;
                            if s != 0 { newv = if br.bit() != 0 { p1 } else { m1 }; } else if r != 15 {
                                eobrun = (1u32 << r) + if r != 0 { br.get(r as u32) } else { 0 };
                                break;
                            }
                            while k <= se {
                                let cv = blk[k] as i32;
                                if cv != 0 {
                                    if br.bit() != 0 && (cv & p1) == 0 { blk[k] = (cv + if cv >= 0 { p1 } else { m1 }) as i16; }
                                } else {
                                    r -= 1;
                                    if r < 0 { break; }
                                }
                                k += 1;
                            }
                            if newv != 0 && k <= 63 { blk[k] = newv as i16; }
                            k += 1;
                        }
                    }
                    if eobrun > 0 {
                        while k <= se {
                            let cv = blk[k] as i32;
                            if cv != 0 && br.bit() != 0 && (cv & p1) == 0 { blk[k] = (cv + if cv >= 0 { p1 } else { m1 }) as i16; }
                            k += 1;
                        }
                        eobrun -= 1;
                    }
                }
            }
            idx += 1;
        }
    }
    let _ = eobrun;
    Ok(())
}
