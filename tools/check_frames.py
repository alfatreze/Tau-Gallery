#!/usr/bin/env python3
"""Compare a frame captured by the whole-core simulation (a P6 PPM written by sim/fullcore/tb_fullcore.v) with a golden frame (gzip-compressed PPM), exactly.
  check_frames.py compare FRAME.ppm GOLDEN.ppm.gz [--diff OUT.png]   exit 0 equal; 1 different (prints the number of differing pixels, their bounding box and the first few, optionally writes a diff PNG: red = differs,
                                                                     grey = the golden's lit pixels); 2 unreadable
  check_frames.py update FRAME.ppm GOLDEN.ppm.gz                       store FRAME as the new golden (do this only after looking at the frame: python3 tools/ppm2png.py)
The simulation is deterministic (fixed clock phases, no random input), so a golden frame taken at a fixed simulated time must reproduce bit for bit; any change in it is a change in RTL, firmware or host model."""
import gzip, struct, sys, zlib
from pathlib import Path

def read_ppm(data):
    parts = data.split(b"\n", 3)
    if len(parts) < 4 or parts[0] != b"P6": raise ValueError("not a P6 PPM")
    w, h = map(int, parts[1].split())
    if int(parts[2]) != 255 or len(parts[3]) != w * h * 3: raise ValueError("bad PPM size: %d bytes for %dx%d" % (len(parts[3]), w, h))
    return w, h, parts[3]

def load(path):
    p = Path(path); raw = p.read_bytes()
    if p.suffix == ".gz": raw = gzip.decompress(raw)
    return read_ppm(raw)

def png(w, h, rgb):
    raw = b"".join(b"\0" + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))
    def ch(t, b): return struct.pack(">I", len(b)) + t + b + struct.pack(">I", zlib.crc32(t + b))
    return b"\x89PNG\r\n\x1a\n" + ch(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) + ch(b"IDAT", zlib.compress(raw)) + ch(b"IEND", b"")

def compare(frame, golden, diff_png=None):
    """-> (equal, message)"""
    fw, fh, f = frame; gw, gh, g = golden
    if (fw, fh) != (gw, gh): return False, "size %dx%d, golden %dx%d" % (fw, fh, gw, gh)
    if f == g: return True, "identical (%dx%d, %d lit pixels)" % (fw, fh, sum(1 for i in range(0, len(g), 3) if g[i:i + 3] != b"\0\0\0"))
    bad = [i // 3 for i in range(0, len(f), 3) if f[i:i + 3] != g[i:i + 3]]
    xs = [p % fw for p in bad]; ys = [p // fw for p in bad]
    msg = "%d of %d pixels differ, bounding box x %d..%d y %d..%d; first: %s" % (len(bad), fw * fh, min(xs), max(xs), min(ys), max(ys), ", ".join("(%d,%d) got %s want %s" % (p % fw, p // fw, f[3 * p:3 * p + 3].hex(), g[3 * p:3 * p + 3].hex()) for p in bad[:4]))
    if diff_png:
        out = bytearray(len(g))
        for i in range(0, len(g), 3):
            if g[i:i + 3] != b"\0\0\0": out[i:i + 3] = b"\x50\x50\x50"
        for p in bad: out[3 * p:3 * p + 3] = b"\xff\x00\x00"
        Path(diff_png).write_bytes(png(fw, fh, bytes(out)))
    return False, msg

def main(argv):
    if len(argv) >= 4 and argv[1] == "update":
        w, h, rgb = load(argv[2]); Path(argv[3]).write_bytes(gzip.compress(b"P6\n%d %d\n255\n" % (w, h) + rgb, mtime=0)); print("golden updated: %s (%dx%d)" % (argv[3], w, h)); return 0
    if len(argv) >= 4 and argv[1] == "compare":
        diff = argv[argv.index("--diff") + 1] if "--diff" in argv else None
        try: frame, golden = load(argv[2]), load(argv[3])
        except (OSError, ValueError, EOFError) as e: print("check_frames: cannot read:", e); return 2
        ok, msg = compare(frame, golden, diff); print(("FRAME OK: " if ok else "FRAME MISMATCH: ") + msg); return 0 if ok else 1
    print(__doc__); return 2

if __name__ == "__main__": sys.exit(main(sys.argv))
