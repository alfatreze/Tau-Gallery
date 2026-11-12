#!/usr/bin/env python3
"""Quartus report gate: read a finished compile's reports and say whether anything changed that a person should look at, in seconds, against a stored baseline.

  report_gate.py OUTPUT_FILES_DIR [--log quartus.log] [--baseline FILE] [--update-baseline] [--strict] [--json] [--ram-pct 95]

Reads (read only): <dir>/*.fit.summary or *.map.summary (resources), *.sta.summary (worst slack per clock and corner), and the compile log (or the .map.rpt / .fit.rpt / .sta.rpt) for
messages. What it checks:
  FAIL    a negative slack; a Critical Warning; any resource above --ram-pct percent (default 95); a message that must never appear (`Ignored filter`, `could not be matched`: an SDC pattern
          that matched nothing silently drops its constraint, KB-027); a baseline that is missing when one was asked for.
  CHANGE  (printed, exit 0; with --strict exit 1) against the baseline: a RAM or divider inferred that was not before, or one that went away; a RAM that is now NOT inferred (276004/276006/276007);
          a warning id that is new or occurs more often; a resource up by more than 2 points.
Why a baseline and not absolute rules: a good build here has 12 `uninferred RAM` messages (small arrays that rightly became registers) and 212 `set_output_delay replaced a delay` warnings (the SDC is read
by both the fitter and the timing analyser): an absolute "no uninferred RAM" rule would fail a healthy build. The baseline records what a known-good build looked like; the gate shows the difference.
Baselines are JSON (resources, warning counts, inferred nodes), made with --update-baseline after a fit you accept. Prints one line `GATE OK|CHANGED|FAIL ...` and the details."""
import argparse, json, re, sys
from pathlib import Path

NEVER = [r"Ignored filter", r"could not be matched"]
WATCH_RES = [("alms", r"Logic utilization \(in ALMs\)\s*:\s*([\d,]+) / ([\d,]+)"), ("ram_blocks", r"Total RAM Blocks\s*:\s*([\d,]+) / ([\d,]+)"), ("dsp", r"Total DSP Blocks\s*:\s*([\d,]+) / ([\d,]+)"),
             ("pll", r"Total PLLs\s*:\s*([\d,]+) / ([\d,]+)")]
INFER = [(r"Info \(276029\).*?from the following design logic: \"([^\"]+)\"", "altsyncram"), (r"Info \(276038\).*?from the following logic: \"([^\"]+)\"", "altdpram"),
         (r"Info \(278004\): Inferred divider/modulo megafunction \(\"([^\"]+)\"\)", "divider"), (r"Warning \(276027\): Inferred dual-clock RAM node \"([^\"]+)\"", "dual_clock_ram")]
UNINF = r"Info \((276004|276006|276007)\): RAM logic \"([^\"]+)\" is uninferred"

def num(s): return int(s.replace(",", ""))

def read(d, *names):
    for n in names:
        for p in sorted(Path(d).glob(n)):
            return p.read_text(errors="replace")
    return ""

def facts(d, log=None):
    fit = read(d, "*.fit.summary") or read(d, "*.map.summary"); sta = read(d, "*.sta.summary")
    text = Path(log).read_text(errors="replace") if log else "\n".join(read(d, f"*.{k}.rpt") for k in ("map", "fit", "sta"))
    res = {}
    for k, rx in WATCH_RES:
        m = re.search(rx, fit)
        if m: res[k] = {"used": num(m.group(1)), "of": num(m.group(2)), "pct": round(100.0 * num(m.group(1)) / num(m.group(2)), 1)}
    slack = {}
    for ty, sl in re.findall(r"Type\s*:\s*(.*?)\nSlack\s*:\s*(-?[\d.]+)", sta):
        m = re.match(r"(Slow|Fast) \d+mV (\d+C) Model (Setup|Hold)", ty)
        if m: key = " ".join(m.groups()); slack[key] = min(slack.get(key, 9e9), float(sl))
    warn, crit, never = {}, 0, []
    for line in text.splitlines():
        m = re.match(r"(Warning|Critical Warning) \((\d+)\)", line)
        if m:
            warn[m.group(2)] = warn.get(m.group(2), 0) + 1
            if m.group(1) == "Critical Warning": crit += 1
        for rx in NEVER:
            if re.search(rx, line): never.append(line[:160])
    inferred = {kind: sorted({m.group(1) for m in re.finditer(rx, text)}) for rx, kind in INFER}
    uninf = sorted({(m.group(1), m.group(2)) for m in re.finditer(UNINF, text)})
    return {"resources": res, "slack": slack, "warning_counts": warn, "critical_warnings": crit, "never_seen": never, "inferred": inferred, "uninferred": [list(u) for u in uninf]}

