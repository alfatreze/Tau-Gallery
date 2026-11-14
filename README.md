# Tau Gallery

A photo gallery core for the Analogue Pocket (openFPGA): fast previews, large images, zoom and pan. A fork of Tau Alpha
(`main` at `12418aab`, see `UPSTREAM.md`; upstream README: `docs/UPSTREAM_TAU_ALPHA_README.md`). Plan: `../docs/GALLERY_ANALYSIS_2026-11-07.md`.

## Identity (independent of Tau Alpha)

Same naming structure as Tau Alpha's release channels (`tools/tau_layout.py`), with its own ids, platforms and settings. Author id stays `alfatreze`.

| Channel | Core id (`Author.Core`, = folder name) | Platform id (<= 15 chars) | Select Core shortname | Version |
|---|---|---|---|---|
| Stable | `alfatreze.Tau Gallery` | `tau_gallery` | Tau Gallery | `X.Y.Z` |
| Stable diagnostics | `alfatreze.Tau Gallery Diagnostics` | `tau_gallery` | Tau Gallery Diagnostics | `X.Y.Z` |
| Preview | `alfatreze.Tau Gallery Preview` | `tau_gallery_pre` (+ reads `tau_gallery`) | Tau Gallery Preview | `X.Y.Z-preview.N` |
| Preview diagnostics | `alfatreze.Tau Gallery Preview Diagnostics` | `tau_gallery_pre` | Tau Gallery Preview Diagnostics | `X.Y.Z-preview.N` |
| Dev | `alfatreze.Tau Gallery DEV NN` | `tau_gallery_dev` (+ reads `tau_gallery`) | Tau Gallery DEV NN | `X.Y.Z-dev.NN` |

- **Settings:** the Pocket keeps them per core id under `/Settings/alfatreze.Tau Gallery.../`, so Tau Gallery never reads or overwrites Tau Alpha's.
- **Files on the card:** `Assets/<platform>/<core id>/` (ROM, cold image, loading splash), `Assets/tau_gallery/common/` (shared gallery data), `Cores/<core id>/`, `Platforms/<platform>.json` and `_images/<platform>.bin`.
- **Current version:** `0.1.0` (`dist/Cores/alfatreze.Tau Gallery/core.json`); dev builds are `0.1.0-dev.NN`.
- **Artwork:** placeholders from `tools/gen_gallery_art.py` (icon, platform image, boot splash); previews in `assets/brand/`.
- **Binaries in `dist/`:** inherited Tau Alpha build products used as test fixtures (`dist/INHERITED_BINARIES.md`).

## Build and package

```bash
export PATH="$PWD/toolchain/xpack-riscv-none-elf-gcc-15.2.0-1/bin:$PATH"     # xPack riscv-none-elf-gcc 15.2.0-1
python3 tools/package_dev_build.py --number 901 --variant player2 --build-flags DIAG=1 \
    --rbf <raw ap_core.rbf> --rbf-sha256 <its sha256>
python3 tools/install_dev_core.py work/diagnostics/tau-dev-901/pocket --card /Volumes/Pock          # dry run; --yes to write
```

The licence is Apache-2.0 (`LICENSE`). The MIT license of the upstream project and of the earlier Tau files is kept in `LICENSE-MIT`, with the attribution in `NOTICE.md` and `NOTICE`.
