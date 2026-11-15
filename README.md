# Tau Gallery

<img width="400" height="360" alt="Tau Gallery grid" src="https://github.com/user-attachments/assets/b3b89f90-f357-4e1a-8b28-e34ed9548dd0" />
<img width="800" height="720" alt="Tau Gallery viewer" src="https://github.com/user-attachments/assets/a42b7f17-764c-431f-91da-00352ed44164" />
<img width="400" height="360" alt="Tau Gallery on the Pocket" src="https://github.com/user-attachments/assets/f021955d-24da-4e0b-870f-5682cceefbe7" />

**A photo gallery for the Analogue Pocket with a hardware image decoder.** Browse a grid of thumbnails, open a picture full screen, zoom in to its full resolution and pan across it, even when the picture is tens or hundreds of megapixels. The pictures are decoded by a decoder built into the Pocket's FPGA, not by its small CPU, so opening and panning feel immediate.

Tau Gallery is an openFPGA core (`alfatreze.Tau Gallery`). Version 0.9 is a beta: see [Status](#status).

## Get started in three steps

1. **Install the core.** Download `alfatreze.Tau_Gallery_*.zip` from the [releases](https://github.com/alfatreze/Tau-Gallery/releases) and unzip it onto the root of the Pocket's SD card (it merges with the card's `Cores`, `Platforms` and `Assets` folders).
2. **Convert your pictures** with the **Tau Gallery Converter**, a small desktop app: pick a picture or a folder, pick an output folder, press Convert. (macOS, Windows and Linux, from the [releases page](https://github.com/alfatreze/Tau-Gallery/releases): the converter has its own releases, `converter-vX.Y.Z`, separate from the core's. On any system with Python there are also command-line [encoding tools](tools/gallery/README.md) that do the same.)
3. **Copy the `Assets` folder** the converter wrote onto the card (merge it with the existing one). Start "Tau Gallery" on the Pocket.

The core shows an empty library until you convert pictures. Your originals are never changed.

## What stands out

- **Hardware decoding.** Pictures are stored in **TGY**, a tiled format (128 x 128 pixel tiles) designed together with the decoder: an adaptive range coder, the integer DCT and 4:2:0 or 4:4:4 colour are all done in the FPGA. A tile takes about 6 ms on the Pocket; the CPU does only the bookkeeping.
- **Huge pictures.** Each picture is stored as a small "fit" level (what you see first) plus its full-resolution level, cut into independent tiles. Zoom and pan read and decode only the tiles on screen. Pictures of more than 400 megapixels have been converted and opened on a Pocket; the measurements below were taken on a 21-megapixel (6000 x 3575) picture.
- **Lossless from JPEG.** A baseline or progressive JPEG goes in as its own DCT coefficients, re-packed without re-encoding: no quality loss, and the result is **smaller than the JPEG** (see [compression](#compression)).
- **Grid of four sizes**, 4 x 3, 5 x 4, 6 x 4 or 8 x 6 pictures a page, with square thumbnails that fill their cell, rounded corners and a rounded selection frame, drawn by the core's own graphics engine.
- **Folders and details.** One level of folders (your subfolders become folders of the gallery). Each picture has a details page set with a page indicator: INFO, SHOT (camera, lens, date taken, shutter, aperture, ISO, focal length: only the fields the picture has) and HISTOGRAMS (luma, red, green, blue, **counted by the hardware decoder** while the picture is opened in the viewer; from the thumbnail otherwise).
- **Look and viewing tools.** Turn a picture 90 degrees either way, mirror it, overlay **crop masks** (1:1, 4:5, 3:2, 16:9, CinemaScope, XPan, 3:1, each turnable) to see how a picture looks at a different crop, and choose a **colour preset** (normal, bright, contrast, midtones, faded, warm, cool, invert). A hardware **dither** (ordered, or ordered with motion) hides the banding the Pocket's 5/6/5-bit panel would show.
- **Comfort.** Resume on the last picture, one-key map to jump around a zoomed picture, themes with 19 accent colours and a dark or light mode, a Controls page and an Info page with timings.

### Controls

| | |
|---|---|
| **Grid** | Left / Right previous / next, Up / Down one row, L1 / R1 page back / on, A open, Select details, Start menu |
| **Picture** | Left / Right previous / next, A zoom in, B back to the grid, X / Y turn, Start + X / Y mirror, L1 crop mask (Start turns it), Select details |
| **Zoomed in** | D-pad pan, Start map (jump to a spot), L1 / R1 zoom out / in, A or B back to fit, Select + Left / Right previous / next |

## Speed

Measured on a Pocket (a 6000 x 3575 pixel, 5.8 MB picture, build DEV 020 and DEV 014 of this core; one session each, so read the numbers as typical, not as guarantees):

| | Measured |
|---|---|
| Decode of one 128 x 128 tile (hardware) | **6.2 ms** on average, 7.4 ms at most (about 2.6 megapixels a second) |
| Read of one tile's data from the SD card | 10.9 ms on average, 20.5 ms at most |
| Open a picture (fit view on screen) | **257 ms** |
| First zoomed view (a window of 50 to 70 tiles) | **538 ms** |
| Panning | 2 stalled frames out of 143 in a session |

For comparison, the same decode in **software on the Pocket's own CPU** (the real core simulated at 66.7 MHz, best case: no memory waits) takes **170 to 250 ms a tile**, so a screenful of 36 tiles would take 6 to 9 seconds, 27 to 40 times slower than the hardware. This is why the decoder is in the FPGA.

The slowest step is now the SD card, not the decoder.

## Compression

TGY is a recompression of JPEG-style data with its own context model and range coder, tile by tile so that any tile can be decoded alone.

**Model measurement** (five photographs, 1024 x 1024 crops, coefficients as stored in the JPEG; size relative to the progressive Huffman JPEG the photos came as, smaller is better):

| Coder | Size |
|---|---|
| JPEG baseline Huffman (optimised) | 1.038 |
| JPEG progressive Huffman (the originals) | 1.000 |
| JPEG arithmetic coding (`jpegtran -arithmetic`) | 0.963 |
| **TGY** | **0.933** |

**On whole pictures through the shipped converter** (four photographs with an sRGB profile, kept as their own coefficients; the "full level" is the full-resolution picture, the whole file adds the fit view, the thumbnail and the tile index):

| Photo | JPEG (MiB) | TGY full level (MiB) | TGY file (MiB) |
|---|---|---|---|
| 2560 x 1705 | 2.43 | 2.29 (0.94) | 2.47 (1.02) |
| 3648 x 5472 | 3.47 | 3.02 (0.87) | 3.13 (0.90) |
| 4878 x 3252 | 3.03 | 2.71 (0.89) | 2.82 (0.93) |
| 3718 x 3718 | 2.79 | 2.33 (0.83) | 2.48 (0.89) |

In this small sample the full level comes out 6 to 17 percent smaller than the JPEG (11.6 percent on average), the whole file between 2 percent larger and 11 percent smaller. A picture with a wide-gamut profile (Adobe RGB, ProPhoto) or a PNG or TIFF is converted to sRGB and encoded at the chosen quality (default 92), so its size reflects that quality and is not comparable to the source.

Newer codecs (WebP, AVIF, JPEG XL) compress better than JPEG, but their entropy decoding and loop filters do not fit a small FPGA decoder; they were left out on purpose and **not measured here**. TGY's gain over JPEG is random access to tiles and a decoder small enough for the core, with a few percent of size as a bonus.

## The Tau Gallery Converter

The converter turns pictures into the files the core reads. It is the easiest way in, and the only app.

- **Pick or drop** a picture or a folder (JPEG, PNG, BMP, GIF, TIFF, WebP). Each subfolder with pictures becomes a folder of the gallery.
- **Output volume picker** with the free space of each disk, a **progress bar** and a **Stop** button; the app remembers its settings.
- **Add to the library already in the output folder**: keep what an earlier run converted, add or replace pictures, merge into the library on the card. Choose the **order inside a folder**: by name, by date taken, or as converted.
- **EXIF-aware**: the orientation is applied, EXIF details go into the library, wide-gamut photos are converted to sRGB.
- **Quality settings** for pictures that are not JPEG, and an option to **enlarge small pictures**.
- **Nothing to install**: the converter is built into the app and uses every core. It is about **50 times faster** than the Python tools on average (58 times over 8 photos of 2.5 to 102 megapixels, 19 to 102 times per photo; 236 s in total down to 3.3 s; measured on one Apple-silicon Mac), and writes the same files as the Python tools below (levels encoded from pixels can differ in a few coefficients per million; see the converter's [changelog](tools/gallery-converter/CHANGELOG.md)).

Needs nothing else installed. **macOS** (Apple silicon): ad-hoc signed, not notarised, so the first time open System Settings, Privacy & Security and choose Open Anyway. **Windows** (x64): an installer and a portable zip; **Linux** (x64): a `.deb` and an AppImage. The Windows and Linux builds are new in converter 0.2.0 and were built by the repository's workflow; they have not been run on those systems by the author, so tell me what breaks. The command-line [encoding tools](tools/gallery/README.md) run on any system with Python.

## Status

Version 0.9.0, a beta. The hardware decoder, the graphics and the converter are built and tested in simulation and on a Pocket. The newest parts (rounded corners, filled thumbnails, the latest Settings page) were built after the last full test on a Pocket, and the Dock has not been tried (the core says it is not supported). A picture of hundreds of megapixels converts, but slowly (minutes) and needs several gigabytes of memory.

## For developers

How the core is named, where its files go on the card, and how to build, package and release it: [docs/CORE_IDENTITY_AND_BUILD.md](docs/CORE_IDENTITY_AND_BUILD.md). The current state and the evidence for every claim above: [docs/CURRENT_STATUS.md](docs/CURRENT_STATUS.md) and [docs/AUDIT_TRAIL.md](docs/AUDIT_TRAIL.md). The TGY format: [docs/TGY_CONTAINER_V1.md](docs/TGY_CONTAINER_V1.md), [docs/TGY_SYMBOL_LAYER_V0.md](docs/TGY_SYMBOL_LAYER_V0.md) and the hardware decoder in [docs/TGY_HW_DECODER.md](docs/TGY_HW_DECODER.md).

## Licence

Apache-2.0 (`LICENSE`). Tau Gallery descends from the open-source HarpMudd MP3 Player by way of Tau Alpha; the upstream MIT license is kept in `LICENSE-MIT`, with the attribution in `NOTICE` and `NOTICE.md`.
