# Tau Gallery M2 design: pan and zoom (2026-11-09, revision 3: M2.0 and M2.1 done; the firmware, the Pocket and the Talos copy time are not)

Plan: `../../docs/GALLERY_ANALYSIS_2026-11-07.md` (sections 6.5, 7, 11, 12.1; M2 and M4 rows). Inputs: `docs/M0_DESIGN.md` (the viewer plane, the landing path, what a Pocket measured) and `docs/M1_DESIGN.md` (the TGY container, the fit canvas, the viewer). Review: `docs/M2_DESIGN_REVIEW.md` (codec and Pocket view); every point of it is applied below and marked (B1, M3 ...). M1 shows one picture fitted into 800 x 720. M2 lets you look at it closer: a 1:1 view you pan across, and whole-step zoom levels. A smooth fractional zoom is M4 (a hardware sampler) and stays out of scope.

## 1. What is already true (measured or built, not assumed)

| Fact | Source |
|---|---|
| The viewer shows a plane of packed RGB888 words; base (`R_VIEW_BASE` 0x1F0, a 25-bit word address) and stride (`R_VIEW_STRIDE` 0x1F4, at least 1,200 words) are registers applied at the next vertical blank; four pixels are six words, so a base must sit on a 6-word group (4 pixels) horizontally | M0.2, `mmio.h`, `gallery_hw.c` |
| The plane in SDRAM is exact (432,000 words read back, 0 bad); the handheld display shows 5/6/5 (KB-237) | B-1007 |
| A card read lands **linearly** at any 4-byte aligned plane address (`R_LAND_BASE` plus the position in the command); every word is counted | M0.6, B-1008 |
| Landing rates from the start of a file: 4 KB 955 KB/s (about 4.2 ms a command), 16 KB 1,562, 64 KB 1,480, 128 KB 1,571 KB/s (KB-238) | B-1008 |
| Memory: scan-out of the viewer is 104 MB/s of the 193 MB/s measured fill (100 MHz CL2), so about **86 MB/s is left** for landing and copying | plan 12.6, M0 |
| Mode switch worst 33 ms (KB-239); CPU mailbox fill of the plane 1.9 s | B-1006 |
| TGY has a level table with `layout`, `mode`, `pixfmt`, `kind`, `stride`, `index_off`, `index_len`; M1 writes one raw raster fit level; the firmware picks the first raw 800 x 720 raster level (`gt_parse`) | `tgy.py`, `gallery_fmt.c` |
| The Pocket caches the open file and up to 16 fragments per slot and **drops the cache when the slot changes** (docs) | `host-target-commands.md` |
| **Talos BLIT (M2.0, read from `tau_talos.sv` and `tau_soc.v`, simulated in `sim/tb_talos_tilecopy.v`):** the sticky source and destination bases are 25-bit word addresses, so they reach the whole 64 MB (A1 holds); a command's offsets are 19 bits (524,288 words, 437 plane lines); a row is at most **127 words** (the width is used modulo 128: a 192-word tile row is silently cut to 64); the stride registers are **10 bits** (at most 1,023 words), so a multi-row BLIT cannot step a 1,200-word plane line or a 1,728-word window line | M2.0, B-1019 |
| `TAU_BLIT` is in both gallery macro bundles, so the installed bitstream has BLIT | `tools/gallery_m0b_qsf_append.txt` |

