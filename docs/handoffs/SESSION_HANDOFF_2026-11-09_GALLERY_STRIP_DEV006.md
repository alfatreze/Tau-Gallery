# Session handoff 2026-11-09 (later): DEV 005 read, audio firmware stripped, DEV 006 installed

Repo `gallery/`, branch `main`. Detail in `docs/AUDIT_TRAIL.md` B-1010; the earlier state is `SESSION_HANDOFF_2026-11-09_GALLERY_M0_M1.md`.

## State
- DEV 005 ran on a Pocket: grid, five thumbnails, all five pictures open. One bug (the counter lost its last digit) is fixed in the source and in **DEV 006, installed on the TauV card and not yet run** (version 0.1.0-dev.6+1e1640b, same seed-2 bitstream).
- M0.8 second half is done (commit 1e1640b): the audio firmware, its tests and generators are gone. `settings/`, `gfx/`, `stream/slot.c` and the pack tools that live code imports stay.
- Gallery tests (`test-gallery`, `-fmt`, `-app`, `test-tgy`) and both firmware builds pass. The host suite has 3 failures that also fail on a clean HEAD: `test_check_qsf_files`, `test_check_deps`, `tools/check_deps.py` (the deps table does not know `fw2/gallery`).

## Next
1. Owner runs DEV 006: check the `1/5` counter, then report open time per picture, picture quality, button mapping. Mid-read screenshots show a half-landed picture and a transient `PICTURE READ FAILED` (owner: caused by the screenshot); if it recurs without a screenshot, add one retry in `open_picture`.
2. Decouple `ui/` (appearance, controls_view, help_view, configure_view, settings_view, theme, views) from the deleted audio headers, then bring settings, controls and help to the gallery; `browser_view` and `fullscreen_view` may be a base for the grid and viewer. Five `diagnostics/` pages and `test_p2_configure_view` wait on this.
3. Fix `tools/check_deps.py` (add `fw2/gallery`) and the qsf file check (`tau_scanout_v`).
4. Open owner items from the earlier handoff: dock run, 565 dither, 150 MHz SDRAM, real folders of photos, ICC. Then M2 (pan and zoom).

## Rules that bit
- A deletion pass must check what live code imports (tau_compat reads `meter_pack_core.h`; mpreset needs meter_desc). Test on a clean HEAD worktree for a baseline; a worktree lacks the untracked `dist/`, so copy it in.
- Card writes and fits need the owner's yes; installs only through `tools/install_dev_core.py`.

## Addendum (same day, later): the gallery screens are built (tip 5f7f3b3)
M1.6 is built and host-tested, not packaged, not run on a Pocket: Start on the grid opens a menu with CONTROLS, HELP, INFO, SETTINGS (RESUME LAST PICTURE, saved) and THEME (theme, dark or light, accent; saved). Detail in `docs/AUDIT_TRAIL.md` B-1012 to B-1015 and `docs/M1_DESIGN.md` sections 5 and 6. Firmware 18,140 bytes. The owner said "Not yet" to packaging DEV 007; the card still holds DEV 005 and DEV 006.

**Next, by value:**
1. DEV 007 (package, dry run, install on TauV, owner's yes) and a Pocket run: the `1/5` counter, open time per picture, the settings round trip (Info shows LOADED and SAVED), Light mode legibility, the grid on OCEAN. The default accent is now white (was orange).
2. ~~Remove the dead music UI~~ **done** (B-1016, 80 files; helios, view_core, gfx fonts and icons, marquee and the clock cores kept as generic).
3. `test_check_qsf_files`: list `tau_scanout_v`, `tau_vidmode` (and `tau_land`) in `ap_core.qsf`; needs a VM fit and the owner's yes.
4. Small: one retry on a failed picture read if it recurs without a screenshot; the last picture is recorded when its landing succeeds even if the viewer did not start; no font selector (3 bits reserved in word 1).
5. Open owner items: dock run, 565 dither, 150 MHz SDRAM, real folders of hundreds of photos, ICC, then M2 (pan and zoom).

## Addendum 2 (same day, last): folder test, small fixes, M2 design (B-1017)
Done: the folder test (`make test-gallery-folder`; a converter bug fixed: a too-long name no longer loses the whole run and the index), the 300-entry paging tests, one retry of a failed card read, the last picture remembered only when shown. Written: `docs/M2_DESIGN.md` (pan and zoom; four owner decisions at its end). The firmware is 18,300 bytes. Still not packaged: DEV 007 now carries all of this.
**Next:** (1) DEV 007 with the owner's yes, and a Pocket run (also the settings round trip and Light mode). (2) M2.0 and M2.1 need no hardware: read `tau_talos.sv` for A1 and A2, then `gallery_view.h`, the tile level and `tgy_convert.py --levels` (see the design). (3) The qsf listing of the viewer RTL (VM fit, owner's yes).

