# Tau Gallery M2 design: pan and zoom (2026-11-09, design only, nothing built)

Plan: `../../docs/GALLERY_ANALYSIS_2026-11-07.md` (sections 6.5, 7, 11, 12.1; M2 and M4 rows). Inputs: `docs/M0_DESIGN.md` (the viewer plane, the landing path, 6c to 6e: what a Pocket measured) and `docs/M1_DESIGN.md` (the TGY container, the fit canvas, the viewer). M1 shows one picture, fitted into 800 x 720. M2 lets you look at it closer: a 1:1 view you pan across, and whole-step zoom levels. A smooth fractional zoom is M4 (a hardware sampler) and stays out of scope.

## 1. What is already true (measured or built, not assumed)

| Fact | Source |
|---|---|
| The viewer shows a plane of packed RGB888 words; its base and stride are registers (`R_VIEW_BASE` 0x1F0, `R_VIEW_STRIDE` 0x1F4, applied at the next vertical blank); a plane line is 1,200 words, four pixels are six words | M0.2, `gallery_hw.c` |
| The plane in SDRAM is exact (432,000 words read back, 0 bad); the handheld display is 5/6/5 | B-1007 |
| A card read can land **linearly** at any 4-byte aligned plane address (`R_LAND_BASE` plus the position in the command); every word is counted | M0.6, B-1008 |
| Landing rates: 4 KB 955 KB/s, 16 KB 1,562, 64 KB 1,480, 128 KB 1,571 KB/s (card-limited, not memory-limited) | B-1008 |
| Mode switch worst 33 ms; a fill of the plane by the CPU mailbox takes 1.9 s | B-1006 |
| TGY already has a level table with `layout`, `mode`, `pixfmt`, `kind`, `stride`, `index_off` fields; M1 writes one level (the fit canvas, raster layout, raw) | `tools/gallery/tgy.py` |
| The firmware reads a picture's header and picks the first level that is a raw 800 x 720 raster fit canvas (`gt_parse`) | `gallery_fmt.c` |

**The geometry problem.** A landing is a straight run of bytes. A rectangle of a large picture is not one run in a raster file (its rows are as far apart as the picture is wide), and a 128 x 128 tile stored contiguously (48 KB) is not what a strided plane wants (its rows are a plane stride apart). Every M2 option below is a different answer to that mismatch.

## 2. The numbers that frame the choice (12-megapixel photo, 4,000 x 3,000, raw RGB888)

| Level | Size | Full read at 1.57 MB/s |
|---|---|---|
| 1:1 | 36.0 MB | 23 s |
| 1/2 | 9.0 MB | 5.7 s |
| 1/4 | 2.25 MB | 1.4 s |
| fit canvas (M1) | 1.75 MB | 1.1 s |
| **one screen at 1:1** (800 x 720) | 1.73 MB | 1.1 s |
| one tile column (6 tiles of 48 KB) | 288 KB | 0.18 s |
| one tile row (7 tiles) | 336 KB | 0.21 s |

A raw pyramid of one such photo is about 47 MB: 100 photos would be 4.7 GB. So raw levels are a **proof for a few pictures** (or a converter option, off by default); the compressed tiles of the plan's M3 are what makes a whole library affordable. Sustained pan speed with tile prefetch is about 128 pixels per 0.2 s, 600 pixels a second, if the card is the only limit.

## 3. Options (A1 to A4 are the plan's Talos assumptions; none is verified yet)

| Option | How a pan or zoom reaches the screen | Needs | Cost and risk |
|---|---|---|---|
| **A. Strip levels, no RTL** | The file stores a level as full-width row strips; the plane stride equals the level width; panning is a register change (base, stride); new rows are one linear landing | the converter writes raster levels; nothing else | Wasteful: a viewport 800 wide reads rows 4,000 wide (5x the data for a horizontal pan). Fine for the 1/2 and 1/4 levels (rows of 2,000 and 1,000 pixels), poor at 1:1. No new hardware to prove |
| **B. Tile levels + Talos copy** | Tiles land linearly in a staging area (48 KB each), Talos BLITs each tile row (192 words) into the plane at its place; a pan shifts the plane base and copies the new tiles | A1 (copy reach over 64 MB), A2 (row buffer width 128 words: a tile row is 192, so two commands per row, an 800-pixel line is 1,200 words: 5 to 10 commands) | The plan's choice. 128 x 128 x 3 B per tile, about 128 row copies a tile. Copy speed is unmeasured; a verified A1/A2 and one Pocket measurement decide it |
| **C. Tile-major scan-out (RTL)** | The scan-out walks a tile table (7 bursts of 192 words a line); landed tiles are used where they are, no copies | a new scan-out mode, a tile table, one fit | Cleanest at run time, largest RTL and timing risk; the M10K budget is already at 264 of 308 blocks |
| **D. Fractional sampler (M4)** | A hardware affine sampler | a large RTL block | Out of scope here; the viewport descriptor below is shaped so it can replace B later |