**What is not known (the review's M1, B1, B2):** how a read at a *random offset* of a 36 MB file performs (the 1.57 MB/s figure is one sequential command from the start of a file, KB-238; not tested: fragmented files, other offsets, a slot switch), the cost of a Talos copy (A1 and A2 are unverified), and whether the scan-out is ever late while a copy runs.

**The geometry problem.** A landing is a straight run of bytes. A rectangle of a large picture is not one run in a raster file, and a 128 x 128 tile stored contiguously (48 KB) is not what a strided plane wants. Every option below answers that mismatch. A second constraint came from the review: **the plane that is scanned out must be larger than the screen** for a pan to be a register write (B1), and **it must not be written while it is shown** (B2).

## 2. The numbers that frame the choice (12-megapixel photo, 4,000 x 3,000, raw RGB888)

| Level | Scale | Size | Full read at 1.57 MB/s |
|---|---|---|---|
| 1:1 | 1 | 36.0 MB | 23 s |
| 1/2 | 0.5 | 9.0 MB | 5.7 s |
| fit canvas (M1) | 0.2 | 1.75 MB | 1.1 s |
| one screen at 1:1 (800 x 720) | 1 | 1.73 MB | 1.1 s |
| one 128-pixel tile | | 48 KB | 31 ms |
| one tile column (6 tiles) / row (7) | | 288 KB / 336 KB | 0.18 s / 0.21 s |

**Per-command economics (derived from the two measured points, 4 KB in about 4.3 ms and 128 KB in about 83 ms):** the overhead is about 1.5 to 2 ms a command, so a command is efficient from about 16 KB. A raw 48 KB tile is one efficient command; a compressed tile of 6 to 12 KB (M3) is latency-bound at about 4 ms each, which is why the file order must let neighbouring compressed tiles be read as one command (section 6).

**The ladder (review M3).** The panel shows each core pixel as 2 x 2 panel pixels, so **1:1 is already the finest detail the panel can show**: nothing is gained beyond it. A fit canvas has an arbitrary scale (a 4,000 x 3,000 photo fits at 0.2, so fit to a 1/4 level would be a 1.25x step). So at most **three stops: fit, 1/2 and 1:1**; the 1/2 stop exists only when the source is at least about 1,600 pixels wide; a stop that is less than about 1.5x from the previous one is dropped; a source no larger than the screen has no zoom. A raw 1:1 plus 1/2 pair of a 12-megapixel photo is about 45 MB (1/4 is gone): 100 photos would be 4.5 GB. Raw levels are therefore a **proof for a few pictures** and a converter option that is off by default; the compressed tiles of the plan's M3 make a library affordable.

## 3. Options (A1 to A4 are the plan's Talos assumptions; none is verified yet)

| Option | How a pan or zoom reaches the screen | Needs | Cost and risk |
|---|---|---|---|
| **A. Strip levels** | A level is stored as full-width row strips; the plane stride equals the level width; a pan is a register change; new rows are one linear landing | the converter writes raster levels | A horizontal pan reads rows as wide as the picture (2.5x the data it shows at the 1/2 level of a 4,000-pixel photo, 5x at 1:1). No Talos, no RTL, nothing new to prove. **Used first, for the 1/2 level, to prove the pan state machine and the card read model** |
| **B. Tile levels + Talos copy into an oversized, double-buffered resident window** | Tiles land linearly in a staging area; Talos BLITs each tile row, in two pieces of 96 words, into the **back** window (one command per row piece: 256 commands for a 128-pixel tile, about 10.7 thousand for a whole screen, each with its own destination offset); a base-and-stride write at the vertical blank flips it in; a pan inside the resident window is a base write only | A1 and A2 are settled by M2.0 (the copy works, as row pieces). The engine time per command, the command-issue cost and the late scan-out counter are the measurements of M2.2 | The plan's choice, now with explicit geometry (section 4). The copy rate and the re-centre stall are unmeasured |
| **C. Tile-major scan-out (RTL)** or a **toroidal scan-out** (address wrap in X and Y) | No copies, or a pan costs only the new strip | a scan-out mode, a tile table or a wrap, one fit | Cleanest at run time; the largest RTL and timing risk (M10K is at 264 of 308); the fallback if B's stall or copy is too slow |
| **D. Fractional sampler (M4)** | A hardware affine sampler | a large RTL block | Out of scope |

**Decision (review, owner approved the review 2026-11-09): B with the geometry of section 4, gated on M2.2; the pan state machine is built first on A at the half level; C only if the re-centre stall or the copy is too slow.**

**Fine pan.** The scan-out starts a line on a 4-pixel group; one-pixel pans need a 0 to 3 pixel fine offset in the scan-out (RTL). Until then pans move in **steps of 4 pixels** (8 panel pixels; a held key at 60 Hz is 240 px/s; a single press jumps visibly). Accepted for M2.

## 4. The geometry and the viewport rules (frozen early so a later sampler fits without firmware changes)

**The resident window (B1).** For a tiled level the plane that is scanned out is an **oversized resident window** of 9 x 8 tiles = 1,152 x 1,024 pixels (tile aligned; the screen has 176 to 352 pixels of slack across and 304 down; the first draft's 1,056 x 976 could not be tile aligned, found in M2.1), stride 1,728 words (`R_VIEW_STRIDE` is 16 bits and only has to be at least 1,200), 3.54 MB. A pan inside it is a base-register write. Only the tiles the move reveals are copied. When the viewport comes within one tile of the window edge the window **re-centres**: the back window is composed again around the new viewport (3.54 MB read and written as about 14.3 thousand row-piece commands, about 82 ms at the spare 86 MB/s, derived; the real figure is an M2.2 measurement, and re-centres are rare because the window is 1,152 wide and a re-centre only comes when the viewport is within 64 pixels of an edge) and flipped in. A toroidal scan-out would remove the re-centre and is the reason C stays on the list. For a level read by option A the window is the level itself (stride = its width).

**Two windows, one flip (B2).** SDRAM holds a front and a back window (7.1 MB of 64 MB). Landing and copying write only the back window. The flip is a base-and-stride write that applies at the next vertical blank. Nothing alternates frame to frame: a still view holds one window. (KB-075: output that alternates between two images every frame leaves retention on the panel.) An axis shorter than the screen (a 4,000 x 600 panorama) is centred; the rest of the window is black, cleared when the picture opens.

**Pure rules, host-testable now, in `fw2/gallery/gallery_view.h` (to write, M2.1):**
1. **Clamp:** x in [0, level_width - 800], y in [0, level_height - 720]; an axis shorter than the screen is centred and cannot pan.
2. **Alignment:** the viewport x is always a multiple of 4; the window stride and base keep every line start on a 6-word group (tested for every clamped position).
3. **Tiles needed:** the set of tiles a viewport touches (up to 7 x 6 at 128), in landing order: nearest the screen centre first, then the ring one tile out in the direction of travel.
4. **Ladder and zoom mapping (review M3):** explicit scales f (fit), 0.5 and 1.0; the centre of the screen stays on the same point of the picture: x' = (x + 400) * s_new / s_old - 400, rounded to 4 and clamped; the round trip fit to 1:1 to fit stays within 4 pixels.
5. **Fit and 1:1:** A toggles the fit canvas (M1 level) and the 1:1 level, centred on where it was.
6. **Pan speed:** a held direction accelerates from 4 pixels a frame to 16; a hold across a tile edge never shows a pixel that has not landed (the window's ring and the landing order guarantee it; tested).
7. **Command size and the loop (review M2):** a card command during a pan is **at most 48 KB** (about 30 ms), so the keys are polled between commands; tiles are a **job queue** the main loop steps (one command or a bounded number of Talos row commands a pass); a direction reversal drops the queued tiles (the command in flight finishes). The same bound applies to any copy.
8. **Eviction:** the window keeps the viewport tiles plus the one-tile ring; the staging area is reused as tiles are copied.

## 5. Controls (decided with the review)

At the fit level Left and Right keep their M1 meaning (previous and next picture). **A** toggles fit and 1:1; **L1 / R1** step the zoom out and in over the (at most three) stops; the **D-pad** pans when zoomed; **Select + Left / Right** go to the previous and next picture from a zoomed view; **B** backs out one level (zoomed to fit, fit to grid). The Controls page lists them (its rows are data in `gallery_pages.h`).

## 6. Converter and format (host side, no hardware needed)

`tgy_convert.py --levels` (off by default, only for sources of at least about 1,600 pixels) writes, besides the fit canvas, the stops of the ladder as tile levels. **Level-table layout ids are reserved now so M3 does not redefine a field M2 shipped (review M4): 1 = raw tile-contiguous, 128 x 128 tiles, row-major, each 49,152 bytes (48 KB, one command at the limit of section 4.7), tiles padded to 4 KB; 2 = the plan's band-major compressed layout (M3); 3 = reserved (macro-tiles of adjacent tiles for coalesced reads).** Other fields: `mode` raw, `pixfmt` RGB888, `index_off` and `index_len` for the `u32` offset of every tile, `stride` the tile row length. M3's tile size is decided from the command economics of section 2 when M3 starts (256 x 256 tiles or macro-tiles, so a horizontal pan step is one coalesced command of 16 KB or more); 128 and 256 are multiples of the 16 x 16 MCU of 4:2:0 JPEG, so they are legal re-encode boundaries. The firmware reader gets `gt_level()` and `gt_tile_off()` with the same bounds checks as `gt_parse`.

**Colour and depth (review M5, M6).** The converter converts a source with an ICC profile (Display P3 is common from phones) to sRGB with Pillow's `ImageCms` before anything else (M2.1, host only). The handheld shows 5/6/5, which hides nothing at 1:1: a smooth sky bands. A **static** ordered dither at the scan-out (a few ALMs, one fit) is the fix if M2.6 shows banding on a real photo; the converter does not dither (the dock keeps more depth, KB-237) and nothing may alternate per frame.

**Card names (a fix made with this review).** The converter now gives every picture an ASCII-only file name on the card (a non-ASCII character becomes `_`, plus a short CRC of the original name when anything was replaced); the Latin-1 display name stays in the index. A path with a byte of 0x80 or more cannot be opened by the core (KB-041). Tile levels live inside the one `.tgy` file, so they inherit this.

## 7. Slices

| # | Slice | Needs a card write |
|---|---|---|
| M2.0 | Read `tau_talos.sv` and `MMIO_ALLOCATION.md` for A1 (copy reach) and A2 (row buffer width), run the host simulation the plan asks for, decide B or C | no. **Done 2026-11-09 (B-1019): B stays, as row-piece commands; section 9** |
| M2.1 | Host work: `gallery_view.h` (section 4) with tests (below); the tile level in `tgy.py` and the reader in `gallery_fmt.c`; `tgy_convert.py --levels` and the sRGB conversion | no. **Done 2026-11-09 (B-1019): section 9** |
| M2.2 | Measurement page in the M0 test firmware: **a read matrix on a real 36 MB file** (sequential against random offsets from 0 to 30 MB; commands of 16, 48 and 128 KB; a deliberately fragmented copy; a slot switch between reads), tile landing, Talos BLIT of 128 rows, the re-centre copy, the late-fill counter; one Pocket run | **yes** (DEV package, install) |
| M2.3 | The pan state machine on **option A, the half level**: queue, flip, the 48 KB command limit, 4-pixel steps; proven on a Pocket | yes |
| M2.4 | Tile levels with the Talos copy into the resident window (B) if M2.2 allows it; otherwise the fallback decision (C) | yes |
| M2.5 | The ladder, zoom steps and the controls of section 5, the Controls page rows | yes |
| M2.6 | One Pocket proof with real photos: first 1:1 view time, pan speed, no late scan-out, zoom step time, **a 1:1 gradient (sky) for banding, a Display P3 photo, and one dock run** (the dock scales 800 x 720 to 1080p by a non-integer factor; `sharpness` and the step look are untested) | yes |

**Host tests for M2.1 (review):** (1) the tile set of a viewport against a brute-force pixel coverage over random viewports and levels; (2) inside the resident window no viewport ever shows a pixel that has not landed, across pans, re-centres and flips; (3) the zoom mapping keeps the centre within 4 pixels and the round trip is stable; (4) the 4-pixel alignment of the base and stride for every clamped position; (5) no card command over 48 KB, and the queue drops its tiles on a reversal; (6) mutants for each rule, as in M1.3.

**What can start now with no hardware:** M2.0 and M2.1. M2.2 is the first slice that needs a build on the card and your yes. Time estimates in this document for copying are lower bounds: a host cycle model understated the real core by about 1.9x in a sibling project (KB-100), so the real figure comes from M2.2.

## 8. Owner decisions (answered with the review)

1. B (tile levels, Talos copy, oversized double-buffered window) with C only if the copy or the re-centre is too slow, the half level on A first: **adopted**.
2. 4-pixel pan steps for M2: **adopted**.
3. The controls of section 5: **adopted**.
4. Raw levels as an off-by-default converter option, for sources of at least about 1,600 pixels, no 1/4 level: **adopted**.

**Candidate after M2.3, not a commitment (untested):** a second scaler slot (400 x 360) with a scan-out window of the plane would magnify the pixels already in memory 2x while the next level lands, so a zoom step is never a 1 s wait; the slot switch itself runs in about 33 ms (KB-239). It needs scan-out window registers and a glitch check.

## 9. Results of M2.0 and M2.1 (2026-11-09)

**M2.0, the plan's Talos assumptions (read from the RTL, simulated, not run on a Pocket):**

| | Assumption | Result |
|---|---|---|
| A1 | The sticky bases reach the whole 64 MB, so BLIT can copy from a tile cache into an image plane | **True.** `blt_src_base` and `blt_dst_base` are 25-bit word addresses (`tau_talos.sv`, `tau_soc.v` `R_BLT_DATA` fields 0 and 2); a command adds a 19-bit offset. `sim/tb_talos_tilecopy.v` copies with bases at 37.7 MB and 56 MB: every address and word is right |
| A2 | The copy row buffer is wide enough for a tile row (192 words) | **False, with a way round.** A row is at most 127 words (a 7-bit field; 192 becomes 64, silently). The stride registers are 10 bits: a destination stride of 1,200 words becomes 176. A multi-row BLIT cannot step the plane, so a tile goes in as **row pieces**: one command per piece of 96 words with its own destination offset, 256 commands per 128-pixel tile. The simulation lands a whole tile this way, every address and source right. Cost is the number of commands, not a hardware limit |
| A3 | A CPU write window is fast enough | Not needed: tiles arrive by the landing path (linear, card-limited) and move by BLIT. The bridged CPU window costs about 50 cycles a word (KB-030) and is not used |
| A4 | A scan-out mode bit exists | Built in M0 (`R_VIEW_BASE`, `R_VIEW_STRIDE`, `R_VIEW_CAPS`, the mode sequencer) |

Decision: **B stays**. Its cost is command issue and engine time per row piece, which only a Pocket run can measure (M2.2): about five register writes per command from the CPU (address, size, the source offset in the two colour fields, the go write, and the base registers every 437 rows) and an engine run of a 96-word read and write. The estimates in section 3 are lower bounds (KB-100).

**M2.1, built and host-tested (no hardware):**
- `fw2/gallery/gallery_view.h` (pure): the ladder, the fit and 1:1 toggle, zoom steps, clamp with 4-pixel alignment, the centre-keeping zoom mapping, pan speed (4, 8, 16 pixels a frame), the 9 x 8 tile resident window and its re-centre rule (margin 64), the tiles a viewport needs (screen tiles nearest the centre first, then the ring ahead in the direction of travel), the tile job queue with its retarget on a reversal, and the 48 KB command limit. `make test-gallery-view`: 19 mutants killed, none survive.
- `tools/gallery/tgy.py`: `ladder()` (equals `gv_ladder` on 313 sizes), `tile_data()`, `zoom_levels()`, `pack_tgy(..., levels)` (raw 128 x 128 tile levels, layout 1, no index, 4 KB aligned; without levels the file is byte for byte the M1 file), `to_srgb()` (ICC to sRGB, Display P3 tested with a hand-built profile), `tile_of()`. `fw2/gallery/gallery_fmt.c`: `gt_zoom_level()` and `gt_tile_off()` with bounds checks (the M1 reader `gt_parse` still finds the fit canvas). `tgy_convert.py --levels` (off by default; the `converted` event lists the levels written). `make test-tgy-levels`: 12 mutants killed, none survive.
- A refinement found while modelling: the resident window had to be tile aligned, so it is 1,152 x 1,024 (3.54 MB, stride 1,728 words), not 1,056 x 976. Zooming out near an edge moves the view toward the middle (the centre-keeping map meets the smaller stop's edge); tested.

**Next:** M2.2 is the first slice that needs a build on the card (the read matrix, the row-piece copy rate, the re-centre copy, the late-fill counter) and the owner's yes. Until then the host work that remains is the firmware state machine on top of `gallery_view.h` (the half level on option A), which can be built and tested against the hardware model without a card.