## Addendum 3 (same day, last): the M2 review applied (B-1018)
`docs/M2_DESIGN_REVIEW.md` reviewed the M2 design; `docs/M2_DESIGN.md` is now revision 2 with every point applied (oversized double-buffered resident window, 48 KB command limit and tile queue, three-stop zoom ladder, reserved layout ids, option A for the half level first). The converter now writes ASCII-only card file names (the Pocket cannot open a path with a byte of 0x80 or more); **a library converted before this change that contains non-ASCII names must be converted again.** The DEV 005 and DEV 006 pictures are ASCII names and are unaffected.
**Next:** M2.0 (read `tau_talos.sv`, A1 and A2) and M2.1 (the host work) need no hardware; M2.2 needs a DEV build and the owner's yes; DEV 007 (everything since DEV 006) is still unpackaged.

## Addendum 4 (same day, last): M2.0 and M2.1 done (B-1019)
Talos reach and row width read from the RTL and simulated (`make test-rtl-talos-tilecopy`): the bases reach 64 MB, but a row is at most 127 words and the strides are 10 bits, so a tile is copied as row pieces (256 commands per 128-pixel tile). `fw2/gallery/gallery_view.h` (the viewport rules) and the tile levels in TGY (`tgy.py`, `gallery_fmt.c`, `tgy_convert.py --levels`) are built and host-tested; new make targets `test-gallery-view`, `test-tgy-levels`, `test-rtl-talos-tilecopy`, `test-gallery-folder`. `docs/M2_DESIGN.md` is revision 3. Nothing is in the firmware app yet and nothing is on the card.
**Next:** M2.2 needs a DEV build (the read matrix on a 36 MB file, the row-piece copy rate, the re-centre copy, the late-fill counter) and the owner's yes; or the firmware state machine on the half level (option A) built against the hardware model first. DEV 007 (everything since DEV 006) is still unpackaged.

## Addendum 5 (same day, last): the option-A pan is in the firmware, host-tested (B-1020)
Pictures converted with `tgy_convert.py --levels` (the converter writes raster zoom levels up to 2,048 pixels wide, tile levels beyond) can be zoomed in the viewer: A zooms in and back, the D-pad pans, B returns to fit, Select + Left/Right change picture; the screen is shown only inside rows that have fully landed. The `zoom` scenario of `make test-gallery-app` checks every displayed frame pixel for pixel. **Nothing has run on a Pocket.** Firmware 22,916 bytes. `docs/M2_DESIGN.md` is revision 4.
**Next:** a DEV build to prove it on the Pocket (and the M2.2 measurements in the same build: the read matrix, the row-piece copy rate, the late-fill counter), which needs the owner's yes and a re-converted library with `--levels`; or M2.4 (tile levels with the Talos copy) and M2.5 (L1/R1 stops) on the host first. DEV 007 (everything since DEV 006) is still unpackaged.

## Addendum 6 (same day, last): M2.4 and M2.5 on the host (B-1021)
Zoom now works for tile levels too: A takes the last stop (the 1:1 level) and goes back to fit, L1/R1 step the stops, the D-pad pans. Tile levels are landed tile by tile (48 KB) and copied by Talos (`gfx_blit_raw`, 256 row pieces a tile, 64 a pass) into two 3.54 MB windows; a re-centre composes the other window and flips; nothing is ever written into the screen being shown. The stops are chosen by the converter from the padded size (raster up to 9 MiB, else tiles, else left out). Firmware 28,940 bytes. **Nothing has run on a Pocket.** `docs/M2_DESIGN.md` is revision 5.
**Next:** the Pocket proof, which needs a DEV build (everything since DEV 006, plus the M2.2 measurements: the read matrix on a 36 MB file, the row-piece copy rate, the re-centre copy, the late scan-out counter) and the owner's yes, and a library converted with `tgy_convert.py --levels`. If the copy proves too slow, option C (a toroidal scan-out) is the fallback. DEV 007 is still unpackaged.
