# The TGY decoder's own clock (50 MHz) and its clock crossings (simulation only)

**Why.** The first fit of the core with the decoder (`gallery-m1c`, B-1058) closed no timing on the SDRAM clock: the decoder's logic needs about 15 ns (Fmax 65.6 MHz against 100 MHz, worst setup -5.26 ns, 3,217 failing endpoints, all in the pixel stage and the entropy decoder; the IDCT met +0.36 ns). Pipelining the two modules to 100 MHz would have meant many paths and more fits; the decoder is not bandwidth bound (a dense tile is 3.6 ms of work at 100 MHz against 250 ms in software), so it gets its own **50 MHz** clock (PLL output 4, 600 MHz VCO / 12: a 20 ns period for 15 ns of logic) and the data crosses between that clock and the SDRAM clock through FIFOs and dual clock RAMs. Nothing inside the decoder changed: every decoder-side test of the frozen vectors applies as before.

## Structure (`tau_tgy_unit`)
| Part | Clock | Contents |
|---|---|---|
| `tau_tgy_regs` | clk_sys 66.7 MHz | the MMIO registers (unchanged), toggles to and from the SDRAM clock |
| `tau_tgy_dma` | clk_mem 100 MHz | the scheduler client (tables, stream fetch, row writes), the byte packer, the writer, events, timers |
| `tau_tgy_wfifo` | clk_mem write / clk_dec read | 64 x 32-bit stream words, show-ahead |
| `tau_tgy_dcore` | clk_dec 50 MHz | `tau_tgy_dec` (entropy decoder, tile controller, IDCT, pixel stage), the row ring |

## The crossings
- **Commands** (DMA to decoder): `start_tog`, `rst_tog` (abort / error), 3-flop synchronisers, the edge is the command. The configuration (format, size, length, destination, pitch) crosses as buses that the DMA latched at `cmd_tile` and holds until the next tile. The toggles are not reset by the system reset (a reset cannot fake a command).
- **Quantisation tables:** written on the SDRAM clock (while the decoder is idle, `cmd_tables`) into the tile controller's table RAM, read by the dequantiser on the decoder clock: a two clock RAM.
- **Stream:** the DMA packs the fetched halfwords into exactly `ceil(len / 4)` words and pushes them into the word FIFO; the decoder pops them. The FIFO is **cleared before every tile** by a handshake (the writer holds `clear_req`, the reader zeroes its pointer, `clear_ack` comes back, both stay a few clocks, the request is released and the acknowledgement must drop before the decoder starts), because an aborted tile leaves words behind.
- **Pixel rows:** the decoder writes each row into a ring of 8 rows (4 byte-wide dual clock RAMs, 1,024 entries; a pixel's three bytes land in three different arrays) and a record of the row (SDRAM address, halfword count, odd start, odd end); the writer reads both on the SDRAM clock. The counts `rows_arr` (decoder side) and `rows_wr` (DMA side) cross as Gray codes; the record is written BEFORE `rows_arr` counts the row (data before pointer). `hold` stops the decoder's row starts while 4 rows wait: the view of `rows_wr` is late, which only makes the decoder wait longer.
- **Events back:** `fin_tog` (the tile is done) is delayed 8 decoder clocks so the last `rows_arr` has crossed before the DMA looks at it; `err_tog`.
- **Counters** keep running between tiles (they are equal when a tile ends); only an abort or an error resets them (the decoder resets `rows_arr`, the DMA waits 24 clocks and then resets `rows_wr`).
- The scheduler's read data and `done` are registered once on entry (their paths were -0.2 to -0.6 ns at 100 MHz).
SDC: the new PLL output is in the asynchronous clock groups (`core_constraints.sdc`), like every other clock; the crossings are by construction, not by timing analysis.

## Results (simulation)
- `make test-rtl-tgy-wfifo` (`sim/tb_tau_tgy_wfifo.v`): 100 MHz writer and 50 MHz reader with an odd phase, random pushes and pops, 40 clears; every word read is the next word since the last clear, nothing valid after the acknowledgement of a clear, the handshake completes; 3 of 3 mutants killed.
- `make test-rtl-tgy-dma`: the unchanged whole-memory test (11 tile streams, odd/even stream start, destination and pitch, abort, corrupt stream, timeout, a good tile after each) with the decoder on its own unrelated clock: identical bytes. The dense real tile now takes 785,851 SDRAM clocks (7.9 ms) from start to the last row in memory (it took 402,069 with the decoder at 100 MHz): the decoder clock sets the pace. A slow-SDRAM scenario (the port refuses everything 3,500 clocks of every 9,000 during a dense tile) fills the ring and `hold` must stop the decoder: identical bytes. 12 of 12 mutants killed (stream FIFO space, odd-start skip, last and first byte enable, enables forced, timeout, no FIFO clear before a tile, done before the last rows, and the decoder-clock half's hold, green/blue swap, lost third byte, first-row address); one more (words not limited to ceil(len / 4)) is equivalent because the FIFO is cleared before every tile.
- `make test-rtl-tgy-unit` (three unrelated clocks, 66.7 / 100 / 50 MHz): the CPU view passes, 6 of 6 register mutants killed. `make test-rtl-tgy-soc` (the real VexRiscv programs the decoder through the MMIO window): passes.
Not run: the new clock in Quartus. The PLL gets a fifth output only with `TAU_TGY` (the other builds keep four and the output reads 0); the fit bundle `tools/gallery_m1c_qsf_append.txt` is unchanged.
