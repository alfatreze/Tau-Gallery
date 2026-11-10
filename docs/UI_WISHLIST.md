# Gallery UI wishlist (owner ideas, not specified yet)

Raised 2026-11-10: a **histogram** and **more visual menus**. **Owner decision (same day): these come at the end of the plan; until then the work follows what is already defined** (TGY and TGX hardware first, `docs/M3_STREAMING_NOTE.md` section 0). Nothing here is scheduled before that. All of this is firmware or host-tool work unless marked, so it can be done between Pocket runs and Quartus fits.

## Histogram
| Step | What | Cost | Notes |
|---|---|---|---|
| H1 | The converter computes a per-picture histogram (luma 256 bins, or R, G, B) and stores it in the TGY header's meta area (about 1 KB; `meta_off` and `meta_len` exist and are 0 today); the firmware draws it on an **info page in UI mode** (bars with `gfx_rect`, plus size, orientation and later EXIF) opened by a key from the viewer or the grid | host tool and firmware, no RTL | shows the whole picture's histogram, not the zoomed view's; costs a file-format meta chunk (version it) |
| H2 | Histogram of **what is on screen** (the zoomed window): a hardware tap in the viewer scan-out, 3 x 256 counters in about 1 to 2 M10K, read by the CPU at a vblank | small RTL, one fit | needs the scan-out to count pixels as they leave the line buffer; the value is a live clipping and exposure check while panning |
| H3 | The histogram **over the picture** (an overlay in viewer mode) | needs the overlay compositor of plan M4 (about 414 ALMs and 3 M10K) or a CPU draw into the plane | not before M4; the info page of H1 is the interim |

## More visual menus
Ideas the current firmware can do without RTL (the UI plane is drawn by Talos with rectangles, text, blits, the 565 alpha blend and the icon system that is already in `fw2`):
- icons beside the menu entries (Controls, Help, Info, Settings, Theme, Resume) from the existing icon set, a selection bar and a scroll indicator;
- settings as visible controls: toggles and sliders drawn as such, with the value shown;
- a preview strip or a larger thumbnail of the selected picture inside the grid menu, the file name and its size and date on a status bar;
- an optional 3 x 3 grid of 128 x 96 thumbnails (plan section 7 keeps it as a setting), and a film-strip view in the viewer;
- animated transitions: a cross-fade between the grid and the picture, or between two pictures, using the Talos alpha blend (to confirm on the 565 UI plane; the plan also lists a 565 alpha check for RGB888);
- the zoom map (DEV 012) and its stop label and loader bar drawn in the same visual language.

Needs from the owner before building: which of these first, the look (themes exist: OCEAN, light and dark, accent), and what the info page should show.
