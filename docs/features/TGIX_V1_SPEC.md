# TGIX version 1: the library index of Tau Gallery 1.0 (DRAFT specification)

Status: **draft for the owner, 2026-11-13; nothing built.** It carries the owner's decisions of that day (`docs/GALLERY_1_0_PLAN.md` section 4): one level of folders, square thumbnails, several grid sizes, details in pages with no empty fields. Today's index is TGIX **version 0** (`tools/gallery/tgy.py`, `fw2/gallery/gallery_fmt.c`). The field value of the new version is **1**. Little-endian throughout. The file is the data slot 5 file `tau-library.tdb`; the pictures it lists are `.tgy` files read by their path.

## 1. Goals and what stays
- **Stays:** one file the PC tool writes and the firmware reads page by page (never whole: the CPU has no room), a thumbnail atlas after the entries, 4 KB padding at the end (a 4 KB read of the last piece must not run past the file), every offset and count checked by the reader before it is used.
- **New:** a folder table (one level), per-picture **details** stored only for the fields a picture has, **square thumbnails** (96 x 96) with their size in the header, a sort key.
- **Compatible:** the firmware reads version 0 (one folder called ALL PICTURES, 96 x 72 thumbnails shown as they are, no details) and version 1. The converter writes version 1 only; `--merge` and `merge_index.py` turn an old index into version 1 (the old thumbnails are padded to 96 x 96 with black).

## 2. Layout
`header (64 B) | entries (count x 176 B) | padding to 4 KB | atlas (count x 18,432 B) | padding | folders (fcount x 48 B) | details bytes | padding to 4 KB`. Sections start on a 4 KB boundary except the header; their offsets are in the header.

### 2.1 Header (64 bytes)
| Offset | Size | Field |
|---|---|---|
| 0 | 4 | magic `TGIX` |
| 4 | 2 | version = 1 |
| 6 | 2 | header size = 64 |
| 8 | 4 | entry count (at most 65,535) |
| 12 | 2 | entry size = 176 |
| 14 | 2 | thumbnail width = 96 |
| 16 | 2 | thumbnail height = 96 |
| 18 | 2 | folder count (at least 1, at most 255) |
| 20 | 4 | entries offset |
| 24 | 4 | atlas offset |
| 28 | 4 | folders offset |
| 32 | 2 | folder entry size = 48 |
| 34 | 2 | sort mode: 0 name, 1 date taken, 2 as converted |
| 36 | 4 | details offset |
| 40 | 4 | details length |
| 44 | 4 | flags (0) |
| 48 | 12 | reserved (0) |
| 60 | 4 | CRC-32 of bytes 0 to 59 |

The reader refuses a header whose CRC, sizes or offsets do not fit the file or the reader (an entry size it does not know, a thumbnail size other than 96 x 96, a section past the end).

### 2.2 Entry (176 bytes)
`path[96]` (UTF-8, NUL padded; the card path from the card root, never cut: the converter skips a picture whose path does not fit), `name[40]` (Latin-1, the display name), `size u32` (file bytes), `width u32`, `height u32` (the picture after its EXIF orientation), `thumb u32` (atlas number), `folder u16` (index into the folder table), `flags u16` (bit 0: has details), `details_off u32` (offset inside the details section), `details_len u16`, `reserved u16`, `taken u32` (seconds since 2000-01-01 00:00:00, 0 unknown: the sort key for sort mode 1), 8 reserved bytes (field offsets: size 136, width 140, height 144, thumb 148, folder 152, flags 154, details_off 156, details_len 160, reserved 162, taken 164). Entries are sorted by folder, then by the sort mode, so **a folder is the range [first, first + count)** of entries and the grid code works on an offset and a count.

