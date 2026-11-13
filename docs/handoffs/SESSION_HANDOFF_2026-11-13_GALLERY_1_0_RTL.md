# Handoff 2026-11-13: Tau Gallery 1.0 RTL batch (K1..K6), stopped on the owner's request

## Done and committed (host simulation only; nothing fitted, nothing on a Pocket)
- K1 ordered dither + K5 per-channel colour table: `src/fpga/core/tau_pixfx.sv`, wired in `tau_scanout_v.sv`; regs PIX_CTRL 0x250, PIX_LUT 0x254 in `tau_soc.v`; `sim/test_rtl_pixfx.py` (10 mutants killed). Commit a27b2cd.
- K3 rounded-corner mask: Talos `OP_CORNERS` = 10 (`tau_talos.sv`, `sim/tb_mp3_fb.v`, 8 mutants killed). Commit 359cf62.
- K4 histogram core `tau_hist.sv` (+ `tb_tau_hist.v`, `test_rtl_hist.py`, 8 mutants killed). Commit 359cf62.
- Whole-core `make test-fullcore` passes after the pixfx wiring (FULLCORE CPU PASSED). The 22,400 "[CHIPMODEL] store collision" lines are PRE-EXISTING: the base commit gives the same count and also passes. Not a regression.
- Rule 10 (Haiku for mechanical work) in `docs/PROJECT_RULES.md`.

## In progress, NOT committed, NOT tested
- `src/fpga/core/tau_hist_if.sv`: clk_sys register block + CDC wrapper around `tau_hist` (regs 0x260 CTRL, 0x264 RECT0, 0x268 RECT1, 0x26C ORG, 0x270 IDX, 0x274 DATA, 0x278 N). Written, never compiled, no testbench. Treat as a draft.
- Remaining K4 wiring (design settled): tap `px_valid/px_x/px_y/px_r/g/b` (already wires inside `tau_tgy_dcore.sv` line ~51) out through `tau_tgy_dcore` -> `tau_tgy_dma` (instance line ~75) -> `tau_tgy_unit` (instantiate `tau_hist_if`, merge `reg_hit`/`reg_rdata` with the decoder regs), extend the soc forward range (`mmio_reg <= 10'h24C` for TGY) to 0x278, add tests + mutants, rerun scan-out and whole-core tests (Haiku agent), then Tier-RTL items (CDC test with unrelated clocks, probe).

## Not started
- K2 Talos downscale op; K6 4:4:4 chroma (`C444` param exists, core_game.vh uses 0).
- Strip unused diagnostics (psram probe/window, sdr_stat_win, audio/pcm) to free ALMs (89% used); needs a map-only synthesis (owner's yes + finish time).
- CORE_VERSION bump at RTL freeze; block probes; multi-seed fits (seed 2 failed hold by 49 ps before).
- `docs/features/GALLERY_RTL_1_0_SPEC.md`, audit entry B-1111, firmware use of new registers.
- Other 1.0 packages: see `docs/GALLERY_1_0_PLAN.md` (TGIX v1, folders/details/grid sizes, zoom pyramid, release engineering, DEV 023 install).

## Housekeeping
- Worktree /tmp/gal_base removed. A stale prunable worktree entry (scratchpad basewt) remains; `git worktree prune` clears it.
- A background `make test-gallery-quick` (log /tmp/quick2.log) may still be running or finished; check its result before trusting Tier B.