**Recommendation: B, preceded by A for the half and quarter levels if a quick win is wanted.** B reuses proven pieces (the landing path for the tile, Talos for the copy) and adds no RTL if A1 and A2 hold. C is the fallback only if the copy proves too slow. Nothing in B is decided until step M2.0 below is done.

**Fine pan.** The scan-out starts a line on a 4-pixel group (six words). One-pixel pans need a 0 to 3 pixel fine offset in the scan-out (an RTL change, a few registers). Until then a pan moves in steps of 4 pixels (at 60 Hz that is smooth enough for a held direction, coarse for a single press). OWNER: accept 4-pixel steps for M2, or wait for the fine offset.

## 4. The viewport (frozen early so a later sampler fits without firmware changes)

`viewport = { picture, level, x, y }` with x and y the top-left corner of the screen in that level's pixels, always a multiple of 4 in x. The pure rules, all host-testable now, in `fw2/gallery/gallery_view.h` (to write):
1. **Clamp:** x in [0, level_width - 800], y in [0, level_height - 720]; a level smaller than the screen is centred and cannot pan.
2. **Tiles needed:** the set of 128 x 128 tiles a viewport touches (up to 7 x 6), in the order they should land (the tiles nearest the screen centre first, then the ring one tile out in the direction of travel).
3. **Zoom step:** changing level keeps the centre of the screen on the same point of the picture (x' = (x + 400) * s - 400 with s = 2 or 1/2, rounded to 4), then clamps.
4. **Fit and 1:1:** A toggles the fit canvas (M1 level) and the 1:1 level, centred on where it was.
5. **Pan speed:** a held direction accelerates from 4 pixels a frame to 16; a hold across a tile edge never skips a tile that is needed.
6. **What to evict:** the plane keeps the tiles of the viewport plus a one-tile ring; the staging area is reused as tiles are copied.

## 5. Controls (OWNER: the plan says D-pad pans, L/R zoom a step, A toggles fit and 1:1, B back)

At the fit level Left and Right keep their M1 meaning (previous and next picture). Zoomed in, the D-pad pans, so previous and next need other keys. Proposed: **A** toggles fit and 1:1; **L1 / R1** step the zoom out and in; the **D-pad** pans when zoomed; **Select + Left / Right** go to the previous and next picture from a zoomed view; **B** backs out one level (zoomed to fit, fit to grid). The Controls page lists them (the page's rows are data in `gallery_pages.h`).

## 6. Converter and format (host side, no hardware needed)

`tgy_convert.py --levels` writes, besides the fit canvas, tile levels: a 1:1 level and (halving while larger than the screen) 1/2, 1/4. A tile level uses the existing level-table fields: `layout` 1 (tile-contiguous, 128 x 128, row-major tiles, each 49,152 bytes, tiles padded to 4 KB), `mode` raw, `pixfmt` RGB888, `index_off` and `index_len` for the `u32` offset of every tile, `stride` the tile row length. The firmware reader gets `gt_level()` and `gt_tile_off()` with the same bounds checks as `gt_parse`. Files grow (section 2): the option is off by default and the Info page and the converter's `done` event report the size.

## 7. Slices

| # | Slice | Needs a card write |
|---|---|---|
| M2.0 | Read `tau_talos.sv` and `MMIO_ALLOCATION.md` for A1 (copy reach) and A2 (row buffer), run the one host simulation the plan asks for, decide B or C | no |
| M2.1 | `gallery_view.h` (section 4) with host tests and mutants; the tile level in `tgy.py` and the reader in `gallery_fmt.c` with tests; `tgy_convert.py --levels` | no |
| M2.2 | A Talos-copy measurement page in the M0 test firmware: tile landing, BLIT of 128 rows, rate and the late-fill counter; one Pocket run (a DEV build) | **yes** (DEV package, install) |
| M2.3 | 1:1 pan in the gallery app (staging, copy, base register at the vertical blank, 4-pixel steps) | tested on the host, proven on a Pocket with a DEV build |
| M2.4 | Zoom steps and the controls of section 5, the Controls page rows | same |
| M2.5 | One Pocket proof with a real photo: first 1:1 view time, pan speed, no scan-out late counter, zoom step time | yes |

**What can start now with no hardware:** M2.0 and M2.1. They settle the open Talos assumptions, freeze the viewport rules and the tile format, and produce everything M2.2 needs. M2.2 is the first slice that needs a build on the card, and it needs your yes.

## 8. Open owner decisions

1. B (tile levels + Talos copy) as the first attempt, with C only if the copy is too slow? (recommended)
2. 4-pixel pan steps for M2, or wait for a fine-offset scan-out change?
3. The controls of section 5, in particular previous and next picture while zoomed.
4. Raw levels as an off-by-default converter option for a few pictures (about 47 MB each), with the compressed tiles left to M3?
