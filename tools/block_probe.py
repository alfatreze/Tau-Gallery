#!/usr/bin/env python3
"""Timing probes of single blocks on the build VM: the answer in minutes instead of the full core's 1h45 (docs/FULLCORE_SIM.md).

  python3 tools/block_probe.py check                          # local: iverilog elaboration + Verilator lint of every probe top (no VM)
  python3 tools/block_probe.py launch NAME PROBE [--seed 1] [--period-scale 1.0] [--dry-run] [--wait-free] [--force]
  python3 tools/block_probe.py status NAME                    # running / done; resources and the worst setup / hold slack per corner and per clock
  python3 tools/block_probe.py collect NAME                   # copy the reports to work/diagnostics/block_probe-NAME/
PROBE is one of: """ + "unit, ent, pixfx" + """. The probe project has the RTL of the block copied from the CURRENT working tree (uncommitted changes included), a top that puts every input and output one register away from a virtual pin, the clocks as
pins at the block's real periods (unit: 15 / 10 / 20 ns, ent: 10 ns) and asynchronous clock groups, the device and the fit settings of src/fpga/ap_core.qsf. Needs the owner's yes for the VM (rule 3: state the finish time; the tool prints it;
measured 2026-11-11: `ent` about 15 minutes, `unit` about 40; the full core takes 1h45); it refuses to start while a Quartus process runs on the VM (--wait-free polls until it is free; --force only if sure it is idle). --period-scale 0.8 tightens every clock to see how much margin
there is. Same ssh constants and shape as tools/vm_fit.py."""
import argparse, io, re, subprocess, sys, tarfile, time
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
import vm_fit as V

ROOT = V.ROOT; CORE = ROOT / "src/fpga/core"; PD = ROOT / "tools/block_probe"
QBIN = V.QUARTUS.rsplit("/", 1)[0]; QSTA = QBIN + "/quartus_sta"
PROBES = {
    "unit": dict(top="tau_tgy_unit_probe_top", clocks={"clk_sys": 15.0, "clk_mem": 10.0, "clk_dec": 20.0},
                 files=["tau_sdram_dmux.sv", "tau_tgy_unit.sv", "tau_tgy_regs.sv", "tau_tgy_dma.sv", "tau_tgy_dcore.sv", "tau_tgy_wfifo.sv", "tau_cdc_gray_bus.sv", "tau_cdc_sync1.sv", "tau_tgy_dec.sv", "tau_tgy_tile.sv",
                        "tau_tgy_ent.sv", "tau_tgy_idct.sv", "tau_tgy_recon.sv", "tau_hist_if.sv", "tau_hist.sv"], virtual=["vi_sys[*]", "vi_mem[*]", "vo_sys[*]", "vo_mem[*]"]),
    "pixfx": dict(top="tau_pixfx_probe_top", clocks={"clk_sys": 15.0, "clk_vid": 25.0}, files=["tau_pixfx.sv"], virtual=["vi_sys[*]", "vi_vid[*]", "vo_sys[*]", "vo_vid[*]"]),
    "ent": dict(top="tau_tgy_ent_probe_top", clocks={"clk": 10.0}, files=["tau_tgy_ent.sv"], virtual=["vi[*]", "vo[*]"]),
}
SETTINGS = """set_global_assignment -name FAMILY "Cyclone V"
set_global_assignment -name DEVICE 5CEBA4F23C8
set_global_assignment -name DEVICE_FILTER_PACKAGE FBGA
set_global_assignment -name DEVICE_FILTER_PIN_COUNT 484
set_global_assignment -name DEVICE_FILTER_SPEED_GRADE 8
set_global_assignment -name PROJECT_OUTPUT_DIRECTORY output_files
set_global_assignment -name SAFE_STATE_MACHINE ON
set_global_assignment -name ADV_NETLIST_OPT_SYNTH_WYSIWYG_REMAP ON
set_global_assignment -name SYNTH_PROTECT_SDC_CONSTRAINT ON
set_global_assignment -name PRE_MAPPING_RESYNTHESIS ON
set_global_assignment -name OPTIMIZATION_TECHNIQUE SPEED
set_global_assignment -name MUX_RESTRUCTURE OFF
set_global_assignment -name OPTIMIZATION_MODE "HIGH PERFORMANCE EFFORT"
set_global_assignment -name ECO_OPTIMIZE_TIMING ON
set_global_assignment -name PERIPHERY_TO_CORE_PLACEMENT_AND_ROUTING_OPTIMIZATION ON
set_global_assignment -name PHYSICAL_SYNTHESIS_COMBO_LOGIC ON
set_global_assignment -name PHYSICAL_SYNTHESIS_REGISTER_DUPLICATION ON
set_global_assignment -name PHYSICAL_SYNTHESIS_REGISTER_RETIMING ON
set_global_assignment -name PHYSICAL_SYNTHESIS_ASYNCHRONOUS_SIGNAL_PIPELINING ON
set_global_assignment -name FITTER_EFFORT "AUTO FIT"
set_global_assignment -name ROUTER_LCELL_INSERTION_AND_LOGIC_DUPLICATION ON
"""
REPORT_TCL = """project_open -revision probe probe
create_timing_netlist -model slow
read_sdc
update_timing_netlist
report_clock_fmax_summary -file probe_fmax.txt
report_timing -setup -npaths 10 -detail summary -file probe_worst_setup.txt
report_timing -setup -npaths 1 -detail full_path -file probe_worst_path.txt
"""

