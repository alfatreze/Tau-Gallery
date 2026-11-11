# RTL coverage of the TGY decoder blocks (2026-11-11)

`python3 tools/rtl_coverage.py BLOCK|all` (`make coverage-rtl`): each block's own testbench and vectors (the ones `make test-rtl-tgy-*` runs), built with Verilator 5.052 `--coverage`, annotated lines with zero hits listed. Verilator's make cannot build in a path with spaces, so the tool builds in a temporary directory.

| block | lines hit | not hit |
|---|---|---|
| tau_tgy_ent | 333 / 338 (98.5%) | `default:` of the three ROM functions (kgrom, lnrom, recrom: the index covers every value), `default:` of two state decodes |
| tau_tgy_idct | 88 / 89 (98.9%) | `default: st <= S_IDLE` |
| tau_tgy_recon | 72 / 72 (100%) | none |
| tau_tgy_tile (4:4:4 build and the build without) | 143 / 146 (97.9%) | `default: zzn`, `default: st`, and line 181 `if (rc_err)` |
| tau_tgy_dma + dcore, wfifo, gray bus, sync | 73 / 74 and 100% | `default: pst <= P_IDLE` |
| tau_tgy_wfifo (own test) | 28 / 28 | none |

Every miss is a branch that cannot be reached, none is an untested feature: the `default:` arms are recovery from illegal state encodings, kept on purpose (a bit flip lands in a known state), and the tile controller's `rc_err` is dead by construction (the tile checks the same format expression before it ever starts the pixel stage, so the pixel stage cannot report what the tile already refused). That is line coverage of the 11 TGY files under their own tests: it says no line is unexercised; it does not say the checks are strong (that is what the mutant sweeps are for) and it does not cover toggles of every bit.

## What the Verilator run found
The recon testbench driving its write port with non-blocking assignments at the very edge the design samples lost every write but the first under Verilator (692,167 pixel mismatches; the test passes in Icarus). A race in the testbench, not the design: it now drives on the falling edge and passes in both simulators, all 17 mutants still die. Worth knowing: a second simulator is a cheap check of the testbenches themselves.
