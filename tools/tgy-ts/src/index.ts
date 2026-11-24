// Tau Gallery (.tgy) Container v1 + Symbol Layer v0 + Reconstruction v0
// Bit-exact TypeScript implementation of alfatreze/Tau-Gallery `tgy-core`:
// - TVC v0 binary range coder (32-bit range, 15-bit probabilities, 5-byte flush)
// - TGY symbol layer v0 (1,250 adaptive contexts, 128x128 independent luma tiles)
// - TGY reconstruction v0 (libjpeg jpeg_idct_islow, tile-local 4:2:0 fancy upsampling, integer YCbCr -> RGB)
// - TGY container v1 (4 KB aligned sections, 64-byte header, 96x72 RGB565 LE thumbnail, per-tile IEEE CRC-32)

import { crc32 } from './crc32.ts';

export type RgbaRaster = {
  width: number;
  height: number;
  data: Uint8ClampedArray;
};

export type TgySubsampling = '4:2:0' | '4:4:4' | 'grey';

export type TgyEncodeOptions = {
  /** Full-resolution DCT quality (1–100, default 92). */
  quality?: number;
  /** 800x720 Analogue Pocket fit-canvas DCT quality (1–100, default 90). */
  fitQuality?: number;
  /** Chroma subsampling mode (default '4:2:0'). */
  subsampling?: TgySubsampling;
  /** Include 800x720 Tau Gallery fit canvas level (default true). */
  fitCanvas?: boolean;
  /** Enlarge images smaller than 800x720 to fill the fit canvas (default false). */
  upscale?: boolean;
};

export type TgyLevelHeader = {
  width: number;
  height: number;
  rect: [number, number, number, number];
  layout: number;
  mode: number;
  pixelFormat: number;
  kind: number;
  qy: number;
  qc: number;
  tileSizeLog2: number;
  flags: number;
  dataOffset: number;
  dataLength: number;
  tileIndexOffset: number;
  tileIndexLength: number;
  tileIndexCrc: number;
};

export type TgyDecoded = {
  width: number;
  height: number;
  channels: number;
  orientation: number;
  levels: TgyLevelHeader[];
  data: Uint8ClampedArray<ArrayBuffer>;
};

export const TGY_MAGIC = new Uint8Array([0x54, 0x47, 0x59, 0x00]); // "TGY\0"
export const TGY_VERSION = 1;
const HEADER_SIZE = 64;
const LEVEL_SIZE = 48;
const ALIGN = 4096;
const CANVAS_W = 800;
const CANVAS_H = 720;
const THUMB_W = 96;
const THUMB_H = 72;
const THUMB_BYTES = THUMB_W * THUMB_H * 2;
const MAX_TABLES = 8;
const TILE_SIZE = 128;

export const TGY_KIND_FULL = 1;
export const TGY_KIND_FIT = 2;
export const TGY_SUB_GREY = 0;
export const TGY_SUB_444 = 1;
export const TGY_SUB_420 = 2;

export const ZZN = new Uint8Array([
  0, 1, 8, 16, 9, 2, 3, 10,
  17, 24, 32, 25, 18, 11, 4, 5,
  12, 19, 26, 33, 40, 48, 41, 34,
  27, 20, 13, 6, 7, 14, 21, 28,
  35, 42, 49, 56, 57, 50, 43, 36,
  29, 22, 15, 23, 30, 37, 44, 51,
  58, 59, 52, 45, 38, 31, 39, 46,
  53, 60, 61, 54, 47, 55, 62, 63,
]);

const STD_LUMA = new Uint16Array([
  16, 11, 10, 16, 24, 40, 51, 61,
  12, 12, 14, 19, 26, 58, 60, 55,
  14, 13, 16, 24, 40, 57, 69, 56,
  14, 17, 22, 29, 51, 87, 80, 62,
  18, 22, 37, 56, 68, 109, 103, 77,
  24, 35, 55, 64, 81, 104, 113, 92,
  49, 64, 78, 87, 103, 121, 120, 101,
  72, 92, 95, 98, 112, 100, 103, 99,
]);

const STD_CHROMA = (() => {
  const t = new Uint16Array(64).fill(99);
  const head = [
    17, 18, 24, 47, 99, 99, 99, 99,
    18, 21, 26, 66, 99, 99, 99, 99,
    24, 26, 56, 99, 99, 99, 99, 99,
    47, 66, 99, 99, 99, 99, 99, 99,
  ];
  t.set(head, 0);
  return t;
})();

// --- TVC v0 binary range coder ---

const POW32 = 4294967296; // 2^32

class TvcEncoder {
  private range = 0xffffffff;
  private low = 0; // exact integer in [0, 2^33)
  private cache = 0;
  private cacheSize = 1;
  private out: number[] = [];

  private shiftLow() {
    const lo = this.low % POW32;
    const hi = Math.floor(this.low / POW32);
    if (lo < 0xff000000 || hi !== 0) {
      const carry = hi & 0xff;
      let temp = this.cache;
      while (this.cacheSize > 0) {
        this.out.push((temp + carry) & 0xff);
        temp = 0xff;
        this.cacheSize--;
      }
      this.cache = (lo >>> 24) & 0xff;
    }
    this.cacheSize++;
    this.low = ((lo & 0x00ffffff) * 256);
  }

  private normalize() {
    while (this.range < 0x01000000) {
      this.range = (this.range << 8) >>> 0;
      this.shiftLow();
    }
  }

  bitP(p: number, bit: number) {
    const bound = ((this.range >>> 15) * p) >>> 0;
    if (bit !== 0) {
      this.low += bound;
      this.range = (this.range - bound) >>> 0;
    } else {
      this.range = bound;
    }
    this.normalize();
  }

  bypass(bit: number) {
    this.range = this.range >>> 1;
    if (bit !== 0) this.low += this.range;
    this.normalize();
  }

  finish(): Uint8Array {
    for (let i = 0; i < 5; i++) this.shiftLow();
    return Uint8Array.from(this.out);
  }
}

class TvcDecoder {
  private pos = 0;
  private range = 0xffffffff;
  private code = 0;
  private readonly data: Uint8Array;

  constructor(data: Uint8Array) {
    this.data = data;
    for (let i = 0; i < 5; i++) {
      this.code = (((this.code << 8) >>> 0) | this.nextByte()) >>> 0;
    }
  }

  private nextByte(): number {
    return this.pos < this.data.length ? this.data[this.pos++] : 0;
  }

  private normalize() {
    while (this.range < 0x01000000) {
      this.range = (this.range << 8) >>> 0;
      this.code = (((this.code << 8) >>> 0) | this.nextByte()) >>> 0;
    }
  }

  bitP(p: number): number {
    const bound = ((this.range >>> 15) * p) >>> 0;
    let b: number;
    if (this.code >= bound) {
      this.code = (this.code - bound) >>> 0;
      this.range = (this.range - bound) >>> 0;
      b = 1;
    } else {
      this.range = bound;
      b = 0;
    }
    this.normalize();
    return b;
  }

  bypass(): number {
    this.range = this.range >>> 1;
    let b: number;
    if (this.code >= this.range) {
      this.code = (this.code - this.range) >>> 0;
      b = 1;
    } else {
      b = 0;
    }
    this.normalize();
    return b;
  }
}

