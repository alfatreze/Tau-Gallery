# `@tau-gallery/tgy` (`tools/tgy-ts`)

Zero-dependency TypeScript implementation of the **Tau Gallery (`.tgy`)** image format for browsers, Node.js, and embedded JS/TS tools:

- **TVC v0 binary range coder** (`32`-bit range, `15`-bit probabilities, carry-safe `low`, `5`-byte flush)
- **TGY Symbol Layer v0** (`1,250` adaptive probability contexts, `128 × 128` independent tiles, DC prediction, 6-bit last-index binary tree, AC significance/gt1/Exp-Golomb/sign)
- **TGY Reconstruction v0** (`jpeg_idct_islow`, tile-local 4:2:0 fancy upsampling, integer JFIF YCbCr → RGB888)
- **TGY Container v1** (`TGY\0`, `4 KB` aligned sections, `64`-byte header + IEEE CRC-32, `96 × 72` RGB565 LE thumbnail, tile index + per-tile CRC-32)

## Conformance

Verified bit-exact against all three frozen conformance suites in `docs/conformance/`:

- `docs/conformance/tgy_v0_vectors.json` (tile encode & decode byte-for-byte)
- `docs/conformance/tgy_recon_v0_vectors.json` (`idctIslow`, `upsampleH2v2Fancy`, `yccToRgba`)
- `docs/conformance/tgy_container_v1_vectors.json` (full container v1 decode SHA-256)

## Usage

```ts
import { encodeTgy, decodeTgy, isTgy } from './src/index.ts';

// Encode an RGBA raster to a TGY Container v1 ArrayBuffer
const { bytes, preview } = encodeTgy(
  { width, height, data: rgba },
  {
    quality: 92,          // full-level DCT quality (1–100)
    fitQuality: 90,       // 800×720 fit-canvas DCT quality (1–100)
    subsampling: '4:2:0', // '4:2:0' | '4:4:4' | 'grey'
    fitCanvas: true,      // include 800×720 Analogue Pocket fit level
    upscale: false,       // do not enlarge images smaller than 800×720
  },
);

// Inspect & decode a TGY v1 file back to RGBA
if (isTgy(bytes)) {
  const decoded = decodeTgy(bytes);
  // decoded.width, decoded.height, decoded.levels, decoded.data (Uint8ClampedArray)
}
```

## Running Tests

Requires Node.js ≥ 22.6 (no `npm install` needed):

```sh
cd tools/tgy-ts
npm test
```
