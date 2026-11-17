# Session handoff 2026-11-17: Tau Gallery 0.9.0 released, public repository, converter 0.2.0

Read `docs/CURRENT_STATUS.md` first (one page), then this file. Evidence for every claim is in `docs/AUDIT_TRAIL.md` (B-1111 to B-1130).

## Where things are
- **Repository:** https://github.com/alfatreze/Tau-Gallery, public, Apache-2.0 (`LICENSE`; the upstream MIT notice is `LICENSE-MIT`, attribution in `NOTICE` and `NOTICE.md`). Work in a clone of it: here the folder `gallery-publish` (links to the old folder `gallery` for `toolchain`, `work/quartus_sim_lib`, `work/card-backups`, `work/reports`). **The old folder `gallery` is an archive** with the original history (AI trailers, other hashes, unpushed branches `rust-converter` and `mac-converter-adhoc-signing`, a leftover worktree `tau-alpha-mac-adhoc-sign`); hashes quoted in audit entries up to B-1128 and in older handoffs are the old ones (find a commit by subject and date).
- **Rule 11:** commit messages never mention an AI tool and carry no Co-Authored-By line; `git config core.hooksPath .githooks` enables the hook in a clone (it is on in `gallery-publish`). Check `git branch --show-current` before every push (another session left `converter-0.2.0-final` checked out and a push of `main` was rejected); the remote still has the branches `converter-0.2.0` and `converter-0.2.0-final` (leftovers, ask before deleting).
- **Releases (both pre-releases):** `v0.9.0` (core zip with the m1f seed 2 bitstream, CORE_VERSION 4D50331E, firmware 0.9.0, tau-compat.json, SHA256SUMS, the old converter and the Python tools) and `converter-v0.2.0` (the app, Rust, no Python, macOS ad-hoc signed, Windows and Linux builds). Both: assets downloaded back and equal to SHA256SUMS.txt.
- **Bitstream:** m1f seed 2 closes timing in all eight corners (setup +0.669 ns, hold +0.082 ns; ALMs 91%, RAM blocks 94%, DSP 80%); RBF `work/diagnostics/gallery-m1f/ap_core_s2.rbf`, SHA-256 `cbd7eeaa6480869b8f3401f9afefb21d418489471ba95ba0482130fe4df1b69d` (not tracked; keep it). Seed 1 failed to route and was stopped.
- **Card TauV:** `Tau Gallery DEV 025` (corner table loaded, Settings overlap fixed in a really rebuilt ROM) and a version 1 library index with cover thumbnails (57 pictures, one folder, EXIF for 9) are installed. **Not yet seen on a Pocket.**
- **README:** rewritten for Tau Gallery (converter first, features, measured speed, compression); the owner's three screenshots are in it and show the old look.

## What is verified and what is not
Pocket: the v1 zoom, pan, grid keys and timings (DEV 014 and DEV 020; tile decode 6.2 ms, open 257 ms, first zoom 538 ms). Only host-simulated: rounded corners, cover thumbnails, the colour presets, crop masks, rotation and mirror, the decoder histograms, grid sizes on the card's version 1 index, the Settings page fix. Never tried: the Dock (`dock.supported` is false), the Windows and Linux converter apps on those systems, a notarised macOS app.

## Next, in this order
1. **Pocket check of DEV 025** (the card is TauV; read its screenshots from `Memories/Screenshots/`): Settings (four rows, hint below COLOUR), thumbnails filling their squares, rounded corners and frame (if still square, the corner table or op 10 is not taking effect on the core: look at the engine itself), grid sizes 5x4 to 8x6, details pages with EXIF, turn, mirror, masks, colour presets, decoder histograms. New screenshots then go into the README.
2. **The red `gate` workflow** (the music player's `make test-host-fast`, 11 of 93 fail on Ubuntu): replace it with a gallery check or switch it off; `converter.yml` is green.
3. **Release notes and status:** the v0.9.0 notes and `tau-gallery-tools_0.9.0.zip` describe the old converter; the next core release should say converter 0.2.0 supersedes it. A core 1.0.0 needs: the Pocket check, the Dock decision, `core.json` 1.0.0, a changelog heading, release notes (`make_gallery_release.py` builds and checks everything and prints the `gh` command; it runs on a committed tree only and needs the RBF's manifest commit field to exist in the repository's history).
4. **Decisions waiting for the owner:** nested folders (flatten, or join names such as "Trip - Day 1"); notarising the macOS converter; the tracked files that still mention Claude in their text (`CLAUDE.md`, `AGENTS.md`, `.clinerules.yml`, many docs and your home path in 12 files); whether to delete the leftover branches.
5. **Possible features:** PSRAM cold UI image (frees RAM at 94% blocks used), TGX (lossless) in the converter and decoder, rotated zoom (decided: zoom stays upright).

## Lessons from this stretch (also in the audit trail)
- `package_dev_build.py` used to package whatever ROM was last built: DEV 024 carried DEV 023's ROM under a new commit stamp. It now always rebuilds the gallery firmware (`--variant galleryapp`).
- A table-driven draw op passes its RTL bench and does nothing on the core if the table resets to zero and the firmware never loads it (corner table; `fw2/gfx/rc_cut.h`, engine model in `sim/p2_engine_model.h`).
- Cover thumbnails for an existing library: `tools/gallery/rethumb.py LIBRARY_DIR NEW_INDEX` (reads fit levels, writes only the new index); swap it onto the card with a backup and a SHA-256 check (`install_dev_core.py` cannot ship an index without pictures).
- A measuring script that starts worker processes on macOS needs an `if __name__ == "__main__"` guard (mine did not and stalled the machine).
- The card is exFAT: macOS leaves `._` files; the converter ignores names that start with a dot.

## Resume prompt
"Continue Tau Gallery. Work in `gallery-publish` (the clone of github.com/alfatreze/Tau-Gallery; `main`, hook on, rule 11). Read `docs/CURRENT_STATUS.md` and `docs/handoffs/SESSION_HANDOFF_2026-11-17_GALLERY_RELEASE_AND_REPO.md`. I will mount the TauV card: fetch the screenshots of DEV 025 and judge corners, cover thumbnails and Settings; then fix what is wrong, update the README screenshots, and sort out the red `gate` workflow."
