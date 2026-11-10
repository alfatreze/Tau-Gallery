# TGY symbol layer, version 0 (FROZEN 2026-11-10)

Normative. Reference implementation: `tools/gallery/tgy_v0.py`. Conformance vectors: `docs/conformance/tgy_v0_vectors.json` (`tools/gallery/make_tgy_v0_vectors.py` writes them, `sim/test_tgy_v0.py` checks them; `make test-tgy-v0`). Measurement and reasons: `docs/M3_STREAMING_NOTE.md` sections 11, 13 and 14 (this model is 0.933 of the progressive-Huffman size of the same coefficients on the five test photos; measured, not a promise for other pictures).

## 1. Scope
This layer turns the quantised DCT coefficients of ONE tile into ONE byte stream and back, losslessly. It does not define the container, the quantisation tables, the colour conversion, the inverse transform, the pyramid, the fit level, per-tile checksums or the band layout: those are later parts of the TGY specification. Both TGY and TGX use the same range coder; this layer is TGY's.

**Change control.** Anything that changes a bit of any stream (a context, a table, a rate, an order) is a new version number, never an edit of this version. The reference module, the vectors and this text change together or not at all.

## 2. Tile and blocks
A tile covers 128 x 128 luma samples. Its components are coded in the order Y, Cb, Cr (a grey picture has only Y). Each component is a grid of 8 x 8 blocks, coded in raster order (row by row, left to right): for 4:2:0 the Y grid is 16 x 16 blocks and each chroma grid 8 x 8; for 4:4:4 all three are 16 x 16. A block is 64 quantised coefficients in **zigzag order** (index 0 is DC; the natural-order position of zigzag index k is `ZZN[k]`, the table of JPEG). Component class `cc` is 0 for Y and 1 for Cb and Cr (Cb and Cr share contexts). Coefficient magnitudes: DC difference magnitude below 2^12, AC magnitude at most 1,023 (JPEG's 8-bit ranges); the code is correct up to Exp-Golomb exponent 24, the contexts cap at 9.

## 3. Range coder
The TVC range coder of Tau codecs (`docs/TVC_BITSTREAM_V0.md` section 3, `tau-video/src/tvc/range.rs`), frozen here by copy: 32-bit `range` and `code`; probabilities are 15-bit values `p` of a 0 bin; `bound = (range >> 15) * p`; decode: bin = 1 iff `code >= bound` (then `code -= bound; range -= bound`, else `range = bound`); the adaptation is NOT the TVC shift-5 update but section 4; renormalise while `range < 2^24` (`range <<= 8`, `code = (code << 8) | next byte`, bytes past the end of the stream read as 0); the decoder starts with `range = 0xFFFFFFFF` and five bytes into `code`; the encoder keeps `low` (33 bits), a cache byte and a pending count, propagates carries, and ends with five `shift_low` steps (the stream is five bytes longer than the information strictly needs). A **bypass bin** halves `range` (`range >>= 1`), codes the bit as `code >= range` (subtracting when 1), then renormalises. Reference: `tools/gallery/tvcrc.py` (bit-exact with the Rust implementation, `make test-tvcrc`).

## 4. The adaptive probability engine
There are **N_CTX = 1,250 contexts**, each a probability `p` (15 bit, start 16,384) and a count `n` (start 0, saturating at LIMIT = 30). After every context-coded bin `b` of context `c`:
`r = REC[n]`; if `b = 1`: `p = p - ((p * r) >> 16)`, else `p = p + (((32768 - p) * r) >> 16)`; then `p` is clamped to 32 .. 32,736; then `n = min(n + 1, 30)`.
`REC[n] = min(32768, floor(65536 / (n + 1.5)))` for n = 0 .. 30: 32768, 26214, 18724, 14563, 11915, 10082, 8738, 7710, 6898, 6241, 5698, 5242, 4854, 4519, 4228, 3971, 3744, 3542, 3360, 3196, 3048, 2912, 2788, 2674, 2570, 2473, 2383, 2299, 2221, 2148, 2080 (the vectors file carries the table). All contexts are reset at the start of every tile.

## 5. Context numbering (flat index, offsets are the numbering of the vectors and of implementations)
| Symbol | Index | Count |
|---|---|---|
| DC zero flag | `0 + cc` | 2 |
| DC sign | `2 + cc` | 2 |
| DC exponent step i | `4 + cc*10 + min(i, 9)` | 20 |
| DC top mantissa bit, exponent e | `24 + cc*10 + min(e, 9)` | 20 |
| last-index tree node `node` (1 .. 63) | `44 + cc*63 + node - 1` | 126 |
| significance flag, position k, local class lc | `170 + (cc*13 + KG[k])*5 + lc` | 130 |
| greater-than-1 flag | `300 + (cc*13 + KG[k])*5 + lc` | 130 |
| magnitude exponent step i | `430 + (((cc*8 + min(KG[k], 7))*5 + lc)*10) + min(i, 9)` | 800 |
| magnitude top mantissa bit, exponent e | `1230 + cc*10 + min(e, 9)` | 20 |

`KG[k]` (k = 1 .. 63) is the number of thresholds in (2, 3, 4, 5, 7, 9, 12, 16, 21, 28, 37, 49) that are <= k: a group 0 .. 12. The **local class** `lc` of position k uses the two neighbours of coefficient k in the same block, in natural order: the left coefficient (natural index - 1, absent in column 0) and the upper coefficient (natural index - 8, absent in row 0); both are always coded before k. With `s` the sum of their magnitudes (one missing: the other's magnitude; both missing: lc = 0): `lc = 0` if s = 0, 1 if s = 1, 2 if s = 2, 3 if s <= 4, else 4. The table of the neighbours' zigzag indices is `LN` in the vectors file.

## 6. Coding a block (`left` and `above` are the already coded neighbouring blocks of the SAME component inside the tile)
1. **DC.** `pred` = the DC of `left`, else of `above`, else 0 (never across the tile edge). `d = DC - pred`. A context bin `d != 0` (DC zero flag); if set: a context bin `d < 0` (DC sign), then `|d| - 1` in **Exp-Golomb order 0** with the DC contexts. The block's DC is `pred + d`.
2. **Last index.** `last` = the zigzag index (1 .. 63) of the last nonzero AC coefficient, 0 if all AC coefficients are zero. Six context bins, most significant bit first: `node` starts at 1; for each bit `node = node*2 + bit` with the tree contexts; `last = node - 64`.
3. **Coefficients.** For k = 1 .. last: compute `lc`. If k < last, a context bin significance (coefficient nonzero?), 0 skips the rest of the loop body; at k = last the coefficient is known to be nonzero and no flag is coded. For a nonzero coefficient of magnitude a: a context bin `a > 1` (greater-than-1 flag); if set, `a - 2` in Exp-Golomb order 0 with the magnitude contexts (position group `min(KG[k], 7)` and `lc` select the exponent contexts); the magnitude is 1 otherwise. Then the sign as one **bypass** bin (1 = negative).

**Exp-Golomb order 0** of m >= 0: let `e = floor(log2(m + 1))`; the exponent in unary, `e` context bins of 1 then a 0, step i using the exponent context of index `min(i, 9)`; then the `e` low bits of `m + 1`, most significant first: the first of them is a context bin (the top-mantissa context of index `min(e, 9)`), all the others are bypass bins.

## 7. Conformance
An implementation conforms if, for every vector of `tgy_v0_vectors.json`, decoding `stream` with the vector's `shapes` gives exactly `blocks`, and encoding `blocks` gives exactly `stream`. The vectors cover an empty tile, extreme values (DC +-2047, AC +-1023, a lone last coefficient, fully populated blocks), synthetic tiles of several shapes, and two real photo tiles (one dense, one sparse). `sim/test_tgy_v0.py --mutants` shows that 21 plausible one-line deviations are each caught by the vectors.

## 8. Notes for hardware and firmware (estimates unless marked)
- State: 1,250 contexts x (15 + 5 bits) = 25,000 bits (about 3 M10K blocks, or registers/MLAB for the small groups).
- Per block the decoder holds the 64 coefficients of the block being decoded and the left and upper blocks' DC values (one DC per block of the current and the previous block row of the component); there are **no coefficient line buffers** (measured value of neighbour contexts: none, `docs/M3_STREAMING_NOTE.md` section 13).
- Measured decision counts per tile on the five photos: 24,000 to 89,000 context bins (mean 42,000) and 7,000 to 33,000 bypass bins; at 3 clock cycles per context bin a mean tile takes about 1.3 ms at 100 MHz (an estimate: the hardware does not exist yet).
- The decode is serial inside a tile; parallelism is across tiles.

## 9. Not defined here (to be specified next)
The quantisation tables and their storage; the colour transform and chroma upsampling; the normative inverse transform (libjpeg islow) and rounding; padding of partial blocks; the per-tile offsets, lengths and CRC; the band layout and the fit level; how a JPEG's coefficients become a tile stream (the converter); the encoder's quantisation for pixel sources (rate-distortion optimisation); TGX's symbol layer.
