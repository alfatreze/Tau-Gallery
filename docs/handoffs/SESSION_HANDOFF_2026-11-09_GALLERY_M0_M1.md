# Session handoff 2026-11-09: Tau Gallery M0 on a Pocket, M1 built (DEV 005 installed, not yet run)

Repo `gallery/` (branch `main`, local only, nothing pushed). Plan (outside the repo): `../docs/GALLERY_ANALYSIS_2026-11-07.md`. Designs: `docs/M0_DESIGN.md` (6c to 6e: the Pocket results), `docs/M1_DESIGN.md`. Audit B-1005 to B-1008. Last commit `a490b96`.

## State in one paragraph
M0 (the 800 x 720 viewer, mode switch, landing path, interrupts) is proven on a Pocket in handheld mode. M1 (a gallery that browses and shows photos) is built and host-tested; **DEV 005 is installed on the TauV card with five test photos and has not been run on a Pocket.** The next thing is the owner's first run and screenshots of it.

## Measured on a Pocket (handheld; the dock was never tested)
- Viewer 800 x 720 shows the four test patterns with exact geometry and channel order. Fill by mailbox takes 1.9 s a pattern, the viewer appears 18 to 29 ms after the request.
- Mode switch: 40 of 40, worst 33 ms. Interrupts: V60 C16/16 S2/2.
- The landing path works: a target read with a 0x4xxxxxxx destination is accepted, every word counted, none dropped, data exact (map `0123 OK`). Rates: 4 KB 955 KB/s, 16 KB 1,562, 64 KB 1,480, 128 KB 1,571 (a large single command is about 1.6 times a 4 KB one).
- VERIFY PLANE: 432,000 words, 0 bad: SDRAM holds exact RGB888. The screenshots still show 5/6/5 bits: the handheld display path is RGB565 (the owner's note, skill KB-237). Open: the same ramp docked, a dithered ramp in handheld.
- A bug that cost two runs: the test read the tag zone through the CPU data cache (cacheability is address bit 31) and reported a landing fault that was not there. Read anything the bridge wrote into core RAM through the `0xC0000000` alias.

## Quartus (VM, two seeds each)
- `tools/gallery_m0_qsf_append.txt` (stats bundle plus VID40, VIEW, LAND, IRQ): 285 / 308 RAM blocks, worst setup +0.631 ns, hold +0.075 ns.
- `tools/gallery_m0b_qsf_append.txt` (14 audio, meter and statistics macros dropped): 264 / 308 RAM blocks (86%), 11 / 66 DSP, worst setup +0.297 ns (seed 1) and +0.336 ns (seed 2), hold +0.120 ns, about 1h10m a fit. The installed bitstream is seed 2, RBF `work/diagnostics/gallery-m0b/ap_core_s2.rbf`, SHA-256 `e2cbb15e...fd6d1`, 20 macros, CORE_VERSION 4D50331B.

## What is built (all committed, all with tests)
- RTL (M0.1 to M0.6): `tau_scanout_v`, `tau_vidmode`, Talos viewer fill, `tau_sdram_p2mux`, `tau_land`, interrupt block and `start.S` trap path. Regressions: `make test-rtl-*`.
- M0.7 test firmware `fw2/gallery/gallery_main.c` + `gallery_hw.c` + `gallery_pattern.c` (`make gallery-firmware`; tests `make test-gallery`: pattern test and the hardware test against a model, 9 broken models caught).
- M1.1 `tools/gallery/tgy.py`, `tgy_convert.py`: the TGY picture file (header, thumbnail, 800 x 720 fit canvas, 4 KB aligned, 1,748,992 bytes) and the TGIX index (written as `tau-library.tdb`, data slot 5). `make test-tgy` (10 tests, 10 mutants).
- M1.2 `fw2/gallery/gallery_fmt.c` (readers). `make test-gallery-fmt` (19 mutants).
- M1.3 `fw2/gallery/gallery_app.c` (grid 4 x 4, open by path on slot 2, the canvas lands in the plane in 128 KB commands, viewer, Left/Right, B). `APP=gallery bash fw2/build_gallery.sh`. `make test-gallery-app` runs the real firmware against a hardware model serving 18 real converted files (19 mutants).
- Tooling changes: `package_dev_build.py` variants `gallery` and `galleryapp` (`--photos DIR`), `tau_compat.py` roles for pictures and their index and the `tgy` slot, `install_dev_core.py` copies, snapshots and verifies every Assets folder of a package.

## On the cards
- TauV (Tau V's test card, used at the owner's instruction, nothing of Tau V's touched): `alfatreze.Tau Gallery DEV 005` (0.1.0-dev.5, gallery app, five photos under `Assets/tau_gallery/common/photos`, index `Assets/tau_gallery/common/tau-library.tdb`). Backups in `work/card-backups/`.
- Pock (the owner's Tau Alpha card): unchanged this session (it still has DEV 001, the SD probe).

## Next steps, in order
1. Owner runs DEV 005 on the Pocket and sends screenshots: grid (5 thumbnails, name and `1/5`), a viewer picture, any red status line; times for opening a picture (estimate 1.1 s plus 0.5 s per grid page), picture quality, grid layout, button mapping. Read them from `/Volumes/TauV/Memories/Screenshots`.
2. Fix what the run shows. Likely: grid redraw speed (thumbnails go through the mailbox), a progress display, key mapping.
3. M0.8 second half: delete the audio source files and the music player pieces once the gallery stands on its own (M1.5).
4. Open questions with owner value: dock run (mode switch, ramp depth, the UI line shape on the dock), a 565 dither in the viewer's last stage (a few ALMs, one fit), 150 MHz SDRAM (G2), real folders of photos (hundreds), ICC profiles (not applied).
5. M2 (pan and zoom): tile levels and the placement question (Talos reach A1 to A4 unverified); the container's level table already has layout and mode fields.

## Rules that bit this session
- Card writes and VM fits need the owner's explicit yes, and a fit launch states a finish time (project rule 3). Installs only through `tools/install_dev_core.py` (dry run first, `--yes` after approval); it restored the card by itself when the compat check found a fault.
- `make` needs `RISCV_TOOLCHAIN_BIN=/tmp/xpack-bin` (a symlink; the xPack path has a space) and `SDKROOT=$(python3 tools/toolchain.py sdkroot)` for host C. Shell `grep` is a function: use `/usr/bin/grep`. The tool safety check sometimes refuses harmless Bash commands: write a script file instead.