// --- TGY Symbol Layer v0 ---

const N_CTX = 1250;
const CTX_LIMIT = 30;

const REC = (() => {
  const t = new Uint32Array(CTX_LIMIT + 1);
  for (let n = 0; n <= CTX_LIMIT; n++) {
    const v = Math.floor(65536 / (n + 1.5));
    t[n] = v < 32768 ? v : 32768;
  }
  return t;
})();

const KG = (() => {
  const edges = [2, 3, 4, 5, 7, 9, 12, 16, 21, 28, 37, 49];
  const t = new Uint8Array(64);
  for (let k = 1; k < 64; k++) {
    let c = 0;
    for (const e of edges) if (k >= e) c++;
    t[k] = c;
  }
  return t;
})();

const LN_L = new Int8Array(64).fill(-1);
const LN_U = new Int8Array(64).fill(-1);
(() => {
  const zzi = new Uint8Array(64);
  for (let i = 0; i < 64; i++) zzi[ZZN[i]] = i;
  for (let k = 0; k < 64; k++) {
    const n = ZZN[k];
    LN_L[k] = n % 8 !== 0 ? zzi[n - 1] : -1;
    LN_U[k] = n >= 8 ? zzi[n - 8] : -1;
  }
})();

const O_D0 = 0;
const O_DS = 2;
const O_DX = 4;
const O_DM = 24;
const O_LT = 44;
const O_SG = 170;
const O_G1 = 300;
const O_MX = 430;
const O_MM = 1230;

export type TgyGrid = { rows: number; cols: number; blocks: Int16Array };

interface SymbolIo {
  bit(ctx: number, b: number): number;
  bypass(b: number): number;
}

class ProbEngine {
  readonly p = new Uint16Array(N_CTX).fill(16384);
  readonly n = new Uint8Array(N_CTX);

  update(c: number, bit: number) {
    const p = this.p[c];
    const r = REC[this.n[c]];
    const next = bit !== 0 ? p - ((p * r) >>> 16) : p + (((32768 - p) * r) >>> 16);
    this.p[c] = next < 32 ? 32 : next > 32736 ? 32736 : next;
    if (this.n[c] < CTX_LIMIT) this.n[c]++;
  }
}

class EncIo implements SymbolIo {
  private readonly eng = new ProbEngine();
  private readonly enc = new TvcEncoder();

  bit(c: number, b: number): number {
    this.enc.bitP(this.eng.p[c], b);
    this.eng.update(c, b);
    return b;
  }

  bypass(b: number): number {
    this.enc.bypass(b);
    return b;
  }

  finish(): Uint8Array {
    return this.enc.finish();
  }
}

class DecIo implements SymbolIo {
  private readonly eng = new ProbEngine();
  private readonly dec: TvcDecoder;

  constructor(data: Uint8Array) {
    this.dec = new TvcDecoder(data);
  }

  bit(c: number): number {
    const b = this.dec.bitP(this.eng.p[c]);
    this.eng.update(c, b);
    return b;
  }

  bypass(): number {
    return this.dec.bypass();
  }
}

function lb(x: number): number {
  return x === 0 ? 0 : x === 1 ? 1 : x === 2 ? 2 : x <= 4 ? 3 : 4;
}

function bitlen(x: number): number {
  return 32 - Math.clz32(x >>> 0);
}

function codeEg(io: SymbolIo, m: number, unaryBase: number, mantBase: number): number {
  const e0 = bitlen(m + 1) - 1;
  let i = 0;
  while (true) {
    if (io.bit(unaryBase + (i < 9 ? i : 9), i < e0 ? 1 : 0) === 0) break;
    i++;
  }
  const e = i;
  let v = 1;
  for (let j = e - 1; j >= 0; j--) {
    const bit = ((m + 1) >>> j) & 1;
    const got = j === e - 1 ? io.bit(mantBase + (e < 9 ? e : 9), bit) : io.bypass(bit);
    v = (v << 1) | got;
  }
  return v - 1;
}

function codeBlock(
  io: SymbolIo,
  enc: boolean,
  cc: number,
  src: Int16Array,
  srcOff: number,
  dst: Int16Array,
  dstOff: number,
  pred: number,
) {
  const d = enc ? src[srcOff] - pred : 0;
  let dv = 0;
  if (io.bit(O_D0 + cc, d !== 0 ? 1 : 0) !== 0) {
    const neg = io.bit(O_DS + cc, d < 0 ? 1 : 0) !== 0;
    const m = enc ? Math.abs(d) - 1 : 0;
    const a = codeEg(io, m, O_DX + cc * 10, O_DM + cc * 10) + 1;
    dv = neg ? -a : a;
  }
  dst[dstOff] = pred + dv;

  let last = 0;
  if (enc) {
    for (let k = 63; k >= 1; k--) {
      if (src[srcOff + k] !== 0) {
        last = k;
        break;
      }
    }
  }
  let node = 1;
  for (let i = 5; i >= 0; i--) {
    node = node * 2 + io.bit(O_LT + cc * 63 + node - 1, (last >>> i) & 1);
  }
  last = node - 64;

  for (let k = 1; k <= last; k++) {
    const li = LN_L[k];
    const ui = LN_U[k];
    const lc = li >= 0 && ui >= 0
      ? lb(Math.abs(dst[dstOff + li]) + Math.abs(dst[dstOff + ui]))
      : li >= 0
        ? lb(Math.abs(dst[dstOff + li]))
        : ui >= 0
          ? lb(Math.abs(dst[dstOff + ui]))
          : 0;
    const g = (cc * 13 + KG[k]) * 5 + lc;
    const val = enc ? src[srcOff + k] : 0;
    if (k < last && io.bit(O_SG + g, val !== 0 ? 1 : 0) === 0) continue;
    const a0 = enc ? Math.abs(val) : 0;
    let a = 1;
    if (io.bit(O_G1 + g, a0 > 1 ? 1 : 0) !== 0) {
      const kg = KG[k] < 7 ? KG[k] : 7;
      const m = enc ? a0 - 2 : 0;
      a = codeEg(io, m, O_MX + (((cc * 8 + kg) * 5 + lc) * 10), O_MM + cc * 10) + 2;
    }
    const neg = io.bypass(val < 0 ? 1 : 0) !== 0;
    dst[dstOff + k] = neg ? -a : a;
  }
}

export function encodeTgyTile(grids: TgyGrid[]): Uint8Array {
  const io = new EncIo();
  const scratch = new Int16Array(64);
  for (let ci = 0; ci < grids.length; ci++) {
    const gr = grids[ci];
    const cc = ci === 0 ? 0 : 1;
    const out = new Int16Array(gr.rows * gr.cols * 64);
    for (let y = 0; y < gr.rows; y++) {
      for (let x = 0; x < gr.cols; x++) {
        const idx = (y * gr.cols + x) * 64;
        const pred = x > 0 ? out[idx - 64] : y > 0 ? out[idx - gr.cols * 64] : 0;
        codeBlock(io, true, cc, gr.blocks, idx, out, idx, pred);
      }
    }
  }
  void scratch;
  return io.finish();
}