def baseline_of(f): return {"resources": {k: v["pct"] for k, v in f["resources"].items()}, "warning_counts": f["warning_counts"], "inferred": f["inferred"], "uninferred": f["uninferred"]}

def judge(f, base, ram_pct=95.0, rise=2.0):
    fails, changes = [], []
    for k, v in f["slack"].items():
        if v < 0: fails.append("negative slack %s %+.3f" % (k, v))
    if f["critical_warnings"]: fails.append("%d Critical Warning(s)" % f["critical_warnings"])
    for k, v in f["resources"].items():
        if v["pct"] > ram_pct: fails.append("%s at %.1f%% (limit %.0f%%)" % (k, v["pct"], ram_pct))
    for l in f["never_seen"][:3]: fails.append("must never appear: " + l)
    if base is None: return fails, changes
    for k, v in f["resources"].items():
        b = base["resources"].get(k)
        if b is not None and v["pct"] - b > rise: changes.append("%s %.1f%% -> %.1f%%" % (k, b, v["pct"]))
    for kind in sorted(set(f["inferred"]) | set(base["inferred"])):
        new = sorted(set(f["inferred"].get(kind, [])) - set(base["inferred"].get(kind, []))); gone = sorted(set(base["inferred"].get(kind, [])) - set(f["inferred"].get(kind, [])))
        if new: changes.append("%d new %s node(s), e.g. %s" % (len(new), kind, new[0]))
        if gone: changes.append("%d %s node(s) no longer inferred, e.g. %s" % (len(gone), kind, gone[0]))
    nu = sorted({tuple(u) for u in f["uninferred"]} - {tuple(u) for u in base["uninferred"]})
    if nu: changes.append("%d RAM now NOT inferred (%s), e.g. %s" % (len(nu), nu[0][0], nu[0][1]))
    for i, n in sorted(f["warning_counts"].items()):
        b = base["warning_counts"].get(i)
        if b is None: changes.append("new warning id %s (x%d)" % (i, n))
        elif n > b: changes.append("warning %s up %d -> %d" % (i, b, n))
    return fails, changes

def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dir"); ap.add_argument("--log"); ap.add_argument("--baseline"); ap.add_argument("--update-baseline", action="store_true"); ap.add_argument("--strict", action="store_true")
    ap.add_argument("--json", action="store_true"); ap.add_argument("--ram-pct", type=float, default=95.0); a = ap.parse_args(argv)
    f = facts(a.dir, a.log)
    if not f["resources"]: print("GATE FAIL | no resource summary found in", a.dir); return 1
    if a.update_baseline:
        if not a.baseline: sys.exit("--update-baseline needs --baseline FILE")
        Path(a.baseline).write_text(json.dumps(baseline_of(f), indent=1, sort_keys=True) + "\n"); print("baseline written:", a.baseline)
    base = json.loads(Path(a.baseline).read_text()) if a.baseline and Path(a.baseline).is_file() else None
    fails, changes = judge(f, base, a.ram_pct)
    if a.baseline and base is None and not a.update_baseline: fails.append("baseline file %s not found" % a.baseline)
    state = "FAIL" if fails or (changes and a.strict) else ("CHANGED" if changes else "OK")
    res = " ".join("%s %d%%" % (k, round(v["pct"])) for k, v in f["resources"].items()); worst = min(f["slack"].values()) if f["slack"] else None
    if a.json: print(json.dumps({"state": state, "fails": fails, "changes": changes, "facts": f}))
    else:
        for x in fails: print("FAIL    ", x)
        for x in changes: print("CHANGE  ", x)
        print("GATE %s | %s | worst slack %s | warnings %d | inferred RAM nodes %d, not inferred %d | %d fail(s), %d change(s)%s" % (state, res, "%+.3f" % worst if worst is not None else "n/a",
              sum(f["warning_counts"].values()), len(f["inferred"].get("altsyncram", [])), len(f["uninferred"]), len(fails), len(changes), "" if base is not None else " | no baseline"))
    return 0 if state in ("OK", "CHANGED") else 1

if __name__ == "__main__":
    sys.exit(main())
