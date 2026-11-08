# Tau Gallery M0 design (2026-11-07)

Plan: `../../docs/GALLERY_ANALYSIS_2026-11-07.md` (sections 8.2, 12.7, 12.11, 12.13). M0 is the first hardware proof: 800x720 packed RGB888 scan-out from SDRAM, the 400x360 UI mode beside it, a landing path for large SD reads, an interrupt block and the port structure. Process: host model, simulation with mutants, Quartus (two seeds), then a Pocket. Evidence labels: measured, simulated, estimate.

## 1. Slices and status

| # | Slice | Status |
|---|---|---|
| M0.1 | `tau_scanout_v`: viewer timing and RGB888 scan-out, testbench, mutants | **done, simulated** (`make test-rtl-scanout-v`, 10 of 10 mutants killed) |
| M0.2 | Talos fill engine for the viewer: plane base and stride, 1,200-word line (3 requests of 400), 12-bit line-buffer address, `tau_gfx` with both scan-outs | **done, simulated** (`make test-rtl-gfx-view`, mutants BUG_VIEW 1..5 killed; the 8 existing graphics regressions pass with the viewer off) |
| M0.3 | PLL macros (`TAU_VID40`: 40 MHz and twin 6,250 ps; `TAU_MEM150`: 150 MHz SDRAM), UI re-timing to 1,666 x 400, `tau_vidmode` (the mode sequencer), the video mux in `tau_gfx`, `video.json` with two scaler slots, MMIO registers 0x1EC-0x1F8 | **done, simulated** (`make test-rtl-vidmode` with 4 mutants, `test-rtl-gfx-view` through the sequencer, `test-rtl-view-soc` on the real CPU with a mutant and a viewer-off run; the 8 graphics regressions, the SoC tests and `rtl-lint` pass with the viewer off) |
| M0.4 | Port structure: the CPU port (port 2) shared by the CPU bridge and the landing path through `tau_sdram_p2mux` (a write mux; the read mux waits for the sampler, M4) | **done, simulated** (`make test-rtl-p2mux`, 7 mutants killed) |
| M0.5 | Landing path: bridge-to-SDRAM FIFO and burst master (`tau_land`), MMIO registers 0x200-0x210 (the MMIO page widened to 1 KB) | **done, simulated** (`make test-rtl-land`, 11 mutants killed, a slow SDRAM overflows with an exact drop count; wired in `core_game.vh` behind `TAU_LAND`) |
| M0.6 | Interrupt block: pending register 0x214 on the CPU's 32 external interrupt inputs, trap path in `start.S` (`irq_enable`, `irq_disable`, weak `irq_handler`) | **done, simulated on the real CPU** (`make test-rtl-irq-soc`: polling, three interrupts with every register preserved, a masked and an unmasked source; 8 mutants of `start.S` killed; wired behind `TAU_IRQ`) |
| M0.7 | Firmware test page (`fw2/gallery`, `make gallery-firmware`): four patterns, the mode switch, landing reads of 4 to 128 KB, the interrupt block | **built and host-tested against a model** (`make test-gallery`: 4 pattern mutants and 6 broken hardware models caught); **not run on a Pocket** |
| M0.8 | Strip audio and meters from the fork (`fw2/aout`, `decode`, `stream`, meters); budget M10K after the strip | when a build exists after each removal |

## 2. Video timing (physical, one 40 MHz clock)

Viewer: 900 x 740 total = 666,000 clocks = 60.06 Hz. Active window hc 16..815 and vc 8..727. Horizontal sync hc 840..879 (40 clocks), vertical sync vc 734..737 (4 lines), the fill request at hc 0 of every line, the vblank toggle at vc 734. These are the positions of the 800x720 mode that Tau V ran on a Pocket (`tau_tvc_video.sv`, logical 450 x 370, doubled). `video.json`: a scaler mode 800 x 720, aspect 10:9.

UI (to be proven with the switch in M0.3): the existing 400x360 contract re-timed to the same clock as about 1,666 x 400 total with 400 data-enable clocks per line, so 60 Hz and the same line cadence. If the Pocket does not accept the shape of that line, the fallback is the always-800 build (the UI doubled in the video stage), which Tau V proved.

## 3. Plane format (frozen interface)

A line is 800 pixels of 3 bytes, memory order R, G, B, no padding: 2,400 bytes = 1,200 16-bit words; word i = {byte(2i+1), byte(2i)} (little-endian). Four pixels are 12 bytes = 6 words. A plane line has a stride register (words), the plane has a base register (25-bit word address, reaches all 64 MB); Talos copies work on whole rows of 127 words at most (section 12.7). 4-pixel alignment of copies keeps the packing intact.

## 4. `tau_scanout_v` (done)