export function decodeTgyTile(data: Uint8Array, shapes: readonly [number, number][]): TgyGrid[] {
  const io = new DecIo(data);
  const empty = new Int16Array(64);
  const outGrids: TgyGrid[] = [];
  for (let ci = 0; ci < shapes.length; ci++) {
    const [rows, cols] = shapes[ci];
    const cc = ci === 0 ? 0 : 1;
    const blocks = new Int16Array(rows * cols * 64);
    for (let y = 0; y < rows; y++) {
      for (let x = 0; x < cols; x++) {
        const idx = (y * cols + x) * 64;
        const pred = x > 0 ? blocks[idx - 64] : y > 0 ? blocks[idx - cols * 64] : 0;
        codeBlock(io, false, cc, empty, 0, blocks, idx, pred);
      }
    }
    outGrids.push({ rows, cols, blocks });
  }
  return outGrids;
}

// --- Reconstruction v0 (libjpeg jpeg_idct_islow + tile-local fancy upsampling + YCbCr -> RGB) ---

const CONST_BITS = 13;
const PASS1_BITS = 2;
const F0298 = 2446;
const F0390 = 3196;
const F0541 = 4433;
const F0765 = 6270;
const F0899 = 7373;
const F1175 = 9633;
const F1501 = 12299;
const F1847 = 15137;
const F1961 = 16069;
const F2053 = 16819;
const F2562 = 20995;
const F3072 = 25172;

function descale(x: number, n: number): number {
  return Math.floor((x + (1 << (n - 1))) / (1 << n));
}

function rangeLimit(d: number): number {
  const j = d & 1023;
  return j < 128 ? j + 128 : j < 512 ? 255 : j < 896 ? 0 : j - 896;
}

function butterfly(
  i0: number, i1: number, i2: number, i3: number,
  i4: number, i5: number, i6: number, i7: number,
  out: Float64Array,
) {
  const z1e = (i2 + i6) * F0541;
  const t2 = z1e + i6 * -F1847;
  const t3 = z1e + i2 * F0765;
  const t0 = (i0 + i4) * (1 << CONST_BITS);
  const t1 = (i0 - i4) * (1 << CONST_BITS);
  const t10 = t0 + t3;
  const t13 = t0 - t3;
  const t11 = t1 + t2;
  const t12 = t1 - t2;

  let a0 = i7, a1 = i5, a2 = i3, a3 = i1;
  let z1 = a0 + a3;
  let z2 = a1 + a2;
  let z3 = a0 + a2;
  let z4 = a1 + a3;
  const z5 = (z3 + z4) * F1175;
  a0 *= F0298;
  a1 *= F2053;
  a2 *= F3072;
  a3 *= F1501;
  z1 *= -F0899;
  z2 *= -F2562;
  z3 *= -F1961;
  z4 *= -F0390;
  z3 += z5;
  z4 += z5;
  a0 += z1 + z3;
  a1 += z2 + z4;
  a2 += z2 + z3;
  a3 += z1 + z4;

  out[0] = t10 + a3;
  out[1] = t11 + a2;
  out[2] = t12 + a1;
  out[3] = t13 + a0;
  out[4] = t13 - a0;
  out[5] = t12 - a1;
  out[6] = t11 - a2;
  out[7] = t10 - a3;
}

const idctWs = new Float64Array(64);
const idctTmp = new Float64Array(8);

export function idctIslow(c: Float64Array | Int32Array, out: Uint8Array) {
  for (let col = 0; col < 8; col++) {
    if (
      c[8 + col] === 0 && c[16 + col] === 0 && c[24 + col] === 0 &&
      c[32 + col] === 0 && c[40 + col] === 0 && c[48 + col] === 0 && c[56 + col] === 0
    ) {
      const dc = c[col] * (1 << PASS1_BITS);
      for (let r = 0; r < 8; r++) idctWs[r * 8 + col] = dc;
    } else {
      butterfly(
        c[col], c[8 + col], c[16 + col], c[24 + col],
        c[32 + col], c[40 + col], c[48 + col], c[56 + col],
        idctTmp,
      );
      for (let r = 0; r < 8; r++) idctWs[r * 8 + col] = descale(idctTmp[r], CONST_BITS - PASS1_BITS);
    }
  }
  for (let r = 0; r < 8; r++) {
    const o = r * 8;
    if (
      idctWs[o + 1] === 0 && idctWs[o + 2] === 0 && idctWs[o + 3] === 0 &&
      idctWs[o + 4] === 0 && idctWs[o + 5] === 0 && idctWs[o + 6] === 0 && idctWs[o + 7] === 0
    ) {
      const v = rangeLimit(descale(idctWs[o], PASS1_BITS + 3));
      for (let k = 0; k < 8; k++) out[o + k] = v;
    } else {
      butterfly(
        idctWs[o], idctWs[o + 1], idctWs[o + 2], idctWs[o + 3],
        idctWs[o + 4], idctWs[o + 5], idctWs[o + 6], idctWs[o + 7],
        idctTmp,
      );
      for (let k = 0; k < 8; k++) out[o + k] = rangeLimit(descale(idctTmp[k], CONST_BITS + PASS1_BITS + 3));
    }
  }
}

export function upsampleH2v2Fancy(p: Uint8Array, w: number, h: number): Uint8Array {
  const ow = 2 * w;
  const out = new Uint8Array(ow * 2 * h);
  if (w <= 2) {
    for (let y = 0; y < h; y++) {
      for (let x = 0; x < w; x++) {
        const v = p[y * w + x];
        out[(2 * y) * ow + 2 * x] = v;
        out[(2 * y) * ow + 2 * x + 1] = v;
        out[(2 * y + 1) * ow + 2 * x] = v;
        out[(2 * y + 1) * ow + 2 * x + 1] = v;
      }
    }
    return out;
  }
  for (let y = 0; y < h; y++) {
    const up = y > 0 ? y - 1 : 0;
    const down = y + 1 < h ? y + 1 : h - 1;
    const curOff = y * w;
    for (let v = 0; v < 2; v++) {
      const othOff = (v === 0 ? up : down) * w;
      const dstOff = (2 * y + v) * ow;
      const s = (x: number) => 3 * p[curOff + x] + p[othOff + x];
      out[dstOff] = (s(0) * 4 + 8) >> 4;
      out[dstOff + 1] = (s(0) * 3 + s(1) + 7) >> 4;
      for (let x = 1; x < w - 1; x++) {
        const sx = s(x);
        out[dstOff + 2 * x] = (sx * 3 + s(x - 1) + 8) >> 4;
        out[dstOff + 2 * x + 1] = (sx * 3 + s(x + 1) + 7) >> 4;
      }
      out[dstOff + ow - 2] = (s(w - 1) * 3 + s(w - 2) + 8) >> 4;
      out[dstOff + ow - 1] = (s(w - 1) * 4 + 7) >> 4;
    }
  }
  return out;
}