### 2.3 Folder entry (48 bytes)
`name[32]` (Latin-1), `first u32`, `count u32` (the entries are `first .. first+count-1`, at least 1), `cover u32` (atlas number of the cover thumbnail; the first picture's by default), 4 reserved. One level only: a folder holds pictures, never folders. A library of pictures not in any subfolder has one folder, **ALL PICTURES**, and the folder level is skipped by the viewer.

### 2.4 Details (per picture)
A run of items, `tag u8, length u8, bytes`, ending at `details_len`. A reader skips a tag it does not know; a field the picture does not have is simply not there (**no empty fields are stored or shown**).
| Tag | Item | Bytes |
|---|---|---|
| 1 | FORMAT of the source (JPEG, PNG, TIFF, WEBP ...) | ASCII up to 15 |
| 2 | CAMERA (make and model) | Latin-1 up to 40 |
| 3 | LENS | Latin-1 up to 40 |
| 4 | TAKEN | u16 year, u8 month, day, hour, minute, second (7) |
| 5 | SHUTTER seconds | u32 numerator, u32 denominator (8) |
| 6 | APERTURE | u16 f-number x 10 (2) |
| 7 | ISO | u32 (4) |
| 8 | FOCAL LENGTH mm | u16 x 10 (2) |
GPS is never stored. All of it comes from the EXIF of the source file, read by the converter (Pillow).

## 3. Thumbnails and the grid sizes
- **Stored:** one square 96 x 96 RGB565 thumbnail per picture, the picture **scaled to cover and cropped to the square** (a centre crop; a better crop point is a later converter option). 18,432 bytes each: a page of 12 is the same 221 KB as today's page of 16.
- **Grid sizes** are a Settings choice, a table in the firmware: 96 px cells in 4 x 3, 72 px in 5 x 4, 64 px in 6 x 5, 48 px in 8 x 6 (the exact gaps and rows are set on the screen). **Correction to the plan:** the Talos scaled blit (`OP_SBLIT`) only **enlarges** by 1, 2 or 3 (`gfx.h`); it cannot scale down. The smaller cells are made by the **CPU** when a page is drawn: a 2 x 2 box average for 48 px, a bilinear average for 72 and 64 (about 3 ms a thumbnail on the 66 MHz core, estimated, not measured: a page of 30 is about 90 ms). If that proves slow, the converter can write a second small atlas, or the Talos downscale mode of the plan's package K2 does it in hardware (RTL, one fit batch with the dither).
- **Corners:** a mask with a radius of 8 px at draw time (a table of the inset per row), the selection frame a rounded outline of the same radius; the hardware rounded rectangle (`OP_RRECT`) for frames and backgrounds.
- **Folder tiles** use the same cells: the cover thumbnail with a darkened strip at the bottom carrying the folder name and the picture count.

## 4. The viewer's use
- **Grid:** `PER_PAGE = cols x rows` of the chosen size; paging, selection and resume work on [first, first + count) of the open folder; resume stores folder and index with the path CRC as now (a changed library falls back to the first folder).
- **Folder level:** shown first when there is more than one folder: a grid of folder tiles, A opens, B goes back to it from a folder's grid, B on the folder level leaves as today. The viewer's Left/Right wraps inside the folder.
- **Details pages** (Select): left and right turn the pages, B or Select closes. A small indicator at the top right: the page number over the page count and a dot for each page (`1/3`). Pages: **INFO** (name, picture n of N, size, file, and from the details only the ones that exist: format, camera, lens, taken), **SHOT** (shutter, aperture, ISO, focal length; **the page is not offered when none of them exists**), **HISTOGRAMS** (of the picture's own part of the thumbnail, as now).
- **RAM:** an entry page buffer of PER_PAGE x 176 bytes (3.5 KB for 20) instead of 16 x 160; the folder table is read a tile page at a time, never held; the details of one picture are read on demand (at most 255 bytes) when its page opens.

## 5. The converter (`tgy.pack_index`, `tgy_convert`, the app)
Writes version 1; walks the source folder one level deep (each subfolder is a folder of the library; pictures in the top folder go to ALL PICTURES when there are no subfolders, else to a folder named after the source folder); reads EXIF; crops the thumbnails; sorts (sort mode a converter option, default by name); `--merge` keeps the library's existing entries, folders and thumbnails (an old version 0 index is upgraded). `merge_index.py` and `tau_compat` (the card check, its schema) learn version 1. The converter app gets a source-folder choice that shows the folders it found.

## 6. Tests (project rules: reference + mutants for the parser, fuzz with CRC repair)
Host: a reference writer and reader for every field; the firmware reader against files the tool wrote, **fuzzed** (header fields at edge values, offsets and counts past the end, 32-bit wrap-around cases such as `off + count * size`, a folder range outside the entries, a details run that overruns, an unknown tag); the paging and range maths; the thumbnail down-scalers against a floating-point reference; the corner mask against the circle equation; details formatting (nothing printed for a missing field; page count with and without a SHOT page; the indicator). App harness: a library with folders, with one folder, a version 0 library, a damaged folder table, resume with a folder. Pocket: the look of each grid size and the corner radius, the folder level, details pages.

## 7. Open points
The crop point for faces; whether the SHOT page also lists the lens settings; the sort of the folder level (by name only); whether the folder cover can be chosen in the app; the thumbnail down-scaler's cost on the real core (to be measured with the Info page's timers before the sizes are fixed).
