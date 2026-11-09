# Tau Gallery Converter

A small desktop app (Tauri 2 + Svelte 5) that turns pictures into Tau Gallery files for the Pocket. Pick one picture or one folder, pick an output folder, press Convert.

The output folder gets the card's own tree:

```
Assets/tau_gallery/common/photos/NAME.tgy      one file per picture
Assets/tau_gallery/common/tau-library.tdb      the index (the thumbnails and the list)
```

Copy the `Assets` folder to the root of the card (merge it with an existing `Assets` folder).

## Options

- **Add to the library already in this folder** (on): keeps what an earlier run converted, so one picture can be added to a library. A picture with the same name is replaced; a picture whose file was removed is dropped from the index. Off starts a new library.
- **Zoom levels** (on): large pictures also get the half and 1:1 zoom stops for the pan and zoom. About 45 MB more on the card for each large picture; small ones add nothing.
- **Enlarge small pictures** (off): pictures smaller than the screen show at their own size unless this is on.

Drag a picture or a folder onto the window to pick it. A folder is read one level deep (subfolders are not read). Pictures: JPEG, PNG, BMP, GIF, TIFF, WebP. File names on the card are ASCII only (anything else becomes `_` plus a short checksum); the name shown in the gallery keeps the original letters it can (Latin-1).

## What it needs

Python 3 with Pillow (`python3 -m pip install Pillow`). The app looks for one by itself (the PATH, Homebrew, python.org); the **Python** line at the bottom shows what it found and takes a path if it has to be told. The converter (`tools/gallery/tgy.py`, `tgy_convert.py`) is copied into the app at build time (`npm run sync`), so the app always runs the tested converter.

## Where it comes from

The shell is the Tau Encoder's (`Tau codecs/tau-encoder`): the Tauri 2 / Vite / Svelte 5 setup, its dependency versions and lockfile, the dialog plugin and capability, the look of the page. The work is done by the Python converter as a child process that prints one JSON event a line (`plan`, `converted`, `skip`, `warning`, `done`), the same idea as the codecs CLI's machine events; the app relays them to the page. It does not share code with the Tau Encoder and changes nothing in it.

## Develop

```sh
npm install
npm run check                 # svelte-check
npm run dev                   # the page in a browser tab, with stand-in data (no app needed)
cd src-tauri && cargo test    # the backend against the real converter
cargo tauri dev               # the app
cargo tauri build --bundles app
```

Tests: `src-tauri` has Rust tests that run the real converter (a folder, one picture added to it, a file that is not a picture, stopping a running conversion, one conversion at a time). The converter's own modes are tested by `sim/test_tgy_convert_modes.py` (`make test-tgy`).

## Status

0.1.0. The macOS build is not signed or notarized (right-click, Open the first time). Windows and Linux are not tried.