export function upsampleH2v2Tile(p: Uint8Array, w: number, h: number, tile = 64): Uint8Array {
  const ow = 2 * w;
  const out = new Uint8Array(ow * 2 * h);
  for (let y = 0; y < h; y += tile) {
    const th = Math.min(tile, h - y);
    for (let x = 0; x < w; x += tile) {
      const tw = Math.min(tile, w - x);
      const sub = new Uint8Array(tw * th);
      for (let r = 0; r < th; r++) sub.set(p.subarray((y + r) * w + x, (y + r) * w + x + tw), r * tw);
      const up = upsampleH2v2Fancy(sub, tw, th);
      for (let r = 0; r < 2 * th; r++) {
        out.set(up.subarray(r * 2 * tw, (r + 1) * 2 * tw), (2 * y + r) * ow + 2 * x);
      }
    }
  }
  return out;
}

const YCC_TABS = (() => {
  const fix = (x: number) => Math.trunc(x * 65536 + 0.5);
  const crR = new Int32Array(256);
  const cbB = new Int32Array(256);
  const crG = new Int32Array(256);
  const cbG = new Int32Array(256);
  const fCrR = fix(1.402);
  const fCbB = fix(1.772);
  const fCrG = fix(0.71414);
  const fCbG = fix(0.34414);
  for (let i = 0; i < 256; i++) {
    const x = i - 128;
    crR[i] = Math.floor((fCrR * x + 32768) / 65536);
    cbB[i] = Math.floor((fCbB * x + 32768) / 65536);
    crG[i] = -fCrG * x;
    cbG[i] = -fCbG * x + 32768;
  }
  return { crR, cbB, crG, cbG };
})();

export function yccToRgba(y: Uint8Array, cb: Uint8Array, cr: Uint8Array): Uint8ClampedArray<ArrayBuffer> {
  const n = y.length;
  const out = new Uint8ClampedArray(n * 4);
  const { crR, cbB, crG, cbG } = YCC_TABS;
  for (let i = 0; i < n; i++) {
    const yy = y[i];
    const b = cb[i];
    const r = cr[i];
    const o = i * 4;
    out[o] = yy + crR[r];
    out[o + 1] = yy + ((cbG[b] + crG[r]) >> 16);
    out[o + 2] = yy + cbB[b];
    out[o + 3] = 255;
  }
  return out;
}

// --- Forward DCT & Quantization ---

export function scaledQuantTable(base: Uint16Array, quality: number): Uint16Array {
  const q = Math.max(1, Math.min(100, Math.round(quality)));
  const s = q < 50 ? Math.floor(5000 / q) : 200 - 2 * q;
  const nat = new Uint16Array(64);
  for (let i = 0; i < 64; i++) {
    const v = Math.floor((base[i] * s + 50) / 100);
    nat[i] = v < 1 ? 1 : v > 255 ? 255 : v;
  }
  const zz = new Uint16Array(64);
  for (let k = 0; k < 64; k++) zz[k] = nat[ZZN[k]];
  return zz;
}

const CMAT = (() => {
  const c = new Float64Array(64);
  for (let u = 0; u < 8; u++) {
    const a = u === 0 ? 1 : 2;
    const scale = Math.sqrt(a / 8);
    for (let x = 0; x < 8; x++) {
      c[u * 8 + x] = scale * Math.cos(((2 * x + 1) * u * Math.PI) / 16);
    }
  }
  return c;
})();

const fdctTmp = new Float64Array(64);
const fdctNat = new Int16Array(64);

function fdctQuant(x: Float64Array, qnat: Float64Array, out: Int16Array, outOff: number) {
  // Separable C * X * C^T with identical summation order to tgy-core
  for (let xx = 0; xx < 8; xx++) {
    for (let v = 0; v < 8; v++) {
      let t = 0;
      for (let y = 0; y < 8; y++) t += x[xx * 8 + y] * CMAT[v * 8 + y];
      fdctTmp[xx * 8 + v] = t;
    }
  }
  for (let u = 0; u < 8; u++) {
    for (let v = 0; v < 8; v++) {
      let s = 0;
      for (let xx = 0; xx < 8; xx++) s += CMAT[u * 8 + xx] * fdctTmp[xx * 8 + v];
      const f = s / qnat[u * 8 + v];
      fdctNat[u * 8 + v] = f === 0 ? 0 : (f > 0 ? Math.floor(f + 0.5) : -Math.floor(-f + 0.5));
    }
  }
  for (let k = 0; k < 64; k++) out[outOff + k] = fdctNat[ZZN[k]];
}

type CoefComp = { h: number; v: number; tq: number; blocksW: number; blocksH: number; coef: Int16Array };
type CoefPicture = { width: number; height: number; quant: (Uint16Array | null)[]; components: CoefComp[] };

