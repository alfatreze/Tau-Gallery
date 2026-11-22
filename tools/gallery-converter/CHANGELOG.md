# Tau Gallery Converter: changes

## 0.3.0 - 22 November 2026

Folders inside folders, and no more skipped pictures for a long file name. Made with Tau Gallery core 1.0.0; a library from this version needs that firmware to show the tree (an older one shows every folder as a folder of its pictures).

- **Your folder tree is kept.** The pictures are copied to the card nested, as in your source (`photos/Trip/Day 1/a.tgy`), and the library index holds the whole tree. The Pocket shows it either way, by a setting in the core (Settings, FOLDERS): flat, where a top folder holds every picture under it, or nested, where you walk down the tree. This is the new default ("Keep them"); "flatten" and "join the names" stay as options and copy the pictures flat, with no tree. A folder that holds pictures and folders gets its own pictures into a folder called THIS FOLDER.
- **Folders are read at every depth.** The window says how many pictures and folders it found; hidden folders are skipped.
- **A long file name is shortened to fit.** The card path may be 95 bytes; a longer one used to be skipped, now the stored file name is shortened (two names that shorten alike get `_2`, `_3`), and the gallery still shows the whole name.
- **Adding to a library** (the default) works with the tree: the pictures already in nested folders stay where they are.
- **Same pictures, same files.** Nothing changed in how a picture is encoded.
- **Still true:** a picture that is converted again always replaces the one of the same name; it is not skipped.

## 0.2.0 - 15 November 2026

The converter now has its own version and its own releases (`converter-vX.Y.Z`), separate from the core's (`vX.Y.Z`). The 0.9.0 the app carried in the core's v0.9.0 release was the core's number; this release supersedes it.

The converter is now part of the app. It is a Rust port of the Python converter (the `tgy-core` crate), so the app needs nothing else installed, and it is much faster.

- **Only the hardware-decoder format in the app.** The page no longer offers the plain (version 0) format or its "Zoom levels" option: the app always writes the hardware-decoder files (better quality, small files). The plain format stays in `tgyconv` and the Python tools; another format (TGX) is planned.
- **No Python.** Python 3, Pillow and numpy are no longer needed, and the Python setup panel is gone from the page.
- **Much faster: about 50 times on average.** Measured on 8 photos (2.5 to 102 megapixels, JPEG) on one Apple-silicon Mac, the old Python converter against this one, same machine, same files: the average speed-up is **58 times** (geometric mean 48 times; per photo 19 to 102 times), and the total time for the 8 photos fell from **236 s to 3.3 s** (71 times). A 102 megapixel JPEG took 98 s and now takes 1.1 s; the five ordinary photos (3 to 16 s each) now take 0.1 to 0.3 s each. It uses every core. The speed-up is the same code path on every platform, but the figures were measured on macOS only.
- **Very large pictures work.** A 416 megapixel, 1 GB TIFF that the Python converter refused (Pillow's limit of 179 megapixels) now converts in 8.5 s on an 8-core Mac to a 198 MB file that passes every checksum. A picture that size needs about 4 GB of memory while it converts.
- **Same files.** A JPEG's own coefficients, the fit view, the thumbnails, the library index, the EXIF details and the plain (version 0) files are byte for byte what the Python converter wrote, checked on test sets (including a 102 megapixel photo) and against the frozen symbol-layer vectors. Two valid differences: a level encoded from pixels can differ by one in a few coefficients per million (rounding ties), and pictures with a non-sRGB ICC profile are converted with a different colour engine (at most one level per channel).
- **macOS opens it.** The bundle is ad-hoc signed and sealed, so macOS lists it under System Settings, Privacy & Security, where Open Anyway allows it. Before, the unsealed bundle was reported as damaged. It is not notarized.
- **Windows and Linux builds.** Windows (installer and portable zip) and Linux (deb and AppImage) are built for the first time; the card list reads drives D: to Z: on Windows and the usual mount folders on Linux. Output folders typed with backslashes (`E:\Assets\tau_gallery\common`) are understood.
- **Not read:** JPEGs with four components (CMYK), arithmetic-coded and 12-bit JPEGs are skipped, and said so.

## 0.1.0

The first version: the Tauri shell around the Python converter, macOS only, not signed.
