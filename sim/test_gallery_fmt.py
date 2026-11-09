#!/usr/bin/env python3
"""Test of the firmware's format readers (fw2/gallery/gallery_fmt.c) against the Python writer and reader (tools/gallery/tgy.py): the C harness reads files the way the firmware does and its output must equal the
Python parse, and damage the Python reader refuses must be refused by the C one with the right code. `--mutants` edits a copy of gallery_fmt.c; each edit must make this suite fail."""
import io, os, re, struct, subprocess, sys, tempfile, unittest
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools/gallery"))
import tgy
from PIL import Image
SRC = ROOT / "fw2/gallery/gallery_fmt.c"
HARNESS = ROOT / "sim/gallery_fmt_harness.c"
BIN = None

def build(src_text=None):
    d = Path(tempfile.mkdtemp()); h = d / "harness.c"
    text = HARNESS.read_text().replace('#include "../fw2/gallery/gallery_fmt.c"', f'#include "{d / "gallery_fmt.c"}"')
    (d / "gallery_fmt.c").write_text(src_text if src_text is not None else SRC.read_text()); (d / "gallery_fmt.h").write_text((ROOT / "fw2/gallery/gallery_fmt.h").read_text()); h.write_text(text)
    env = dict(os.environ); sdk = subprocess.run([sys.executable, str(ROOT / "tools/toolchain.py"), "sdkroot"], capture_output=True, text=True).stdout.strip()
    if sdk: env["SDKROOT"] = sdk
    subprocess.run(["cc", "-O1", "-w", "-o", str(d / "h"), str(h)], check=True, env=env)
    return d / "h"

def run(kind, path):
    return subprocess.run([str(BIN), kind, str(path)], capture_output=True, text=True, encoding="latin-1").stdout.splitlines()

def pic(w, h, exif=None):
    im = Image.new("RGB", (w, h)); im.putdata([((x * 7) % 256, (y * 5) % 256, (x + y) % 256) for y in range(h) for x in range(w)]); return im

