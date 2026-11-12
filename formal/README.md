# Formal checks (sby + yosys + abc)

`brew install yosys sby` (the engines used, `abc bmc3` / `abc pdr`, ship with yosys; no external solver needed for these). `make test-formal-wfifo` runs `wfifo.sby` through `run_formal.py`.

## wfifo (tau_tgy_wfifo, two unrelated clocks)
`wfifo_formal.sv` drives the FIFO with the DMA's own clear handshake and free push/pop choices, on two clocks derived from one step counter (free phase, ratio set per task), and follows one tracked word (index N) through it.
Asserted: the tracked word comes out intact; occupancy never exceeds 2**AW + 1 (no overrun); rvalid only for a written word; rvalid low during an acknowledged clear.
Result: bounded to 220 steps (110 writer clocks at the 1:1 setting), AW = 2, four clock ratios. NOT an unbounded proof: `abc pdr` did not converge in 8 minutes (the clear handshake counters and the Gray
crossings make the induction weak). Mutants BUG 1..3 of the RTL are caught in seconds, so the harness is not vacuous.
Not covered: the DMA's error path releases `clear_req` without waiting for the acknowledge (tau_tgy_dma.sv, `err_pend`); the wrapper models only the normal handshake.

## p2mux and dmux (the SDRAM port arbiters, one clock) -- unbounded proofs
`p2mux_props.vh` / `dmux_props.vh` are included by the RTL under `ifdef FORMAL` (three lines at the end of each module; not synthesised; `sby` defines FORMAL), because the induction needs the module's own `sub_busy` and `c_out`
(yosys cannot reference them from a wrapper). `p2mux_formal.sv` / `dmux_formal.sv` only supply free inputs and one reset step. `make test-formal-mux`.
Proved for ALL input sequences (k-induction, depth 12), given the scheduler contract (`done` only for an outstanding request; the port does not accept with C's 4-bit counter full): the secondary is granted only with C idle,
nothing outstanding and nothing in flight (and always then); C is held while the secondary runs; each `done` goes to exactly one client; address, length, direction, write data, byte enables, next, index and read data belong
to the owner. Mutants BUG 1..7 (p2mux) and 1..10 (dmux) all FAIL. Not proved: that a request is eventually done (a property of the scheduler) and C's bounded wait (needs a liveness assumption on the scheduler).
