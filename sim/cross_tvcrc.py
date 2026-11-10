#!/usr/bin/env python3
"""Bit-exact cross-check of tools/gallery/tvcrc.py against the Rust TVC range coder it reproduces (Tau codecs, tau-video/src/tvc/range.rs, read only): a throwaway Rust crate is built in a temporary folder from a copy of that
one self-contained file, fed the same sequences, and its encoded bytes and its decode of them must equal the Python ones. Skipped (exit 0, says so) when cargo or the Tau codecs checkout is missing."""
import os, random, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools/gallery")); sys.path.insert(0, str(ROOT / "sim"))
import tvcrc, test_tvcrc as T
RS = ROOT.parent.parent / "Tau codecs/tau-video/src/tvc/range.rs"
MAIN = '''
mod range;
use range::{Probability, RangeDecoder, RangeEncoder};
use std::io::Read;
fn main() {
    let mut text = String::new(); std::io::stdin().read_to_string(&mut text).unwrap();
    let mut lines = text.lines(); let n: usize = lines.next().unwrap().parse().unwrap();
    let ops: Vec<(char, usize, bool)> = lines.map(|l| { let f: Vec<&str> = l.split(' ').collect(); (f[0].chars().next().unwrap(), f[1].parse().unwrap(), f[2] == "1") }).collect();
    let mut probs = vec![Probability::default(); n]; let mut e = RangeEncoder::new();
    for &(k, c, b) in &ops { if k == 'c' { e.bit(&mut probs[c], b) } else { e.bypass(b) } }
    let data = e.finish(); println!("{}", data.iter().map(|b| format!("{:02x}", b)).collect::<String>());
    let mut probs = vec![Probability::default(); n]; let mut d = RangeDecoder::new(&data); let mut s = String::new();
    for &(k, c, _) in &ops { let b = if k == 'c' { d.bit(&mut probs[c]) } else { d.bypass() }; s.push(if b { '1' } else { '0' }); }
    println!("{}", s);
}
'''
def main():
    if not RS.is_file() or subprocess.run(["cargo", "--version"], capture_output=True).returncode: print("SKIPPED: cargo or", RS, "missing"); return 0
    d = Path(tempfile.mkdtemp()); (d / "src").mkdir()
    (d / "Cargo.toml").write_text('[package]\nname = "rccheck"\nversion = "0.0.0"\nedition = "2021"\n[workspace]\n')
    (d / "src/range.rs").write_text(RS.read_text()); (d / "src/main.rs").write_text(MAIN)
    r = subprocess.run(["cargo", "build", "--release", "--offline", "-q"], cwd=d, capture_output=True, text=True)
    if r.returncode: print(r.stderr[-2000:]); return 1
    exe = d / "target/release/rccheck"; bad = 0
    cases = [(T.seq(s, n, nctx=nc, bypass=by, bias=bi), nc) for s, n, nc, by, bi in ((1, 1, 16, 0.2, None), (2, 500, 16, 0.2, None), (3, 20000, 16, 0.2, None), (4, 8000, 4, 0.0, [0.0, 1.0, 0.0, 1.0]), (5, 4000, 2, 0.0, [0.97, 0.03]), (6, 3000, 1, 1.0, None), (7, 0, 1, 0.0, None))]
    cases += [(T.carry_seq(k), 2) for k in range(40)]
    for i, (s, nc) in enumerate(cases):
        inp = f"{nc}\n" + "".join(f"{k} {c if c is not None else 0} {b}\n" for k, c, b in s)
        out = subprocess.run([str(exe)], input=inp, capture_output=True, text=True).stdout.split("\n")
        data, _ = T.encode(s, nc); bits, _ = T.decode(data, s, nc)
        ok = out[0] == data.hex() and out[1] == "".join(map(str, bits)) and bits == [b for _, _, b in s]
        if not ok: bad += 1; print("DIFFERS case", i)
    print(f"cross_tvcrc: {len(cases)} sequences, {'all bit-exact' if not bad else str(bad) + ' differ'}")
    return 1 if bad else 0
if __name__ == "__main__":
    sys.exit(main())