def stage_files(name, probe, scale):
    p = PROBES[probe]; files = {}
    files["probe.qpf"] = "PROJECT_REVISION = \"probe\"\n"
    qsf = SETTINGS + f"set_global_assignment -name TOP_LEVEL_ENTITY {p['top']}\nset_global_assignment -name SDC_FILE probe.sdc\n"
    for f in p["files"]: qsf += f"set_global_assignment -name SYSTEMVERILOG_FILE rtl/{f}\n"
    qsf += f"set_global_assignment -name SYSTEMVERILOG_FILE {p['top']}.sv\n"
    for v in p["virtual"]: qsf += f"set_instance_assignment -name VIRTUAL_PIN ON -to {v}\n"
    files["probe.qsf"] = qsf
    sdc = ""; names = []
    for c, per in p["clocks"].items(): sdc += f"create_clock -name {c} -period {per * scale:.3f} [get_ports {c}]\n"; names.append(c)
    if len(names) > 1: sdc += "set_clock_groups -asynchronous " + " ".join(f"-group {{ {c} }}" for c in names) + "\n"
    sdc += "derive_clock_uncertainty\nset_false_path -from [get_ports {vi*}] \nset_false_path -to [get_ports {vo*}]\n"
    files["probe.sdc"] = sdc; files["report.tcl"] = REPORT_TCL
    return files

def tar_stage(name, probe, scale):
    buf = io.BytesIO(); p = PROBES[probe]
    with tarfile.open(fileobj=buf, mode="w:gz") as t:
        def add(arc, data):
            ti = tarfile.TarInfo(arc); b = data.encode() if isinstance(data, str) else data; ti.size = len(b); ti.mtime = int(time.time()); t.addfile(ti, io.BytesIO(b))
        for arc, text in stage_files(name, probe, scale).items(): add(arc, text)
        add(f"{p['top']}.sv", (PD / f"{p['top']}.sv").read_text())
        for f in p["files"]: add(f"rtl/{f}", (CORE / f).read_text())
    return buf.getvalue()

def cmd_check(a):
    rc = 0
    for name, p in PROBES.items():
        files = [str(CORE / f) for f in p["files"]] + [str(PD / f"{p['top']}.sv")]
        r = subprocess.run(["iverilog", "-g2012", "-s", p["top"], "-o", "/dev/null", *files], capture_output=True, text=True)
        err = [l for l in r.stderr.splitlines() if "error" in l.lower()]
        v = subprocess.run(["verilator", "--lint-only", "-Wno-fatal", "-Wno-lint", "-Wno-style", "--top-module", p["top"], *files], capture_output=True, text=True)
        verr = [l for l in v.stderr.splitlines() if l.startswith("%Error")]
        print(f"{name}: iverilog {'ok' if not err and r.returncode == 0 else 'ERRORS'}, verilator {'ok' if not verr else 'ERRORS'}")
        for l in (err + verr)[:5]: print("   ", l[:200]); rc = 1
    return rc

