#!/usr/bin/env python3
"""Prepares the sources of the whole core (src/fpga/core + the Analogue files it needs) for Icarus (docs/FULLCORE_SIM.md). Quartus accepts a signal that is used before its declaration; Icarus does not.
This copies the sources to OUT (build/fullcore/src) and, repeatedly asking iverilog what it cannot bind, moves every offending declaration to the top of the module that uses it (`wire x = expr;` becomes the
forward declaration `wire x;` there and `assign x = expr;` where it was). The synthesised sources are never modified. Also: sim/make_soc_sim.py's hoist of tau_soc's register constants, and a replication count
that is negative only in a dead branch (sound_i2s). Usage: prepare.py OUT [-D...]  -> prints the iverilog command line pieces it settled on (FILES, DEFS) to OUT/files.txt and OUT/defs.txt."""
import os, re, subprocess, sys, shutil
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent.parent
SRC = ROOT / "src/fpga"
EXTRA = [ROOT / "sim/fullcore/altera_pll_model.v", ROOT / "work/quartus_sim_lib/altera_mf.v"]

def macros():
    defs = []
    for f in (SRC / "ap_core.qsf", ROOT / "tools/gallery_m1e_qsf_append.txt"):
        for m in re.finditer(r'VERILOG_MACRO\s+"([A-Za-z0-9_]+)=([0-9]+)"', f.read_text()): defs.append(f"-D{m.group(1)}={m.group(2)}")
    return sorted(set(defs))

def module_header_end(lines, upto):
    """index of the line after the port list of the module that contains line index `upto` (None when no module starts above it)"""
    m = None
    for i in range(min(upto, len(lines) - 1), -1, -1):
        if re.match(r"\s*module\s", lines[i]): m = i; break
    if m is None: return None
    for j in range(m, len(lines)):
        if re.match(r"\s*\)\s*;", lines[j]) or (j > m and re.search(r"\)\s*;\s*(//.*)?$", lines[j]) and not lines[j].lstrip().startswith("//")): return j + 1
    return None

def split_decl(line):
    m = re.match(r"^(\s*)(wire|reg)(\s+signed)?(\s*\[[^\]]+\])?\s+(.*?);\s*(//.*)?$", line.rstrip("\n"))
    if not m: return None
    ind, kind, sg, rng, body, cmt = m.groups(); sg = sg or ""; rng = rng or ""
    depth = 0; eq = None
    for k, ch in enumerate(body):
        if ch in "([{": depth += 1
        elif ch in ")]}": depth -= 1
        elif ch == "=" and depth == 0: eq = k; break
    if eq is None: return ind, kind, sg, rng, body.strip(), None, cmt or ""
    return ind, kind, sg, rng, body[:eq].strip(), body[eq + 1:].strip(), cmt or ""


REG_LINE = re.compile(r"^(\s*)((?:output\s+)?reg)(\s+signed)?(\s*\[[^\]]+\])?\s+([A-Za-z_]\w*(?:\s*,\s*[A-Za-z_]\w*)*)\s*(;|,|\))(.*)$")
def zero_init(text):
    """Quartus powers every register up at 0 unless told otherwise and parts of this design rely on it; Icarus starts them at X. Every scalar or vector `reg` declaration without an initialiser gets `= 0`."""
    out = []
    for line in text.split("\n"):
        code = line.split("//")[0]
        m = REG_LINE.match(code)
        if m and "=" not in code and "[" not in code.split("reg", 1)[1].split(m.group(5))[-1]:
            names = ", ".join(f"{n.strip()} = 0" for n in m.group(5).split(","))
            out.append(f"{m.group(1)}{m.group(2)}{m.group(3) or ''}{m.group(4) or ''} {names}{m.group(6)}{m.group(7)}" + (("//" + line.split("//", 1)[1]) if "//" in line else ""))
        else: out.append(line)
    return "\n".join(out)

LOGIC_ARR = re.compile(r'(\(\*\s*ramstyle\s*=\s*"logic"\s*\*\)\s*reg\s*(?:\[[^\]]+\])?\s*(\w+)\s*\[0:(\d+)\]\s*;)')
def zero_logic_arrays(text):
    """An array marked ramstyle "logic" is built from registers, which Quartus powers at 0 (the settings register file `set_reg` is one: the firmware reads it at boot, X made it compute a table address from X
    and wait on a bus read that never ends). Arrays in block RAM are left alone: their contents are not promised."""
    return LOGIC_ARR.sub(lambda m: m.group(1) + f" integer _zi_{m.group(2)}; initial for (_zi_{m.group(2)} = 0; _zi_{m.group(2)} <= {m.group(3)}; _zi_{m.group(2)} = _zi_{m.group(2)} + 1) {m.group(2)}[_zi_{m.group(2)}] = 0;", text)