- **Interface.** Same fill protocol as `tau_scanout`: a toggle plus a 10-bit plane line number per line request, a line buffer write port on `clk_sdram` (`lb_waddr` = {line parity, word 0..1199}, 16 bit), the finished line number and a sticky seen flag back, a vblank toggle, a late-line counter (Gray into `clk_sys`), `enable` (black until set).
- **Line buffer.** Two 16-bit banks (even and odd word) of 2 x 1,024 pairs, read as one 32-bit word: three reads per four pixels. Sized for 8 M10K as written (1,200 pairs mapped to 2,048 deep); an offset adder or the 40-bit RAM mode can bring it to 5 or 6 (a later optimisation, in the M10K budget).
- **Pixel path.** Fetch (hc 12..811, one word per clock), a 96-bit group register loaded at hc 15 + 4g, a 4:1 pick, then the output register: every stage registered, at most a 4:1 mux of 24 bits between registers, as Tau V's 40 MHz fit required (its unpipelined blend failed by 7.8 to 9 ns).
- **Tests.** `sim/tb_tau_scanout_v.v`: a golden picture, a filler model (1,200 words per requested line), every active pixel of two enabled frames, black while disabled, 800 clocks x 720 lines, frame = 666,000 clocks, hs 40 clocks and 24 after the end of the active line, vs 4 lines with 7 hs pulses before and 10 after, the vblank toggle one clock before vs, a slow filler counted as late lines. Mutants BUG 1..10 (window, hs, vs, vblank, request lead, enable, line half, group word, byte order, late counter) all killed.
- **Not yet checked:** timing in Quartus at 40 MHz; the interface to the real fill engine (M0.2); the late-line counter against the real SDRAM scheduler.

## 5. Fill engine change (M0.2, done)

Implemented behind `VIEW_ENABLE` (default 0, so every old testbench and the oracle diff see the same engine cycle for cycle). With it on: `view_en`, `view_base` (25-bit word address of plane line 0) and `view_stride` (16-bit words per plane line) are quasi-static CPU registers, latched in Talos at the next vblank edge (never mid-frame; informal CDC like `blt_src_base`); the line address is `base + line * stride` (a DSP multiply in two registered stages, the start waits three clocks for it); a line is three requests of 400 words (`vaddr` steps by 400, `vcont` marks a continuation, `fill_chunk` counts, `vline` and `vlb_par` hold the line number and buffer half for all three), written to the viewer's line buffer through `vlb_*` (12-bit address, {parity, word 0..1199}); `fill_done_line` is reported after the last chunk. `tau_gfx` instantiates `tau_scanout_v` beside `tau_scanout`; Talos's latched mode picks which one is served and shown (video outputs, fill toggles and counters are muxed, the select synchronised into `clk_vid`). Known limits: a mode change while a fill is in flight glitches that one line; the UI scan-out keeps running while the viewer is shown (a few ALMs wasted).

Test `sim/tb_tau_gfx_view.v`: the real `tau_gfx` with `VIEW_ENABLE = 1` and an SDRAM model that holds a packed RGB888 plane at a base of 4096 words with a stride of 1,208 (not 1,200, so the stride is really used); the UI runs first, `view_en` is set, the outputs switch to the viewer at a vblank, one frame settles and one whole frame is checked pixel by pixel (576,000 pixels), the frame is 666,000 clocks, the late counter does not move; with a slow memory (3 wait clocks per word) it counts late lines. Mutants (Talos `BUG_VIEW`): 1 chunk step 399, 2 wrong line half, 3 word index not restarted, 4 stride ignored, 5 no wait for the multiplier: all killed.

(The text below is the design as written before M0.2.)

