# Tau Gallery encoding tools (Python)

Command-line tools that turn pictures into the files the Tau Gallery core reads: one `.tgy` per picture and the library index `tau-library.tdb`. They run on any system with **Python 3**, **Pillow** and **numpy** (`python3 -m pip install Pillow numpy`). The converter app (macOS) is a front end for `tgy_convert.py`.

## Convert pictures

```
python3 tgy_convert.py SRC OUT --card-layout --format v1
```

- `SRC` is one picture or a folder. Each subfolder that has pictures directly in it becomes a folder of the gallery; pictures in the folder itself go to a folder named after it. Folders deeper than that are not read. Files that start with a dot (macOS `._` files) are ignored.
- `--card-layout` writes `OUT/Assets/tau_gallery/common/photos/*.tgy` and `OUT/Assets/tau_gallery/common/tau-library.tdb`: copy the `Assets` folder to the root of the Pocket's SD card.
- `--format v1` writes the compressed pictures the hardware decoder reads (use it). `--merge` keeps the pictures the output library already has; `--sort name|taken|converted`; `--jobs N` the number of processes (0 = automatic; 1 is slowest and lowest in memory); `--upscale` enlarges small pictures; `--index-format v0` writes the old flat index for a gallery before 1.0.
- A very large picture (hundreds of megapixels) takes minutes and several gigabytes of memory.

## Other tools

- `merge_index.py` adds the pictures of a package to a card's existing library instead of replacing it.
- `rethumb.py LIBRARY_DIR NEW_INDEX` rebuilds a version 1 library's thumbnails as full squares (scaled to fill, centre-cropped) from the pictures themselves. The library is only read.

Output is one JSON line per event on stdout. Licence: Apache-2.0 (see `LICENSE` and `NOTICE` at the root of the repository).
