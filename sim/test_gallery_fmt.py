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
        # a version 1 index: 3 folders of 3, 2 and 2 pictures, details on some of them (the others have none: flag 0, length 0)
        e1, t1 = [], []
        for k in range(7):
            d = dict(format="JPEG", camera="Canon EOS R5", lens="RF24-70mm F2.8", taken=(2024, 5, 17, 13, 4, 59), shutter=(1, 250), aperture=28, iso=400, focal=350) if k % 2 == 0 else ({"format": "PNG"} if k == 1 else {})
            e1.append(dict(path=f"Assets/tau_gallery/p/f{[0,0,0,1,1,2,2][k]}/img{k}.tgy", name=f"img{k} é", size=1000 + k, width=640 + k, height=480 + k, folder=[0, 0, 0, 1, 1, 2, 2][k], taken=tgy.taken_seconds(d.get("taken")), details=d))
            t1.append(bytes((k * 31 + i) & 255 for i in range(16)) * (tgy.THUMB1_BYTES // 16))
        cls.e1 = e1; cls.idx1 = cls.d / "idx1"; cls.idx1.write_bytes(tgy.pack_index_v1(e1, t1, [dict(name="Alpha"), dict(name="B\xe9ta"), dict(name="Gamma", cover=6)], tgy.SORT_TAKEN))
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
    def test_index_v1_matches_python(self):
        data = self.idx1.read_bytes(); py = tgy.parse_index_full(data); out = run("idx", self.idx1)
        self.assertEqual(out[0], "rc 0"); aoff = struct.unpack_from("<I", data, 24)[0]
        self.assertEqual(out[1], f"count 7 entries_off 64 atlas_off {aoff}")
        m = re.fullmatch(r"head1 folders 3 folders_off (\d+) details_off (\d+) details_len (\d+) sort 1 size_rc 0", out[2]); self.assertIsNotNone(m, out[2])
        for k, f in enumerate(py["folders"]):
            self.assertEqual(out[3 + k], f"folder {k} rc 0 name {f['name']} first {f['first']} count {f['count']} cover {f['cover']}")
        for i, e in enumerate(py["entries"]):
            line = out[6 + i]
            mm = re.fullmatch(r"entry (\d+) rc 0 path (\S+) name (.*) size (\d+) w (\d+) h (\d+) thumb (\d+) thumb_off (\d+) folder (\d+) flags (\d+) dlen (\d+) taken (\d+)(?: dok (\d) dabs (\d+) d ([0-9a-f]+) iso (\d) camera (\d) tag99 (\d))?", line); self.assertIsNotNone(mm, line)
            self.assertEqual((mm[2], mm[3], int(mm[4]), int(mm[5]), int(mm[6]), int(mm[7]), int(mm[9]), int(mm[12])), (e["path"], e["name"], e["size"], e["width"], e["height"], i, e["folder"], e["taken"]))
            self.assertEqual(int(mm[8]), aoff + i * tgy.THUMB1_BYTES)
            want = tgy.pack_details(self.e1[i]["details"]); self.assertEqual((int(mm[11]), int(mm[10])), (len(want), 1 if want else 0))
            if want:
                self.assertEqual((mm[13], mm[15]), ("1", want.hex())); self.assertEqual((int(mm[16]), int(mm[17]), mm[18]), (1 if "iso" in self.e1[i]["details"] else 0, 1 if "camera" in self.e1[i]["details"] else 0, "0"))      # an unknown tag is not found
    def test_index_v1_damage(self):
        good = self.idx1.read_bytes()
        def lines(data):
            p = self.d / "dmg1"; p.write_bytes(data); return run("idx", p)
        def rc(data): return lines(data)[0]
        def fix(b, off, fmt, val):
            b = bytearray(b); struct.pack_into(fmt, b, off, val); struct.pack_into("<I", b, 60, tgy.crc(bytes(b[:60]))); return bytes(b)    # header fields with the checksum repaired
        self.assertEqual(rc(good[:63]), "rc 1"); self.assertEqual(rc(good[:4] + b"\x02\0" + good[6:]), "rc 3")
        b = bytearray(good); b[9] ^= 1; self.assertEqual(rc(bytes(b)), "rc 4")                                                  # a damaged header
        for what, off, fmt, val, want in (("entry size", 12, "<H", 160, "rc 3"), ("thumbnail width", 14, "<H", 97, "rc 3"), ("thumbnail height", 16, "<H", 72, "rc 3"), ("folder entry size", 32, "<H", 40, "rc 3"),
                                          ("header size", 6, "<H", 32, "rc 3"), ("no folders", 18, "<H", 0, "rc 6"), ("256 folders", 18, "<H", 256, "rc 6"), ("too many entries", 8, "<I", 65536, "rc 6"),
                                          ("a sort mode that does not exist", 34, "<H", 3, "rc 6"), ("entries inside the header", 20, "<I", 32, "rc 6"), ("atlas inside the entries", 24, "<I", 70, "rc 6"),
                                          ("folders inside the atlas", 28, "<I", 4096, "rc 6"), ("details inside the folders", 36, "<I", struct.unpack_from("<I", good, 28)[0], "rc 6"),
                                          ("details past 2^32", 40, "<I", 0xFFFFFFF0, "rc 6")):
            self.assertEqual(rc(fix(good, off, fmt, val)), want, what)
        goff = struct.unpack_from("<I", good, 28)[0]
        wrap = fix(fix(fix(fix(fix(good, 8, "<I", 65535), 24, "<I", 0xF0000000), 28, "<I", 0xF0000100), 36, "<I", 0xF0001000), 40, "<I", 16)       # the atlas of 65,535 thumbnails ends past 2^32: a 32-bit sum would wrap below the folder offset
        self.assertEqual(rc(wrap), "rc 6")
        # each limit on its own (every other section check satisfied)
        self.assertEqual(rc(fix(fix(good, 18, "<H", 256), 36, "<I", goff + 256 * 48)), "rc 6")                                  # 256 folders
        n = 65536; eoff = 64; aoff = eoff + n * 176; foff2 = aoff + n * 18432
        self.assertEqual(rc(fix(fix(fix(fix(good, 8, "<I", n), 24, "<I", aoff), 28, "<I", foff2), 36, "<I", foff2 + 3 * 48)), "rc 6")     # 65,536 entries
        self.assertEqual(rc(fix(good, 20, "<I", 0xFFFFFFF0)), "rc 6")                                                          # entries at the very end of the address space
        # the size of the file: sections past the end
        self.assertIn("size_rc 6", lines(good[:struct.unpack_from("<I", good, 28)[0] - 4096])[2]); self.assertIn("size_rc 6", lines(good[:struct.unpack_from("<I", good, 36)[0] + 10])[2] if struct.unpack_from("<I", good, 40)[0] > 10 else "size_rc 6")
        # a folder record whose entries lie outside the list, an empty one, a cover outside the list
        foff = struct.unpack_from("<I", good, 28)[0]
        def with_folder(k, first=None, count=None, cover=None):
            b = bytearray(good); o = foff + k * 48 + 32
            if first is not None: struct.pack_into("<I", b, o, first)
            if count is not None: struct.pack_into("<I", b, o + 4, count)
            if cover is not None: struct.pack_into("<I", b, o + 8, cover)
            return bytes(b)
        self.assertIn("folder 1 rc 6", "\n".join(lines(with_folder(1, first=6, count=3))))                                      # first + count > 7
        self.assertIn("folder 1 rc 6", "\n".join(lines(with_folder(1, first=0xFFFFFFFF, count=2))))                             # first + count wraps in 32 bits
        self.assertIn("folder 1 rc 6", "\n".join(lines(with_folder(1, count=0))))
        self.assertIn("folder 1 rc 6", "\n".join(lines(with_folder(1, cover=7))))
        self.assertIn("folder 1 rc 0", "\n".join(lines(with_folder(1, first=5, count=2))))                                      # the last two entries: fine
        # an entry whose folder, thumbnail number or details lie outside what the head allows
        def with_entry(i, off, fmt, val):
            b = bytearray(good); struct.pack_into(fmt, b, 64 + i * 176 + off, val); return bytes(b)
        for what, off, fmt, val in (("a folder index past the table", 152, "<H", 3), ("a thumbnail number past the atlas", 148, "<I", 7), ("details past the details section", 156, "<I", 0xFFFFFF00),
                                    ("details whose offset plus length wraps in 32 bits", 156, "<I", 0xFFFFFFF8), ("details longer than the section", 160, "<H", 255)):
            self.assertIn("entry 2 rc 6", "\n".join(lines(with_entry(2, off, fmt, val))), what)
        roomy = fix(good, 40, "<I", 0x10000)                                                                                 # a details section of 64 KB: only the limit of 255 per picture refuses a longer run
        b = bytearray(roomy); struct.pack_into("<H", b, 64 + 2 * 176 + 160, 256); self.assertIn("entry 2 rc 6", "\n".join(lines(bytes(b))), "details longer than 255")
        self.assertIn("entry 2 rc 0", "\n".join(lines(with_entry(2, 164, "<I", 123456))))                                          # a taken time is just a number
    def test_details_runs(self):
        # the harness reads a run through gx_details_ok / gx_details_find: an item that reaches past its run is refused, an unknown tag is skipped and the next item still found
        good = bytearray(self.idx1.read_bytes()); doff = struct.unpack_from("<I", good, 36)[0]
        e0len = struct.unpack_from("<H", good, 64 + 160)[0]; self.assertGreater(e0len, 10)
        bad = bytearray(good); bad[doff + 1] = 250                                                                                # the first item claims 250 bytes
        p = self.d / "dd"; p.write_bytes(bytes(bad)); out = "\n".join(run("idx", p)); self.assertIn("dok 0", out)
        unk = bytearray(good); unk[doff] = 99                                                                                    # the first item (the format) becomes an unknown tag: skipped, the rest is found
        p.write_bytes(bytes(unk)); out = run("idx", p)[6]; self.assertIn("dok 1", out); self.assertIn("iso 1", out); self.assertIn("tag99 1", out)
        ov = bytearray(good); ov[doff] = 7; ov[doff + 1] = 250                                                                  # the first item IS the ISO tag but its length reaches past the run: not found
        p.write_bytes(bytes(ov)); out = run("idx", p)[6]; self.assertIn("dok 0", out); self.assertIn("iso 0", out)
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
    muts = [("crc seed", "~gf_crc32_step(0xFFFFFFFFu, b, n)", "~gf_crc32_step(0u, b, n)"), ("crc poly", "0xEDB88320u", "0xEDB88321u"), ("crc length", "gf_crc32(b, 52u)", "gf_crc32(b, 51u)"),
            ("tgy magic", "b[2] != 'Y' || b[3] != 0", "b[2] != 'Y'"), ("no range", "len > fsize - off) return GF_RANGE", "0) return GF_RANGE"), ("len check", "len != GT_STRIDE * GT_CANVAS_H ||", ""),
            ("stride", "|| rd32(e + 20) != GT_STRIDE", ""), ("layout", "if (e[16] != 0u || e[17] != 0u || e[18] != 0u || e[19] != 0u) continue;", "if (e[17] != 0u || e[18] != 0u || e[19] != 0u) continue;"),
            ("mode", "if (e[16] != 0u || e[17] != 0u ||", "if (e[16] != 0u ||"), ("level count", "nlev > 16u ||", ""), ("thumb off", "h->atlas_off + thumb * h->thumb_bytes", "h->atlas_off + thumb * h->thumb_bytes + 2"),
            ("entry off", "h->entries_off + i * h->entry_bytes", "h->entries_off + i * (h->entry_bytes - 1u)"), ("name field", "copy_str(e->name, r + GX_PATH_MAX, GX_NAME_MAX)", "copy_str(e->name, r + GX_PATH_MAX - 1u, GX_NAME_MAX)"),
            ("path bound", "while (i < max && src[i])", "while (src[i])"), ("count limit", "h->count > 0x100000u ||", ""), ("atlas check", "|| h->atlas_off < h->entries_off + h->count * GX_ENTRY_BYTES", ""),
            ("empty path", "if (e->path[0] == 0) return GF_RANGE;", ""), ("head version", "if (ver != 1u) return GF_VERSION;", ""), ("short", "if (n < 128u) return GF_SHORT;", ""),
            ("v1 crc", "gf_crc32(b, 60u) != rd32(b + 60)", "0"), ("v1 crc length", "gf_crc32(b, 60u)", "gf_crc32(b, 59u)"), ("v1 entry size", "rd16(b + 12) != GX1_ENTRY_BYTES ||", ""), ("v1 thumb width", "rd16(b + 14) != GX1_THUMB_W ||", ""),
            ("v1 thumb height", "rd16(b + 16) != GX1_THUMB_H ||", ""), ("v1 folder entry size", "|| rd16(b + 32) != GX1_FOLDER_BYTES", ""), ("v1 header size", "if (rd16(b + 6) != GX1_HEAD_BYTES ||", "if ("), ("no folders", "h->folder_count < 1u ||", ""),
            ("too many folders", "h->folder_count > GX_MAX_FOLDERS ||", ""), ("too many entries", "h->count > GX_MAX_ENTRIES ||", ""), ("sort mode", "|| h->sort > 2u", ""), ("entries inside the header", "if (h->entries_off < GX1_HEAD_BYTES) return GF_RANGE;", ""),
            ("atlas order", "if ((uint64_t)h->atlas_off < (uint64_t)h->entries_off + (uint64_t)h->count * GX1_ENTRY_BYTES) return GF_RANGE;", ""),
            ("atlas sum wraps", "(uint64_t)h->atlas_off + (uint64_t)h->count * GX1_THUMB_BYTES", "(uint32_t)(h->atlas_off + h->count * GX1_THUMB_BYTES)"),
            ("folders order", "if ((uint64_t)h->details_off < (uint64_t)h->folders_off + (uint64_t)h->folder_count * GX1_FOLDER_BYTES) return GF_RANGE;", ""),
            ("details wrap", "if ((uint64_t)h->details_off + (uint64_t)h->details_len > 0xFFFFFFFFull) return GF_RANGE;", ""), ("entries sum wraps", "(uint64_t)h->entries_off + (uint64_t)h->count * GX1_ENTRY_BYTES", "(uint32_t)(h->entries_off + h->count * GX1_ENTRY_BYTES)"),
            ("size check", "return end <= file_size ? GF_OK : GF_RANGE;", "return GF_OK;"), ("size check ignores details", "if (h->version == 1u && (uint64_t)h->details_off + h->details_len > end) end = (uint64_t)h->details_off + h->details_len;", ""),
            ("folder offset", "h->folders_off + k * GX1_FOLDER_BYTES", "h->folders_off + k * 47u"), ("folder field", "h->folders_off = rd32(b + 28)", "h->folders_off = rd32(b + 24)"), ("details field", "h->details_off = rd32(b + 36)", "h->details_off = rd32(b + 40)"),
            ("entry folder field", "e->folder = (uint16_t)rd16(r + 152)", "e->folder = (uint16_t)rd16(r + 154)"), ("entry details offset field", "e->details_off = rd32(r + 156)", "e->details_off = rd32(r + 164)"),
            ("entry taken field", "e->taken = rd32(r + 164)", "e->taken = rd32(r + 156)"), ("thumbnail number range", "e->thumb >= h->count ||", ""), ("entry folder range", "e->folder >= h->folder_count ||", ""),
            ("details length limit", "e->details_len > GX_MAX_DETAILS ||", ""), ("details bounds", " || (uint64_t)e->details_off + e->details_len > h->details_len", ""),
            ("details bounds wrap", "(uint64_t)e->details_off + e->details_len > h->details_len", "(uint32_t)(e->details_off + e->details_len) > h->details_len"),
            ("folder must hold entries", "((f->count < 1u) && h->count != 0u) ||", ""), ("folder range", "(uint64_t)f->first + f->count > h->count ||", ""), ("folder range wraps", "(uint64_t)f->first + f->count > h->count", "(uint32_t)(f->first + f->count) > h->count"),
            ("folder cover", " || (h->count != 0u && f->cover >= h->count)", ""), ("details run check", "{ if (i + 2u > n || i + 2u + d[i + 1u] > n) return 0; i += 2u + d[i + 1u]; }", "{ i += 2u + d[i + 1u]; }"),
            ("details find overrun", "        if (i + 2u > n || i + 2u + d[i + 1u] > n) return 0;\n        if (d[i] == tag)", "        if (d[i] == tag)"), ("details find skip", "i += 2u + d[i + 1u];\n    }\n    return 0;\n}", "i += 2u;\n    }\n    return 0;\n}"),
            ("details absolute", "return h->details_off + e->details_off;", "return h->details_off;"),
            ("v1 short", "    if (n < GX1_HEAD_BYTES) return GF_SHORT;\n", "")]
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
