#!/usr/bin/env python3
"""Builds (if needed) and runs the whole-core simulation (docs/FULLCORE_SIM.md). Usage:
  python3 sim/fullcore/run.py [--rebuild] [--rom FILE] [--dir DATADIR] [--out PREFIX] [--maxus N] [--script N] [--extra ARG ...]
Without --rom the gallery firmware is built for the core with the hardware decoder (fw2/build_gallery.sh APP=gallery TGY=1). Needs iverilog and, in work/quartus_sim_lib/, Intel's altera_mf.v from a Quartus install
(never committed: scp it from <quartus>/eda/sim_lib/)."""
import argparse, os, re, subprocess, sys
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "sim/fullcore"))
import prepare as P

def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--rebuild", action="store_true"); ap.add_argument("--rom"); ap.add_argument("--dir"); ap.add_argument("--out", default="build/fullcore/frame")
    ap.add_argument("--maxus", type=int, default=20000); ap.add_argument("--script", type=int, default=0); ap.add_argument("--log"); ap.add_argument("--extra", nargs="*", default=[]); a = ap.parse_args()
    os.chdir(ROOT); vvp = Path("build/fullcore/tb.vvp")
    if not (ROOT / "work/quartus_sim_lib/altera_mf.v").is_file(): sys.exit("work/quartus_sim_lib/altera_mf.v is missing: copy it from <quartus>/eda/sim_lib/ (Intel's file, never committed)")
    if a.rebuild or not vvp.is_file():
        P.main("build/fullcore")
        files = (Path("build/fullcore/files.txt").read_text()).split("\n"); defs = Path("build/fullcore/defs.txt").read_text().split()
        cmd = ["iverilog", "-g2012", "-Ibuild/fullcore/core", "-Ibuild/fullcore/apf", "-Isim/sdram_chip_model", *defs, "-s", "tb_fullcore", "-o", str(vvp), "sim/fullcore/tb_fullcore.v", "sim/sdram_chip_model.sv", *files]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode: print("\n".join(l for l in r.stderr.splitlines() if "warning" not in l and "Pruning" not in l)[:4000]); sys.exit("build failed")
    rom = a.rom
    if not rom:
        env = dict(os.environ, APP="gallery", TGY="1", TAU_BUILD_OUT=str(ROOT / "work/fullcore_fw")); subprocess.run(["bash", "fw2/build_gallery.sh"], env=env, check=True, capture_output=True)
        rom = "work/fullcore_fw/gallery-app/tau.rom"
    cmd = ["vvp", "-n", str(vvp.resolve()), f"+ROM={Path(rom).resolve()}", f"+DIR={Path(a.dir or '.').resolve()}", f"+OUT={Path(a.out).resolve()}", f"+MAXUS={a.maxus}", f"+SCRIPT={a.script}"] + a.extra
    r = subprocess.run(cmd, capture_output=True, text=True, cwd=ROOT / "build/fullcore")                                   # cwd: the Altera RAM model reads ./apf/build_id.ver
    if a.log: Path(a.log).write_text(r.stdout + r.stderr)
    else: sys.stdout.write(r.stdout + r.stderr)
    if "FULLCORE CPU PASSED" not in r.stdout: print("FULLCORE: the CPU check did not pass (see docs/issues/026)"); sys.exit(r.returncode or 1)
    sys.exit(r.returncode)

if __name__ == "__main__": main()
