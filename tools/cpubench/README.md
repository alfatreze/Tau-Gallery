CPU kernel benchmark on the real VexRiscv RTL (iverilog, `sim/tb_vexriscv.v`). `gen.py` writes `rcdata.h`; build with
`riscv-none-elf-gcc -march=rv32im -mabi=ilp32 -O2 -ffreestanding -nostartfiles -nostdlib -T link.ld crt0.S bench.c`
(`bench2.c` = colour conversion only), `objcopy -O verilog --verilog-data-width=4`, then
`iverilog -g2012 sim/tb_vexriscv.v src/fpga/rtl/VexRiscv_Full.v; vvp a.out +firmware=bench.hex +timeout=30000000`.
The full run takes about 5 minutes. Results: docs/GALLERY_ANALYSIS_2026-11-07.md section 12.11.

## bench3 (software PNG) and bench4 (the TGY tile pipeline), 2026-11-10 and 2026-11-12
`bench3.c` + `inflate.c` + `pngdata.h` (`gen_png.py`): inflate and the PNG row filters on three 128 x 128 crops (docs/M3_STREAMING_NOTE.md section 9). `bench4.c` + `tgydata.h` (`gen_tgy.py`): the C decoder and pixel pipeline of the frozen TGY layers on a dense and a sparse real tile (section 20); `-DONLY=0` or `-DONLY=1` builds one tile, which lets the two run in parallel. Lessons: the simulation runs at only 7,000 to 17,000 cycles per second, so a tile takes 20 to 70 minutes; its output is buffered and **lost when the run is killed** (give it `+timeout=<cycles>` so the testbench ends it, and redirect to a file, do not pipe through `tail` under a `timeout`); and the testbench truncates a long `+firmware=` path, so `cd` into the folder and pass a short relative name.
