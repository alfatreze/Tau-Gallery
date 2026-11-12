# Whole-core simulation (sim/fullcore) and the other pre-hardware checks

Status 2026-11-12: **built, boots, runs the host protocol and the firmware** (issue 026 fixed, B-1072; `make test-fullcore`, about 3 minutes for 1.5 ms after reset exit). It ends with `FULLCORE CPU PASSED` (no exception after reset exit, fetch address beyond 0x100) and `run.py` exits non-zero otherwise. It does NOT yet check what the firmware shows: in 10 ms after reset exit the firmware answers its first target command (the library index, slot 5), then spends the rest in startup code (digit conversion loops), and one frame is captured but not checked. A golden-frame comparison and a longer scenario are the next steps; slots 4, 6 and 8 have no file in `work/fullcore_data`.

## What it is
`python3 sim/fullcore/run.py [--rebuild] [--dir work/fullcore_data] [--maxus N] [--script 0|1|2] [--extra +TRACE]`: `core_top` as it is built for the Pocket (the real VexRiscv SoC, `tau_tgy_unit`, the SDRAM PHY and scheduler against the timing-checked `sim/sdram_chip_model/`, the video scan-out), a model of the Pocket's host in `tb_fullcore.v` speaking the APF bridge protocol of `core_bridge_cmd.v`, in Icarus. Needs `iverilog` and, in `work/quartus_sim_lib/` (git-ignored, copied from `<quartus>/eda/sim_lib/` on the VM; never committed), `altera_mf.v`; the PLL is our own model (`altera_pll_model.v`). `sim/fullcore/prepare.py` makes the source tree Icarus can elaborate: Quartus tolerates use before declaration and power-up-zero registers, Icarus does not (14 declarations are hoisted, registers are zero-initialised, a zero `build_id.ver` is written). Speed: about 50 microseconds of simulated time per second of wall time; the boot (PLLs, loading the 40 KB firmware through the bridge at the Pocket's pace) is 5.7 ms of simulated time = 2 minutes, a video frame is 16.7 ms = 5-6 minutes. Scenarios: 0 run for `--maxus`; 1 a frame every maxus/12 (slow); 2 one frame after `--maxus`. `+TRACE` prints the CPU's fetch and exception state every 100 us.

## What it has shown so far
- The core elaborates whole (every unconnected input is a finding: one, `vblank_rd`, put X into the CPU in the first attempt).
- The host protocol works: status 4 reached, data slot 1 written (the firmware image lands in RAM, verified word by word), "all slots complete" (0x008F), the core's ready-to-run (0x0140) is answered, reset exit (0x0011), video frames are captured (400 x 360, all black so far).
- Three testbench bugs found by running it: a 32-bit overflow of the watchdog time, a loop index shared by name between the video capture and the boot loop, the first scenario waiting for 12 frames of 16.7 ms.
- **The CPU runs the firmware** since B-1072 (docs/issues/026-fullcore-cpu-illegal-instruction.md: Icarus does not evaluate `always @(*)` at time 0, which left the instruction cache's tags X; `prepare.py` turns the CPU's `always @(*)` into `always_comb`). `+RSTTRACE` prints the reset history and the first 400 fetch clocks with the tag RAM's write chain.
- The host model registers the slots that have a fixed file name in data.json (4, 5, 6, 8) from `--dir`, as the Pocket's host does.

## Other checks added with it
| check | what | state |
|---|---|---|
| `tools/block_probe.py` | timing of one block alone on the VM, 15 min (`ent`) to 40 min (`unit`) | run: unit clk_dec 58.68 MHz vs 63.69 in the full fit (B-1066) |
| `tools/netlist_sim.py` | the unit's own testbench against Quartus' functional netlist of the same RTL | netlist built (DSP inference off); Verilator compiles it (13 min), result in B-1067 |
| `tools/rtl_coverage.py` | Verilator line coverage of the 11 TGY files | docs/RTL_COVERAGE.md |
| `sim/test_fuzz_c.py` | mutation fuzzer of the C parsers and the driver | B-1063 |
| `make gate` | the set that must be green before a commit | B-1065 |
