# Tau Gallery 1.0: the plan (TGY only)

Status: **draft, 2026-11-13**, written with the owner's decision that 1.0 ships the complete **TGY** path only (the lossy tiled-DCT format drawn by the hardware decoder) and the other formats (TGX lossless, direct JPEG/PNG, camera RAW, container v2) come in 2.0, 3.0 and later. The order of work is the owner's to set; this file proposes one. Evidence in the audit trail (B-1063 to B-1108).

## 1. What 1.0 is
A gallery core for the Analogue Pocket that shows a library of pictures converted on a PC with the Tau Gallery Converter: a grid with thumbnails, a fit view, zoom and pan of any picture the converter can make (tested to 233 megapixels), a map to jump around a zoomed picture, a details page, settings (resume, theme), Help, Info. The decoder is the hardware TGY decoder in the core (bit-exact in simulation, fitted with margin, run on a Pocket).

## 2. Where it stands (DEV 022 on the owner's card, DEV 023 packaged)
Done and run on a Pocket: grid, held keys, fit view, v1 zoom and pan (smooth since DEV 020), pipeline, Info, details page, settings, themes, library of 56 pictures. Done on the host, not yet on a Pocket: histograms of the picture only (DEV 022), the card-resident tile index (DEV 021, partly tried: the owner's big pictures open), the picture-rectangle fix for the navigator (DEV 023). Converter: format choice, parallel tile coding (2.9x), 4:4:4 zoom levels re-encoded as 4:2:0, wide-gamut JPEGs encoded from the sRGB picture so the colours do not jump on zoom.

## 3. Work packages (proposed order)

| # | Package | What is in it | Needs the owner |
|---|---|---|---|
| A | Correctness found by testing | **done**: fit rectangle of a version 1 picture (navigator and zoom entry were wrong for any shape that does not fill 800 x 720), histograms without the letterbox bars, 4:4:4 zoom, ICC colour consistency, converter reads. Left: install DEV 023 and run it on a Pocket | install, the world map and a portrait picture |
| B | Library format version 2 | One new index format that carries: **folders** (folder table; entries sorted by folder), **details** per picture (format, camera, taken, exposure from EXIF, read by the converter), the **thumbnail size** as a field (for square thumbnails), the sort key. TGIX version 1 keeps reading (one folder, "ALL PICTURES"). Converter writes it, merge tool merges it, `tau_compat` and its schema know it, the firmware reads it. Spec: `features/GALLERY_FOLDERS_COLLECTIONS_SPEC.md` | **decisions below** |
| C | Folder UI and the grid's look | folder grid, open, back, breadcrumb, resume with folder; square thumbnails (cover crop in the converter) and rounded corners (firmware mask; the hardware rounded rectangle, `OP_RRECT`, for frames and tiles); a build with a switch to compare cell sizes on the real screen | cell size, corner radius |
| D | Rotated pictures | a picture with an EXIF orientation shows in fit but has no zoom. The converter writes the zoom level upright (re-encode first; lossless coefficient transpose later) | none |
| E | Converter app for users | builds for macOS, Windows and Linux (so far a macOS bundle only); no Python install for users (bundle a runtime, or a compiled helper: a decision); optional `jpeglib` for the JPEG read (19 to 75 times faster, same coefficients, measured) with the pure-Python fallback; skip the coefficient read for 4:4:4 (free, about 50 s of 83 s on a 102 megapixel photo); progress and error messages; code signing and notarisation on macOS; a user guide | Python/helper decision, signing |
| F | Release engineering | a release core (not `DEV NN`): name, id, platform folder, icon and cover art, `tau-compat` release manifest, a gallery `make_release`, checksums, release notes, `NOTICE` (numpy, Pillow, Tauri, the CPU core, Altera megafunctions), `PROVENANCE`, a card layout a user can copy | name, art, version number |
| G | The bitstream | the shipped fit is the m1d **seed 1** (setup +0.659 ns, hold +0.084 ns; seed 2 misses hold by 49 ps): fragile. Run 3 to 4 seeds, choose with margin, record the Quartus report baseline (`make gate-report`) | VM fits (1h45 each, your yes and a stated finish time) |
| H | Validation breadth | the Dock and a TV (never tried), other SD cards (sizes, brands, exFAT only?), a library of 500 to 1,000 pictures, sleep and wake, a card pulled during a read, a damaged picture, a long soak; the 128 KB RAM link has only 2,640 bytes of margin over its floor (B-1105), so every feature in B and C must give RAM back | the Pocket, cards, a TV |
| I | Test debt | `make verdict` on the final build, a long fuzz of the container and index readers (with the new card-resident index), the golden frame refreshed to cover a zoom, the whole-core run with a zoom, the wfifo error path | none |
| J | Installer for media | `install_dev_core.py --add-media` (merge a converter output into the card's library, backup, verify), so the card is never written by hand again | none |

## 4. Decisions needed from the owner (blocking B and C)
1. **Thumbnail cell**: (a) 72 x 72 squares, 5 columns x 4 rows (20 a page), (b) 96 x 96, 4 x 3 (12 a page), (c) keep 4 x 4 with 72 x 72 and more air. Corner radius (8 to 12 px).
2. **Folders**: one level only, or nested? Folder cover = first picture, or a chosen one? Sort inside a folder: name, date taken?
3. **Details**: which EXIF fields (camera, lens?, taken, exposure, ISO, focal length, GPS never?), and whether to keep the placeholder rows that stay empty for pictures with no EXIF (hide them).
4. **Converter distribution**: bundle a Python runtime (large, simple) or ship a compiled helper (a Rust port of the JPEG read and the tile coder: fast, no Python, real work)?
5. **Name and version**: the release core's name and id, and whether 1.0.0 is the first public number.

## 5. Risks
- **RAM**: 2,640 bytes of margin on the 128 KB link. Folder tables and details strings need RAM; plan to reclaim it (the 32 KB zoom index buffer is shared with boot already; the Info and pages code can share buffers).
- **Format change late**: B changes the index and the converter and the firmware together; do it once, early, with a version field, then freeze.
- **The fit**: any RTL change restarts G. B and C need none; D none. Keep it that way for 1.0.
- **Untested hardware paths**: the Dock, other cards, 1,000 pictures. H should start as soon as DEV 023 is proven, in parallel with B and C.

## 6. Suggested sequence
1. Install DEV 023, confirm A on a Pocket (world map, portrait, histograms).
2. Decide section 4 items 1 to 3; then B (format) and C (folders and look) together, behind one format freeze.
3. D, then E and J (they do not touch the firmware).
4. G in parallel as soon as no RTL change is expected; H as cards and a TV are at hand.
5. I, then F, then a release candidate that gets a full Tier C sweep (`CLAUDE.md` section 4).