def cmd_launch(a):
    if a.probe not in PROBES: sys.exit("PROBE must be one of " + ", ".join(PROBES))
    data = tar_stage(a.name, a.probe, a.period_scale); d = f"{V.BASE}/block_probe-{a.name}"
    if a.dry_run:
        print("dry run: would stage", [n for n in tarfile.open(fileobj=io.BytesIO(data)).getnames()], "to", d); print(stage_files(a.name, a.probe, a.period_scale)["probe.sdc"]); return 0
    busy = V.running_compiles()
    while busy and a.wait_free:
        print(time.strftime("%H:%M"), f"VM busy ({len(busy)} Quartus process(es)); checking again in {a.poll // 60} min", flush=True); time.sleep(a.poll); busy = V.running_compiles()
    if busy and not a.force: sys.exit("the VM already has a Quartus process running (use --wait-free, or --force only if you are sure it is idle):\n" + "\n".join(busy))
    if V.ssh(f"test -e {d} && echo EXISTS", check=False).strip(): sys.exit(f"{d} already exists on the VM; pick another NAME")
    p = subprocess.run(V.SSH + [f"mkdir -p {d} && tar xzf - -C {d}"], input=data, capture_output=True)
    if p.returncode: sys.exit("stage failed: " + p.stderr.decode()[-300:])
    V.ssh(f"cd {d} && echo 'set_global_assignment -name SEED {a.seed}' >> probe.qsf")
    V.ssh(f"cd {d} && (setsid nohup sh -c '{V.QUARTUS} --flow compile probe && {QSTA} -t report.tcl > report.log 2>&1; touch probe.done' < /dev/null > quartus.log 2>&1 &) ; echo started")
    fin = time.strftime("%H:%M", time.localtime(time.time() + (40 if a.probe == 'unit' else 15) * 60))
    print(f"launched {d}; expect it done in about {40 if a.probe == 'unit' else 15} minutes (measured: unit 39, ent 15), by about {fin} local time"); return 0

STATUS_PY = r'''
import re,sys,os
d=sys.argv[1]; o=d+"/output_files/"
def rd(p):
    try: return open(p).read()
    except Exception: return ""
log=rd(d+"/quartus.log"); fit=rd(o+"probe.fit.summary"); done=os.path.exists(d+"/probe.done")
ok=re.search(r"Full Compilation was successful",log); err=re.search(r"Full Compilation was unsuccessful|^Error \(",log,re.M)
print(os.path.basename(d), "DONE" if (ok and done) else ("FAILED" if err else "running/unknown"))
for k in ("Fitter Status","Logic utilization","Total registers","Total RAM Blocks","Total DSP Blocks"):
    m=re.search(k+r"\s*:\s*(.*)",fit)
    if m: print("  ",k,":",m.group(1).strip())
print(rd(d+"/probe_fmax.txt")[:2500])
'''

def cmd_status(a):
    d = f"{V.BASE}/block_probe-{a.name}"; print(V.ssh(f"python3 - {d}", inp=STATUS_PY).rstrip()); return 0

def cmd_collect(a):
    out = ROOT / "work/diagnostics" / f"block_probe-{a.name}"; out.mkdir(parents=True, exist_ok=True)
    for f in ("probe_fmax.txt", "probe_worst_setup.txt", "probe_worst_path.txt", "output_files/probe.fit.summary", "output_files/probe.sta.summary", "report.log"):
        subprocess.run(["scp", "-q", "-i", str(V.KEY), "-P", "2222", f"taualpha@127.0.0.1:{V.BASE}/block_probe-{a.name}/{f}", str(out / Path(f).name)])
    print("collected to", out); return 0

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter); sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("check").set_defaults(f=cmd_check)
    l = sub.add_parser("launch"); l.add_argument("name"); l.add_argument("probe"); l.add_argument("--seed", type=int, default=1); l.add_argument("--period-scale", type=float, default=1.0)
    l.add_argument("--dry-run", action="store_true"); l.add_argument("--wait-free", action="store_true"); l.add_argument("--force", action="store_true"); l.add_argument("--poll", type=int, default=300); l.set_defaults(f=cmd_launch)
    for n, f in (("status", cmd_status), ("collect", cmd_collect)): s = sub.add_parser(n); s.add_argument("name"); s.set_defaults(f=f)
    a = ap.parse_args(); sys.exit(a.f(a) or 0)

if __name__ == "__main__": main()
