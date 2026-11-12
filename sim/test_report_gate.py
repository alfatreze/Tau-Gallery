#!/usr/bin/env python3
"""Host test for tools/report_gate.py on small synthetic report sets (the formats are those of Quartus 25.1 Lite, taken from a real compile): a clean set is OK; every FAIL and CHANGE rule fires on the
set that breaks it and not on the clean one; --strict turns a change into a failure; a missing baseline fails; --update-baseline round-trips. `--mutants` edits copies of the tool; each edit must fail the test.
If work/reports/gallery-m1d-s1 (a real compile's reports, git-ignored) is present, the real set must be OK against tools/report_baselines/gallery-m1d.json."""
import json, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
TOOL = ROOT / "tools/report_gate.py"
FIT = "Logic utilization (in ALMs) : %d / 18,480 ( %d %% )\nTotal RAM Blocks : %d / 308 ( %d %% )\nTotal DSP Blocks : 47 / 66 ( 71 %% )\nTotal PLLs : 2 / 4 ( 50 %% )\n"
def sta(setup=0.659, hold=0.084): return "Type  : Slow 1100mV 85C Model Setup 'clk'\nSlack : %s\nTNS   : 0.000\n\nType  : Fast 1100mV 0C Model Hold 'clk'\nSlack : %s\nTNS   : 0.000\n" % (setup, hold)
BASE_LOG = ('Info (276029): Inferred altsyncram megafunction from the following design logic: "top|ram_a" \nInfo (276029): Inferred altsyncram megafunction from the following design logic: "top|ram_b" \n'
            'Info (276004): RAM logic "top|small" is uninferred due to inappropriate RAM size File: x\nWarning (332054): Assignment set_output_delay is accepted but has some problems\nWarning (332054): again\n')
def make(d, alms=16440, ram=242, log=BASE_LOG, setup=0.659):
    d = Path(d); d.mkdir(exist_ok=True); (d / "ap.fit.summary").write_text(FIT % (alms, round(100 * alms / 18480), ram, round(100 * ram / 308))); (d / "ap.sta.summary").write_text(sta(setup)); (d / "q.log").write_text(log); return d
fails = 0
def check(name, cond, extra=""):
    global fails
    if not cond: fails += 1; print("FAIL:", name, extra)
def gate(d, *args, tool=TOOL):
    r = subprocess.run([sys.executable, str(tool), str(d), "--log", str(Path(d) / "q.log"), *args], capture_output=True, text=True); return r.returncode, r.stdout
