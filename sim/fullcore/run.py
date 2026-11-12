#!/usr/bin/env python3
"""Builds (if needed) and runs the whole-core simulation (docs/FULLCORE_SIM.md). Usage:
  python3 sim/fullcore/run.py [--rebuild] [--rom FILE] [--dir DATADIR] [--out PREFIX] [--maxus N] [--script N] [--extra ARG ...] [--golden compare|update]
Without --rom the gallery firmware is built for the core with the hardware decoder (fw2/build_gallery.sh APP=gallery TGY=1). Needs iverilog and, in work/quartus_sim_lib/, Intel's altera_mf.v from a Quartus install
(never committed: scp it from <quartus>/eda/sim_lib/)."""
import argparse, os, re, subprocess, sys
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "sim/fullcore"))
import prepare as P

def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--rebuild", action="store_true"); ap.add_argument("--rom"); ap.add_argument("--dir"); ap.add_argument("--out", default="build/fullcore/frame")
    ap.add_argument("--maxus", type=int, default=20000); ap.add_argument("--script", type=int, default=0); ap.add_argument("--log"); ap.add_argument("--extra", nargs="*", default=[]); ap.add_argument("--golden", choices=["compare", "update"], help="run the golden-frame scenario (20 ms after reset exit, one frame grabbed) and compare with / update sim/fullcore/golden/boot_20ms.ppm.gz"); a = ap.parse_args()
    if a.golden: a.maxus, a.script, a.out = 20000, 2, "work/fullcore_frames/golden"
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
    if a.golden:                                                                                                              # the frames of an earlier run must not be mistaken for this one's
        for old in Path("work/fullcore_frames").glob("golden_*.ppm"): old.unlink()
        Path("work/fullcore_frames").mkdir(parents=True, exist_ok=True)
    cmd = ["vvp", "-n", str(vvp.resolve()), f"+ROM={Path(rom).resolve()}", f"+DIR={Path(a.dir or '.').resolve()}", f"+OUT={Path(a.out).resolve()}", f"+MAXUS={a.maxus}", f"+SCRIPT={a.script}"] + a.extra
    r = subprocess.run(cmd, capture_output=True, text=True, cwd=ROOT / "build/fullcore")                                   # cwd: the Altera RAM model reads ./apf/build_id.ver
    if a.log: Path(a.log).write_text(r.stdout + r.stderr)
    else: sys.stdout.write(r.stdout + r.stderr)
    if "FULLCORE CPU PASSED" not in r.stdout: print("FULLCORE: the CPU check did not pass (see docs/issues/026)"); sys.exit(r.returncode or 1)
    if a.golden: sys.exit(golden(a, rom))
    sys.exit(r.returncode)

GOLDEN = ROOT / "sim/fullcore/golden/boot_20ms.ppm.gz"; META = ROOT / "sim/fullcore/golden/boot_20ms.json"

def inputs(datadir, rom):
    """SHA-256 of what the golden frame depends on: the data directory's files (git-ignored, see docs/FULLCORE_SIM.md) and the firmware image"""
    import hashlib
    h = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
    return {"data": {f.name: h(f) for f in sorted(Path(datadir).iterdir()) if f.is_file()}, "firmware": h(rom)}

def golden(a, rom):
    import json
    sys.path.insert(0, str(ROOT / "tools")); import check_frames as C
    frames = sorted(Path("work/fullcore_frames").glob("golden_*.ppm"))
    if not frames: print("FRAME: the run saved no frame"); return 1
    now = inputs(a.dir or ".", rom)
    if a.golden == "update":
        C.main(["check_frames", "update", str(frames[-1]), str(GOLDEN)]); META.write_text(json.dumps(now, indent=1, sort_keys=True) + "\n"); print("golden inputs recorded in", META.name); return 0
    then = json.loads(META.read_text()) if META.is_file() else {}
    for k in ("data", "firmware"):
        if then.get(k) != now[k]: print("FRAME NOTE: the %s differ from the ones the golden was taken with%s" % (k, ("" if k == "firmware" else " (" + ", ".join(sorted(n for n in set(then.get(k, {})) | set(now[k]) if then.get(k, {}).get(n) != now[k].get(n))) + ")")))
    return C.main(["check_frames", "compare", str(frames[-1]), str(GOLDEN), "--diff", "work/fullcore_frames/golden_diff.png"])

if __name__ == "__main__": main()
