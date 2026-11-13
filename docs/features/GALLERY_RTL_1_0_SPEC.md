# Tau Gallery 1.0: the non-format RTL batch (K1 to K6), CORE_VERSION rev 30 = 0x4D50331E

Status 2026-11-13: **built and simulated; not synthesised as a whole, not fitted, never run on a Pocket.** Evidence per block is in `docs/AUDIT_TRAIL.md` (B-1111 and the entry that follows it). The macro bundle of the core is `tools/gallery_m1f_qsf_append.txt` (the m1d bundle plus `TAU_NO_SDA`; the PSRAM window and instruction fetch stay in: owner's decision 2026-11-13, the cold image is how Tau Alpha freed resources; the first bundle, m1e, had left them out and its fit was stopped). The owner's decisions behind the batch are in `docs/GALLERY_1_0_PLAN.md` section 4.

## Blocks
| Id | What | Where | Registers / ops |
|---|---|---|---|
| K1 | Ordered dither (8x8 Bayer, off / ordered / ordered + temporal), applied in the viewer scan-out before the Pocket's panel truncates 8 to 5/6/5 bits | `tau_pixfx.sv` in `tau_scanout_v.sv` | PIX_CTRL |
| K5 | Per-channel colour tables (256 x 8 each, identity at power-up): brightness, contrast, gamma, tints, invert | `tau_pixfx.sv` | PIX_CTRL, PIX_LUT |
| K3 | Rounded-corner mask: paints only the four corner cuts of a rounded rectangle over what is there | `tau_talos.sv` | `OP_CORNERS` = 10 |
| K4 | Four 256-bin histograms (luma, R, G, B) counted from the decoder's pixel stream, in a window of the canvas | `tau_hist.sv` (core), `tau_hist_if.sv` (registers + clock crossings), tap in `tau_tgy_dcore/dma/unit` | HIST regs |
| K2 | Scaled blit **down** (3:4, 2:3, 1:2, 1:3 per axis), each output pixel the rounded average of a 2 x 2 source box | `tau_talos.sv` | `OP_DSBLIT` = 11 |
| K6 | 4:4:4 chroma in the hardware TGY decoder (`C444 = 1` in `core_game.vh`) | `tau_tgy_*` | geometry register fmt = 1 |
| K7 | Rotation and mirror of the decoded picture: the DMA's scatter writer puts each pixel at `dst + x * stx + y * sty` (signed byte steps), so any of the 8 orientations is a choice of the steps and the pixels are those of the plain decode | `tau_tgy_dma.sv` (writer), `tau_tgy_dcore.sv` (no parity shift), `tau_tgy_regs.sv` | GEOM bit 18, STX 0x248, STY 0x24C |
| strip | The SDRAM statistics window left out of the gallery build (`TAU_NO_SDA`) | `gallery_m1f_qsf_append.txt` | none |

## Registers (byte offsets in the MMIO page, `0x80000000 +`)
- **PIX_CTRL 0x250.** Write: `[1:0]` dither mode (0 off, 1 ordered, 2 ordered + temporal), `[2]` colour tables on. Read: `[8]` table load busy, `[2]` tables on, `[1:0]` mode.
- **PIX_LUT 0x254.** Write `{channel[17:16] (0 R, 1 G, 2 B), index[15:8], value[7:0]}`; one store at a time (poll PIX_CTRL busy). The store crosses to the video clock by a toggle handshake.
- **VIEW_CAPS** (existing register) reads 0x80000007 now (the new capability bits).
- **HIST 0x260 to 0x278** (decoder clock domain behind two-flop and handshake crossings):
  - `0x260` CTRL: write `[0]` count on, `[1]` clear (sweeps all 4 x 256 bins and the count to 0, about 256 decoder clocks); read `[8]` busy, `[0]` count on.
  - `0x264` RECT0 `{y0[25:16], x0[9:0]}`, `0x268` RECT1 `{y1, x1}` (exclusive), `0x26C` ORG `{y, x}`: the canvas position of the next tile's pixel (0, 0). Set them while the decoder is idle.
  - `0x270` IDX write `{histogram[9:8], bin[7:0]}` starts a read (0 luma, 1 red, 2 green, 3 blue); `0x274` DATA read `[31]` busy, `[23:0]` the bin; `0x278` N read `[23:0]` pixels counted as of that read.
  - Luma = (77 R + 150 G + 29 B) >> 8. A pixel counts when enabled and its canvas position lies in the half-open window. Counts are 24 bits.
- **TGY decoder 0x220 to 0x24C:** geometry register `fmt` may now be 1 (4:4:4). **K7:** GEOM `[18]` SCATTER (write each pixel at DST + x * STX + y * STY instead of whole rows at DST + y * PITCH, DST = where source pixel (0, 0) goes); `0x248` STX and `0x24C` STY, signed 18-bit byte steps from one pixel of a source row to the next and from one row to the next (identity is STX = 3, STY = PITCH; the 8 orientations and their steps are in `fw2/gallery/gallery_rot.h`). A pixel is a 2-halfword write with byte enables (odd addresses included); about 30 SDRAM clocks a pixel with a random-ready port model, 612,000 clocks for a 128 x 128 tile against about 340,000 for row writes; the writer keeps up with the decoder's row rate. PITCH is ignored while SCATTER is set.

## Draw engine ops
- **OP_CORNERS (10):** the fields of `OP_RRECT` (rectangle, radius table, foreground = the corner colour); no main fill. Used to round a picture that is already drawn.
- **OP_DSBLIT (11):** source width and height in the width / height fields, source offset as `OP_SBLIT`, destination offset in the address field, **x ratio in the `sx` field and y ratio in the `sy` field: 0 = 3:4, 1 = 2:3, 2 = 1:2, 3 = 1:3 (output : source)**. Output size per axis: `src*3>>2`, `floor(src*2/3)`, `src>>1`, `floor(src/3)`, at most 127. Output column i takes the source column `floor(i * den / num)` and the pixel to its right, the row below and the one diagonally; the output pixel is the per-channel rounded average `(sum + 2) >> 2` of the four RGB565 words. The neighbours always exist (the ratios are at least 4:3). Four single-word reads per output pixel, each its own transaction through the scheduler like `OP_SBLIT`, so scan-out can pre-empt between any two. Same 127-pixel row limit as `OP_BLIT`.

## Budgets (m1d fit, before this batch)
89% of the ALMs, 79% of the M10K, 71% of the DSP blocks. Added: `tau_pixfx` 3 M10K (the tables), `tau_hist` 4 M10K, 4:4:4 about 18 M10K, the downscale a few hundred ALMs and no DSP. Freed: the PSRAM and statistics logic. **The real numbers come from the probes and the fit; this table is the estimate.**

## Timing
Probes (`tools/block_probe.py`): `unit` (decoder + histogram, clocks 66.7 / 100 / 50 MHz) and `pixfx` (66.7 / 40 MHz). Results are recorded in the audit entry. The draw engine's timing is only visible in the whole-core fit (it has no probe).

## Rotation (K7), why a scatter writer
Rotating inside the pixel stage would have needed a transposed readout of the line-buffered 4:2:0 filter and a re-alignment of the edge tiles (their padding changes sides). The scatter writer leaves the decoder alone: the pixels are bit-identical to the unrotated decode and edge tiles need nothing special; the firmware chooses the destination of each tile (`gr_tile_off`) and the steps. Cost: the 128 x 128 tile takes longer to write (see above), and nothing else.

## Tests (all simulation)
`make test-rtl-gallery-fx` (K1, K5, K4 core and wrapper, each against an independent reference, mutants killed), `make test-rtl-fb-mutation` (K3 and K2, 8 + 5 mutants), `make test-rtl-tgy-unit / -dma / -soc` (the DMA bench writes 3 tile shapes in all 8 orientations and compares every byte of memory, 10 scatter mutants; the unit bench programs STX / STY / GEOM bit 18 through the registers, 8 register mutants), `sim/test_gallery_rot.py` (the orientation maths against numpy, 11 mutants), `make test-fullcore` (whole core with the real CPU), `sim/test_tgy_hw.py` and `sim/test_tgy_parallel.py` (firmware driver and converter, 4:4:4 kept as it is).

## What the firmware still has to do
Use PIX_CTRL / PIX_LUT (a Settings entry for the dither, colour presets), call `OP_CORNERS` after each grid thumbnail, `OP_DSBLIT` for the smaller grid sizes and zoom levels, read the histograms from the fit-level decode instead of the thumbnail, accept 4:4:4 levels (done in `tgy_hw.c`). The firmware's `P2_CORE_VERSION` is already 0x4D50331E.
