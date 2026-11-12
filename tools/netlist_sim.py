#!/usr/bin/env python3
"""Post-synthesis (functional) netlist simulation of the hardware TGY unit: does the logic Quartus builds from tau_tgy_unit.sv behave like the RTL the tests ran on?
Synthesis can differ from simulation (width and sign rules, X handling, RAM inference, retiming); this runs the unit's own testbench (sim/tb_tau_tgy_unit.v, the
same vectors) against the Quartus netlist of the same RTL (C444 = 0 and TMO = 0xFFFFFF baked in, as the testbench uses them), in Icarus with Intel's cyclonev_atoms.v
(copied from the VM's eda/sim_lib into git-ignored work/quartus_sim_lib/, never committed).
  python3 tools/netlist_sim.py launch NAME [--wait-free]    # stage on the VM, quartus_map + quartus_eda in the background (minutes; needs the owner's yes, rule 3)
  python3 tools/netlist_sim.py status NAME
  python3 tools/netlist_sim.py run NAME [--maxmin 60]       # copy the netlist, build the testbench variant, simulate, report PASSED / FAILED
"""
import argparse, io, re, subprocess, sys, tarfile, time
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
import vm_fit as V, block_probe as B
ROOT = V.ROOT; CORE = ROOT / "src/fpga/core"; QBIN = V.QUARTUS.rsplit("/", 1)[0]
FILES = B.PROBES["unit"]["files"]
QSF = B.SETTINGS + "set_global_assignment -name TOP_LEVEL_ENTITY tau_tgy_unit\nset_parameter -name C444 0\nset_parameter -name TMO 16777215\nset_global_assignment -name AUTO_DSP_RECOGNITION OFF\nset_global_assignment -name AUTO_SHIFT_REGISTER_RECOGNITION OFF\n" + "".join(f"set_global_assignment -name SYSTEMVERILOG_FILE rtl/{f}\n" for f in FILES)

def stage():
    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode="w:gz") as t:
        def add(arc, text):
            b = text.encode(); ti = tarfile.TarInfo(arc); ti.size = len(b); ti.mtime = int(time.time()); t.addfile(ti, io.BytesIO(b))
        add("nl.qpf", 'PROJECT_REVISION = "nl"\n'); add("nl.qsf", QSF)
        for f in FILES: add(f"rtl/{f}", (CORE / f).read_text())
    return buf.getvalue()

def cmd_launch(a):
    d = f"{V.BASE}/netlist-{a.name}"
    busy = V.running_compiles()
    while busy and a.wait_free: print(time.strftime("%H:%M"), "VM busy, checking again in 5 min", flush=True); time.sleep(300); busy = V.running_compiles()
    if busy: sys.exit("the VM already has a Quartus process running (use --wait-free)")
    if V.ssh(f"test -e {d} && echo EXISTS", check=False).strip(): sys.exit(f"{d} exists; pick another NAME")
    p = subprocess.run(V.SSH + [f"mkdir -p {d} && tar xzf - -C {d}"], input=stage(), capture_output=True)
    if p.returncode: sys.exit("stage failed: " + p.stderr.decode()[-300:])
    q = QBIN
    V.ssh(f"cd {d} && (setsid nohup sh -c '{q}/quartus_map nl && {q}/quartus_eda nl --simulation --tool=questasim --format=verilog --functional=on --output_directory=sim && touch nl.done' < /dev/null > quartus.log 2>&1 &) ; echo started")
    print(f"launched {d}; synthesis of the unit alone, expect a few minutes (not measured yet), by about {time.strftime('%H:%M', time.localtime(time.time() + 15 * 60))}"); return 0

def cmd_status(a):
    d = f"{V.BASE}/netlist-{a.name}"
    print(V.ssh(f"cd {d} && (test -e nl.done && echo DONE || echo running/failed); tail -3 quartus.log; ls sim sim/* 2>/dev/null | head", check=False)); return 0

