CPU kernel benchmark on the real VexRiscv RTL (iverilog, `sim/tb_vexriscv.v`). `gen.py` writes `rcdata.h`; build with
`riscv-none-elf-gcc -march=rv32im -mabi=ilp32 -O2 -ffreestanding -nostartfiles -nostdlib -T link.ld crt0.S bench.c`
(`bench2.c` = colour conversion only), `objcopy -O verilog --verilog-data-width=4`, then
`iverilog -g2012 sim/tb_vexriscv.v src/fpga/rtl/VexRiscv_Full.v; vvp a.out +firmware=bench.hex +timeout=30000000`.
The full run takes about 5 minutes. Results: docs/GALLERY_ANALYSIS_2026-11-07.md section 12.11.
