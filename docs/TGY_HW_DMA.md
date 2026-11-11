# TGY decoder: SDRAM DMA and register block (simulation only)

Status: bit-exact in simulation against the scheduler port model; not synthesised, not fitted, **not yet instantiated in the core** (no `tau_soc.v` / `core_game.vh` change, no macro in a fit bundle), nothing on a Pocket.

## Parts
| Module | File | Test | Role |
|---|---|---|---|
| `tau_sdram_dmux` | `src/fpga/core/tau_sdram_dmux.sv` | `make test-rtl-dmux` | a secondary client D that can READ and write on the CPU scheduler port, chained behind `tau_sdram_p2mux` (which is write-only for the landing path) |
| `tau_tgy_dma` | `src/fpga/core/tau_tgy_dma.sv` | `make test-rtl-tgy-dma` | table load, stream fetch, pixel row writer, around `tau_tgy_dec`; one clock (the SDRAM clock) |
| `tau_tgy_regs` | `src/fpga/core/tau_tgy_regs.sv` | `make test-rtl-tgy-unit` | the MMIO registers (clk_sys), commands and events across the two clocks |
| `tau_tgy_unit` | `src/fpga/core/tau_tgy_unit.sv` | `make test-rtl-tgy-unit` | registers + DMA + decoder: what the core will instantiate |

## What the DMA does
- **Tables:** `cmd_tables` reads the two quantisation tables (64 u16 each, zigzag, even byte address, the container's tables section) from SDRAM into the decoder.
- **Stream:** the tile stream is read from ANY byte address (halfword reads of up to 32 into a 128-halfword FIFO, an odd start byte is dropped) and handed to the entropy decoder as 32-bit words; the decoder zero-fills past the length.
- **Pixels:** the decoded RGB888 rows are collected into an 8-row ring (four byte-wide RAMs of 1,024 entries; a pixel's three bytes always land in three different arrays) and written one request per row at `dst + y * pitch`, any byte address and any pitch (odd pitches included): when a row starts or ends on an odd byte the first or last halfword is written with ONE byte enable, so the neighbouring bytes stay untouched. The viewer plane is `0x200000` halfwords, pitch 2,400 bytes (800 x RGB888); a tile at an odd canvas column works.
- `hold` pauses the decoder's row starts while 4 or more rows wait to be written (the pixel pipeline is 3 clocks deep and rows can be one pixel wide: 4 waiting + up to 3 in flight < 8 slots). One request is in flight at a time; writes go first, then stream reads (the FIFO holds about 7,000 decode clocks of stream, a row write takes about 200).
- **Errors:** a decoder error or its watchdog gives `ev_err` code 1, a tile that does not finish in 2^24 SDRAM clocks (about 170 ms at 100 MHz; the biggest dense tile takes about 4 ms) code 2, an `ABORT` code 15; the decoder is reset, the in-flight request completes (an unaccepted one is withdrawn), the FIFOs are flushed, the unit is idle and the next tile decodes correctly (tested).

## Registers (MMIO 0x220 .. 0x24C, word access, `tau_tgy_regs.sv`)
`0x220 CTRL` (write: bit 0 START_TILE, 1 LOAD_TABLES, 2 ABORT, 3 CLEAR) | `0x224 STATUS` (busy, done, err, tables loaded, start rejected, error code [11:8], present [31]) | `0x228 SRC` stream byte address | `0x22C LEN` | `0x230 DST` first pixel byte address | `0x234 PITCH` | `0x238 GEOM` {format, width, height} | `0x23C QY` | `0x240 QC` | `0x244 CYCLES`. `irq` = done | err. Write the configuration first, START in a separate store. A start while busy is ignored and sets `rejected`; a STATUS read right after a START already says busy (the flags are event driven through toggle synchronisers).

## Results (simulation)
- `test-rtl-dmux`: random CPU (32-bit, byte enables) and a random read/write client D share the port model; every read beat, every write byte and every `done` checked, the whole memory compared; 10 of 10 mutants killed (one, C accepted while D is in flight, needed an explicit check of the contract).
- `test-rtl-tgy-dma`: 11 tile streams (two real photo tiles included) with odd and even stream start, odd and even destination, pitches 400, 397 and 512; after each tile EVERY byte of the 512 KB model memory is compared (the tile's RGB bytes exact, everything else untouched). Abort, a corrupt stream, a starved port (timeout), each followed by a good tile. Dense real tile: 402,069 SDRAM clocks from start to the last row in memory (about 4.0 ms at 100 MHz). Both `C444` configurations. 10 of 10 mutants killed (hold off, green and blue swapped, third byte lost, last and first byte enable, enables forced, no FIFO space check, no odd-start skip, no timeout, first-row detection).
- `test-rtl-tgy-unit`: the CPU view with two unrelated clocks (66.7 and 100 MHz): registers read back, LOAD_TABLES, START with the immediate busy/not-done check, a start while busy rejected, done, irq, CYCLES, CLEAR, ABORT then a good tile; 6 of 6 register mutants killed (three took a sharper check: a start must clear a stale done flag, irq must rise on err, the done flag must survive until the next start).
- Caught on the way: the first-row test used the 5-bit row counter, which wraps at 32 rows (a 64-row tile overwrote its first row); a withdrawn-request deadlock when the port stays busy during an error.

## Integration (not done, small)
1. `tau_soc.v`: forward MMIO 0x220-0x24C to external `fb_tgy_*` ports (address, we, wdata out; hit, rdata, irq in), OR the read data, add `irq` as interrupt source 4; parameter `TGY_ENABLE` (inert at 0).
2. `core_game.vh` under `TAU_TGY`: instantiate `tau_tgy_unit` (`clk_mem` = the SDRAM clock) and chain `tau_sdram_dmux` between the output of `tau_sdram_p2mux` and the scheduler's CPU port.
3. The files into `ap_core.qsf` (the listing check `test_check_qsf_files` already lists missing files), a new fit bundle macro `TAU_TGY=1` on top of `tools/gallery_m1b_qsf_append.txt`, then synthesis and the two-seed fit (needs the owner's yes).
4. Firmware: a small driver (`tgy_hw.c`) that reads the container's tile index and drives the registers tile by tile.
Clock domain choice: the whole decoder runs on the SDRAM clock, so no pixel or stream data crosses clocks; the cost is that the decoder's multipliers must close at that clock (100 MHz now, 150 MHz later); unmeasured until Quartus.