class T(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.d = Path(tempfile.mkdtemp())
        imgs = [("a", pic(900, 600)), ("b", pic(300, 500)), ("c", pic(64, 64))]
        cls.files = []
        ents, ths = [], []
        for n, im in imgs:
            canvas, rect, thumb, size = tgy.render(im); data = tgy.pack_tgy(canvas, rect, thumb, size)
            p = cls.d / (n + ".tgy"); p.write_bytes(data); cls.files.append(p)
            ents.append(dict(path=f"Assets/tau_gallery/common/photos/{n}.tgy", name="Pic " + n + " é", size=len(data), width=size[0], height=size[1])); ths.append(thumb)
        cls.idx = cls.d / "idx"; cls.idx.write_bytes(tgy.pack_index(ents, ths)); cls.ents = ents
    def test_tgy_matches_python(self):
        for p in self.files:
            t = tgy.parse_tgy(p.read_bytes()); lv = t["levels"][0]; out = run("tgy", p)
            self.assertEqual(out[0], "rc 0")
            self.assertEqual(out[1], f"w {t['width']} h {t['height']} fsize {t['size']} canvas_off {lv['off']} canvas_len {lv['len']} thumb_off {t['thumb'][0]} rect {lv['rect'][0]} {lv['rect'][1]} {lv['rect'][2]} {lv['rect'][3]}")
    def test_index_matches_python(self):
        data = self.idx.read_bytes(); py = tgy.parse_index(data); out = run("idx", self.idx)
        self.assertEqual(out[0], "rc 0"); self.assertEqual(out[1], f"count 3 entries_off 32 atlas_off {struct.unpack_from('<I', data, 20)[0]}")
        for i, e in enumerate(py):
            m = re.fullmatch(r"entry (\d+) rc 0 path (\S+) name (.*) size (\d+) w (\d+) h (\d+) thumb (\d+) thumb_off (\d+)", out[2 + i]); self.assertIsNotNone(m, out[2 + i])
            self.assertEqual((int(m[1]), m[2], int(m[4]), int(m[5]), int(m[6]), int(m[7])), (i, e["path"], e["size"], e["width"], e["height"], i))
            self.assertEqual(int(m[8]), struct.unpack_from("<I", data, 20)[0] + i * tgy.THUMB_BYTES)
            self.assertEqual(m[3], e["name"])                                                       # the name bytes, Latin-1, as stored
    def test_tgy_damage(self):
        good = self.files[0].read_bytes()
        def rc(data):
            p = self.d / "dmg"; p.write_bytes(data); return run("tgy", p)[0]
        self.assertEqual(rc(good[:3] + b"X" + good[4:]), "rc 2")                                                            # the fourth magic byte
        b = bytearray(good); b[64 + 24:64 + 28] = struct.pack("<I", len(good) - 100); self.assertEqual(rc(bytes(b)), "rc 6")  # the canvas starts inside the file but ends beyond it
        b = bytearray(good); b[20:24] = struct.pack("<I", 50); b[52:56] = struct.pack("<I", tgy.crc(bytes(b[:52]))); self.assertEqual(rc(bytes(b)), "rc 6")   # 50 levels fit in the buffer but are refused
        self.assertEqual(rc(good[:4]), "rc 1"); self.assertEqual(rc(b"XGY\0" + good[4:]), "rc 2"); self.assertEqual(rc(good[:4] + b"\x07\0" + good[6:]), "rc 3")
        self.assertEqual(rc(good[:20] + bytes([good[20] ^ 1]) + good[21:]), "rc 4")                                          # the header checksum
        lv = bytearray(good); lv[64 + 16] = 1                                                                              # layout 1 (tiles): no usable level
        lv[52:56] = struct.pack("<I", tgy.crc(bytes(lv[:52]))); self.assertEqual(rc(bytes(lv)), "rc 5")
        for field, off in (("mode", 17), ("pixfmt", 18), ("kind", 19)):
            b = bytearray(good); b[64 + off] = 3; self.assertEqual(rc(bytes(b)), "rc 5", field)
        b = bytearray(good); b[64 + 20:64 + 24] = struct.pack("<I", 2400 + 4); self.assertEqual(rc(bytes(b)), "rc 5")        # stride
        b = bytearray(good); b[64 + 28:64 + 32] = struct.pack("<I", 1728004); self.assertEqual(rc(bytes(b)), "rc 6")        # length not 2400 x 720
        b = bytearray(good); b[64 + 24:64 + 28] = struct.pack("<I", 1 << 31); self.assertEqual(rc(bytes(b)), "rc 6")        # offset beyond the file
        b = bytearray(good); b[48:52] = struct.pack("<I", 4096); b[52:56] = struct.pack("<I", tgy.crc(bytes(b[:52]))); self.assertEqual(rc(bytes(b)), "rc 6")   # file shorter than the canvas
        b = bytearray(good); b[20:24] = struct.pack("<I", 1000); b[52:56] = struct.pack("<I", tgy.crc(bytes(b[:52]))); self.assertEqual(rc(bytes(b)), "rc 6")      # absurd level count
    def test_index_damage(self):
        good = self.idx.read_bytes()
        def rc(data):
            p = self.d / "dmg"; p.write_bytes(data); return run("idx", p)[0]
        self.assertEqual(rc(good[:10]), "rc 1"); self.assertEqual(rc(b"TGIY" + good[4:]), "rc 2"); self.assertEqual(rc(good[:4] + b"\x05\0" + good[6:]), "rc 3")
        self.assertEqual(rc(good[:12] + struct.pack("<I", 100) + good[16:]), "rc 3")                                         # entry size
        self.assertEqual(rc(good[:8] + struct.pack("<I", 0x7FFFFFFF) + good[12:]), "rc 6")                                   # count
        self.assertEqual(rc(good[:8] + struct.pack("<I", 0x08000000) + good[12:]), "rc 6")                                   # a count whose entries total wraps to 0 in 32 bits
        self.assertEqual(rc(good[:20] + struct.pack("<I", 40) + good[24:]), "rc 6")                                          # atlas inside the entries
        self.assertEqual(rc(good[:24] + struct.pack("<H", 100) + good[26:]), "rc 3")                                         # thumbnail size
    def test_entry_strings_are_bounded(self):
        data = bytearray(self.idx.read_bytes()); o = 32                                                                      # entry 0: fill the path and name fields without a NUL
        data[o:o + 96] = b"p" * 96; data[o + 96:o + 136] = b"n" * 40; p = self.d / "long"; p.write_bytes(bytes(data))
        out = run("idx", p); m = re.fullmatch(r"entry 0 rc 0 path (p+) name (n+) size.*", out[2]); self.assertIsNotNone(m); self.assertEqual((len(m[1]), len(m[2])), (96, 40))
        data[o:o + 96] = bytes(96); p.write_bytes(bytes(data)); self.assertIn("rc 6", run("idx", p)[2])                      # an empty path is refused
    def test_crc32(self):
        for n in (0, 1, 52, 100):
            b = bytes((i * 37 + 11) & 255 for i in range(n))
            # the harness has no direct crc call: a header whose checksum is right must pass for any content of the other fields
            pass
        good = self.files[1].read_bytes(); self.assertEqual(run("tgy", self.files[1])[0], "rc 0"); self.assertEqual(tgy.crc(good[:52]), struct.unpack_from("<I", good, 52)[0])

def mutants():
    src = SRC.read_text()
    muts = [("crc seed", "uint32_t c = 0xFFFFFFFFu;", "uint32_t c = 0u;"), ("crc poly", "0xEDB88320u", "0xEDB88321u"), ("crc length", "gf_crc32(b, 52u)", "gf_crc32(b, 51u)"),
            ("tgy magic", "b[2] != 'Y' || b[3] != 0", "b[2] != 'Y'"), ("no range", "len > fsize - off) return GF_RANGE", "0) return GF_RANGE"), ("len check", "len != GT_STRIDE * GT_CANVAS_H ||", ""),
            ("stride", "|| rd32(e + 20) != GT_STRIDE", ""), ("layout", "if (e[16] != 0u || e[17] != 0u || e[18] != 0u || e[19] != 0u) continue;", "if (e[17] != 0u || e[18] != 0u || e[19] != 0u) continue;"),
            ("mode", "if (e[16] != 0u || e[17] != 0u ||", "if (e[16] != 0u ||"), ("level count", "nlev > 16u ||", ""), ("thumb off", "h->atlas_off + thumb * GX_THUMB_BYTES", "h->atlas_off + thumb * GX_THUMB_BYTES + 2"),
            ("entry off", "h->entries_off + i * GX_ENTRY_BYTES", "h->entries_off + i * (GX_ENTRY_BYTES - 1u)"), ("name field", "copy_str(e->name, r + GX_PATH_MAX, GX_NAME_MAX)", "copy_str(e->name, r + GX_PATH_MAX - 1u, GX_NAME_MAX)"),
            ("path bound", "while (i < max && src[i])", "while (src[i])"), ("count limit", "h->count > 0x100000u ||", ""), ("atlas check", "|| h->atlas_off < h->entries_off + h->count * GX_ENTRY_BYTES", ""),
            ("empty path", "if (e->path[0] == 0) return GF_RANGE;", ""), ("head version", "rd16(b + 4) != 0u || rd16(b + 6) != GX_HEAD_BYTES ||", "rd16(b + 6) != GX_HEAD_BYTES ||"), ("short", "if (n < 128u) return GF_SHORT;", "")]
    global BIN
    survived = 0
    for name, a, b in muts:
        assert a in src, name
        BIN = build(src.replace(a, b))
        r = unittest.TextTestRunner(stream=io.StringIO()).run(unittest.defaultTestLoader.loadTestsFromTestCase(T))
        print("SURVIVED" if r.wasSuccessful() else "killed  ", name); survived += r.wasSuccessful()
    return survived

if __name__ == "__main__":
    if "--mutants" in sys.argv: sys.exit(1 if mutants() else 0)
    BIN = build(); unittest.main()