function encodeRgbToCoef(rgb: Uint8Array, w: number, h: number, quality: number, subsampling: TgySubsampling): CoefPicture {
  const qy = scaledQuantTable(STD_LUMA, quality);
  const qc = scaledQuantTable(STD_CHROMA, quality);
  const qnY = new Float64Array(64);
  const qnC = new Float64Array(64);
  for (let k = 0; k < 64; k++) {
    qnY[ZZN[k]] = qy[k];
    qnC[ZZN[k]] = qc[k];
  }

  const blk = new Float64Array(64);
  const yOf = (yy: number, xx: number) => {
    const o = (Math.min(yy, h - 1) * w + Math.min(xx, w - 1)) * 3;
    return 0.299 * rgb[o] + 0.587 * rgb[o + 1] + 0.114 * rgb[o + 2];
  };
  const cbOf = (yy: number, xx: number) => {
    const o = (Math.min(yy, h - 1) * w + Math.min(xx, w - 1)) * 3;
    return -0.168736 * rgb[o] - 0.331264 * rgb[o + 1] + 0.5 * rgb[o + 2] + 128;
  };
  const crOf = (yy: number, xx: number) => {
    const o = (Math.min(yy, h - 1) * w + Math.min(xx, w - 1)) * 3;
    return 0.5 * rgb[o] - 0.418688 * rgb[o + 1] - 0.081312 * rgb[o + 2] + 128;
  };

  if (subsampling === 'grey' || subsampling === '4:4:4') {
    const bw = Math.ceil(w / 8);
    const bh = Math.ceil(h / 8);
    const yCoef = new Int16Array(bw * bh * 64);
    for (let by = 0; by < bh; by++) {
      for (let bx = 0; bx < bw; bx++) {
        for (let r = 0; r < 8; r++) {
          for (let c = 0; c < 8; c++) blk[r * 8 + c] = yOf(by * 8 + r, bx * 8 + c) - 128;
        }
        fdctQuant(blk, qnY, yCoef, (by * bw + bx) * 64);
      }
    }
    if (subsampling === 'grey') {
      return {
        width: w, height: h, quant: [qy, null, null, null],
        components: [{ h: 1, v: 1, tq: 0, blocksW: bw, blocksH: bh, coef: yCoef }],
      };
    }
    const cbCoef = new Int16Array(bw * bh * 64);
    const crCoef = new Int16Array(bw * bh * 64);
    const blkCr = new Float64Array(64);
    for (let by = 0; by < bh; by++) {
      for (let bx = 0; bx < bw; bx++) {
        for (let r = 0; r < 8; r++) {
          for (let c = 0; c < 8; c++) {
            blk[r * 8 + c] = cbOf(by * 8 + r, bx * 8 + c) - 128;
            blkCr[r * 8 + c] = crOf(by * 8 + r, bx * 8 + c) - 128;
          }
        }
        const off = (by * bw + bx) * 64;
        fdctQuant(blk, qnC, cbCoef, off);
        fdctQuant(blkCr, qnC, crCoef, off);
      }
    }
    return {
      width: w, height: h, quant: [qy, qc, null, null],
      components: [
        { h: 1, v: 1, tq: 0, blocksW: bw, blocksH: bh, coef: yCoef },
        { h: 1, v: 1, tq: 1, blocksW: bw, blocksH: bh, coef: cbCoef },
        { h: 1, v: 1, tq: 1, blocksW: bw, blocksH: bh, coef: crCoef },
      ],
    };
  }

  // 4:2:0 (matches tgy-core encode.rs)
  const mw = Math.ceil(w / 16);
  const mh = Math.ceil(h / 16);
  const cw = Math.ceil(w / 2);
  const ch = Math.ceil(h / 2);
  const ybw = mw * 2;
  const ybh = mh * 2;
  const yCoef = new Int16Array(ybw * ybh * 64);
  const cbCoef = new Int16Array(mw * mh * 64);
  const crCoef = new Int16Array(mw * mh * 64);
  const bcb = new Float64Array(64);
  const bcr = new Float64Array(64);

  for (let my = 0; my < mh; my++) {
    for (let by = 0; by < 2; by++) {
      for (let bx = 0; bx < ybw; bx++) {
        for (let r = 0; r < 8; r++) {
          for (let c = 0; c < 8; c++) {
            blk[r * 8 + c] = yOf(my * 16 + by * 8 + r, bx * 8 + c) - 128;
          }
        }
        fdctQuant(blk, qnY, yCoef, ((my * 2 + by) * ybw + bx) * 64);
      }
    }
    for (let bx = 0; bx < mw; bx++) {
      for (let r = 0; r < 8; r++) {
        for (let c = 0; c < 8; c++) {
          const cy = Math.min(my * 8 + r, ch - 1);
          const cx = Math.min(bx * 8 + c, cw - 1);
          bcb[r * 8 + c] = (
            cbOf(2 * cy, 2 * cx) + cbOf(2 * cy + 1, 2 * cx) +
            cbOf(2 * cy, 2 * cx + 1) + cbOf(2 * cy + 1, 2 * cx + 1)
          ) / 4 - 128;
          bcr[r * 8 + c] = (
            crOf(2 * cy, 2 * cx) + crOf(2 * cy + 1, 2 * cx) +
            crOf(2 * cy, 2 * cx + 1) + crOf(2 * cy + 1, 2 * cx + 1)
          ) / 4 - 128;
        }
      }
      const off = (my * mw + bx) * 64;
      fdctQuant(bcb, qnC, cbCoef, off);
      fdctQuant(bcr, qnC, crCoef, off);
    }
  }

  return {
    width: w, height: h, quant: [qy, qc, null, null],
    components: [
      { h: 2, v: 2, tq: 0, blocksW: ybw, blocksH: ybh, coef: yCoef },
      { h: 1, v: 1, tq: 1, blocksW: mw, blocksH: mh, coef: cbCoef },
      { h: 1, v: 1, tq: 1, blocksW: mw, blocksH: mh, coef: crCoef },
    ],
  };
}

// --- Resizing & RGB565 Thumbnail ---

function roundTiesEven(x: number): number {
  const f = Math.floor(x);
  const frac = x - f;
  if (frac > 0.5) return f + 1;
  if (frac < 0.5) return f;
  return f % 2 === 0 ? f : f + 1;
}

function fitRect(w: number, h: number, cw: number, ch: number, upscale: boolean): [number, number, number, number] {
  let s = Math.min(cw / w, ch / h);
  if (!upscale) s = Math.min(s, 1);
  const rw = Math.max(1, Math.min(cw, roundTiesEven(w * s)));
  const rh = Math.max(1, Math.min(ch, roundTiesEven(h * s)));
  return [Math.floor((cw - rw) / 4) * 2, Math.floor((ch - rh) / 2), rw, rh];
}

function resizeRgbBilinear(src: Uint8Array, sw: number, sh: number, dw: number, dh: number): Uint8Array {
  if (sw === dw && sh === dh) return src.slice();
  const dst = new Uint8Array(dw * dh * 3);
  const scaleX = sw / dw;
  const scaleY = sh / dh;
  for (let y = 0; y < dh; y++) {
    const sy = (y + 0.5) * scaleY - 0.5;
    const y0 = Math.max(0, Math.min(sh - 1, Math.floor(sy)));
    const y1 = Math.min(sh - 1, y0 + 1);
    const fy = Math.max(0, Math.min(1, sy - y0));
    for (let x = 0; x < dw; x++) {
      const sx = (x + 0.5) * scaleX - 0.5;
      const x0 = Math.max(0, Math.min(sw - 1, Math.floor(sx)));
      const x1 = Math.min(sw - 1, x0 + 1);
      const fx = Math.max(0, Math.min(1, sx - x0));
      const o00 = (y0 * sw + x0) * 3;
      const o10 = (y0 * sw + x1) * 3;
      const o01 = (y1 * sw + x0) * 3;
      const o11 = (y1 * sw + x1) * 3;
      const dstOff = (y * dw + x) * 3;
      for (let c = 0; c < 3; c++) {
        const top = src[o00 + c] + (src[o10 + c] - src[o00 + c]) * fx;
        const bot = src[o01 + c] + (src[o11 + c] - src[o01 + c]) * fx;
        dst[dstOff + c] = Math.round(top + (bot - top) * fy);
      }
    }
  }
  return dst;
}

function pasteRgb(dst: Uint8Array, dw: number, src: Uint8Array, sw: number, sh: number, x: number, y: number) {
  for (let r = 0; r < sh; r++) {
    dst.set(src.subarray(r * sw * 3, (r + 1) * sw * 3), ((y + r) * dw + x) * 3);
  }
}

function rgb565Le(rgb: Uint8Array): Uint8Array {
  const out = new Uint8Array((rgb.length / 3) * 2);
  for (let i = 0, o = 0; i < rgb.length; i += 3, o += 2) {
    const v = ((rgb[i] & 0xf8) << 8) | ((rgb[i + 1] & 0xfc) << 3) | (rgb[i + 2] >>> 3);
    out[o] = v & 0xff;
    out[o + 1] = (v >>> 8) & 0xff;
  }
  return out;
}

// --- Container v1 Writer & Reader ---

function align4k(n: number): number {
  return Math.ceil(n / ALIGN) * ALIGN;
}

function tileDims(pic: CoefPicture): [number, number] {
  const hmax = Math.max(...pic.components.map(c => c.h));
  const vmax = Math.max(...pic.components.map(c => c.v));
  const c0 = pic.components[0];
  const pr = (TILE_SIZE / 8) * (c0.v / vmax);
  const pc = (TILE_SIZE / 8) * (c0.h / hmax);
  return [Math.ceil(c0.blocksH / pr), Math.ceil(c0.blocksW / pc)];
}

