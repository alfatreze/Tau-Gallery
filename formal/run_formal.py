#!/usr/bin/env python3
"""Run a formal suite: python3 formal/run_formal.py wfifo. The tasks named in SUITES must PASS, the mutant tasks must FAIL.
A mutant listed in "induct_kill" cannot be reached by a bounded run (BUG 12: the 2**17 clock watchdog), so it counts as killed when the induction step fails on an assertion.
Work directories are temporary (sby in a spaced path is fine, but results stay out of the repo). Needs sby/yosys (brew install yosys sby)."""
import subprocess, sys, tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

HERE = Path(__file__).resolve().parent
SUITES = {"p2mux": {"sby": "p2mux.sby", "pass": ["good"], "fail": ["bug%d" % i for i in range(1, 8)]},
          "dmux": {"sby": "dmux.sby", "pass": ["good"], "fail": ["bug%d" % i for i in range(1, 11)]},
          "tile": {"sby": "tile.sby", "pass": ["good", "good444"], "fail": ["bug%d" % i for i in range(1, 14)], "induct_kill": ["bug12"]},
          "wfifo": {"sby": "wfifo.sby", "pass": ["good", "r_equal", "r_quarter", "r_fast"], "fail": ["bug1", "bug2", "bug3"]}}

def run(sby, task, tmp):
    r = subprocess.run(["sby", "-f", "-d", str(tmp / f"o_{task}"), sby, task], cwd=HERE, capture_output=True, text=True)
    return task, r.stdout + r.stderr

def main(name):
    s = SUITES[name]; bad = 0
    with tempfile.TemporaryDirectory() as t, ThreadPoolExecutor(4) as ex:
        tmp = Path(t)
        for task, out in ex.map(lambda k: run(s["sby"], k, tmp), s["pass"] + s["fail"]):
            if task in s["pass"]: ok = "DONE (PASS" in out; print(("formal ok: " if ok else "FORMAL FAILED: ") + task)
            else: ok = "DONE (FAIL" in out or (task in s.get("induct_kill", []) and "DONE (UNKNOWN" in out and "induction: ##" in out and "Assert failed" in out); print(("mutant killed: " if ok else "MUTANT SURVIVED: ") + task)
            bad += not ok
    print("FORMAL %s %s" % (name, "PASSED" if not bad else "FAILED"))
    return 1 if bad else 0

if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