def cut_lnsim(lib):
    """lnsim_subset.v = the modules of altera_lnsim.sv the netlist instantiates (generic_m10k and what it needs), because Icarus cannot read the whole file."""
    if (lib / "lnsim_subset.v").is_file(): return
    t = (lib / "altera_lnsim.sv").read_text(); need = ["generic_m10k"]; got = {}
    while need:
        n = need.pop()
        if n in got: continue
        m = re.search(r"\nmodule\s+" + n + r"\b.*?\nendmodule", t, re.S)
        if not m: continue
        got[n] = m.group(0)
        for k in re.findall(r"^\s*(\w+)\s+(?:#\s*\(.*?\)\s*)?\w+\s*\(", m.group(0), re.M | re.S):
            if re.search(r"\nmodule\s+" + k + r"\b", t) and k not in got: need.append(k)
    (lib / "lnsim_subset.v").write_text("\n".join(got.values()) + "\n")

def cmd_run(a):
    d = f"{V.BASE}/netlist-{a.name}"; lib = ROOT / "work/quartus_sim_lib"; lib.mkdir(parents=True, exist_ok=True); out = ROOT / "work/netlist_sim" / a.name; out.mkdir(parents=True, exist_ok=True)
    def scp(src, dst): return subprocess.run(["scp", "-q", "-i", str(V.KEY), "-P", "2222", f"taualpha@127.0.0.1:{src}", str(dst)]).returncode
    for f in ("cyclonev_atoms.v", "altera_primitives.v", "altera_lnsim.sv"):   # lnsim is huge SystemVerilog: only the modules the netlist uses are cut out of it (lnsim_subset.v)
        if not (lib / f).is_file() and scp(f"{QBIN.rsplit('/', 1)[0]}/eda/sim_lib/{f}", lib / f): sys.exit(f"could not copy {f}")
    vo = V.ssh(f"ls {d}/sim/*.vo {d}/sim/questasim/*.vo {d}/sim/*/*.vo 2>/dev/null | head -1").strip()
    if not vo: sys.exit("no .vo netlist yet: `status`")
    if scp(vo, out / "unit.vo"): sys.exit("copy of the netlist failed")
    cut_lnsim(lib)
    tb = (ROOT / "sim/tb_tau_tgy_unit.v").read_text(); tb = re.sub(r"tau_tgy_unit #\([^)]*\([^)]*\)[^)]*\([^)]*\)[^)]*\([^)]*\)\) dut", "tau_tgy_unit dut", tb); tb = tb.replace("parameter integer C444 = 1;", "parameter integer C444 = 0;")
    (out / "tb.v").write_text(tb)
    subprocess.run([sys.executable, str(ROOT / "sim/gen_tgy_dec_vectors.py"), str(out / "vec.hex")], capture_output=True, check=True)
    r = subprocess.run(["iverilog", "-g2012", "-s", "tb_tau_tgy_unit", "-o", str(out / "nl.vvp"), str(out / "tb.v"), str(ROOT / "sim/sp_model.v"), str(out / "unit.vo"), str(lib / "cyclonev_atoms.v"), str(lib / "altera_primitives.v"), str(lib / "lnsim_subset.v")], capture_output=True, text=True)
    if r.returncode: print(r.stderr[-2500:]); sys.exit("iverilog failed")
    t0 = time.time()
    try: r = subprocess.run(["vvp", str(out / "nl.vvp"), f"+VEC={out / 'vec.hex'}"], capture_output=True, text=True, timeout=a.maxmin * 60)
    except subprocess.TimeoutExpired: sys.exit(f"netlist simulation not finished after {a.maxmin} minutes")
    lines = [l for l in r.stdout.splitlines() if re.search(r"^tile|PASSED|FAILED|abort|corrupt|CLEAR", l)]; print("\n".join(lines[-14:])); print(f"{time.time() - t0:.0f} s")
    return 0 if "UNIT PASSED" in r.stdout else 1

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter); s = ap.add_subparsers(dest="cmd", required=True)
    l = s.add_parser("launch"); l.add_argument("name"); l.add_argument("--wait-free", action="store_true"); l.set_defaults(f=cmd_launch)
    s.add_parser("status").add_argument("name"); s.choices["status"].set_defaults(f=cmd_status)
    r = s.add_parser("run"); r.add_argument("name"); r.add_argument("--maxmin", type=int, default=60); r.set_defaults(f=cmd_run)
    a = ap.parse_args(); sys.exit(a.f(a) or 0)
if __name__ == "__main__": main()