function tileGrids(pic: CoefPicture, ty: number, tx: number): TgyGrid[] {
  const hmax = Math.max(...pic.components.map(c => c.h));
  const vmax = Math.max(...pic.components.map(c => c.v));
  return pic.components.map(c => {
    const pr = (TILE_SIZE / 8) * (c.v / vmax);
    const pc = (TILE_SIZE / 8) * (c.h / hmax);
    const r0 = ty * pr;
    const c0 = tx * pc;
    const rows = Math.min(pr, c.blocksH - r0);
    const cols = Math.min(pc, c.blocksW - c0);
    const blocks = new Int16Array(rows * cols * 64);
    for (let y = 0; y < rows; y++) {
      for (let x = 0; x < cols; x++) {
        const srcOff = ((r0 + y) * c.blocksW + c0 + x) * 64;
        blocks.set(c.coef.subarray(srcOff, srcOff + 64), (y * cols + x) * 64);
      }
    }
    return { rows, cols, blocks };
  });
}

function subOf(pic: CoefPicture): number {
  if (pic.components.length === 1) return TGY_SUB_GREY;
  return pic.components[0].h === 2 && pic.components[0].v === 2 ? TGY_SUB_420 : TGY_SUB_444;
}

function sameTable(a: Uint16Array, b: Uint16Array): boolean {
  for (let i = 0; i < 64; i++) if (a[i] !== b[i]) return false;
  return true;
}

function writeContainerV1(
  width: number,
  height: number,
  orientation: number,
  thumb565: Uint8Array,
  levels: { kind: number; pic: CoefPicture; rect: [number, number, number, number] }[],
): ArrayBuffer {
  if (thumb565.length !== THUMB_BYTES || !levels.length || levels.length > 16) {
    throw new Error('Invalid TGY container arguments.');
  }
  const tables: Uint16Array[] = [];
  type BuiltLevel = {
    w: number; h: number; rect: [number, number, number, number]; pixfmt: number; kind: number;
    qy: number; qc: number; dataOff: number; dataLen: number; indexOff: number; indexLen: number; indexCrc: number;
    index: Uint8Array; data: Uint8Array;
  };
  const built: BuiltLevel[] = [];

  for (const lv of levels) {
    const pic = lv.pic;
    const sub = subOf(pic);
    let qy = 0, qc = 0;
    for (const [slot, ci] of [[0, 0], [1, 1]] as const) {
      if (slot === 1 && sub === TGY_SUB_GREY) continue;
      const t = pic.quant[pic.components[ci].tq];
      if (!t) throw new Error('Missing TGY quantisation table.');
      let idx = tables.findIndex(x => sameTable(x, t));
      if (idx < 0) {
        idx = tables.length;
        tables.push(t);
      }
      if (slot === 0) qy = idx; else qc = idx;
    }
    if (tables.length > MAX_TABLES) throw new Error('TGY supports at most 8 quantisation tables.');

    const [tRows, tCols] = tileDims(pic);
    const streams: Uint8Array[] = [];
    let totalData = 0;
    for (let ty = 0; ty < tRows; ty++) {
      for (let tx = 0; tx < tCols; tx++) {
        const s = encodeTgyTile(tileGrids(pic, ty, tx));
        streams.push(s);
        totalData += s.length;
      }
    }
    const index = new Uint8Array(streams.length * 12);
    const idxView = new DataView(index.buffer);
    const data = new Uint8Array(totalData);
    let pos = 0;
    for (let i = 0; i < streams.length; i++) {
      const s = streams[i];
      idxView.setUint32(i * 12, pos, true);
      idxView.setUint32(i * 12 + 4, s.length, true);
      idxView.setUint32(i * 12 + 8, crc32(s), true);
      data.set(s, pos);
      pos += s.length;
    }
    built.push({
      w: pic.width, h: pic.height, rect: lv.rect, pixfmt: sub, kind: lv.kind,
      qy, qc, dataOff: 0, dataLen: data.length, indexOff: 0, indexLen: index.length,
      indexCrc: crc32(index), index, data,
    });
  }

  const tablesBlob = new Uint8Array(2 + tables.length * 128);
  const tabView = new DataView(tablesBlob.buffer);
  tabView.setUint16(0, tables.length, true);
  for (let i = 0; i < tables.length; i++) {
    for (let k = 0; k < 64; k++) tabView.setUint16(2 + (i * 64 + k) * 2, tables[i][k], true);
  }

  const ltOff = HEADER_SIZE;
  const tabOff = ltOff + LEVEL_SIZE * built.length;
  const thumbOff = align4k(tabOff + tablesBlob.length);
  let pos = thumbOff + align4k(THUMB_BYTES);
  for (const e of built) {
    if (e.index.length > 0) {
      e.indexOff = pos;
      e.indexLen = e.index.length;
      pos = align4k(pos + e.index.length);
    }
    e.dataOff = pos;
    e.dataLen = e.data.length;
    pos = align4k(pos + e.data.length);
  }
  const fileSize = pos;
  const buf = new Uint8Array(fileSize);
  const view = new DataView(buf.buffer);

  // Header (64 bytes, CRC32 over bytes 0..51)
  buf.set(TGY_MAGIC, 0);
  view.setUint16(4, TGY_VERSION, true);
  view.setUint16(6, HEADER_SIZE, true);
  view.setUint32(8, width, true);
  view.setUint32(12, height, true);
  buf[16] = levels.some(l => l.pic.components.length === 1) ? 1 : 3;
  buf[17] = 0;
  buf[18] = orientation;
  buf[19] = 0;
  view.setUint32(20, built.length, true);
  view.setUint32(24, ltOff, true);
  view.setUint32(28, thumbOff, true);
  view.setUint32(32, THUMB_BYTES, true);
  view.setUint16(36, THUMB_W, true);
  view.setUint16(38, THUMB_H, true);
  view.setUint32(40, tabOff, true);
  view.setUint32(44, tablesBlob.length, true);
  view.setUint32(48, fileSize, true);
  view.setUint32(52, crc32(buf.subarray(0, 52)), true);

  for (let i = 0; i < built.length; i++) {
    const e = built[i];
    const o = ltOff + i * LEVEL_SIZE;
    view.setUint32(o, e.w, true);
    view.setUint32(o + 4, e.h, true);
    view.setUint16(o + 8, e.rect[0], true);
    view.setUint16(o + 10, e.rect[1], true);
    view.setUint16(o + 12, e.rect[2], true);
    view.setUint16(o + 14, e.rect[3], true);
    buf.set([1, 1, e.pixfmt, e.kind, e.qy, e.qc, 7, 0], o + 16);
    view.setUint32(o + 24, e.dataOff, true);
    view.setUint32(o + 28, e.dataLen, true);
    view.setUint32(o + 32, e.indexOff, true);
    view.setUint32(o + 36, e.indexLen, true);
    view.setUint32(o + 40, e.indexCrc, true);
  }

  buf.set(tablesBlob, tabOff);
  buf.set(thumb565, thumbOff);
  for (const e of built) {
    buf.set(e.index, e.indexOff);
    buf.set(e.data, e.dataOff);
  }
  return buf.buffer;
}

