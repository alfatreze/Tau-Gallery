#!/usr/bin/env python3
"""Mutants of fw2/platform/start.S for the interrupt test (Makefile test-rtl-irq-soc): each copy breaks one thing the external-interrupt path must do. Prints the mutant names.
usage: irq_start_mutants.py <start.S> <outdir>"""
import sys
import pathlib

src = pathlib.Path(sys.argv[1]).read_text()
out = pathlib.Path(sys.argv[2])
out.mkdir(parents=True, exist_ok=True)
M = {
    "no_restore_ra": ("\tlw ra, 0(sp)\n", ""),
    "no_restore_a3": ("\tlw a3, 36(sp)\n", ""),
    "no_restore_t4": ("\tlw t4, 12(sp)\n", ""),
    "no_restore_t0": ("\tlw t0, 0(sp)\n\tlw t1, 4(sp)\n\taddi sp, sp, 8\n\tmret", "\tlw t1, 4(sp)\n\taddi sp, sp, 8\n\tmret"),
    "no_restore_t1": ("\tlw t1, 4(sp)\n\taddi sp, sp, 8\n\tmret", "\taddi sp, sp, 8\n\tmret"),
    "no_pending_arg": ("\tcsrr a0, 0xFC0\t\t\t\t/* the pending external lines (LiteX irq pending CSR) */\n", ""),
    "no_dispatch": ("\tbeq t0, t1, irq_entry\n", ""),
    "no_mask_csr": ("\tcsrw 0xBC0, a0\n", ""),
}
for name, (a, b) in M.items():
    assert src.count(a) == 1, (name, src.count(a))
    (out / ("start_%s.S" % name)).write_text(src.replace(a, b))
    print(name)
