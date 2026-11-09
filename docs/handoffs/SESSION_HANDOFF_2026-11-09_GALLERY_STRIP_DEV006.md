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
