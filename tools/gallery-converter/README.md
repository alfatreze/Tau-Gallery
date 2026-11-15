# Tau Gallery Converter

A small desktop app (Tauri 2 + Svelte 5) that turns pictures into Tau Gallery files for the Pocket. Pick one picture or one folder, pick an output folder, press Convert.

The output folder gets the card's own tree:

```
Assets/tau_gallery/common/photos/NAME.tgy      one file per picture
Assets/tau_gallery/common/tau-library.tdb      the index (the thumbnails and the list)
```

Copy the `Assets` folder to the root of the card (merge it with an existing `Assets` folder).

## Format

- **Hardware decoder** (the default): the compressed container the Tau Gallery core with the hardware decoder draws (DEV 013 and later). Pictures open quickly and zoom to their full size; the files are small. A picture larger than the screen also gets its full-size level automatically (a JPEG keeps its own data as it is, with no loss; any other picture is encoded at the **Zoom level quality**, default 92; the **Fit view quality** is the picture as first shown, default 90). Needs **numpy** besides Pillow; the Python line at the bottom says whether it was found. A JPEG whose data is kept as it is converts quickly; the time goes into re-coding the picture's detail, so a dense photograph takes longest: a 102 megapixel one took about 5 minutes on one processor and about 105 seconds on this machine's 8 (the tiles are coded on several processors, the file is the same), and a flat 233 megapixel map 2 minutes.
- **Plain**: the raw tile format every Tau Gallery core reads; the files are large. The **Zoom levels** option below is for this format only.

## Options

- **Add to the library already in this folder** (on): keeps what an earlier run converted, so one picture can be added to a library. A picture with the same name is replaced; a picture whose file was removed is dropped from the index. Off starts a new library.
- **Zoom levels** (on; plain format only): large pictures also get the half and 1:1 zoom stops for the pan and zoom. About 45 MB more on the card for each large picture; small ones add nothing.
- **Enlarge small pictures** (off): pictures smaller than the screen show at their own size unless this is on.

Drag a picture or a folder onto the window to pick it. A folder is read one level deep: each subfolder with pictures becomes a folder of the gallery (deeper folders are not read). Pictures: JPEG, PNG, BMP, GIF, TIFF, WebP. File names on the card are ASCII only (anything else becomes `_` plus a short checksum); the name shown in the gallery keeps the original letters it can (Latin-1).

## What it needs

Nothing else installed. The converter is built into the app (the `tgy-core` crate in this folder, a Rust port of `tools/gallery/tgy_convert.py`): no Python, Pillow or numpy. It uses every core of the machine. Measured against the Python converter on 8 photos (2.5 to 102 megapixels) on one Apple-silicon Mac: **58 times faster on average** (geometric mean 48, per photo 19 to 102), 236 s in total down to 3.3 s; a 102 megapixel JPEG takes about 1 second.

## Platforms

- **macOS** (Apple silicon): `cargo tauri build --bundles app` (ad-hoc signed, see Status). Built and run here.
- **Windows** (x64): the executable cross-builds from a Mac with `cargo tauri build --runner cargo-xwin --target x86_64-pc-windows-msvc --no-bundle` (needs `cargo install cargo-xwin` and its one-time SDK download). That gives one `.exe` that needs only the WebView2 runtime Windows 10 and 11 already have; it was built but **not run on Windows**. The NSIS installer is not built on a Mac (`makensis` for macOS runs out of memory on this project): `.github/workflows/converter.yml` builds it on a Windows runner.
- **Linux** (x64): needs `webkit2gtk` and GTK development libraries, so it is built by the same workflow on an Ubuntu runner (`deb` and `AppImage`). The converter crate itself was type-checked for `x86_64` and `aarch64` Linux; the Linux app was **not built or run**.

The workflow also runs the page check and both test suites on each platform. It has **not been run on a runner yet**.

