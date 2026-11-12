#!/usr/bin/env python3
"""ppm2png.py IN.ppm OUT.png: a P6 PPM to a PNG (no third-party modules), to look at a captured frame."""
import struct, sys, zlib
d = open(sys.argv[1], "rb").read(); h = d.split(b"\n", 3); w, hh = map(int, h[1].split()); px = h[3]
raw = b"".join(b"\0" + px[y * w * 3:(y + 1) * w * 3] for y in range(hh))
def ch(t, b): return struct.pack(">I", len(b)) + t + b + struct.pack(">I", zlib.crc32(t + b))
open(sys.argv[2], "wb").write(b"\x89PNG\r\n\x1a\n" + ch(b"IHDR", struct.pack(">IIBBBBB", w, hh, 8, 2, 0, 0, 0)) + ch(b"IDAT", zlib.compress(raw)) + ch(b"IEND", b""))
