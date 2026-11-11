#!/usr/bin/env python3
"""Line and toggle coverage of one decoder block under its own testbench, with Verilator (5.x, --binary --timing --coverage). Usage:
  python3 tools/rtl_coverage.py BLOCK [--list]        BLOCK = ent | idct | recon | tile | tile0 (the build without 4:4:4) | dma | wfifo | all
Builds in a temporary directory without spaces (GNU make cannot build in one), runs the same vectors as the `make test-rtl-tgy-*` target, and prints for each design
file under src/fpga/core the percentage of covered points and the uncovered lines (testbench files are not counted). A line that never executes is code no test
needs: either a missing test or dead logic, and each is listed in docs/RTL_COVERAGE.md with the verdict. Needs verilator on PATH."""
import os, re, shutil, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools")); import toolchain
C = "src/fpga/core/"
BLOCKS = {
    "ent":   dict(gen="sim/gen_tgy_ent_vectors.py", tb="sim/tb_tau_tgy_ent.v", top="tb_tau_tgy_ent", src=[C + "tau_tgy_ent.sv"], n=17, params=[]),
    "idct":  dict(gen="sim/gen_tgy_idct_vectors.py", tb="sim/tb_tau_tgy_idct.v", top="tb_tau_tgy_idct", src=[C + "tau_tgy_idct.sv"], n=2870, params=[]),
    "recon": dict(gen="sim/gen_tgy_recon_rtl_vectors.py", tb="sim/tb_tau_tgy_recon.v", top="tb_tau_tgy_recon", src=[C + "tau_tgy_recon.sv"], n=144, params=["C444=1"]),
    "tile":  dict(gen="sim/gen_tgy_tile_vectors.py", tb="sim/tb_tau_tgy_tile.v", top="tb_tau_tgy_tile", src=[C + "tau_tgy_tile.sv", C + "tau_tgy_idct.sv", C + "tau_tgy_recon.sv"], n=36, params=["C444=1"]),
    "tile0": dict(gen="sim/gen_tgy_tile_vectors.py", tb="sim/tb_tau_tgy_tile.v", top="tb_tau_tgy_tile", src=[C + "tau_tgy_tile.sv", C + "tau_tgy_idct.sv", C + "tau_tgy_recon.sv"], n=36, params=["C444=0"]),
    "dma":   dict(gen="sim/gen_tgy_dec_vectors.py", tb="sim/tb_tau_tgy_dma.v", top="tb_tau_tgy_dma", n=11, params=["C444=1"],
                  src=["sim/sp_model.v"] + [C + f for f in ("tau_tgy_dma.sv", "tau_tgy_dcore.sv", "tau_tgy_wfifo.sv", "tau_cdc_gray_bus.sv", "tau_cdc_sync1.sv", "tau_tgy_dec.sv", "tau_tgy_tile.sv", "tau_tgy_ent.sv", "tau_tgy_idct.sv", "tau_tgy_recon.sv")]),
    "wfifo": dict(gen=None, tb="sim/tb_tau_tgy_wfifo.v", top="tb_tau_tgy_wfifo", src=[C + "tau_tgy_wfifo.sv", C + "tau_cdc_gray_bus.sv", C + "tau_cdc_sync1.sv"], n=None, params=[]),
}

def run_block(name):
    b = BLOCKS[name]; d = Path(tempfile.mkdtemp(prefix="cov_")); env = dict(os.environ, SDKROOT=toolchain.host_env().get("SDKROOT", os.environ.get("SDKROOT", "")))
    files = [b["tb"]] + b["src"]
    for f in files: shutil.copy(ROOT / f, d / Path(f).name)
    args = []
    if b["gen"]: subprocess.run([sys.executable, str(ROOT / b["gen"]), str(d / "vec.hex")], check=True, capture_output=True); args = [f"+VEC={d / 'vec.hex'}", f"+N={b['n']}"]
    cmd = ["verilator", "--binary", "--timing", "--coverage", "--Wno-fatal", "-Wno-lint", "-Wno-style", "--top-module", b["top"], "-Mdir", "obj", "-o", "cov"] + [f"-G{p}" for p in b["params"]] + [Path(f).name for f in files]
    r = subprocess.run(cmd, cwd=d, capture_output=True, text=True, env=env)
    if r.returncode: print(r.stdout[-1500:], r.stderr[-1500:]); sys.exit(f"{name}: verilator build failed")
    r = subprocess.run(["./obj/cov"] + args, cwd=d, capture_output=True, text=True); out = r.stdout
    passed = re.search(r"(\w+) PASSED", out)
    subprocess.run(["verilator_coverage", "--annotate", "annot", "--annotate-all", "--annotate-min", "1", "coverage.dat"], cwd=d, capture_output=True, text=True)
    print(f"== {name}: test {'PASSED' if passed else 'DID NOT PASS: ' + out[-300:]}")
    tot_all = [0, 0]
    for f in b["src"]:
        if not f.startswith(C): continue
        ann = (d / "annot" / Path(f).name)
        if not ann.is_file(): print(f"   {Path(f).name}: no annotation"); continue
        hit = miss = 0; lines = []; decl = re.compile(r"^\s*(input|output|inout|wire|reg|logic|integer)\b")
        for i, l in enumerate(ann.read_text().splitlines(), 1):
            m = re.match(r"^([ %~+-])(\d{6}) (.*)$", l)
            if not m: continue
            if m.group(1) == "%": miss += 1; lines.append((i, ("[signal never toggles] " if decl.match(m.group(3)) else "") + m.group(3).rstrip()))
            else: hit += 1
        tot_all[0] += hit; tot_all[1] += miss
        print(f"   {Path(f).name}: {hit} of {hit + miss} annotated lines hit ({100.0 * hit / max(1, hit + miss):.1f}%), {sum(1 for _, t in lines if t.startswith('[signal'))} of the misses are signals that never toggle")
        for i, t in lines[:60]: print(f"      not hit {f}:{i}  {t.strip()[:120]}")
        if len(lines) > 60: print(f"      ... {len(lines) - 60} more")
    shutil.rmtree(d, ignore_errors=True)
    return tot_all

if __name__ == "__main__":
    if len(sys.argv) < 2 or sys.argv[1] not in list(BLOCKS) + ["all"]: sys.exit(__doc__)
    for n in (BLOCKS if sys.argv[1] == "all" else [sys.argv[1]]): run_block(n)