The app lists the cards it finds under `/Volumes` (macOS), `/media/USER` and `/run/media/USER` (Linux) and the drives D: to Z: (Windows, without label or free space); on any platform the output folder can be chosen by hand.

## Where it comes from

The shell is the Tau Encoder's (`Tau codecs/tau-encoder`): the Tauri 2 / Vite / Svelte 5 setup, its dependency versions and lockfile, the dialog plugin and capability, the look of the page. The work is done in process by `tgy-core` (`tgy-core/src`): the range coder and symbol layer (`tvcrc.rs`, `v0.rs`), the JPEG coefficient reader (`jpegcoef.rs`), the reconstruction (`recon.rs`), Pillow-compatible Lanczos (`resize.rs`), the encoder for pictures that are not JPEG (`encode.rs`), the container and index writers (`container.rs`, `index.rs`) and the folder run (`convert.rs`). It emits the same one-JSON-event-a-line records the Python tool printed (`plan`, `converted`, `skip`, `warning`, `done`); the app relays them to the page. The Python converter in `tools/gallery` stays as the reference the Rust port is tested against. `tgyconv` (`cargo run --release --bin tgyconv -- SRC OUT --card-layout --format v1`) is the same converter on the command line, with the options of `tgy_convert.py`.

Same output as the Python converter: the symbol layer matches the frozen conformance vectors bit for bit; a JPEG's own coefficients, the fit view, thumbnails, the folder index, EXIF details and the plain (version 0) files come out byte for byte the same (checked on test sets, including a 102 megapixel photo). Two things can differ by a rounding step and both are valid files: a level encoded from pixels (a picture that is not a JPEG, or a 4:4:4 or wide-gamut JPEG) can differ by one in a handful of coefficients per million where a value falls exactly on a rounding tie (numpy sums in an order that is not reproducible), and a picture with a non-sRGB ICC profile is converted with `moxcms` instead of LittleCMS (at most one level of difference per channel). Not read: JPEGs with four components (CMYK), arithmetic-coded or 12-bit JPEGs; the file is skipped and said so.

## Develop

```sh
npm install
npm run check                 # svelte-check
npm run dev                   # the page in a browser tab, with stand-in data (no app needed)
cd tgy-core && cargo test --release   # the converter against the Python reference's values
cd src-tauri && cargo test    # the app's backend (a folder, one picture added, a picture that cannot be read, stopping a run)
cargo tauri dev               # the app
cargo tauri build --bundles app
```

Tests: `tgy-core/tests/vectors.rs` runs the frozen symbol-layer vectors (`docs/conformance/tgy_v0_vectors.json`); `tgy-core/tests/golden.rs` pins coefficients, pixels, Lanczos output and a whole container to values taken from the Python reference. The Python converter's own modes are tested by `sim/test_tgy_convert_modes.py` (`make test-tgy`).

## Status

0.2.0. The macOS build is ad-hoc signed (`signingIdentity: "-"` in `tauri.conf.json`), not notarized, so the bundle is sealed and macOS lists it under System Settings, Privacy & Security, where "Open Anyway" allows it the first time. If a downloaded copy still says it is damaged, run `xattr -dr com.apple.quarantine "Tau Gallery Converter.app"`. Windows and Linux are not tried.


## Tau Gallery 1.0 library (folders, details, sort)
The converter writes the version 1 library index by default: the subfolders of the folder you choose (one level) become folders of the library (pictures directly in the chosen folder go to a folder named after it, or to ALL PICTURES when there are no subfolders), each picture keeps the EXIF details it has (camera, lens, date taken, shutter, aperture, ISO, focal length: nothing is stored for a field the picture does not have), and thumbnails are 96 x 96 squares cropped from the middle of the picture. The "Library" section sets the order inside a folder (by name, by date taken, as converted) and, for a gallery before 1.0, the old flat index. Adding to a library already on the card (version 0 or 1) keeps its pictures, thumbnails and details.