`tau_talos` fills from `line * 512` words, 400 words per request, into a 1,024-word line buffer. For the viewer: line address = plane base + line * stride, three requests of 400 words (or six of 200: Tau V's fetch uses 200), `lb_waddr` of 12 bits, a mode bit chosen by the scan-out in use. The existing contract tests (`test-rtl-gfx-fill`, the oracle diff) must keep passing with the mode bit low, and the viewer mode gets its own test against `tau_scanout_v`.

## 5b. Mode switch, PLL and registers (M0.3, done)

- **PLL** (`mf_pllbase_0002.v`): `TAU_VID40` makes `clk_vid` 40 MHz and its twin 40 MHz shifted 6,250 ps (Tau V's hardware-proven setting); `TAU_MEM150` makes the SDRAM clock 150 MHz. Without them: 12 MHz and 100 MHz as before. `core_game.vh`: `TAU_VIEW` (needs `TAU_VID40`, checked with an `error`) sets `TAU_VIEW_EN`, which passes `VIEW_ENABLE` to `tau_soc` and `tau_gfx`.
- **UI re-timing**: `tau_scanout` got `H_TOT` (500 by default, 1,666 in the viewer build: 400 lines at 60.02 Hz, 400 data-enable clocks per line, the sync positions unchanged) and, behind `MODE_EN`, `hold`, `slot_word` and `frame_end`. With `MODE_EN = 0` it is the module of before (the differential test against the oracle passes).
- **`tau_vidmode`** (the design of the `test/720` branch, B3): a request is noticed at a frame wrap; the next frame is the pending frame (old geometry, the target scaler-slot word `{10'b0, slot, 13'b0}` in the RGB word right after DE falls on every line); at the wrap after that the other scan-out takes over from its first clock and the one that leaves is held at counter 0. Talos takes the mode of the next frame at the vblank edge of the frame in progress (target mode, two-flop synchronised), so the fill engine is already in the new mode when the new frame's first request arrives. `tau_gfx` muxes video, fill toggles, vblank, beam position and the late counter by the mode on the bus.
- **Registers** (`tau_soc.v`, `VIEW_ENABLE`): `VID_MODE` 0x1EC (write bit 0 = request; read [31:16] completed switches, [2] requested, [1] pending, [0] on the bus), `VIEW_BASE` 0x1F0, `VIEW_STRIDE` 0x1F4 (reset 1,200), `VIEW_CAPS` 0x1F8; `docs/MMIO_ALLOCATION.md` and `fw2/platform/mmio.h` list them. `dist/.../video.json` has the second scaler mode (slot 1 = 800 x 720, 10:9).
- **Tests**: `sim/tb_tau_vidmode.v` (UI to viewer to UI, per frame: geometry, 666,400 and 666,000 clocks, the slot word on every line, zero elsewhere, no DE with hs or vs; mutants: no pending frame, wrong slot word, the leaving scan-out not held, the counter), `sim/tb_tau_gfx_view.v` (the real graphics path through the sequencer: the first two viewer frames pixel-exact from their first clock, no late line), `sim/tb_view_soc.v` with `sim/fw_view` (the registers read and written by the real CPU; a swap mutant; the viewer-off build reads 0 everywhere).
- **Not yet proven**: that the Pocket accepts the 400-active-clocks-in-1,666 line of the UI mode and the slot-word sideband, and that the mode switch is clean on a handheld and a dock (G1); the 40 MHz timing in Quartus.

## 6b. M0.4 to M0.6 as built (2026-11-08)

- **`tau_sdram_p2mux`** (write mux on scheduler port 2): the CPU bridge is the primary client (reads and writes), the landing master the secondary one (writes only); the landing master is granted only while the CPU offers nothing and has nothing outstanding, and holds the CPU off from its acceptance to its `done`, so the pulled write data belongs to exactly one client. The test uses a port model whose writes are pipelined (the next write's data pull starts before the previous `done`), which is what makes the hold observable.
- **`tau_land`**: bridge words at `0x4xxxxxxx` (window nibble `WIN`) are byte-reversed (the bridge is big-endian) into little-endian SDRAM halfwords at `base + 2 * word offset`; a 256-entry dual-clock FIFO (58 bits: word offset and data; M10K, estimated 2 blocks) absorbs the bridge, which cannot be stalled; a burst master of up to 8 words (16 halfwords); `idle` is "every accepted word is in SDRAM" (accepted and written counts compared in `clk_sys`, stable for 8 clocks); a full FIFO drops and counts. Wired behind `TAU_LAND` in `core_game.vh` (the CPU bridge's wires are now `cb_*` and either connect straight to the scheduler or through the mux). Firmware use: set `R_LAND_BASE`, issue a data-slot target read with bridge address `0x40000000 | offset` (whether `R_TGT_ADR` carries a 32-bit bridge address and the real APF burst rate are unmeasured), wait for `R_LAND_STATUS` idle, check `R_LAND_OVF` is 0.
- **MMIO page**: widened to 1 KB (`mmio_reg` is 10 bits; `sim/make_soc_sim.py` follows); registers 0x200-0x214.
- **Interrupts**: `R_IRQ_PEND` 0x214 (sticky bits, write 1 to clear) is the CPU's `externalInterruptArray`; the CPU's LiteX-style mask CSR 0xBC0 and pending CSR 0xFC0 apply. `fw2/platform/start.S`: `trap_entry` saves t0 and t1, hands mcause 0x8000000B to `irq_entry`, which saves every caller-saved register, calls the weak `irq_handler(pending)` and returns with `mret`; without a handler it restarts like any other trap, so the player's behaviour is unchanged until firmware defines one. `irq_enable(mask)` and `irq_disable()` are in `start.S`.
- **Not yet measured on hardware**: that a target read with a `0x4xxxxxxx` bridge address is accepted and streams at what rate (the open question of 12.8), the interrupt block on a Pocket, and any of this in Quartus (area and timing).

## 6c. M0.7 test page as built (2026-11-08)

- **What it is.** A small hot-RAM program (9.3 KB ROM, no audio, no cold image, no assets) for the Gallery bitstream of `tools/gallery_m0b_qsf_append.txt`: `fw2/gallery/gallery_main.c` (the page), `gallery_hw.c` (the tests), `gallery_pattern.c` (pure pattern code). `fw2/build_gallery.sh` builds it to `work/diagnostics/gallery/tau.rom`; `tau-cold.bin` there is a copy of the shipped 147 KB file, because data slot 6 is the file the landing and interrupt tests read. The player firmware cannot run on this bitstream (it requires the audio units), which is why this is a separate build.
- **Page (UI mode, 400 x 360).** Up/Down select, A runs, B backs out. VIEWER RAMP / GRADIENT / CHECKER / TEXTURE fill the plane through the SDRAM mailbox (864,000 halfwords at 0x200000, about one second, progress on screen), switch to the viewer and stay there; Left/Right change pattern, B returns to the UI. MODE SWITCH X20 does 20 UI-viewer-UI round trips with 150 ms dwell and reports completed/asked, switches counted by the hardware and the worst request-to-bus time. LANDING READS reads 4, 16, 64 and 128 KB of slot 6 as ONE command each to bridge address 0x40000000, with `R_LAND_BASE` = 0x400000: KB/s of the command, words accepted (must be length / 4), words dropped, idle, and a comparison of the first and last 4 KB of the window with a normal read of the same bytes into core RAM (the window is poisoned first). INTERRUPTS counts vsync interrupts in one second (59 to 61 expected, the page accepts 55 to 65), one command-done interrupt per 4 KB read (16 of 16) and one mode-switch interrupt per switch (2 of 2). Results stay on the page in green or red for a screenshot.
- **Patterns.** Ramp (four 180-line bands: grey, red, green, blue), gradient (red right, green down, blue the rest), checker (8-pixel squares, exactly 0 or 255) and texture (diagonal triangle waves in all three channels), each with a one-pixel white frame (a cropped or shifted picture shows at once) except the texture.
- **Tests.** `sim/test_gallery_pattern.c` (16 checks: the frozen plane format, bands, frame, checker edges, byte packing; mutants byte order, channel order, frame, square size all caught). `sim/test_gallery_hw.c` runs the real `gallery_hw.c` against a model of the hardware (mailbox, plane, sequencer, landing path, interrupts, slot reads): every pixel of all four patterns lands in the frozen format, nothing outside the plane, the switch and landing tests measure what they claim, and six broken models (landing swaps bytes, writes nothing, drops a word, interrupts never fire, the switch never completes, reads fail) are all reported as bad by the fields the page judges on. The page's own judging expressions are repeated, not shared, in the test.
- **What this does not show.** Everything the model assumes about the Pocket: that a target read with a 0x4xxxxxxx destination is accepted, what rate a 128 KB command gets, that the line shape and the mode switch are accepted, that interrupts arrive. Those are exactly what the page measures on the first run.

## 6. Ports (M0.4)

Scan-out fill (urgent), draw engine, CPU window and the landing path are the M0 clients. At most four scheduler ports; a mux shares a port between clients that are never busy together (landing and decoder write, sampler and draw engine read). Each mux must never starve the scan-out. Costs from Tau V: a port about 345 ALMs, a mux pair about 164.

## 7. Landing path (M0.5)

Bridge writes (clk_74a) land in a dual-clock FIFO (data words and address commands, Gray pointers) and an 8-word burst master writes SDRAM; one target read of 4 KiB up to 1 MiB. Template: Tau V `tau_tvc_subw` (195 ALMs, 1 M10K, 154 MHz). This is also what lets M-1 measure reads larger than 4 KB, the open question of 12.8.

## 8. Interrupts (M0.6)

An enable register and a pending register on the CPU's 32 external interrupt inputs; sources: SD command done, frame edge (vblank), later the decoder; firmware keeps polling as the fallback. Gate G1a.

## 9. Gates

G1 (video and the mode switch on a handheld and a dock), G1a (interrupts), G2 (150 MHz and the line-fetch margin), G0 (larger reads, with M0.5). Evidence is recorded in the plan with its label as each slice runs.

## 10. Open items

- Whether the Pocket accepts the UI line shape (400 active clocks in about 1,666).
- The 40 MHz pixel path closing in Quartus for the real design (Tau V needed a second seed).
- M10K: the viewer line buffer, the audio strip, the budget.
- The RGB888 alpha-blend check in Talos (12.9).