def build(out):
    out = Path(out); shutil.rmtree(out, ignore_errors=True); (out / "core").mkdir(parents=True); (out / "apf").mkdir(); (out / "mf").mkdir()
    for f in list((SRC / "core").glob("*.v")) + list((SRC / "core").glob("*.sv")) + list((SRC / "core").glob("*.vh")) + list((SRC / "core").glob("*.svh")): shutil.copy(f, out / "core" / f.name)
    shutil.copy(SRC / "core/mf_pllbase/mf_pllbase_0002.v", out / "core/mf_pllbase_0002.v")
    for n in ("common.v", "mf_datatable.v", "mf_ddio_bidir_12.v"): shutil.copy(SRC / "apf" / n, out / "apf" / n)
    shutil.copy(SRC / "rtl/VexRiscv_Full.v", out / "core/VexRiscv_Full.v")
    (out / "apf/build_id.ver").write_text("\n".join("00000000" for _ in range(256)) + "\n"); shutil.copy(SRC / "apf/build_id.mif", out / "apf/build_id.mif")
    subprocess.run([sys.executable, str(ROOT / "sim/make_soc_sim.py"), str(SRC / "core/tau_soc.v"), str(out / "core/tau_soc.v")], check=True)
    # VexRiscv_Full.v (docs/issues/026): its combinational blocks are `always @(*)`, which Icarus does NOT evaluate at time 0 (it waits for an input to change). A reg that Quartus powers at 0 and that this pass
    # initialises to 0 may then never see the event that would update it: the instruction cache's tag write enable stayed 0 through the whole flush, the tags stayed X, every fetch "hit" on X and the CPU took an
    # illegal instruction at 0 for ever (and the outcome moved with unrelated edits: a race). `always_comb` runs once at time 0, so every combinational block is evaluated before the first clock.
    vp = out / "core/VexRiscv_Full.v"; vp.write_text(vp.read_text() if os.environ.get("TAU_FULLCORE_MUTANT_NO_COMB") else vp.read_text().replace("always @(*)", "always_comb"))   # the env var is the mutant: the check must then FAIL
    for f in list((out / "core").glob("*.v")) + list((out / "core").glob("*.sv")) + list((out / "core").glob("*.vh")) + list((out / "apf").glob("*.v")): f.write_text(zero_logic_arrays(zero_init(f.read_text())))
    p = out / "core/sound_i2s.v"; t = p.read_text()
    t = t.replace("{(15 - CHANNEL_WIDTH){1'b0}}", "{(CHANNEL_WIDTH >= 15 ? 1 : 15 - CHANNEL_WIDTH){1'b0}}"); p.write_text(t)
    files = sorted(str(f) for f in (out / "core").glob("*.v")) + sorted(str(f) for f in (out / "core").glob("*.sv")) + sorted(str(f) for f in (out / "apf").glob("*.v")) + [os.path.relpath(e, ROOT) for e in EXTRA]
    return files

def errors(files, defs, out):
    r = subprocess.run(["iverilog", "-g2012", f"-I{out}/core", f"-I{out}/apf", *defs, "-s", "core_top", "-o", str(out / "x.vvp"), *files], capture_output=True, text=True)
    return r.returncode, r.stderr

def main(outdir):
    out = Path(outdir); defs = macros(); files = build(out); done = set()
    for it in range(200):
        rc, err = errors(files, defs, out)
        if rc == 0: break
        lines = err.splitlines(); fixes = []
        for i, l in enumerate(lines):
            m = re.match(r"(.*?):(\d+): error: (?:Unable to bind wire/reg/memory `(\w+)'|Could not find variable ``(\w+)'' in ``[\w.]+''|Net (\w+) is not defined in this context)", l)
            if not m: continue
            use_file, use_line, sym = Path(m.group(1)), int(m.group(2)), next(g for g in m.groups()[2:] if g)
            d = next((re.match(r"(.*?):(\d+):\s*: A symbol with that name was declared here", x) for x in lines[i + 1:i + 3] if re.match(r"(.*?):(\d+):\s*: A symbol with that name was declared here", x)), None)
            if d: fixes.append((use_file, use_line, sym, Path(d.group(1)), int(d.group(2))))
        if not fixes: print(err[:3000]); sys.exit("iverilog errors that are not use-before-declaration; see above")
        for use_file, use_line, sym, dfile, dline in fixes[:1]:                                            # one fix, then ask iverilog again (an insertion shifts the line numbers it reported)
            if (dfile, dline, sym) in done: sys.exit(f"{sym} at {dfile}:{dline} was moved but is still unbound")
            done.add((dfile, dline, sym))
            dl = dfile.read_text().split("\n"); end = dline - 1
            while not re.sub(r"//.*", "", dl[end]).rstrip().endswith(";"): end += 1                      # a declaration may span several lines
            stmt = " ".join(re.sub(r"//.*", "", x).strip() if k else re.sub(r"//.*", "", x).rstrip() for k, x in enumerate(dl[dline - 1:end + 1]))
            s = split_decl(stmt)
            if s is None: sys.exit(f"cannot split the declaration of {sym} at {dfile}:{dline}: {dl[dline - 1]}")
            ind, kind, sg, rng, names, init, cmt = s
            if init is None: dl[dline - 1] = f"{ind}// (declaration of {names} moved up for Icarus) {cmt}"
            elif kind == "reg": dl[dline - 1] = f"{ind}initial {names} = {init};   // (declaration moved up for Icarus)"
            else: dl[dline - 1] = f"{ind}assign {names} = {init};   // (declaration moved up for Icarus)"
            for k in range(dline, end + 1): dl[k] = ""                                                    # the lines the declaration continued on
            dfile.write_text("\n".join(dl))
            # insertion: the module that contains the use; a .vh has none: its includer is core_top.v
            host = use_file if use_file.suffix != ".vh" else out / "core/core_top.v"
            hl = host.read_text().split("\n"); at = module_header_end(hl, use_line - 1 if use_file == host else len(hl))
            if at is None: sys.exit(f"no module around the use of {sym} in {use_file}:{use_line}")
            hl.insert(at, f"{ind}{kind}{sg} {rng.strip()} {names};   // (forward declaration for Icarus)".replace("  ", " ")); host.write_text("\n".join(hl))
    else: sys.exit("did not settle")
    (out / "files.txt").write_text("\n".join(files)); (out / "defs.txt").write_text(" ".join(defs))
    print("settled after", len(done), "moved declarations;", len(files), "files")

if __name__ == "__main__": main(sys.argv[1])
