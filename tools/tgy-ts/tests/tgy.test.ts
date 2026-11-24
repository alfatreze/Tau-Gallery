import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';
import {
  TGY_KIND_FIT,
  TGY_KIND_FULL,
  TGY_SUB_420,
  TGY_SUB_444,
  TGY_SUB_GREY,
  decodeTgy,
  decodeTgyTile,
  encodeTgy,
  encodeTgyTile,
  idctIslow,
  isTgy,
  type TgyGrid,
} from '../src/index.ts';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const CONFORMANCE_DIR = path.resolve(HERE, '../../../docs/conformance');

function unpackV0Blocks(b64: string, shapes: readonly [number, number][]): TgyGrid[] {
  const raw = Buffer.from(b64, 'base64');
  const view = new DataView(raw.buffer, raw.byteOffset, raw.byteLength);
  let pos = 0;
  return shapes.map(([rows, cols]) => {
    const count = rows * cols * 64;
    const blocks = new Int16Array(count);
    for (let i = 0; i < count; i++) {
      blocks[i] = view.getInt16(pos, true);
      pos += 2;
    }
    return { rows, cols, blocks };
  });
}

function gradientRaster(width: number, height: number) {
  const data = new Uint8ClampedArray(width * height * 4);
  for (let y = 0; y < height; y++) {
    for (let x = 0; x < width; x++) {
      const o = (y * width + x) * 4;
      data[o] = Math.round((x / Math.max(1, width - 1)) * 255);
      data[o + 1] = Math.round((y / Math.max(1, height - 1)) * 255);
      data[o + 2] = Math.round(((x + y) / Math.max(1, width + height - 2)) * 255);
      data[o + 3] = 255;
    }
  }
  return { width, height, data };
}

test('conformance: docs/conformance/tgy_v0_vectors.json (bit-exact encode & decode)', () => {
  const vec = JSON.parse(fs.readFileSync(path.join(CONFORMANCE_DIR, 'tgy_v0_vectors.json'), 'utf8'));
  assert.ok(vec.vectors.length >= 9);
  for (const tc of vec.vectors) {
    const shapes = tc.shapes as [number, number][];
    const grids = unpackV0Blocks(tc.blocks, shapes);
    const encoded = encodeTgyTile(grids);
    assert.equal(Buffer.from(encoded).toString('hex'), tc.stream, `encode mismatch on ${tc.name}`);

    const decoded = decodeTgyTile(Buffer.from(tc.stream, 'hex'), shapes);
    assert.equal(decoded.length, grids.length, `grid count mismatch on ${tc.name}`);
    for (let ci = 0; ci < grids.length; ci++) {
      assert.deepEqual(decoded[ci].blocks, grids[ci].blocks, `decode mismatch on ${tc.name} comp ${ci}`);
    }
  }
});

test('conformance: docs/conformance/tgy_recon_v0_vectors.json (bit-exact IDCT)', () => {
  const recon = JSON.parse(fs.readFileSync(path.join(CONFORMANCE_DIR, 'tgy_recon_v0_vectors.json'), 'utf8'));
  assert.ok(recon.idct.length > 0);
  for (const tc of recon.idct) {
    const out = new Uint8Array(64);
    idctIslow(Int32Array.from(tc.dequantised_natural), out);
    assert.deepEqual([...out], tc.samples, `IDCT mismatch on ${tc.name}`);
  }
});

test('conformance: docs/conformance/tgy_container_v1_vectors.json (SHA-256 bit-exact decode)', () => {
  const cont = JSON.parse(fs.readFileSync(path.join(CONFORMANCE_DIR, 'tgy_container_v1_vectors.json'), 'utf8'));
  assert.ok(cont.files.length >= 2);
  for (const f of cont.files) {
    const raw = Buffer.from(f.file_b64, 'base64');
    const ab = raw.buffer.slice(raw.byteOffset, raw.byteOffset + raw.byteLength);
    assert.equal(isTgy(ab), true, `isTgy failed on ${f.name}`);
    for (let li = 0; li < f.levels.length; li++) {
      const dec = decodeTgy(ab, li);
      const rgb = new Uint8Array(dec.width * dec.height * 3);
      for (let i = 0, o = 0; i < dec.data.length; i += 4, o += 3) {
        rgb[o] = dec.data[i];
        rgb[o + 1] = dec.data[i + 1];
        rgb[o + 2] = dec.data[i + 2];
      }
      const sha = crypto.createHash('sha256').update(rgb).digest('hex');
      assert.equal(sha, f.level_rgb_sha256_tgy_profile[li], `SHA-256 mismatch on ${f.name} level ${li}`);
    }
  }
});

