#!/usr/bin/env python3
"""Reference tests of TGIX version 1 (tools/gallery/tgy.py, docs/features/TGIX_V1_SPEC.md): the writer's layout checked with an independent struct reading, round trips, details with missing fields, the square thumbnail,
refusals of bad input, version 0 still read. The firmware's C reader is tested against these files in sim/test_gallery_fmt.py. Run: python3 sim/test_tgix_v1.py [--mutants]"""
import struct, sys, random
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools" / "gallery"))
import tgy
fails = 0
def check(name, ok):
    global fails
    print(("ok   " if ok else "FAIL ") + name); fails += 0 if ok else 1

def make(n=7, nf=3, seed=1, details=True):
    rng = random.Random(seed); ents, thumbs = [], []
    per = [n // nf + (1 if k < n % nf else 0) for k in range(nf)]
    k = 0
    for f in range(nf):
        for j in range(per[f]):
            d = dict(format="JPEG", camera="Canon EOS R5", lens="RF24-70mm F2.8", taken=(2024, 5, 17, 13, 4, 59), shutter=(1, 250), aperture=28, iso=400, focal=350) if details and k % 2 == 0 else ({"format": "PNG"} if details else {})
            ents.append(dict(path=f"/Assets/tau_gallery/p/f{f}/img{k:03d}.tgy", name=f"img{k:03d}", size=1000 + k, width=640 + k, height=480 + k, folder=f, taken=tgy.taken_seconds(d.get("taken")), details=d))
            thumbs.append(bytes(rng.randrange(256) for _ in range(16)) * (tgy.THUMB1_BYTES // 16)); k += 1
    return ents, thumbs, [dict(name=f"Folder {f}") for f in range(nf)]

def main():
    ents, thumbs, folders = make()
    data = tgy.pack_index_v1(ents, thumbs, folders, tgy.SORT_NAME)
    # an independent reading of the layout
    magic, ver, hsz, n, esz, tw, th, nf, eoff, aoff, foff, fesz, sort, doff, dlen, flags = struct.unpack_from("<4sHHIHHHHIIIHHIII", data, 0)
    check("header fields", (magic, ver, hsz, n, esz, tw, th, nf, fesz, sort, flags) == (b"TGIX", 1, 64, 7, 176, 96, 96, 3, 48, 0, 0))
    check("header checksum is the CRC-32 of bytes 0 to 59", struct.unpack_from("<I", data, 60)[0] == tgy.crc(data[:60]) and data[48:60] == bytes(12))
    check("sections: entries at 64, atlas, folders and details start on 4 KB boundaries, in order", eoff == 64 and aoff % 4096 == 0 and foff % 4096 == 0 and aoff >= eoff + n * esz and foff >= aoff + n * tgy.THUMB1_BYTES and doff == foff + nf * fesz)
    check("the file is padded to 4 KB with room after the last piece", len(data) % 4096 == 0 and len(data) > doff + dlen)
    r = data[eoff + 1 * esz:eoff + 2 * esz]
    check("an entry: path, name, size, width, height, thumb, folder, flags", r[:96].rstrip(b"\0").decode() == ents[1]["path"] and struct.unpack_from("<IIIIHH", r, 136) == (1001, 641, 481, 1, ents[1]["folder"], 1 if ents[1]["details"] else 0))
    fr = [struct.unpack_from("<II", data, foff + k * 48 + 32) for k in range(nf)]
    check("folders are the ranges [first, first + count)", fr == [(0, 3), (3, 2), (5, 2)])
    p = tgy.parse_index_full(data)
    check("round trip: entries, folders, thumbnails, details", p["version"] == 1 and [e["path"] for e in p["entries"]] == [e["path"] for e in ents] and all(a["thumb"] == t for a, t in zip(p["entries"], thumbs))
          and [(f["first"], f["count"]) for f in p["folders"]] == fr and p["entries"][0]["details"] == tgy.parse_details(tgy.pack_details(ents[0]["details"])) and p["entries"][1]["details"] == {"format": "PNG"})
    check("a field a picture does not have is not stored (a PNG has only its format: 1 item of 2 + 3 bytes)", len(tgy.pack_details({"format": "PNG"})) == 5 and tgy.pack_details({}) == b"" and tgy.pack_details({"camera": None, "iso": 0}) == b"")
    check("taken seconds count from 2000-01-01; unknown is 0", tgy.taken_seconds((2000, 1, 1, 0, 0, 1)) == 1 and tgy.taken_seconds(None) == 0 and tgy.taken_seconds((1999, 5, 1, 0, 0, 0)) == 0)
    one = tgy.parse_index_full(tgy.pack_index_v1(ents[:2], thumbs[:2]))
    check("no folders given: one folder, ALL PICTURES, holding every entry", len(one["folders"]) == 1 and one["folders"][0] == dict(name="ALL PICTURES", first=0, count=2, cover=0))
    check("an unknown details tag is skipped", tgy.parse_details(bytes((99, 3, 1, 2, 3)) + tgy.pack_details({"iso": 100})) == {"iso": 100})
    e0 = tgy.parse_index_full(tgy.pack_index_v1([], [], None))
    check("an empty library is a valid file with one folder", e0["entries"] == [] and len(e0["folders"]) == 1)
    # refusals by the writer
    def refuses(f):
        try: f(); return False
        except ValueError: return True
    bad = [dict(e) for e in ents]; bad[0]["folder"] = 2
    check("the writer refuses entries not sorted by folder", refuses(lambda: tgy.pack_index_v1(bad, thumbs, folders)))
    check("the writer refuses a folder with no picture", refuses(lambda: tgy.pack_index_v1(ents[:3], thumbs[:3], folders)))
    long_ = [dict(ents[0], path="/" + "x" * 96)]
    check("the writer refuses a path that does not fit", refuses(lambda: tgy.pack_index_v1(long_, thumbs[:1])))
    check("the writer refuses a thumbnail of the wrong size", refuses(lambda: tgy.pack_index_v1(ents[:1], [b"x"])))
    # refusals by the reader (header fields with the checksum repaired)
    def mutate(off, fmt, val):
        b = bytearray(data); struct.pack_into(fmt, b, off, val); struct.pack_into("<I", b, 60, tgy.crc(bytes(b[:60]))); return bytes(b)
    cases = [("a wrong entry size", 12, "<H", 160), ("a thumbnail width other than 96", 14, "<H", 97), ("zero folders", 18, "<H", 0), ("an atlas offset inside the entries", 24, "<I", 70),
             ("a folder table past the end of the file", 28, "<I", len(data) + 4096), ("a details run past the end", 40, "<I", len(data)), ("a sort mode that does not exist", 34, "<H", 9)]
    for name, off, fmt, val in cases: check("the reader refuses " + name, refuses(lambda: tgy.parse_index_full(mutate(off, fmt, val))))
    b = bytearray(data); b[10] ^= 1
    check("the reader refuses a damaged header (checksum)", refuses(lambda: tgy.parse_index_full(bytes(b))))
    b = bytearray(data); struct.pack_into("<I", b, foff + 32, 6)
    check("the reader refuses a folder range outside the entries", refuses(lambda: tgy.parse_index_full(bytes(b))))
    b = bytearray(data); struct.pack_into("<H", b, eoff + 2 * esz + 152, 200)
    check("the reader refuses a folder index outside the table", refuses(lambda: tgy.parse_index_full(bytes(b))))
    # version 0 stays readable, and its thumbnails pad to squares
    v0 = tgy.pack_index([dict(path="/a/x.tgy", name="x", size=5, width=3, height=2)], [bytes(range(256)) * (tgy.THUMB_BYTES // 256) + bytes(tgy.THUMB_BYTES % 256)])
    pv = tgy.parse_index_full(v0)
    check("a version 0 index reads as one folder of its pictures with 96 x 72 thumbnails", pv["version"] == 0 and pv["folders"][0]["count"] == 1 and (pv["thumb_w"], pv["thumb_h"]) == (96, 72))
    sq = tgy.thumb72_to_square(pv["entries"][0]["thumb"])
    check("a version 0 thumbnail pads to 96 x 96 with black bars of 12 rows", len(sq) == tgy.THUMB1_BYTES and sq[:2304] == bytes(2304) and sq[-2304:] == bytes(2304) and sq[2304:-2304] == pv["entries"][0]["thumb"])
    # the square thumbnail: scaled to cover, centre crop
    from PIL import Image
    wide = Image.new("RGB", (400, 100), (255, 0, 0)); wide.paste((0, 255, 0), (150, 0, 250, 100))
    t = tgy.square_thumb(wide); px = lambda x, y: struct.unpack_from("<H", t, (y * 96 + x) * 2)[0]
    check("square thumbnail of a wide picture: 18,432 bytes, the centre of the picture fills the square (no bars)", len(t) == tgy.THUMB1_BYTES and px(48, 48) == 0x07E0 and px(0, 0) in (0x07E0, 0xF800) and px(48, 0) != 0 and px(48, 95) != 0)
    # details from EXIF
    from PIL import Image as I
    im = I.new("RGB", (8, 8)); ex = im.getexif(); ex[271] = "Canon"; ex[272] = "Canon EOS R5"; ifd = ex.get_ifd(0x8769)
    ifd[0x9003] = "2024:05:17 13:04:59"; ifd[0x829A] = 0.004; ifd[0x829D] = 2.8; ifd[0x8827] = 400; ifd[0x920A] = 35.0; ifd[0xA434] = "RF24-70mm F2.8"
    d = tgy.exif_details(im, "JPEG")
    import PIL; print("  (Pillow", PIL.__version__, "exif details:", d, ")")
    check("EXIF details: camera (make not repeated), lens, taken, shutter 1/250, f/2.8, ISO 400, 35 mm", d == dict(format="JPEG", camera="Canon EOS R5", lens="RF24-70mm F2.8", taken=(2024, 5, 17, 13, 4, 59), shutter=(1, 250), aperture=28, iso=400, focal=350))
    check("a picture with no EXIF has only its format", tgy.exif_details(I.new("RGB", (4, 4)), "PNG") == {"format": "PNG"})
    print("tgix-v1: %d failure(s)" % fails); return 1 if fails else 0

if __name__ == "__main__":
    rc = main()
    sys.exit(rc)