export function isTgy(bytes: ArrayBuffer): boolean {
  if (bytes.byteLength < HEADER_SIZE) return false;
  const u = new Uint8Array(bytes, 0, 6);
  return u[0] === 0x54 && u[1] === 0x47 && u[2] === 0x59 && u[3] === 0x00 && u[4] === 1 && u[5] === 0;
}

function reconstructComponentPlane(
  blocks: Int16Array,
  blocksW: number,
  blocksH: number,
  qzz: Uint16Array,
  tw: number,
  th: number,
): Uint8Array {
  const qNat = new Int32Array(64);
  for (let k = 0; k < 64; k++) qNat[ZZN[k]] = qzz[k];
  const stride = blocksW * 8;
  const plane = new Uint8Array(stride * blocksH * 8);
  const nat = new Int32Array(64);
  const px = new Uint8Array(64);

  for (let by = 0; by < blocksH; by++) {
    for (let bx = 0; bx < blocksW; bx++) {
      const off = (by * blocksW + bx) * 64;
      for (let k = 0; k < 64; k++) nat[ZZN[k]] = blocks[off + k] * qNat[ZZN[k]];
      idctIslow(nat, px);
      for (let r = 0; r < 8; r++) {
        plane.set(px.subarray(r * 8, r * 8 + 8), (by * 8 + r) * stride + bx * 8);
      }
    }
  }
  if (tw === stride && th === blocksH * 8) return plane;
  const out = new Uint8Array(tw * th);
  for (let y = 0; y < th; y++) out.set(plane.subarray(y * stride, y * stride + tw), y * tw);
  return out;
}

export function decodeTgy(bytes: ArrayBuffer, levelIndex?: number): TgyDecoded {
  if (bytes.byteLength < HEADER_SIZE) throw new Error('TGY file is truncated.');
  const buf = new Uint8Array(bytes);
  const view = new DataView(bytes);
  if (buf[0] !== 0x54 || buf[1] !== 0x47 || buf[2] !== 0x59 || buf[3] !== 0x00) {
    throw new Error('Invalid TGY magic.');
  }
  const version = view.getUint16(4, true);
  const headerSize = view.getUint16(6, true);
  if (version !== TGY_VERSION || headerSize !== HEADER_SIZE) {
    throw new Error(`Unsupported TGY version ${version}.`);
  }
  const width = view.getUint32(8, true);
  const height = view.getUint32(12, true);
  const channels = buf[16];
  const orientation = buf[18];
  const levelCount = view.getUint32(20, true);
  const ltOff = view.getUint32(24, true);
  const tabOff = view.getUint32(40, true);
  const tabLen = view.getUint32(44, true);
  const fileSize = view.getUint32(48, true);
  const headerCrc = view.getUint32(52, true);
  if (fileSize !== bytes.byteLength) throw new Error('TGY file size mismatch.');
  if (crc32(buf.subarray(0, 52)) !== headerCrc) throw new Error('TGY header CRC mismatch.');

  const qCount = view.getUint16(tabOff, true);
  if (qCount > MAX_TABLES || tabOff + 2 + qCount * 128 > tabOff + tabLen) {
    throw new Error('Invalid TGY quantisation table section.');
  }
  const tables: Uint16Array[] = [];
  for (let i = 0; i < qCount; i++) {
    const q = new Uint16Array(64);
    for (let k = 0; k < 64; k++) q[k] = view.getUint16(tabOff + 2 + (i * 64 + k) * 2, true);
    tables.push(q);
  }

  const levels: TgyLevelHeader[] = [];
  for (let i = 0; i < levelCount; i++) {
    const o = ltOff + i * LEVEL_SIZE;
    levels.push({
      width: view.getUint32(o, true),
      height: view.getUint32(o + 4, true),
      rect: [view.getUint16(o + 8, true), view.getUint16(o + 10, true), view.getUint16(o + 12, true), view.getUint16(o + 14, true)],
      layout: buf[o + 16],
      mode: buf[o + 17],
      pixelFormat: buf[o + 18],
      kind: buf[o + 19],
      qy: buf[o + 20],
      qc: buf[o + 21],
      tileSizeLog2: buf[o + 22],
      flags: buf[o + 23],
      dataOffset: view.getUint32(o + 24, true),
      dataLength: view.getUint32(o + 28, true),
      tileIndexOffset: view.getUint32(o + 32, true),
      tileIndexLength: view.getUint32(o + 36, true),
      tileIndexCrc: view.getUint32(o + 40, true),
    });
  }

  // Pick requested level, or prefer KIND_FULL, else level 0
  const chosenIdx = levelIndex !== undefined
    ? levelIndex
    : Math.max(0, levels.findIndex(l => l.kind === TGY_KIND_FULL));
  const lv = levels[chosenIdx];
  if (!lv || lv.mode !== 1 || lv.layout !== 1) throw new Error('Unsupported TGY level mode.');

  const idxSlice = buf.subarray(lv.tileIndexOffset, lv.tileIndexOffset + lv.tileIndexLength);
  if (crc32(idxSlice) !== lv.tileIndexCrc) throw new Error('TGY tile index CRC mismatch.');

  const tileSize = 1 << lv.tileSizeLog2;
  const sub = lv.pixelFormat;
  const is420 = sub === TGY_SUB_420;
  const isGrey = sub === TGY_SUB_GREY;
  const mw = is420 ? Math.ceil(lv.width / 16) : Math.ceil(lv.width / 8);
  const mh = is420 ? Math.ceil(lv.height / 16) : Math.ceil(lv.height / 8);
  const yBlocksW = is420 ? mw * 2 : mw;
  const yBlocksH = is420 ? mh * 2 : mh;
  const cBlocksW = mw;
  const cBlocksH = mh;
  const prY = tileSize / 8;
  const pcY = tileSize / 8;
  const prC = is420 ? prY / 2 : prY;
  const pcC = is420 ? pcY / 2 : pcY;
  const tCols = Math.ceil(yBlocksW / pcY);
  const tRows = Math.ceil(yBlocksH / prY);

  const yCoef = new Int16Array(yBlocksW * yBlocksH * 64);
  const cbCoef = isGrey ? null : new Int16Array(cBlocksW * cBlocksH * 64);
  const crCoef = isGrey ? null : new Int16Array(cBlocksW * cBlocksH * 64);

  for (let ty = 0; ty < tRows; ty++) {
    for (let tx = 0; tx < tCols; tx++) {
      const ti = ty * tCols + tx;
      const tOff = view.getUint32(lv.tileIndexOffset + ti * 12, true);
      const tLen = view.getUint32(lv.tileIndexOffset + ti * 12 + 4, true);
      const tCrc = view.getUint32(lv.tileIndexOffset + ti * 12 + 8, true);
      const stream = buf.subarray(lv.dataOffset + tOff, lv.dataOffset + tOff + tLen);
      if (crc32(stream) !== tCrc) throw new Error(`TGY tile ${ti} CRC mismatch.`);

      const rowsY = Math.min(prY, yBlocksH - ty * prY);
      const colsY = Math.min(pcY, yBlocksW - tx * pcY);
      const shapes: [number, number][] = [[rowsY, colsY]];
      if (!isGrey) {
        const rowsC = Math.min(prC, cBlocksH - ty * prC);
        const colsC = Math.min(pcC, cBlocksW - tx * pcC);
        shapes.push([rowsC, colsC], [rowsC, colsC]);
      }
      const grids = decodeTgyTile(stream, shapes);

      const r0Y = ty * prY, c0Y = tx * pcY;
      for (let r = 0; r < rowsY; r++) {
        for (let c = 0; c < colsY; c++) {
          const sOff = (r * colsY + c) * 64;
          yCoef.set(grids[0].blocks.subarray(sOff, sOff + 64), ((r0Y + r) * yBlocksW + c0Y + c) * 64);
        }
      }
      if (!isGrey && cbCoef && crCoef) {
        const r0C = ty * prC, c0C = tx * pcC;
        const rowsC = grids[1].rows, colsC = grids[1].cols;
        for (let r = 0; r < rowsC; r++) {
          for (let c = 0; c < colsC; c++) {
            const sOff = (r * colsC + c) * 64;
            const dOff = ((r0C + r) * cBlocksW + c0C + c) * 64;
            cbCoef.set(grids[1].blocks.subarray(sOff, sOff + 64), dOff);
            crCoef.set(grids[2].blocks.subarray(sOff, sOff + 64), dOff);
          }
        }
      }
    }
  }

  const yPlane = reconstructComponentPlane(yCoef, yBlocksW, yBlocksH, tables[lv.qy], lv.width, lv.height);
  if (isGrey) {
    const out = new Uint8ClampedArray(lv.width * lv.height * 4);
    for (let i = 0; i < yPlane.length; i++) {
      const v = yPlane[i];
      const o = i * 4;
      out[o] = v; out[o + 1] = v; out[o + 2] = v; out[o + 3] = 255;
    }
    return { width: lv.width, height: lv.height, channels, orientation, levels, data: out };
  }

  const cw = is420 ? Math.ceil(lv.width / 2) : lv.width;
  const ch = is420 ? Math.ceil(lv.height / 2) : lv.height;
  let cbPlane = reconstructComponentPlane(cbCoef!, cBlocksW, cBlocksH, tables[lv.qc], cw, ch);
  let crPlane = reconstructComponentPlane(crCoef!, cBlocksW, cBlocksH, tables[lv.qc], cw, ch);
  if (is420) {
    const upCb = upsampleH2v2Tile(cbPlane, cw, ch, tileSize / 2);
    const upCr = upsampleH2v2Tile(crPlane, cw, ch, tileSize / 2);
    const pw = cw * 2;
    if (pw !== lv.width || ch * 2 !== lv.height) {
      cbPlane = new Uint8Array(lv.width * lv.height);
      crPlane = new Uint8Array(lv.width * lv.height);
      for (let y = 0; y < lv.height; y++) {
        cbPlane.set(upCb.subarray(y * pw, y * pw + lv.width), y * lv.width);
        crPlane.set(upCr.subarray(y * pw, y * pw + lv.width), y * lv.width);
      }
    } else {
      cbPlane = upCb;
      crPlane = upCr;
    }
  }
  const rgba = yccToRgba(yPlane, cbPlane, crPlane);
  return { width: lv.width, height: lv.height, channels, orientation, levels, data: rgba };
}