def suite(tool=TOOL):
    global fails; fails = 0
    with tempfile.TemporaryDirectory() as t:
        t = Path(t); good = make(t / "good"); bl = t / "base.json"
        rc, out = gate(good, "--baseline", str(bl), "--update-baseline", tool=tool); check("update-baseline writes and passes", rc == 0 and bl.is_file() and "GATE OK" in out, out)
        rc, out = gate(good, "--baseline", str(bl), tool=tool); check("clean set is OK", rc == 0 and "GATE OK" in out and "0 change(s)" in out, out)
        rc, out = gate(make(t / "neg", setup=-0.1), "--baseline", str(bl), tool=tool); check("negative slack fails", rc == 1 and "negative slack" in out, out)
        rc, out = gate(make(t / "crit", log=BASE_LOG + "Critical Warning (332012): bad\n"), "--baseline", str(bl), tool=tool); check("a Critical Warning fails", rc == 1 and "Critical Warning" in out, out)
        rc, out = gate(make(t / "full", ram=300), "--baseline", str(bl), tool=tool); check("a resource above 95% fails", rc == 1 and "ram_blocks at" in out, out)
        rc, out = gate(make(t / "ign", log=BASE_LOG + "Warning (332174): Ignored filter at a.sdc(3): x could not be matched with a register\n"), "--baseline", str(bl), tool=tool)
        check("an ignored SDC filter fails (it silently drops the constraint)", rc == 1 and "must never appear" in out, out)
        rc, out = gate(make(t / "newram", log=BASE_LOG + 'Info (276029): Inferred altsyncram megafunction from the following design logic: "top|ram_c" \n'), "--baseline", str(bl), tool=tool)
        check("a new inferred RAM is a CHANGE, exit 0", rc == 0 and "GATE CHANGED" in out and "1 new altsyncram" in out, out)
        rc, out = gate(make(t / "lost", log=BASE_LOG.replace('Info (276029): Inferred altsyncram megafunction from the following design logic: "top|ram_b" \n', "")), "--baseline", str(bl), tool=tool)
        check("a RAM no longer inferred is a CHANGE", rc == 0 and "no longer inferred" in out, out)
        rc, out = gate(make(t / "uninf", log=BASE_LOG + 'Info (276007): RAM logic "top|big" is uninferred due to asynchronous read logic File: x\n'), "--baseline", str(bl), tool=tool)
        check("a RAM that is now not inferred is a CHANGE", rc == 0 and "now NOT inferred" in out and "276007" in out, out)
        rc, out = gate(make(t / "same", log=BASE_LOG.replace("Info (276004): RAM logic \"top|small\"", "Info (276004): RAM logic \"top|small\"")), "--baseline", str(bl), tool=tool); check("the known uninferred RAM is not a change", "NOT inferred" not in out, out)
        rc, out = gate(make(t / "warn", log=BASE_LOG + "Warning (332054): third\nWarning (99999): new kind\n"), "--baseline", str(bl), tool=tool)
        check("more of a known warning and a new warning id are CHANGEs", rc == 0 and "warning 332054 up 2 -> 3" in out and "new warning id 99999" in out, out)
        rc, out = gate(make(t / "res", alms=17500), "--baseline", str(bl), tool=tool); check("a resource up more than 2 points is a CHANGE", rc == 0 and "alms 89.0% -> 94.7%" in out, out)
        rc, out = gate(make(t / "strict", log=BASE_LOG + "Warning (99999): x\n"), "--baseline", str(bl), "--strict", tool=tool); check("--strict fails on a change", rc == 1 and "GATE FAIL" in out, out)
        rc, out = gate(good, "--baseline", str(t / "nope.json"), tool=tool); check("a missing baseline fails", rc == 1 and "not found" in out, out)
        rc, out = gate(good, tool=tool); check("without a baseline only the absolute rules run", rc == 0 and "no baseline" in out, out)
        rc, out = gate(t / "empty", tool=tool); check("a directory without a summary fails", rc == 1, out)
    real = ROOT / "work/reports/gallery-m1d-s1"
    if tool == TOOL and real.is_dir():
        r = subprocess.run([sys.executable, str(tool), str(real), "--log", str(real / "quartus-fit.log"), "--baseline", str(ROOT / "tools/report_baselines/gallery-m1d.json")], capture_output=True, text=True)
        check("the real compile is OK against its baseline", r.returncode == 0 and "GATE OK" in r.stdout, r.stdout)
    return fails
MUTANTS = [("if v < 0: fails.append", "if v < -99: fails.append"), ("if f[\"critical_warnings\"]:", "if False:"), ("if v[\"pct\"] > ram_pct:", "if v[\"pct\"] > 999:"),
           ("for l in f[\"never_seen\"][:3]:", "for l in []:"), ("if new: changes.append(\"%d new %s", "if False: changes.append(\"%d new %s"), ("if nu: changes.append", "if False: changes.append"),
           ("elif n > b: changes", "elif n > b + 999: changes"), ("if b is not None and v[\"pct\"] - b > rise", "if b is not None and v[\"pct\"] - b > 999"), ("(changes and a.strict)", "(False and a.strict)"),
           ("if a.baseline and base is None and not a.update_baseline:", "if False:"), ("if gone: changes.append", "if False: changes.append")]
if __name__ == "__main__":
    n = suite()
    if "--mutants" in sys.argv:
        src = TOOL.read_text(); bad = 0
        for a, b in MUTANTS:
            assert a in src, a
            with tempfile.TemporaryDirectory() as t:
                m = Path(t) / "report_gate.py"; m.write_text(src.replace(a, b, 1)); k = suite(m)
            print("killed  " if k else "SURVIVED", a[:50]); bad += k == 0
        n = n or bad
    print("REPORT_GATE TEST", "PASSED" if not n else "FAILED"); sys.exit(1 if n else 0)
