# TGY hardware pixel stage: chroma upsampling and colour (simulation only)

`src/fpga/core/tau_tgy_recon.sv` (two modules), test `make test-rtl-tgy-recon` (`sim/tb_tau_tgy_recon.v`, `sim/gen_tgy_recon_rtl_vectors.py`). Implements `docs/TGY_RECONSTRUCTION_V0.md` sections 4 and 5 (TGY profile) bit for bit. Status: simulated only; not synthesised, not instantiated in the core, nothing on a Pocket.

## Modules
- `tau_tgy_ycc`: libjpeg integer colour conversion, 2 clocks, 4 multipliers by constants (91881, 116130, 46802, 22554 on c - 128), then add and clamp.
- `tau_tgy_recon`: holds the sample planes of ONE tile (Y 128 x 128; Cb and Cr 64 x 64, or 128 x 128 with `C444 = 1`), written 8 samples at a time in the IDCT unit's row format (`we, wcomp, wx, wy, wdata`), then `start` with `fmt` (0 grey, 1 4:4:4, 2 4:2:0) and the tile size streams RGB in raster order at one pixel per clock (`px_valid, px_x, px_y, px_r/g/b, px_last`), with `done`. `hold` pauses between rows only.

## How the filter is built
Engine A computes the vertical part of the fancy filter one output row ahead into a two-bank line buffer (`s = 3 * p + other row`, edge rows replicated). Engine B applies the horizontal part (`(3 * s + neighbour + 8 or 7) >> 4`, tile edges replicated, which is exactly libjpeg's first and last column formulae) and reads Y. A chroma plane of at most 2 samples across uses the box filter (the spec's rule). A grey tile is Cb = Cr = 128, which the colour tables turn into R = G = B = Y. 4:4:4 needs the larger chroma memory (`C444 = 1`); without it `fmt 1` raises `err`.

## Results (simulation)
- 144 whole tiles against `tgy_recon.py`: all sizes from 1 x 1 to 128 x 128 (odd, tiny, thin), noise, smooth, saturated chroma, every (Cb, Cr) pair (with Y mid grey and random Y), real photo tiles; unused samples hold garbage; half the tests assert `hold` at random. Both configurations (`C444` 0 and 1) bit exact; without `C444` the 47 4:4:4 tiles are refused with `err`.
- A full 128 x 128 4:2:0 tile: 16,582 clocks for 16,384 pixels (about 166 microseconds at 100 MHz).
- 17 mutants killed (colour constants and roundings, the clamps, odd and even rounding, left and right edge replication, vertical direction and bottom clamp, box filter, grey value, Cb/Cr swap). Three first-drafted mutants were equivalent (a 1-unit change in a colour constant or in the rounding never changes an output for inputs 0..255; verified by enumeration) and were replaced by larger deviations.
- Verilator lint: unused-signal and width-truncation style warnings only.

## Cost (estimate, unverified)
Memories: Y 16 M10K, chroma 8 per plane (64 x 64) duplicated for the two read ports (up to 16 more), line buffers in logic (2 x 2 x 64 x 10 bits), 4 small multipliers. About 40 M10K in total: this fits only together with the main RAM reduction (128 KB frees about 64 blocks, awaiting the owner's go-ahead); the stripped core is at 264 of 308.
