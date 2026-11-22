# Tau Gallery: the cold UI image in PSRAM (scoping, 2026-11-22)

Owner: "start the PSRAM cold UI" (from `docs/GALLERY_1_0_PLAN.md`: "PSRAM cold UI image: the core has the window, the firmware does not use it yet"). This is the plan and what was measured; nothing is built yet.

## Why
The 128 KB link has little room: `gallery-app` is about 63 KB of code and read-only data plus 55 KB of RAM data, and the link failed twice in this stretch for a few KB (a 1.7 KB row buffer, then 4 KB of histogram counts). Every feature since needs RAM (the zoom's tile index was cut from 32 KB to 24 KB to fit the 2x and 4x zoom). Code that runs rarely (the menu, Controls, Help, Info, Settings, Theme and the details pages) does not need to live in the fast core RAM.

## What exists (from the player, spec P5)
- `fw2/platform/link.ld` already has the cold regions: `.cold_data` (read-only data at 0xA4800000, the PSRAM window) and `.cold_text` (code, executed from the same PSRAM through the instruction alias at 0x24800000), both chosen **by object file name** in the linker script, 1 MiB.
- `tools/pack_cold.py pack fw.elf OUT_DIR` writes `tau-cold.bin` (header `TCLD`, size, CRC32 of the body, a layout id that is also patched into the ROM, so a firmware refuses an image that is not its own); `tools/check_cold_calls.py` checks that cold code only calls what is safe.
- The loader is `fw/cold_core.h` (reads data slot 6 into the PSRAM at boot). The core's PSRAM window is built (the fit has it).
- The Gallery today throws the cold sections away (`fw2/build_gallery.sh`: `objcopy -R .cold_data -R .cold_text`), and its data slot 6 `tau-cold.bin` is only a 147 KB payload for the landing-read tests.

## What can move (measured on the DEV 032 objects)
`gallery_app.o` is 54.7 KB of text and rodata; the functions that are UI pages: `run_details` 6.1 KB (the details pages and their drawing), `draw_info` 2.1 KB, `draw_settings` 0.5 KB, `draw_theme` 0.4 KB, the menu, Controls and Help drawing (about 3 KB), plus their strings and tables (Controls rows, Help text, key reminders, the label font 0.3 KB if the bar is cold too): about 15 to 18 KB. The grid, the viewer, the zoom, the decoder driver and the settings save stay hot (`main` is 12 KB, `tile_pass_inner` 3.8 KB, `z2_work` 1.3 KB).

## Steps
1. **Split the source.** The linker picks cold objects by file name, so the page code has to be its own translation unit (`gallery_pages_ui.c`). Today `gallery_app.c` is one file with its state `static`; the page code reads `head`, `sel`, `gset`, `info`, `hm_mode`, the thumbnail reader and the draw helpers: those become `extern` through one header. The host test includes the firmware by `#include "gallery_app.c"`; it would include both files.
2. **Link rule** for `*/gallery_pages_ui.o` into `.cold_text` and `.cold_data` (the player's list is a copy of names; add the Gallery's).
3. **Boot loader:** the Gallery firmware needs `cold_core.h` (the read of slot 6, the CRC and layout id check, a failure screen), and data slot 6 becomes the real cold image, a **required** file at boot (today it is a deferload test payload): `data.json`, the packager and `check_tau_package` change with it. The landing tests that used slot 6 as their payload move to another file.
4. **Build:** `pack_cold.py` in `fw2/build_gallery.sh` and `package_dev_build.py`; `tools/check_cold_calls.py` for the Gallery.
5. **Cost to know before starting:** a call into cold code is slower (PSRAM instruction fetch, no cache measured for the Gallery), so only pages that wait for keys qualify; the `run_details` thumbnail and histogram drawing issue about 5,000 mailbox writes: measure its draw time cold against hot.
6. **Tests:** the host test is unchanged in behaviour (one build with both files); a new check that no hot code is called from a cold object without the cold-call rule, the layout id changes when a cold symbol moves, and a boot with a damaged cold image shows an error.

## Risks
- A **boot dependency**: without a valid `tau-cold.bin` the core does not start; every package and the release zip must carry the matching file (the player already lives with this).
- The ROM and the cold image must come from the same build (the layout id covers it).
- Nothing here needs a new fit: the PSRAM window is in the shipped bitstream.

## Gain and order
About 15 to 18 KB of the 128 KB link back, enough to restore the zoom index to 32 KB and leave room for the next features (X and Y in the zoom, per-picture settings). It is a firmware and packaging change, no RTL. Suggested after the pixel doubler fit, since both touch the packager and the boot sequence is easier to prove on one hardware change at a time.