export function encodeTgy(raster: RgbaRaster, options: TgyEncodeOptions = {}): { bytes: ArrayBuffer; preview: Uint8ClampedArray<ArrayBuffer> } {
  const { width, height, data } = raster;
  const quality = Math.max(1, Math.min(100, Math.round(options.quality ?? 92)));
  const fitQuality = Math.max(1, Math.min(100, Math.round(options.fitQuality ?? 90)));
  const subsampling: TgySubsampling = options.subsampling ?? '4:2:0';
  const fitCanvas = options.fitCanvas ?? true;
  const upscale = options.upscale ?? false;

  // Flatten RGBA onto black into RGB888 (Tau Gallery RGB565/YCbCr opaque display)
  const rgb = new Uint8Array(width * height * 3);
  for (let i = 0, o = 0; i < data.length; i += 4, o += 3) {
    const a = data[i + 3];
    if (a === 255) {
      rgb[o] = data[i]; rgb[o + 1] = data[i + 1]; rgb[o + 2] = data[i + 2];
    } else {
      rgb[o] = Math.round((data[i] * a) / 255);
      rgb[o + 1] = Math.round((data[i + 1] * a) / 255);
      rgb[o + 2] = Math.round((data[i + 2] * a) / 255);
    }
  }

  // 96x72 RGB565 thumbnail
  const [, , tw, th] = fitRect(width, height, THUMB_W, THUMB_H, false);
  const thumbResized = resizeRgbBilinear(rgb, width, height, tw, th);
  const thumbCanvas = new Uint8Array(THUMB_W * THUMB_H * 3);
  pasteRgb(thumbCanvas, THUMB_W, thumbResized, tw, th, Math.floor((THUMB_W - tw) / 2), Math.floor((THUMB_H - th) / 2));
  const thumb565 = rgb565Le(thumbCanvas);

  const levels: { kind: number; pic: CoefPicture; rect: [number, number, number, number] }[] = [];
  if (fitCanvas) {
    const [fx, fy, fw, fh] = fitRect(width, height, CANVAS_W, CANVAS_H, upscale);
    const fitResized = resizeRgbBilinear(rgb, width, height, fw, fh);
    const canvas = new Uint8Array(CANVAS_W * CANVAS_H * 3);
    pasteRgb(canvas, CANVAS_W, fitResized, fw, fh, fx, fy);
    const fitPic = encodeRgbToCoef(canvas, CANVAS_W, CANVAS_H, fitQuality, subsampling);
    levels.push({ kind: TGY_KIND_FIT, pic: fitPic, rect: [0, 0, CANVAS_W, CANVAS_H] });
    if (width > CANVAS_W || height > CANVAS_H) {
      const fullPic = encodeRgbToCoef(rgb, width, height, quality, subsampling);
      levels.push({ kind: TGY_KIND_FULL, pic: fullPic, rect: [0, 0, Math.min(width, 65535), Math.min(height, 65535)] });
    }
  } else {
    const fullPic = encodeRgbToCoef(rgb, width, height, quality, subsampling);
    levels.push({ kind: TGY_KIND_FULL, pic: fullPic, rect: [0, 0, Math.min(width, 65535), Math.min(height, 65535)] });
  }

  const bytes = writeContainerV1(width, height, 1, thumb565, levels);
  // Decode full-res level (or fit level cropped back if smaller) for preview
  const decoded = decodeTgy(bytes);
  let preview = decoded.data;
  if (decoded.width !== width || decoded.height !== height) {
    // Source was <= 800x720 with fitCanvas=true: extract the content rect or return exact source-sized preview
    const [fx, fy, fw, fh] = fitRect(width, height, CANVAS_W, CANVAS_H, upscale);
    if (fw === width && fh === height) {
      const cropped = new Uint8ClampedArray(width * height * 4);
      for (let y = 0; y < height; y++) {
        const sOff = ((fy + y) * CANVAS_W + fx) * 4;
        cropped.set(decoded.data.subarray(sOff, sOff + width * 4), y * width * 4);
      }
      preview = cropped;
    } else {
      preview = decoded.data;
    }
  }
  return { bytes, preview };
}
