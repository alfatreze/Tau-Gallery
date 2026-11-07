# Upstream pin

Tau Gallery forks Tau Alpha (local checkout `../../tau-alpha`).

- Commit: `12418aab` (main, 2026-11-07, after Talos 2 T0-T3b)
- Tags in the baseline: `sdram-phy-v1`, `sdram-sched-v-v1.1`
- Not used: `sdram-sched-v-v1.2-rc1`
- Imported with `git archive` (no upstream history). The first commit here is the
  untouched import; every later change is a gallery change.
- Plan and decisions: `../docs/GALLERY_ANALYSIS_2026-11-07.md`

Stripping of audio, decode, stream, meters and nowplaying is deferred to M0, when
the tree can be built after each removal.
