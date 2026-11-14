# Core identity, files on the card, build and package

This page holds what used to be the README: the naming of the core's release channels, where its files live on the card, and the commands that build, package and install a developer build. For what Tau Gallery is and how to use it, see the README.

## Identity (independent of Tau Alpha)

Same naming structure as Tau Alpha's release channels (`tools/tau_layout.py`), with its own ids, platforms and settings. Author id stays `alfatreze`.

| Channel | Core id (`Author.Core`, = folder name) | Platform id (<= 15 chars) | Select Core shortname | Version |
|---|---|---|---|---|
| Stable | `alfatreze.Tau Gallery` | `tau_gallery` | Tau Gallery | `X.Y.Z` |
| Stable diagnostics | `alfatreze.Tau Gallery Diagnostics` | `tau_gallery` | Tau Gallery Diagnostics | `X.Y.Z` |
| Preview | `alfatreze.Tau Gallery Preview` | `tau_gallery_pre` (+ reads `tau_gallery`) | Tau Gallery Preview | `X.Y.Z-preview.N` |
| Preview diagnostics | `alfatreze.Tau Gallery Preview Diagnostics` | `tau_gallery_pre` | Tau Gallery Preview Diagnostics | `X.Y.Z-preview.N` |
| Dev | `alfatreze.Tau Gallery DEV NN` | `tau_gallery_dev` (+ reads `tau_gallery`) | Tau Gallery DEV NN | `X.Y.Z-dev.NN` |

Only the Stable core is released (`tools/make_gallery_release.py`); the other rows are the channel scheme inherited from Tau Alpha (`tools/tau_layout.py`) and matter for numbered developer builds.

- **Settings:** the Pocket keeps them per core id under `/Settings/alfatreze.Tau Gallery.../`, so Tau Gallery never reads or overwrites Tau Alpha's.
- **Files on the card:** `Assets/<platform>/<core id>/` (ROM, cold image, loading splash), `Assets/tau_gallery/common/` (shared gallery data), `Cores/<core id>/`, `Platforms/<platform>.json` and `_images/<platform>.bin`.
- **Current version:** see `dist/Cores/alfatreze.Tau Gallery/core.json` (0.9.0 at the first release); dev builds are `X.Y.Z-dev.NN`.
- **Artwork:** the platform image is the owner's (`assets/brand/` has its source); icon and boot splash come from `tools/gen_gallery_art.py` and `assets/branding/`.
- **Binaries in `dist/`:** inherited Tau Alpha build products used as test fixtures (`dist/INHERITED_BINARIES.md`).

## Build and package

```bash
export PATH="$PWD/toolchain/xpack-riscv-none-elf-gcc-15.2.0-1/bin:$PATH"     # xPack riscv-none-elf-gcc 15.2.0-1
# a numbered developer build (the firmware is rebuilt first, always)
python3 tools/package_dev_build.py --variant galleryapp --number 26 --rbf <raw ap_core.rbf> --rbf-sha256 <its sha256>
python3 tools/install_dev_core.py work/diagnostics/tau-dev-026/pocket --card /Volumes/<card>      # dry run; --yes to write
# a release (zip, tau-compat.json, SHA256SUMS.txt; prints the gh command, publishes nothing)
python3 tools/make_gallery_release.py --rbf <raw ap_core.rbf> --rbf-sha256 <its sha256> --release v0.9.0 --no-previous
```

(`tools/package_dev_build.py --variant galleryapp` always rebuilds the gallery firmware before it packages; use `--number NN` for a numbered dev build, and pass the raw RBF and its SHA-256 with `--rbf` and `--rbf-sha256`.)
