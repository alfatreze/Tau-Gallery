# Tau Gallery M1 design: browse and view (2026-11-09)

Plan: `../../docs/GALLERY_ANALYSIS_2026-11-07.md` (sections 6.5, 7, 8; M1 row of the milestone table). M1 is the first MVP: your photos on the Pocket, with no RTL beyond M0. Process: host tools and tests first, then firmware, then a Pocket. M0 proved what M1 stands on (docs/M0_DESIGN.md 6d, 6e): the 800 x 720 viewer, the mode switch, interrupts, and single reads of 4 to 128 KB landing in SDRAM at about 1.5 MB/s.

## 1. Slices

| # | Slice | Status |
|---|---|---|
| M1.1 | Formats and the host converter: TGY container v0 (one picture), TGIX folder index with a thumbnail atlas, `tools/gallery/tgy_convert.py`, tests with mutants | **done, host-tested** (`python3 sim/test_tgy.py`, 10 tests, `--mutants` 10 of 10 killed; the five test photos convert in 0.7 s) |
| M1.2 | Firmware readers (C, `fw2/gallery/gallery_fmt.c`): index head and entries, picture header with the level checks, CRC-32; bounds-checked, refuse what they do not know | **done, host-tested** (`make test-gallery-fmt`: the C parse equals the Python parse on real files, 15 damaged files refused with the right code, 19 of 19 mutants killed) |
| M1.3 | Firmware gallery (`fw2/gallery/gallery_app.c`, `APP=gallery bash fw2/build_gallery.sh`, 9 KB ROM): 4 x 4 thumbnail grid, selection and paging, open a picture (open by path, header check, the canvas lands in the plane in 128 KB commands, word count and overflow checked), viewer, Left/Right neighbours, B back | **done, host-tested end to end** (`make test-gallery-app`: the real firmware runs against a hardware model that serves 18 real converted files; thumbnails compared with the atlas pixel for pixel, the plane compared byte for byte with the canvas, navigation, wrap-around, paging, redraw after the viewer, open failure, damaged transfer, viewer that does not start; 19 of 19 mutants killed) |
| M1.4 | Package with a few real photos, install, Pocket run, owner review (grid, picture quality, buttons, load times) | packaged and installed 2026-11-09 as DEV 005 (`package_dev_build.py --variant galleryapp --photos ../test-images/unsplash`, five photos); **not run on a Pocket yet** |
| M1.5 | Delete the music player pieces and the audio source files (M0.8's second half) once M1.3 stands on its own | **done** 2026-11-09 (commits 1e1640b, 0565276; B-1010, B-1011) |
| M1.6 | Gallery screens on the kept UI layer: Settings, Controls + Help, Info, Theme (owner choice 2026-11-09, all four). See section 5 | designed, not started |

## 2. Decisions taken (change any of them cheaply now, expensively after M1.4)

1. **The fit view is one contiguous read.** The M1 level of a picture is the "fit canvas": the picture with its EXIF orientation applied, scaled to fit 800 x 720 (never enlarged unless `--upscale`), centred on black, stored exactly as the viewer plane wants it (rows of 800 pixels of R, G, B bytes, 2,400 bytes a row, 1,728,000 bytes). Opening a picture is one data-slot read of that run with the landing window set to the plane base: about 1.1 s at 1.6 MB/s (measured rate for large commands), no copy, no tile placement. The plan's tiles (128 x 128, tile-contiguous) are for zoom and pan (M2): they need strided placement into a plane, which is Talos's unverified reach (A1 to A4), and M1 does not wait for that. The level table of the file has `layout` and `mode` fields so tile levels and TGY payloads are added later without a new format; a reader rejects a level it does not know.
2. **A picture is ONE file, 4 KB aligned inside.** Header (64 bytes), level table, the 96 x 72 RGB565 thumbnail at 4,096, the canvas at 20,480. File size 1,748,992 bytes for every picture in M1 (the canvas is always 800 x 720), so 100 pictures are 175 MB.
3. **The folder index is the library slot.** `tau-library.tdb` (a TGIX v0 file: header, 160-byte entries with the path on the card, a Latin-1 display name, size, original width and height, then the thumbnail atlas, 13,824 bytes each in entry order) is read through data slot 5, whose declared file is `tau-library.tdb`: the converter writes the index under that name (`--index-name`) so no new slot or loader entry is needed. A page of 16 thumbnails is 221 KB contiguous in the atlas: one read.
4. **Pictures are opened by path** with the existing open-by-path command (`slot_open_path`, proven for tracks) on the picture slot (slot 2, renamed "Picture" with the extension `tgy` in M1.3). The path comes from the index.
5. **Pixels stay 8 bits per channel in the file** even though the handheld display shows 5/6/5 (docs/M0_DESIGN.md 6d, skill entry KB-237): the dock may show more, and the stored format should not be the limit. Thumbnails are RGB565 (they are only ever shown in the 565 UI). A dither to 565 in the viewer's last stage stays an option (M0 open item).
6. **The grid is drawn by firmware in UI mode**: 4 x 4 thumbnails of 96 x 72 on the 400 x 360 UI (plan section 7). A thumbnail reaches the screen as 4 KB reads into the tag zone and mailbox writes into the framebuffer (about 30 ms each, 0.5 s a page); a faster path (Talos copy from SDRAM rows) is an M2 matter.
7. **Names:** the display name is the file name without extension, Latin-1 (other characters become `?`), 39 bytes; the path is UTF-8, under 96 bytes. Longer paths stop the converter with a clear error, they are never cut.
8. **Orientation and colour:** the converter applies EXIF orientation (all eight) and converts to RGB; ICC profiles are not applied in M1 (sRGB assumed), a known limit.

## 3. File formats (v0)

TGY header, little-endian, 64 bytes: magic `TGY\0`, u16 version 0, u16 header size 64, u32 width, u32 height (orientation applied, original size), u8 channels 3, u8 tile_log2 7, u8 orientation 0 (applied), u8 flags, u32 level count, u32 level table offset (64), u32 thumbnail offset, u32 thumbnail bytes, u16 thumbnail width and height, u32 meta offset and length (0), u32 file size, u32 CRC-32 of bytes 0 to 51, 8 reserved. Level entry, 48 bytes: u32 width, u32 height of the canvas, u16 content x, y, w, h (the picture's rectangle on the canvas), u8 layout (0 raster), u8 mode (0 RAW), u8 pixel format (0 RGB888), u8 kind (0 fit canvas), u32 row stride (2,400), u32 data offset, u32 data length, u32 index offset and length (0), u32 CRC-32 of the data, u32 reserved. The reference implementation is `tools/gallery/tgy.py` (`pack_tgy`, `parse_tgy`, `level_bytes`, `pack_index`, `parse_index`); the tests are the specification of the damage a reader must refuse (bad magic, version, header checksum, size, level checksum, unknown layout, mode or pixel format).

## 4. Open items for M1.2 to M1.4

- Slot 2's declaration (name "Picture", extension `tgy`) and whether the Pocket's own file picker should be used instead of the index (it is not: the player's way, an index written by a host tool, is the proven one).
- A card layout for the pictures (`Assets/tau_gallery/common/photos/` by default).
- The first-picture experience: a progress bar while 1.7 MB lands (about 1.1 s), and which key does what (A opens, B back, L1 and R1 page, Left and Right neighbour picture).
- Memory: a plane is 1.7 MB of the 64 MB SDRAM; the next picture can be landed into a second plane while the first is shown (M2 prefetch), the viewer switches by the base register at a vblank.


## 5. M1.6 gallery screens (designed 2026-11-09, owner chose all four: Settings, Controls + Help, Info, Theme)

**Base that stays:** `ui/helios` (beam-aware dirty regions), `cores/view_core.h` (view table with enter, exit, input, draw), `ui/regions.h`, `ui/theme*` with `cores/theme_core.h` and `colour_core.h`, `cores/help_core.h`, `controls_core.h`, `menupages_core.h`, `configure_core.h`, `counters_core.h`, `about_core.h`, and the kept views `settings_view`, `controls_view`, `help_view`, `configure_view`, `appearance`. **What they still include and must lose:** `transport/mp3play.h` (player state), `aout/aout.h`, `meter.h`, `meter_registry.h`, `nowplaying.h`, `xfade.h`, `meterlist_thumb.h`, `halcyon_view.h`, `transport/browser.h` (all deleted). `ui/views.c` is the player's view table and is rewritten for the gallery: views GRID, VIEWER, MENU, SETTINGS, CONTROLS, HELP, INFO.

**Proposed shape (change any of it before M1.6 starts):**
1. Start opens a menu from the grid (the viewer keeps B = back, Left/Right = previous/next). Menu entries: Settings, Controls, Help, Info. Select-Start combos stay free.
2. **Controls and Help** are static text pages (no persistence): the grid and viewer key map and what the status lines mean. Low risk, first.
3. **Info** shows what the firmware already knows: core and CORE_VERSION, ROM build, picture count, index build, the card read counters (`counters_core`), the last open time. First after Controls and Help.
4. **Theme** (colour roles and font of the grid and status lines) reuses `theme_core`; the choice has to persist, so it waits on item 5.
5. **Settings need a persistence design (owner decision).** The kept `settings/settings.c` and `cores/settings_core.h` are the APF register file (`interact_persist.json`) and every field in `sf_table` is a music field (volume, meters, tunes, last played); the Gallery packages declare no persist words (`add_library_slot(persist_words=False)`). A gallery table (theme, font, last picture, sort, slideshow interval) needs its own field list, a layout version of its own, `core.json`/`interact.json` persist words for it, and a package change; the mechanism (publish only when nothing is streaming, never a file write) stays. Persistence is the one item that touches the package and needs a run on a Pocket to prove.
6. Tests per screen, in the style of M1.3: host test of the cores (existing), and the real firmware against the hardware model for each page, with mutants.

**Order:** Controls + Help, then Info, then the settings table and Settings, then Theme. Each slice is one commit and one DEV build.