test('encodeTgy & decodeTgy round-trip single full level (4:2:0, 4:4:4, grey)', () => {
  const src = gradientRaster(160, 120);
  for (const [subsampling, expectedSub] of [
    ['4:2:0', TGY_SUB_420],
    ['4:4:4', TGY_SUB_444],
    ['grey', TGY_SUB_GREY],
  ] as const) {
    const { bytes, preview } = encodeTgy(src, { fitCanvas: false, quality: 92, subsampling });
    assert.equal(isTgy(bytes), true);
    assert.equal(bytes.byteLength % 4096, 0);
    assert.equal(preview.length, src.width * src.height * 4);

    const decoded = decodeTgy(bytes);
    assert.equal(decoded.width, src.width);
    assert.equal(decoded.height, src.height);
    assert.equal(decoded.levels.length, 1);
    assert.equal(decoded.levels[0].kind, TGY_KIND_FULL);
    assert.equal(decoded.levels[0].pixelFormat, expectedSub);

    let errSum = 0;
    for (let i = 0; i < src.data.length; i += 4) {
      if (subsampling === 'grey') {
        assert.equal(decoded.data[i], decoded.data[i + 1]);
        assert.equal(decoded.data[i], decoded.data[i + 2]);
      } else {
        errSum += Math.abs(decoded.data[i] - src.data[i]) +
          Math.abs(decoded.data[i + 1] - src.data[i + 1]) +
          Math.abs(decoded.data[i + 2] - src.data[i + 2]);
      }
    }
    if (subsampling !== 'grey') {
      const mae = errSum / (src.width * src.height * 3);
      assert.ok(mae < 3.0, `Expected MAE < 3.0 for ${subsampling}, got ${mae}`);
    }
  }
});

test('encodeTgy writes 800x720 fit canvas and adds full level when source exceeds canvas', () => {
  const small = encodeTgy(gradientRaster(64, 48), { fitCanvas: true, fitQuality: 90 });
  const decSmall = decodeTgy(small.bytes);
  assert.equal(decSmall.levels.length, 1);
  assert.equal(decSmall.levels[0].kind, TGY_KIND_FIT);
  assert.equal(decSmall.levels[0].width, 800);
  assert.equal(decSmall.levels[0].height, 720);

  const large = encodeTgy(gradientRaster(816, 400), { fitCanvas: true, quality: 90, fitQuality: 85 });
  const decLarge = decodeTgy(large.bytes);
  assert.equal(decLarge.levels.length, 2);
  assert.equal(decLarge.levels[0].kind, TGY_KIND_FIT);
  assert.equal(decLarge.levels[1].kind, TGY_KIND_FULL);
  assert.equal(decLarge.width, 816);
  assert.equal(decLarge.height, 400);
});

test('decodeTgy rejects corrupted header and tile CRCs', () => {
  const { bytes } = encodeTgy(gradientRaster(64, 64), { fitCanvas: false });
  const badHeader = bytes.slice(0);
  new Uint8Array(badHeader)[8] ^= 0xff;
  assert.throws(() => decodeTgy(badHeader), /header CRC mismatch/i);

  const badTile = bytes.slice(0);
  const view = new DataView(badTile);
  const dataOff = view.getUint32(64 + 24, true);
  new Uint8Array(badTile)[dataOff] ^= 0xff;
  assert.throws(() => decodeTgy(badTile), /tile 0 CRC mismatch/i);
});
