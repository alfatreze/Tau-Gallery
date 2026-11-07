# Tau Gallery M0 design (2026-11-07)

Plan: `../../docs/GALLERY_ANALYSIS_2026-11-07.md` (sections 8.2, 12.7, 12.11, 12.13). M0 is the first hardware proof: 800x720 packed RGB888 scan-out from SDRAM, the 400x360 UI mode beside it, a landing path for large SD reads, an interrupt block and the port structure. Process: host model, simulation with mutants, Quartus (two seeds), then a Pocket. Evidence labels: measured, simulated, estimate.

## 1. Slices and status

| # | Slice | Status |
|---|---|---|
| M0.1 | `tau_scanout_v`: viewer timing and RGB888 scan-out, testbench, mutants | **done, simulated** (`make test-rtl-scanout-v`, 10 of 10 mutants killed) |
| M0.2 | Talos fill engine for the viewer: plane base and stride registers, 1,200-word line (3 requests of 400), `lb_waddr` of 12 bits | next |
| M0.3 | PLL: `clk_vid` and its twin 12 MHz to 40 MHz (600/15, twin 6,250 ps), `core_top` video mux by mode, `video.json` with two scaler slots, UI re-timing to about 1,666 x 400 | after M0.2 |
| M0.4 | Port structure: at most four scheduler ports, read and write muxes (model: Tau V `tau_tvc_pmux`) | after M0.3 |
| M0.5 | Landing path: bridge-to-SDRAM dual-clock FIFO and burst master (model: Tau V `tau_tvc_subw`) | after M0.4 |
| M0.6 | Interrupt block: the 32 external interrupt inputs (`tau_soc.v:408`), SD-done and frame-edge sources, trap handler in `fw2/platform/start.S` | parallel to M0.4 |
| M0.7 | Firmware test page (ramp, gradient, checkerboard, photo), mode switch, counters; Check page rows | with M0.3 |
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

## 5. Fill engine change (M0.2)

`tau_talos` fills from `line * 512` words, 400 words per request, into a 1,024-word line buffer. For the viewer: line address = plane base + line * stride, three requests of 400 words (or six of 200: Tau V's fetch uses 200), `lb_waddr` of 12 bits, a mode bit chosen by the scan-out in use. The existing contract tests (`test-rtl-gfx-fill`, the oracle diff) must keep passing with the mode bit low, and the viewer mode gets its own test against `tau_scanout_v`.

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
