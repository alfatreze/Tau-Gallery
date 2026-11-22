#!/usr/bin/env python3
"""Driver of sim/test_gallery_app.c: writes 18 small pictures and their index with the real converter library, builds the firmware (gallery_app.c, gallery_fmt.c, gallery_hw.c) against the hardware model and runs it.
`--mutants` edits copies of the firmware sources; every edit must make the run fail."""
import os, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools/gallery"))
import tgy
from PIL import Image

def make_big_set(d, n=300):
    """A 300-entry index with a thumbnail of its own for every entry (a pattern of the index) and real picture files only for the pictures the script opens (the others fail to open, which is also a case)."""
    ents, ths = [], []
    for i in range(n):
        ents.append(dict(path=f"Assets/tau_gallery/common/photos/b{i:03d}.tgy", name=f"Photo {i:03d}", size=1748992, width=800, height=600))
        ths.append(bytes(((i * 7 + j // 192 * 5 + j % 192 * 3) & 255) for j in range(tgy.THUMB_BYTES)))
    (d / "tau-library.tdb").write_bytes(tgy.pack_index(ents, ths))
    for i in (0, 150, 298, 299):
        im = Image.new("RGB", (640, 480), ((i * 5) % 256, (i * 3 + 40) % 256, (i * 11 + 90) % 256)); im.paste(((i * 13) % 256, 255, 0), (i % 200, 10, i % 200 + 100, 60))
        canvas, rect, thumb, size = tgy.render(im); (d / f"b{i:03d}.tgy").write_bytes(tgy.pack_tgy(canvas, rect, thumb, size))

def make_zoom_set(d):
    """Five pictures with zoom levels, for the zoom scenario: z00 2000 x 1500 (a raster 1:1 level), z01 1000 x 800 (raster 1:1), z02 600 x 400 (no zoom level), z03 700 x 1500 (a raster level padded to 800 wide), z04 3200 x 2400 (a
    raster half level 1600 x 1200 and a tile 1:1 level), z05 2300 x 1500 (a tile 1:1 level only: no half stop, wider than a raster level), z06 3600 x 2400 (a window placed for a pan is not clamped by the level's edges). Random pixels, so a wrong address cannot match by accident."""
    import random
    rnd = random.Random(5); ents, ths = [], []
    for i, (w, h) in enumerate([(2000, 1500), (1000, 800), (600, 400), (700, 1500), (3200, 2400), (2300, 1500), (3600, 2400)]):
        prep = tgy.prepare(Image.frombytes("RGB", (w, h), rnd.randbytes(w * h * 3)))
        canvas, rect, thumb, size = tgy.render_prepared(prep); data = tgy.pack_tgy(canvas, rect, thumb, size, tgy.zoom_levels(prep))
        (d / f"z{i:02d}.tgy").write_bytes(data)
        ents.append(dict(path=f"Assets/tau_gallery/common/photos/z{i:02d}.tgy", name=f"Zoom {i:02d}", size=len(data), width=size[0], height=size[1])); ths.append(thumb)
    (d / "tau-library.tdb").write_bytes(tgy.pack_index(ents, ths))

def make_v1_set(d):
    """Version 1 pictures for the hardware decoder (the converter's canvas container): v00 a 1000 x 900 JPEG (fit canvas + lossless full level), v01 a 500 x 300 picture (the fit canvas only), v02 a version 0 picture, v03 a v1 file whose first tile
    stream is damaged, v04 a v1 file with a damaged tile index, v05 a v1 file with a damaged header. Also the reference decode of v00 and v01 (vNN.rgb: the 800 x 720 canvas, TGY profile)."""
    import io, numpy as np, tgy_container as C, tgy_make, test_jpegcoef as TJ
    ents, ths = [], []
    def add(i, data, thumb, size, name):
        (d / f"v{i:02d}.tgy").write_bytes(data)
        ents.append(dict(path=f"Assets/tau_gallery/common/photos/v{i:02d}.tgy", name=name, size=len(data), width=size[0], height=size[1])); ths.append(thumb)
    jp = d / "src0.jpg"; TJ.picture(1000, 900, seed=3).save(jp, quality=90)
    imgs = [TJ.picture(1000, 900, seed=3), TJ.picture(500, 300, seed=4), TJ.picture(640, 480, seed=5), TJ.picture(900, 700, seed=6), TJ.picture(900, 700, seed=7), TJ.picture(900, 700, seed=8)]
    data0 = None
    for i in (0, 1, 3, 4, 5):
        prep = tgy.prepare(imgs[i]); data, thumb, size = tgy_make.canvas_container(jp if i == 0 else None, prep)
        if i in (0, 1):
            c = C.Container(data); fit = [k for k, e in enumerate(c.levels) if e["kind"] == C.KIND_FIT][0]; (d / f"v{i:02d}.rgb").write_bytes(c.pixels(fit).tobytes())
        if i == 0:
            full = [k for k, e in enumerate(c.levels) if e["kind"] == C.KIND_FULL][0]; (d / "v00_full.rgb").write_bytes(c.pixels(full).tobytes())      # the reference of the 1:1 level (1000 x 900) for the zoom scenario
        if i == 3:
            c = C.Container(data); e = c.levels[0]; off = e["data_off"]; b = bytearray(data); b[off:off + 40] = b"\xff" * 40; data = bytes(b)      # the first tile's stream: the decoder reports an error
        if i == 4:
            c = C.Container(data); e = c.levels[0]; b = bytearray(data); b[e["index_off"] + 5] ^= 1; data = bytes(b)
        if i == 5:
            b = bytearray(data); b[9] ^= 0x40; data = bytes(b)
        add(i, data, thumb, size, f"V1 picture {i:02d}")
    prep11 = tgy.prepare(imgs[1]); data11, thumb11, size11 = tgy_make.canvas_container(None, prep11); c11 = C.Container(data11); e11 = c11.levels[0]      # v11: v01's picture with the stream of tile 20 damaged: the decode starts, counts 20 tiles, then fails (a partial decode in the histogram unit)
    ioff, = C.struct.unpack_from("<I", data11, e11["index_off"] + 12 * 20); b11 = bytearray(data11); b11[e11["data_off"] + ioff:e11["data_off"] + ioff + 40] = b"\xff" * 40
    add(11, bytes(b11), thumb11, size11, "V1 late damage 11")
    canvas, rect, thumb, size = tgy.render(imgs[2]); add(2, tgy.pack_tgy(canvas, rect, thumb, size), thumb, size, "V0 picture 02")
    import io as _io
    from PIL import Image as _I
    ex = _I.Exif(); ex[0x0112] = 3; bio = _io.BytesIO(); TJ.picture(1000, 900, seed=10).save(bio, "JPEG", quality=90, exif=ex)                 # a 1000 x 900 JPEG whose EXIF orientation is 180 degrees: its full level is stored unrotated, the viewer would have to rotate it: no zoom
    (d / "src6.jpg").write_bytes(bio.getvalue()); prep6 = tgy.prepare(_I.open(d / "src6.jpg"))
    data6, thumb6, size6 = tgy_make.canvas_container(d / "src6.jpg", prep6); add(6, data6, thumb6, size6, "V1 rotated 06")
    c6 = C.Container(data6); (d / "v06_full.rgb").write_bytes(c6.pixels([k for k, e in enumerate(c6.levels) if e["kind"] == C.KIND_FULL][0]).tobytes()); assert c6.orientation == 1      # the reference of the rotated picture's zoom level (written upright)
    big = TJ.picture(2000, 1700, seed=11); (d / "src7.jpg").write_bytes(b""); big.save(d / "src7.jpg", "JPEG", quality=90); prep7 = tgy.prepare(_I.open(d / "src7.jpg"))      # larger than the 11 x 10 tile window (1408 x 1280): a pan has to load tiles, so the view can stall
    data7, thumb7, size7 = tgy_make.canvas_container(d / "src7.jpg", prep7); add(7, data7, thumb7, size7, "V1 big 07")
    old = bytearray(data6); old[18] = 3; old[52:56] = C.struct.pack("<I", C.crc(bytes(old[:52])))                                       # a file written by an older converter: the upright zoom level but the EXIF tag (180 degrees) still in the header: the viewer must not zoom it
    add(10, bytes(old), thumb6, size6, "V1 old rotated 10")
    for i, (w, h, nm) in ((8, (3600, 1200, "V1 wide 08")), (9, (1100, 3300, "V1 tall 09"))):                                  # a 3:1 panorama (the picture sits at y 226 on the fit canvas) and a 1:3 portrait (at x 280): shapes that do not fill the 800 x 720 canvas
        pic_ = TJ.picture(w, h, seed=20 + i); (d / f"src{i}.jpg").write_bytes(b""); pic_.save(d / f"src{i}.jpg", "JPEG", quality=85); prep_ = tgy.prepare(_I.open(d / f"src{i}.jpg"))
        data_, thumb_, size_ = tgy_make.canvas_container(d / f"src{i}.jpg", prep_, jobs=1); add(i, data_, thumb_, size_, nm)
    order = sorted(range(len(ents)), key=lambda k: ents[k]["path"]); (d / "tau-library.tdb").write_bytes(tgy.pack_index([ents[k] for k in order], [ths[k] for k in order]))

def lib1_thumb(i): return b"".join(((i * 37 + r * 5 + c * 3 + (i * r) % 11) & 0xFFFF).to_bytes(2, "little") for r in range(96) for c in range(96))
def make_libbig_set(d, n=60):
    """A version 1 library of ONE folder of 60 pictures: no folder level, three pages at 20 a page."""
    ents = [dict(path=f"Assets/tau_gallery/common/photos/l{i:02d}.tgy", name=f"Img {i:02d}", size=1000 + i, width=640, height=480) for i in range(n)]
    (d / "tau-library.tdb").write_bytes(tgy.pack_index_v1(ents, [lib1_thumb(i) for i in range(n)]))

def make_v1idx_set(d):
    """The version 1 pictures of make_v1_set under a version 1 index (square cropped thumbnails, one folder): the grid opens their details with the thumbnail a crop of the picture."""
    make_v1_set(d)
    old = tgy.parse_index_full((d / "tau-library.tdb").read_bytes())
    ents = [dict(path=e["path"], name=e["name"], size=e["size"], width=e["width"], height=e["height"], folder=0, details={"format": "JPEG"}) for e in old["entries"]]
    (d / "tau-library.tdb").write_bytes(tgy.pack_index_v1(ents, [lib1_thumb(i) for i in range(len(ents))], [dict(name="All")]))

def make_lib1_set(d):
    """A version 1 library of 36 pictures in three folders (Alpha 20, Beta 3, Gamma 13): 96 x 96 thumbnails of their own pattern, and picture files for the three of Beta (the viewer scenario)."""
    ents, ths = [], []
    for i in range(36):
        f = 0 if i < 20 else 1 if i < 23 else 2
        det = dict(format="JPEG", camera="Canon EOS R5", lens="RF24-70mm F2.8", taken=(2024, 5, 17, 13, 4, 59), shutter=(1, 250), aperture=28, iso=400, focal=350) if i % 3 == 0 else {"format": "PNG"} if i % 3 == 1 else {}
        ents.append(dict(path=f"Assets/tau_gallery/common/photos/l{i:02d}.tgy", name=f"Img {i:02d}", size=1000 + i, width=640, height=480, folder=f, details=det))
        ths.append(lib1_thumb(i))
    (d / "tau-library.tdb").write_bytes(tgy.pack_index_v1(ents, ths, [dict(name="Alpha"), dict(name="Beta"), dict(name="Gamma")]))
    for i in (20, 21, 22):
        im = Image.new("RGB", (640, 480), ((i * 5) % 256, (i * 9) % 256, 120)); canvas, rect, thumb, size = tgy.render(im); (d / f"l{i:02d}.tgy").write_bytes(tgy.pack_tgy(canvas, rect, thumb, size))

def make_set(d, n=18):
    ents, ths = [], []
    for i in range(n):
        w, h = (900, 600) if i % 3 == 0 else (400, 700) if i % 3 == 1 else (640, 640)
        im = Image.new("RGB", (w, h)); im.putdata([((x * 3 + i * 31) % 256, (y * 5 + i * 17) % 256, (x + y + i * 53) % 256) for y in range(h) for x in range(w)])
        canvas, rect, thumb, size = tgy.render(im); data = tgy.pack_tgy(canvas, rect, thumb, size)
        (d / f"p{i:02d}.tgy").write_bytes(data)
        ents.append(dict(path=f"Assets/tau_gallery/common/photos/p{i:02d}.tgy", name=f"Picture {i:02d}", size=len(data), width=size[0], height=size[1])); ths.append(thumb)
    (d / "tau-library.tdb").write_bytes(tgy.pack_index(ents, ths))

def build(workdir, fw=None):
    """Compile the test with the firmware sources from `fw` (a dict name -> text) or from the tree."""
    src = ROOT / "sim/test_gallery_app.c"; text = src.read_text()
    inc = workdir / "fw2/gallery"; inc.mkdir(parents=True, exist_ok=True)
    for sub in ("platform", "gfx", "stream"):
        (workdir / "fw2" / sub).mkdir(parents=True, exist_ok=True)
        for f in (ROOT / "fw2" / sub).glob("*.h"): (workdir / "fw2" / sub / f.name).write_text(f.read_text())
    for f in (ROOT / "fw2/cores").glob("*.h"): (workdir / "fw2/cores").mkdir(exist_ok=True); (workdir / "fw2/cores" / f.name).write_text(f.read_text())
    (workdir / "fw2/ui").mkdir(exist_ok=True); (workdir / "fw").mkdir(exist_ok=True)               # the theme module (the colour roles) and its generated data
    for n in ("theme.c", "theme.h"): (workdir / "fw2/ui" / n).write_text((ROOT / "fw2/ui" / n).read_text())
    (workdir / "fw/theme_data.h").write_text((ROOT / "fw/theme_data.h").read_text())
    for f in (ROOT / "fw2/gallery").iterdir():
        if f.suffix in (".c", ".h"): (inc / f.name).write_text((fw or {}).get(f.name, f.read_text()))
    (workdir / "sim").mkdir(exist_ok=True); (workdir / "sim/t.c").write_text(text)
    env = dict(os.environ); sdk = subprocess.run([sys.executable, str(ROOT / "tools/toolchain.py"), "sdkroot"], capture_output=True, text=True).stdout.strip()
    if sdk: env["SDKROOT"] = sdk
    r = subprocess.run(["cc", "-O1", "-w", "-o", str(workdir / "t"), str(workdir / "sim/t.c"), "-lm"], capture_output=True, text=True, env=env)
    if r.returncode: print(r.stderr[:3000]); return False
    return True

SCENARIOS = ("resume5", "resume17", "badcrc", "toohigh", "off", "layout0", "ocean_light", "badlook", "details")     # boot scenarios: the saved state, the grid must open on picture 6 or 18, or on picture 1 when it is not the same picture / resume is off / the file is not ours
BIGSET = None; ZSET = None; V1SET = None; V1ISET = None; L1SET = None; LBSET = None
def run(workdir, setdir):
    r = subprocess.run([str(workdir / "t"), str(setdir)], capture_output=True, text=True, timeout=600)
    rc, out = r.returncode, r.stdout
    if rc: out += f"\nFAIL the plain run exited {rc}\n" + r.stderr[-3000:]                                     # a crash prints nothing on stdout: the sanitizer's or the runtime's words are on stderr
    for sc in SCENARIOS:
        r = subprocess.run([str(workdir / "t"), str(setdir), sc], capture_output=True, text=True, timeout=600)
        if r.returncode: rc = 1; out += "\nFAIL scenario " + sc + "\n" + "\n".join(l for l in r.stdout.splitlines() if "FAIL" in l) + f" (exit {r.returncode}) " + r.stdout[-200:].replace(chr(10), " | ") + r.stderr[-2500:]
    global BIGSET, ZSET
    if BIGSET is None: BIGSET = Path(tempfile.mkdtemp()); make_big_set(BIGSET)
    if ZSET is None: ZSET = Path(tempfile.mkdtemp()); make_zoom_set(ZSET)
    r = subprocess.run([str(workdir / "t"), str(ZSET), "zoom"], capture_output=True, text=True, timeout=900)
    if r.returncode: rc = 1; out += "\nFAIL scenario zoom\n" + "\n".join(l for l in r.stdout.splitlines() if "FAIL" in l) + f" (exit {r.returncode}) " + r.stdout[-200:].replace(chr(10), " | ") + r.stderr[-2500:]
    global V1SET
    if V1SET is None: V1SET = Path(tempfile.mkdtemp()); make_v1_set(V1SET)
    for sc in ("v1", "v1nohw", "v1zoom", "v1map", "v1rot", "v1mask", "v1hist", "v1mirror", "v1filter", "v1zoom2"):
        r = subprocess.run([str(workdir / "t"), str(V1SET), sc], capture_output=True, text=True, timeout=900)
        if r.returncode: rc = 1; out += "\nFAIL scenario " + sc + "\n" + "\n".join(l for l in r.stdout.splitlines() if "FAIL" in l) + f" (exit {r.returncode}) " + r.stdout[-200:].replace(chr(10), " | ") + r.stderr[-2500:]
        else: out += "\n" + r.stdout
    global V1ISET
    if V1ISET is None: V1ISET = Path(tempfile.mkdtemp()); make_v1idx_set(V1ISET)
    r = subprocess.run([str(workdir / "t"), str(V1ISET), "v1gridhist"], capture_output=True, text=True, timeout=900)
    if r.returncode: rc = 1; out += "\nFAIL scenario v1gridhist\n" + "\n".join(l for l in r.stdout.splitlines() if "FAIL" in l) + f" (exit {r.returncode}) " + r.stdout[-200:].replace(chr(10), " | ") + r.stderr[-2500:]
    else: out += "\n" + r.stdout
    global L1SET
    if L1SET is None: L1SET = Path(tempfile.mkdtemp()); make_lib1_set(L1SET)
    for sc in ("lib1", "lib1resume", "lib1resumeA", "lib1resumeC", "lib1details"):
        r = subprocess.run([str(workdir / "t"), str(L1SET), sc], capture_output=True, text=True, timeout=900)
        if r.returncode: rc = 1; out += "\nFAIL scenario " + sc + "\n" + "\n".join(l for l in r.stdout.splitlines() if "FAIL" in l) + f" (exit {r.returncode}) " + r.stdout[-200:].replace(chr(10), " | ") + r.stderr[-2500:]
    r = subprocess.run([str(workdir / "t"), str(L1SET), "lib1light"], capture_output=True, text=True, timeout=900)
    if r.returncode: rc = 1; out += "\nFAIL scenario lib1light\n" + "\n".join(l for l in r.stdout.splitlines() if "FAIL" in l) + f" (exit {r.returncode}) " + r.stdout[-200:].replace(chr(10), " | ") + r.stderr[-2500:]
    for sc in ("lib1g1", "lib1g2", "lib1g3"):
        r = subprocess.run([str(workdir / "t"), str(L1SET), sc], capture_output=True, text=True, timeout=900)
        if r.returncode: rc = 1; out += "\nFAIL scenario " + sc + "\n" + "\n".join(l for l in r.stdout.splitlines() if "FAIL" in l) + f" (exit {r.returncode}) " + r.stdout[-200:].replace(chr(10), " | ") + r.stderr[-2500:]
    global LBSET
    if LBSET is None: LBSET = Path(tempfile.mkdtemp()); make_libbig_set(LBSET)
    for sc in ("libbig0", "libbig1", "libbig2", "libbig3", "libbigset", "colour"):
        r = subprocess.run([str(workdir / "t"), str(LBSET), sc], capture_output=True, text=True, timeout=900)
        if r.returncode: rc = 1; out += "\nFAIL scenario " + sc + "\n" + "\n".join(l for l in r.stdout.splitlines() if "FAIL" in l) + f" (exit {r.returncode}) " + r.stdout[-200:].replace(chr(10), " | ") + r.stderr[-2500:]
    for sc in ("big", "bigresume250", "bigresume299"):
        r = subprocess.run([str(workdir / "t"), str(BIGSET), sc], capture_output=True, text=True, timeout=900)
        if r.returncode: rc = 1; out += "\nFAIL scenario " + sc + "\n" + "\n".join(l for l in r.stdout.splitlines() if "FAIL" in l) + f" (exit {r.returncode}) " + r.stdout[-200:].replace(chr(10), " | ") + r.stderr[-2500:]
    return rc, out

def mutants(setdir, tail=None):
    w0 = Path(tempfile.mkdtemp())                                                                 # a mutant run means something only on a passing unmutated test: with a failing one every mutant would count as killed
    if not build(w0): print("BASELINE does not build"); return 1
    rc0, out0 = run(w0, setdir)
    if rc0 != 0 or "PASSED" not in out0: print("BASELINE FAILS: no mutant result is valid"); print("\n".join(l for l in out0.splitlines() if "FAIL" in l) + f" (exit {r.returncode}) " + r.stdout[-200:].replace(chr(10), " | ") + r.stderr[-2500:]); return 1
    muts = [("gallery_app.c", "REG_WR(R_LAND_BASE, GAL_PLANE_BASE);", "REG_WR(R_LAND_BASE, GAL_PLANE_BASE + 2u);"),
            ("gallery_app.c", "0x40000000u + c, len,", "0x40000000u, len,"),
            ("gallery_app.c", "t->canvas_off + c, 0x40000000u", "t->canvas_off, 0x40000000u"),
            ("gallery_app.c", "const uint32_t total = (pic.canvas_len + 4095u) & ~4095u;", "const uint32_t total = pic.canvas_len;"),
            ("gallery_app.c", "if (REG_RD(R_LAND_OVF) != 0u ||", "if ("),
            ("gallery_app.c", "|| REG_RD(R_LAND_WORDS) != total / 4u) return 6;", ") return 6;"),
            ("gallery_app.c", "GX0 2u", "GX0 4u") if False else ("gallery_app.c", "{ 4, 4, 96, 72, 2, 100, 0, 76, 2, 0, 0xFF }", "{ 4, 4, 96, 72, 4, 100, 0, 76, 2, 0, 0xFF }"),
            ("gallery_app.c", "{ 4, 4, 96, 72, 2, 100, 0, 76, 2, 0, 0xFF }", "{ 4, 4, 96, 72, 2, 100, 0, 75, 2, 0, 0xFF }"),
            ("gallery_app.c", "const uint32_t rel = a - off, row = rel / 192u, col = (rel % 192u) / 2u;", "const uint32_t rel = a - off, row = rel / 192u, col = (rel % 192u) / 2u + 1u;"),
            ("gallery_app.c", "if (a < off || a >= off + tb) continue;", "if (a < off) continue;"),
            ("gallery_app.c", "path[0] = '/';", "path[0] = 'x';"),
            ("gallery_app.c", "if (edge & KEY_B) { gal_view_off();", "if (edge & KEY_B) { "),
            ("gallery_app.c", "if (!v1_on || h4[18] != 1u) return;", "if (!v1_on) return;"),                                                                          # a file with an orientation tag would zoom
            ("gallery_app.c", "if (!v1_on || h4[18] != 1u) return;", "if (!v1_on || h4[18] != 1u || 1) return;"),                                                       # no picture would zoom
            ("gallery_app.c", "if (v1z.idx_len <= v1z_cap) ir =", "if (v1z.idx_len <= v1z_cap * 99u) ir ="),                                                                                      # an index larger than the buffer is accepted
            ("gallery_app.c", "if (zs[k].layout == 2u && !v1z_idx_ok) {", "if (zs[k].layout == 2u && 0) {"),                                                                 # the tile index is never loaded
            ("gallery_app.c", "{ say(\"ZOOM INDEX READ FAILED\", BAD); zk = -1; return; }", "{ say(\"ZOOM INDEX READ FAILED\", BAD); zk = -1; }"),                              # a failed index read goes on to zoom
            ("gallery_app.c", "        v1z_idx_ok = 1u;\n    }", "    }"),                                                                                                                   # the index is read again at every zoom
            ("gallery_app.c", "zs[zn++] = z; v1z_idx_ok = 0u;", "zs[zn++] = z;"),                                                                                          # the index of the last picture is kept for the next one
            ("gallery_app.c", "if (v1_on) { v1_zoom_list(); return; }", ""),                                                                                                # a version 1 picture has no zoom list
            ("gallery_app.c", "z.scale_q16 = 65536u;", "z.scale_q16 = 32768u;"),                                                                                            # the 1:1 level at half scale: the view opens at the wrong place
            ("gallery_app.c", "len == 0u || len > v1z_smax) return 1;", "len == 0u || len > v1z_smax * 9999u) return 1;"),                                                  # a stream longer than the limit is decoded
            ("gallery_app.c", "const uint32_t a = fo & ~4095u, e = (fo + len + 4095u) & ~4095u,", "const uint32_t a = fo & ~3u, e = (fo + len + 4095u) & ~4095u,"),                 # the read starts unaligned
            ("gallery_app.c", "const uint32_t a = fo & ~4095u, e = (fo + len + 4095u) & ~4095u,", "const uint32_t a = fo & ~4095u, e = (fo + len + 3u) & ~3u,"),                     # the read ends unaligned
            ("gallery_app.c", "ok = REG_RD(R_LAND_OVF) == 0u && REG_RD(R_LAND_WORDS) == n / 4u;", "ok = REG_RD(R_LAND_WORDS) == n / 4u;"),                                         # a landing overflow is not noticed
            ("gallery_app.c", "ok = REG_RD(R_LAND_OVF) == 0u && REG_RD(R_LAND_WORDS) == n / 4u;", "ok = REG_RD(R_LAND_OVF) == 0u;"),                                             # a short landing is not noticed
            ("gallery_app.c", "(win_base(tw) + gv_tile_hw(col, row)) * 2u, GV_WIN_STRIDE_WORDS * 2u,", "win_base(tw) * 2u + gv_tile_hw(col, row), GV_WIN_STRIDE_WORDS * 2u,"),      # the tile lands in the wrong place of the window
            ("gallery_app.c", "(win_base(tw) + gv_tile_hw(col, row)) * 2u, GV_WIN_STRIDE_WORDS * 2u,", "(win_base(tw) + gv_tile_hw(col, row)) * 2u, GV_WIN_STRIDE_WORDS,"),             # the decoder's row pitch is half the window's
            ("gallery_app.c", "v1_stream_hw(buf) * 2u + off) != TH_OK", "v1_stream_hw(buf) * 2u) != TH_OK"),                                                                         # the decoder reads the superset's start, not the stream's
            ("gallery_app.c", "    t_res[tw][row * GV_WIN_TX + col] = 1u;\n    return 0;\n}\n/* Starts the copy", "    return 0;\n}\n/* Starts the copy"),                                      # a decoded tile is never marked whole
            ("gallery_app.c", "else if (zl.layout == 2u) return v1_tile_decode(tw, col, row, tx, ty);", "else if (zl.layout == 2u) { return 1; }"),                                        # a version 1 tile is never decoded
            ("gallery_app.c", "    info.zt_n = info.zt_land_sum = info.zt_land_max = info.zt_dec_n = info.zt_dec_sum = info.zt_dec_max = info.zt_pass_max = info.zt_catch = info.zt_moved = info.zt_stall_run_max = info.zt_sp = info.zt_sc = info.zt_sn = info.zt_sj = info.zt_ss = 0u;", ""),            # the tile statistics are not reset by a new zoom
            ("gallery_app.c", "        info.zt_catch += (uint32_t)tick;", ""),                                                                                                                  # frames behind the wanted corner are not counted
            ("gallery_app.c", "{ tile_show(nx, ny); info.zt_moved += (uint32_t)tick; }", "{ tile_show(nx, ny); }"),                                                                         # moved frames are not counted
            ("gallery_app.c", "info.zt_n++; info.zt_land_sum += us;", "info.zt_land_sum += us;"),                                                         # the tiles are not counted
            ("gallery_app.c", "info.zt_n++; info.zt_land_sum += us;", "info.zt_n++;"),                                                      # the card read time is not summed
            ("gallery_app.c", "info.zt_dec_n++; info.zt_dec_sum += dec_us;", "info.zt_dec_n++;"),                                                                                     # the decode time is not summed
            ("gallery_app.c", "if (us > info.zt_land_max) info.zt_land_max = us;", ""),                                                                           # the longest card read is not kept
            ("gallery_app.c", "if (dec_us > info.zt_dec_max) info.zt_dec_max = dec_us;", ""),                                                                               # the longest decode is not kept
            ("gallery_app.c", "    if (us > info.zt_pass_max) {", "    if (0) {"),                                                                                     # the longest pass is not kept
            ("gallery_app.c", "if (zl.layout != 2u) return tile_pass_inner(dx, dy, held, moving, tick);", ""),                                                              # the version 0 zoom is timed too (extra clock reads change its pace)
            ("gallery_app.c", "    if (v1_next.valid && v1_next.tw == tw) {", "    if (0) {"),                                                                                  # the next tile is not read while this one decodes
            ("gallery_app.c", "const uint32_t nb = 1u - buf;", "const uint32_t nb = buf;"),                                                                                  # the next stream overwrites the one being decoded
            ("gallery_app.c", "    if (tgy_hw_finish() != TH_OK) { v1_pre.valid = 0u; return 1; }", "    if (0) { return 1; }"),                                                                 # the decoder is never waited for
            ("gallery_app.c", "    info.zt_dec_n++; info.zt_dec_sum += dec_us;", "    info.zt_dec_sum += dec_us;"),                                                              # decodes are not counted
            ("gallery_app.c", "const uint32_t dec_us = tgy_hw_cycles() / (SDRAM_HZ / 1000000u);", "const uint32_t dec_us = tgy_hw_cycles() / (SDRAM_HZ / 1000u);"),                  # the decoder's cycles in the wrong unit
            ("gallery_app.c", "    if (v1_pre.valid && v1_pre.buf == buf) v1_pre.valid = 0u;", ""),                                                                    # a read into the prefetch buffer leaves the prefetch offered
            ("gallery_app.c", "return p->valid && p->tx == tx && p->ty == ty; }", "return p->valid && p->tx == tx; }"),                                                # a stream of another tile of the column is taken
            ("gallery_app.c", "else { z_stall_run += (uint32_t)tick; if (z_stall_run > info.zt_stall_run_max) info.zt_stall_run_max = z_stall_run; }", "else { if (z_stall_run > info.zt_stall_run_max) info.zt_stall_run_max = z_stall_run; }"),   # the run of stalled frames is never counted
            ("gallery_app.c", "if (z_stall_run > info.zt_stall_run_max) info.zt_stall_run_max = z_stall_run;", ""),                                                                    # the longest run is never kept
            ("gallery_app.c", "if (k == 2) v1_pipe_off ^= 1u;", ""),                                                                                                                   # A on the Info page does nothing
            ("gallery_app.c", "if (zl.layout == 2u && !v1_pipe_off) for", "if (zl.layout == 2u) for"),                                                                                  # the pipeline cannot be switched off
            ("gallery_app.c", "info.pipe_off = v1_pipe_off; ", ""),                                                                                                                    # the Info page does not show the switch
            ("gallery_app.c", "info.zt_sj = (pst[3] - pst[2]) / u;", "info.zt_sj = 0u;"),                                                                             # the job part of the longest pass is not kept
            ("gallery_app.c", "info.zt_sn = pcopy ? 0u : (pst[2] - pst[1]) / u;", "info.zt_sn = 0u;"),                                                                      # the needed-list part is not kept
            ("gallery_app.c", "info.zt_sc = pcopy ? (pst[2] - pst[1]) / u : 0u;", "info.zt_sc = (pst[2] - pst[1]) / u;"),                                                  # the needed-list time is counted as copy as well
            ("gallery_app.c", "info.zt_ss = (t5 - pst[3]) / u;", "info.zt_ss = (t5 - pst[2]) / u;"),                                                                        # the show part includes the job
            ("gallery_app.c", "        if (zl.layout == 2u) pst[3] = cycles();                                            /* the tile (card read and decode) is done */", ""),             # the job end is never stamped
            ("gallery_app.c", "else if ((edge & KEY_SELECT) && level == 0u) { run_details(); draw_page();", "else if (0) { run_details(); draw_page();"),                                  # details page: no key on the grid
            ("gallery_app.c", "else if ((edge & KEY_SELECT) && level == 0u) { run_details(); draw_page();", "else if ((edge & KEY_SELECT) && level == 0u) { run_details();"),                          # the grid is not redrawn after it
            ("gallery_app.c", "if ((edge & KEY_SELECT) && !(edge & (KEY_LEFT | KEY_RIGHT))) {", "if (0) {"),                                                          # details page: no key in the picture view
            ("gallery_app.c", "if ((edge & KEY_SELECT) && !(edge & (KEY_LEFT | KEY_RIGHT))) {", "if (edge & KEY_SELECT) {"),                                         # Select + Right opens the page instead of stepping on
            ("gallery_app.c", "gal_view_off(); run_details();", "run_details();"),                                                                                 # the page is drawn under the live picture view
            ("gallery_app.c", "if (!gal_view_on()) { loaded_page = 0xFFFFFFFFu; draw_page(); say(\"VIEWER DID NOT START\", BAD); return; }\n            rot_view(); prev = REG_RD(R_INPUT) & 0xFFFFu; held = 0u;", "prev = REG_RD(R_INPUT) & 0xFFFFu; held = 0u;"),                # the view is not switched on again
            ("gallery_app.c", "const gx_entry_t *e = &EN(sel); const gd_t d", "const gx_entry_t *e = &ent[0]; const gd_t d"),                        # the page of the first picture, not the selected one
            ("gallery_app.c", "(rel + cur_count - 1u) % cur_count, n =", "(rel + cur_count - 1u), n ="),
            ("gallery_app.c", "if (np != page) { page = np; if (!load_page())", "if (np != page) { if (!load_page())"),
            ("gallery_app.c", "select_to(cur_first + (rel + pp < cur_count ? rel + pp : cur_count - 1u)); }", "select_to(cur_first + (rel + pp < cur_count ? rel + pp : rel)); }"),
            ("gallery_app.c", "gfx_text(6, 338, EN(sel).name, FG, BG);", "gfx_text(6, 338, ent[0].name, FG, BG);"),
            ("gallery_app.c", "if (!gal_view_on()) { say(\"VIEWER DID NOT START\", BAD); return; }", "gal_view_on();"),
            ("gallery_app.c", "if (r) { say(open_msg[r], BAD); return; }\n    if (!gal_view_on())", "if (r) { say(open_msg[r], BAD); }\n    if (!gal_view_on())"),
            ("gallery_app.c", "{ run_pages(); apply_grid(); draw_page(); prev =", "{ run_pages(); apply_grid(); prev ="),
            ("gallery_app.c", "if (r == GP_OPEN_CONTROLS) { gc_t c;", "if (r == GP_OPEN_HELP) { gc_t c;"),
            ("gallery_app.c", "static void page_clear(const char *title) { gfx_rect(0, 0, GFX_W, GFX_H, BG);", "static void page_clear(const char *title) { "),
            ("gallery_app.c", "draw_menu(&m);\n    }\n}", "}\n}"),
            ("gallery_app.c", "else if (edge & KEY_START) {", "else if (edge & KEY_SELECT) {"),
            ("gallery_app.c", "info.open_ms = (uint32_t)(cycles() - t0) / (CLK_HZ / 1000u);", "info.open_ms = GI_NONE;"),
            ("gallery_app.c", "if (r == 0 || r >= 5) {", "if (1) {"),
            ("gallery_app.c", "info.err = r ? open_msg[r] : 0;", "info.err = 0;"),
            ("gallery_app.c", "else if (r == GP_OPEN_HELP) {", "else if (r == GP_OPEN_INFO) {"),
            ("gallery_app.c", "info.core_version = REG_RD(R_VERSION);", "info.core_version = 0u;"),
            ("gallery_app.c", "const int r = open_timed(sel, 1);", "const int r = open_picture(sel, 1);"),
            ("gallery_app.c", "gset.last_idx = i; gset.last_crc = path_crc(i); }", "gset.last_idx = i; }"),
            ("gallery_app.c", "gs_resume_index(&gset, head.count, path_crc(gset.last_idx)) : 0u;", "gset.last_idx : 0u;"),
            ("gallery_app.c", "        settings_tick();\n        const uint32_t dkeys", "        const uint32_t dkeys"),
            ("gallery_app.c", "else if (r == GP_OPEN_SETTINGS) { gsp_t sp = { 0 }; draw_settings(&sp); while (!gsp_input(&sp, &gset.resume, &gset.grid, &gset.dither, &gset.lut, next_edge(&prev))) { fx_apply(); draw_settings(&sp); } }", "else if (r == GP_OPEN_SETTINGS) { gsp_t sp = { 0 }; draw_settings(&sp); while (!gsp_input(&sp, &gset.resume, &gset.grid, &gset.dither, &gset.lut, next_edge(&prev))) { fx_apply(); draw_settings(&sp); } gset.resume = 1u; }"),
            ("gallery_settings.c", "if (gs_publish_due(cycles(), last_activity, CLK_HZ, 1)) publish(v);", "publish(v);"),
            ("gallery_settings.c", "if (m & (1u << k)) set_wr(k, w[k]); }", "set_wr(k, w[k]); }"),
            ("gallery_settings.c", "gs_stats.layout = (w[GS_W_LAYOUT] >> 27) & 15u; gs_stats.loads++;", "gs_stats.layout = 0u; gs_stats.loads++;"),
            ("gallery_app.c", "theme_set(gset.theme, gset.pol, gset.pal); gset.theme = th_theme;", "gset.theme = th_theme;"),
            ("gallery_app.c", "gset.pol = th_pol; gset.pal = th_pal; }      /* applied at once", "gset.pol = th_pol; }      /* applied at once"),
            ("gallery_app.c", "#define BG   th_role[TR_BG_BOTTOM]", "#define BG   RGB565(0, 0, 0)"),
            ("gallery_app.c", "#define ACC  th_role[TR_ACCENT]", "#define ACC  RGB565(255, 170, 40)"),
            ("gallery_app.c", "    theme_init();\n", ""),
            ("gallery_app.c", "if (k == GT_CHANGED) { theme_set(l.theme, l.pol, l.pal);", "if (k == GT_CHANGED) { "),
            ("gallery_app.c", "if (r == GP_OPEN_THEME) {", "if (r == GP_OPEN_HELP) {"),
            ("gallery_app.c", "const uint32_t np = (n - cur_first) / pp;\n    if (np != page) { sel = n;", "const uint32_t np = (n - cur_first) / (pp + 1u);\n    if (np != page) { sel = n;"),
            ("gallery_app.c", "n_on_page = left < pp ? left : pp;", "n_on_page = pp;"),
            ("gallery_app.c", "else if (edge & KEY_L1) { const uint32_t rel = sel - cur_first; select_to(cur_first + (rel >= pp ? rel - pp : 0u)); }", "else if (edge & KEY_L1) { const uint32_t rel = sel - cur_first; select_to(cur_first + (rel >= pp ? rel - pp : rel)); }"),
            ("gallery_app.c", "if (land_canvas(&pic, total, show_progress) && land_canvas(&pic, total, show_progress)) return 5;", "if (land_canvas(&pic, total, show_progress)) return 5;"),
            ("gallery_app.c", "if (land_canvas(&pic, total, show_progress) && land_canvas(&pic, total, show_progress)) return 5;", "if (land_canvas(&pic, total, show_progress) && land_canvas(&pic, total, show_progress) && land_canvas(&pic, total, show_progress)) return 5;"),
            ("gallery_app.c", "if (!read_bytes(SLOT_PIC, 0u, 4096u, h4) && !read_bytes(SLOT_PIC, 0u, 4096u, h4)) return 2;", "if (!read_bytes(SLOT_PIC, 0u, 4096u, h4)) return 2;"),
            ("gallery_app.c", "    info.err = r ? open_msg[r] : 0;\n", "    info.err = r ? open_msg[r] : 0;\n    if (r == 0) remember(i);\n"),
            ("gallery_app.c", "if (gv_display_y(z_wy, rlo, rhi, &y) &&", "if ((y = z_wy, rhi >= rlo + GV_SCREEN_H) &&"),
            ("gallery_app.c", "REG_WR(R_LAND_BASE, GAL_ZOOM_BASE + c * (GV_CHUNK / 2u)); REG_WR(R_LAND_CTRL, 1u);", "REG_WR(R_LAND_BASE, GAL_ZOOM_BASE + c * (GV_CHUNK / 2u));"),
            ("gallery_app.c", "len = rest < GV_CHUNK ? rest : GV_CHUNK,", "len = GV_CHUNK,"),
            ("gallery_app.c", "if (edge & (KEY_A | KEY_B)) { zoom_goto(-1); continue; }", "if (edge & KEY_A) { zoom_goto(-1); continue; }"),
            ("gallery_app.c", "REG_WR(R_VIEW_STRIDE, GAL_LINE_WORDS16); REG_WR(R_VIEW_BASE, GAL_PLANE_BASE);              /* the fit plane", "REG_WR(R_VIEW_STRIDE, GAL_LINE_WORDS16);              /* the fit plane"),
            ("gallery_app.c", "if (!((keys & KEY_SELECT) && (edge & (KEY_LEFT | KEY_RIGHT)))) {", "if (1) {"),
            ("gallery_app.c", "tick = 1; held = (dx || dy) ? held + 1u : 0u; }", "tick = 1; held = held + 1u; }"),
            ("gallery_app.c", "next_tick = cycles() + ZOOM_FRAME; held = (dx || dy) ? held + 1u : 0u;", "next_tick = cycles() + ZOOM_FRAME; held = held + 1u;"),
            ("gallery_app.c", "zoom_leave(); zoom_open(mk, sx2, sy2); return 1;", "zoom_leave(); zoom_open(0, sx2, sy2); return 1;"),
            ("gallery_app.c", "REG_WR(R_VIEW_STRIDE, z_stride_hw); REG_WR(R_VIEW_BASE, gv_plane_base(GAL_ZOOM_BASE, z_stride_hw, z_wx, y));", "REG_WR(R_VIEW_STRIDE, z_stride_hw); REG_WR(R_VIEW_BASE, gv_plane_base(GAL_ZOOM_BASE, z_stride_hw, z_wx & ~7, y));"),
            ("gallery_app.c", "if (!zoom_land(c)) return 1;", "if (!zoom_land(c)) { }"),
            ("gallery_app.c", "if (gv_next_chunk(zl.stride, zl.len, z_nch, z_lo, z_hi, (uint32_t)z_wy, diry, &c)) {", "if (gv_next_chunk(zl.stride, zl.len, z_nch, z_lo, z_hi, (uint32_t)z_wy, 0, &c)) {"),
            ('gallery_app.c', 'if (zoom_pass(dx, dy, held, tick && (dx || dy), tick)) {', 'if (zoom_pass(dx, dy, held, (dx || dy), tick)) {'),
            ("gallery_app.c", "if (t_recentre && gv_win_tile(t_wx[t_front], t_wy[t_front], tx, ty, &fc, &fr) && t_res[t_front][fr * GV_WIN_TX + fc]) {", "if (0 && gv_win_tile(t_wx[t_front], t_wy[t_front], tx, ty, &fc, &fr) && t_res[t_front][fr * GV_WIN_TX + fc]) {"),
            ('gallery_app.c', 'if (!z_shown) { if (gv_screen_ready(&z_stop, z_wx, z_wy, t_wx[t_front], t_wy[t_front], t_res[t_front])) tile_show(z_wx, z_wy); return 0; }', 'if (!z_shown) { tile_show(z_wx, z_wy); return 0; }'),
            ("gallery_app.c", "t_dst_hw = win_base(tw) + gv_tile_hw(col, row);", "t_dst_hw = win_base(t_front) + gv_tile_hw(col, row);"),
            ("gallery_app.c", "#define COPY_PER_PASS 64u", "#define COPY_PER_PASS 1000u"),
            ('gallery_app.c', 'else if (!gv_window_ok(&z_stop, t_wx[b], t_wy[b], px, py)) { gv_window_lead(&z_stop, px, py, lx, ly, &t_wx[b], &t_wy[b]); res_clear(b); }', 'else if (0) { }'),
            ('gallery_app.c', 'if (gv_window_ok(&z_stop, t_wx[t_front], t_wy[t_front], px, py)) t_recentre = 0u;', 'if (0) t_recentre = 0u;'),
            ("gallery_app.c", "gv_map_corner(sf, fox, foy, zoom_scale(k), tox, toy, x, y, &nx, &ny);", "gv_map_corner(sf, fox, foy, zoom_scale(k), tox, toy, 0, 0, &nx, &ny);"),
            ("gallery_app.c", "if (zn > 0u) { zoom_goto((int)zn - 1);", "if (zn > 0u) { zoom_goto(0);"),
            ("gallery_app.c", "    const uint32_t tc = cycles();\n    for (int attempt = 0; attempt < 2; attempt++) {\n        REG_WR(R_LAND_BASE, GAL_STAGE);", "    const uint32_t tc = cycles();\n    for (int attempt = 0; attempt < 1; attempt++) {\n        REG_WR(R_LAND_BASE, GAL_STAGE);"),
            ('gallery_app.c', '(z_shown ? gv_screen_ready(&z_stop, z_sx, z_sy, t_wx[b2], t_wy[b2], t_res[b2]) : gv_screen_ready(&z_stop, px, py, t_wx[b2], t_wy[b2], t_res[b2]))', 'gv_window_ok(&z_stop, t_wx[b2], t_wy[b2], px, py)'),
            ('gallery_app.c', 'if (z_shown) { px = z_sx + clampi(z_wx - z_sx, -SLACK, SLACK); py = z_sy + clampi(z_wy - z_sy, -SLACK, SLACK); gv_clamp(&z_stop, &px, &py); }', 'if (z_shown) { }'),
            ('gallery_app.c', 'const int32_t cx = (t_recentre && z_shown) ? z_sx : px, cy = (t_recentre && z_shown) ? z_sy : py;', 'const int32_t cx = px, cy = py;'),
            ('gallery_app.c', 'if (t_front != t_shown_win) { tile_show(z_sx, z_sy); return 0; }', 'if (t_front != t_shown_win) { return 0; }'),
            ('gallery_app.c', 'if (tick && (z_sx != z_wx || z_sy != z_wy)) {', 'if (0) {'),
            ('gallery_app.c', '#define FOLLOW_STEP 16', '#define FOLLOW_STEP 64'),
            ("gallery_app.c", "if (edge & KEY_L1) { zoom_goto(zk > 0 ? zk - 1 : -1);", "if (edge & KEY_L1) { zoom_goto(zk > 0 ? zk - 1 : 0);"),
            ("gallery_app.c", "info.land_ms = (cycles() - tc) / (CLK_HZ / 1000u); return 1; }\n    }\n    return 0;\n}\n/* One pass of the raster", "info.land_ms = (cycles() - tc) / (CLK_HZ / 100u); return 1; }\n    }\n    return 0;\n}\n/* One pass of the raster"),
            ("gallery_app.c", "t_copy_cyc += cycles() - tc; info.copy_ms = t_copy_cyc / (CLK_HZ / 1000u); } else t_copy_cyc += cycles() - tc;", "info.copy_ms = (cycles() - tc) / (CLK_HZ / 1000u); } else t_copy_cyc += cycles() - tc;"),
            ("gallery_app.c", "if (z_first) { z_first = 0u; info.zoom_ms = (cycles() - z_t0) / (CLK_HZ / 1000u); }", "if (z_first) { z_first = 0u; info.zoom_ms = 0u; }"),
            ("gallery_app.c", "if (w) gfx_rect(LDR_X, LDR_Y, w > LDR_W ? LDR_W : w, LDR_H, ACC);", "if (w) gfx_rect(LDR_X, LDR_Y, LDR_W, LDR_H, ACC);"),
            ("gallery_app.c", "gfx_rect(LDR_X, LDR_Y, LDR_W, LDR_H, LDR_TRACK);\n", "\n"),
            ("gallery_app.c", "gfx_rect(LDR_X, LDR_Y, LDR_W, LDR_H, LDR_TRACK);", "gfx_rect(LDR_X, LDR_Y, LDR_W, LDR_H, DIM);"),
            ("gallery_app.c", "#define LDR_H 2u", "#define LDR_H 4u"),
            ("gallery_app.c", "*q = 0; say(b, ACC); loader_bar(c + len, total); }", "*q = 0; say(b, ACC); }"),
            ("gallery_app.c", 'if (show_progress) { say("OPENING", ACC); loader_bar(0u, 1u); }', 'if (show_progress) { say("OPENING", ACC); }'),
            ("gallery_app.c", "loader_bar(c + len, total); }", "loader_bar(c, total); }"),
            ("gallery_app.c", "#define LDR_Y 178u", "#define LDR_Y 200u"),
            ("gallery_app.c", "gv_window_lead(&z_stop, px, py, lx, ly, &t_wx[b], &t_wy[b]); res_clear(b); t_recentre = 1u;", "gv_window_lead(&z_stop, px, py, 0, 0, &t_wx[b], &t_wy[b]); res_clear(b); t_recentre = 1u;"),
            ("gallery_app.c", "gv_window_lead(&z_stop, px, py, lx, ly, &t_wx[b], &t_wy[b]); res_clear(b); }", "gv_window_lead(&z_stop, px, py, 0, 0, &t_wx[b], &t_wy[b]); res_clear(b); }"),
            ("gallery_app.c", "const int lx = dx ? dx : (z_wx > px ? 1 : z_wx < px ? -1 : 0), ly = dy ? dy : (z_wy > py ? 1 : z_wy < py ? -1 : 0);", "const int lx = -dx, ly = -dy;"),
            ("gallery_app.c", "const int lx = dx ? dx : (z_wx > px ? 1 : z_wx < px ? -1 : 0), ly = dy ? dy : (z_wy > py ? 1 : z_wy < py ? -1 : 0);", "const int lx = dx, ly = dy;"),
            ("gallery_app.c", "    zb_end();                                                                                /* the fit plane's own pixels back under the loader bar */\n", "\n"),
            ("gallery_app.c", "static void zoom_shown_now(void) { zb_end(); if (z_first) {", "static void zoom_shown_now(void) { if (z_first) {"),
            ("gallery_app.c", "    zb_begin();\n}", "}"),
            ("gallery_app.c", "static void zb_begin(void) { if (zb_on) return; zb_copy(0); zb_on = 1u;", "static void zb_begin(void) { if (zb_on) return; zb_on = 1u;"),
            ("gallery_app.c", "zb_write(0u, 160u, 0u); }", "}"),
            ("gallery_app.c", "if (px > zb_px) { zb_write(zb_px, px, px); zb_px = px; }", "if (px > zb_px) { zb_px = px; }"),
            ("gallery_app.c", "zb_set(whole, on);", "zb_set(0u, on);"),
            ("gallery_app.c", "zb_set(z_hi - z_lo, need); }", "zb_set(0u, need); }"),
            ("gallery_app.c", "#define ZB_Y 358u", "#define ZB_Y 300u"),
            ("gallery_app.c", "#define ZB_HW0 480u ", "#define ZB_HW0 482u "),
            ("gallery_app.c", "const uint32_t o = k * (ZB_HW / 2u), save = GAL_BARSAVE + r * ZB_HW;", "const uint32_t o = k * (ZB_HW / 2u), save = GAL_BARSAVE;"),
            ("gallery_app.c", "const uint16_t c = px < fillpx ? ac : tr;", "const uint16_t c = px < fillpx ? tr : ac;") if False else ("gallery_app.c", "px < fillpx ? ac : tr;", "px < fillpx ? tr : tr;"),
            ("gallery_app.c", "gv_tiles_needed(&z_stop, cx, cy, t_wx[tw], t_wy[tw], lx, ly, need)", "gv_tiles_needed(&z_stop, cx, cy, t_wx[tw], t_wy[tw], 0, 0, need)"),
            ("gallery_app.c", "gv_tiles_needed(&z_stop, cx, cy, t_wx[tw], t_wy[tw], lx, ly, need)", "gv_tiles_needed(&z_stop, cx, cy, t_wx[tw], t_wy[tw], dx, dy, need)"),
            ("gallery_app.c", "{ map_ops(2, dx0, dy0, dw0, dh0); gfx_fence(); zoom_reshow(); return 0; }", "{ gfx_fence(); zoom_reshow(); return 0; }"),
            ("gallery_app.c", "{ map_ops(2, dx0, dy0, dw0, dh0); gfx_fence(); zoom_reshow(); return 0; }", "{ map_ops(2, dx0, dy0, dw0, dh0); gfx_fence(); return 0; }"),
            ("gallery_app.c", "        else if (op == 1) gfx_blit_raw(GAL_BARSAVE, pb, MAP_SPR + d, poff + d, w);", "        else if (op == 1) { }"),
            ("gallery_app.c", "        if (op == 0) gfx_blit_raw(pb, GAL_BARSAVE, poff + d, MAP_SAVE + soff + d, w);", "        if (op == 0) { }"),
            ("gallery_app.c", "map_ops(2, m_rx, m_ry, m_rw, m_rh)", "map_ops(2, m_rx, m_ry, m_rw, m_rh)") if False else ("gallery_app.c", "map_ops(2, dx0, dy0, dw0, dh0); map_ops(0, m_rx, m_ry, m_rw, m_rh);", "map_ops(0, m_rx, m_ry, m_rw, m_rh);"),
            ("gallery_app.c", "if (dx || dy) { const int32_t st = gv_speed(held); m_rx += dx * st; m_ry += dy * st; gv_map_clamp((int32_t)pic.rx, (int32_t)pic.ry, (int32_t)pic.rw, (int32_t)pic.rh, m_rw, m_rh, &m_rx, &m_ry); redraw = 1; }", "if (dx || dy) { const int32_t st = gv_speed(held); m_rx += dx * st; m_ry += dy * st; redraw = 1; }"),
            ("gallery_app.c", "if ((edge & KEY_L1) && mk > 0) { const int32_t cx = m_rx + m_rw / 2, cy = m_ry + m_rh / 2; mk--; map_rect_of(mk, cx, cy); redraw = 1; }", "if ((edge & KEY_L1) && mk > 0) { }"),
            ("gallery_app.c", "if ((edge & KEY_R1) && (uint32_t)mk + 1u < zn) { const int32_t cx = m_rx + m_rw / 2, cy = m_ry + m_rh / 2; mk++; map_rect_of(mk, cx, cy); redraw = 1; }", "if ((edge & KEY_R1) && (uint32_t)mk + 1u < zn) { const int32_t cx = m_rx + m_rw / 2, cy = m_ry + m_rh / 2; mk++; redraw = 1; }"),
            ("gallery_app.c", "m_rw = rw; m_rh = rh; m_rx = cx - rw / 2; m_ry = cy - rh / 2;", "m_rw = rw; m_rh = rh; m_rx = cx; m_ry = cy;"),
            ("gallery_app.c", "if ((edge & KEY_START) && z_shown) {", "if (edge & KEY_START) {"),
            ("gallery_app.c", "if ((edge & KEY_START) && z_shown) { map_mode();", "if (0 && (edge & KEY_START) && z_shown) { map_mode();"),
            ("gallery_app.c", "const int32_t px = side ? rx + rw - GV_RECT_T : rx;", "const int32_t px = side ? rx + rw - GV_RECT_T - 2 : rx;"),
            ("gallery_app.c", "const int32_t row = e < 2 ? ry + e : ry + rh - 4 + e;", "const int32_t row = e < 2 ? ry + e : ry + rh - 3 + e;"),
            ("gallery_app.c", "info.flip_ms = (cycles() - t_rc_t0) / (CLK_HZ / 1000u);", "info.flip_ms = 0u;"),
            ("gallery_fmt.c", "return h->entries_off + i * GX_ENTRY_BYTES;" if False else "uint32_t gx_thumb_off(const gx_head_t *h, uint32_t thumb) { return h->atlas_off + thumb * h->thumb_bytes; }", "uint32_t gx_thumb_off(const gx_head_t *h, uint32_t thumb) { return h->atlas_off + thumb * (h->thumb_bytes - 2u); }")]
    muts += [                                                                                                      # pan frames follow the time (version 1) and the grid's held keys
        ("gallery_app.c", "while (late >= ZOOM_FRAME && tick < ZOOM_CATCHUP) {", "while (0) {"),
        ("gallery_app.c", "#define ZOOM_CATCHUP 4 ", "#define ZOOM_CATCHUP 1 "),
        ("gallery_app.c", "uint32_t late = now - next_tick; tick = 1; next_tick += ZOOM_FRAME;", "uint32_t late = now - next_tick; tick = 1; next_tick = now + ZOOM_FRAME;"),
        ("gallery_app.c", "if ((int32_t)(now - next_tick) >= 0) next_tick = now + ZOOM_FRAME;                       /* far behind", "if (0) next_tick = now + ZOOM_FRAME;                       /* far behind"),
        ("gallery_app.c", "const int32_t fs = FOLLOW_STEP * tick,", "const int32_t fs = FOLLOW_STEP,"),
        ("gallery_app.c", "if (moving) for (int f = 0; f < tick; f++) gv_move(&z_stop, &z_wx, &z_wy, dx, dy, held);", "if (moving) gv_move(&z_stop, &z_wx, &z_wy, dx, dy, held);"),
        ("gallery_app.c", "held = (dx || dy) ? held + (uint32_t)tick : 0u; zt_frames", "held = (dx || dy) ? held + 1u : 0u; zt_frames"),
        ("gallery_app.c", "info.zt_moved += (uint32_t)tick; }\n        else if (nx != z_sx", "info.zt_moved++; }\n        else if (nx != z_sx"),
        ("gallery_app.c", "if (!edge && rep_key && (keys & rep_key)", "if (rep_key && (keys & rep_key)"),
        ("gallery_app.c", "{ dk = rep_key; fresh = 0u; }", "{ dk = rep_key; fresh = 1u; }"),
        ("gallery_app.c", "if (fresh) rep_key = dk;", ""),
        ("gallery_app.c", "#define REP_DELAY (CLK_HZ * 2u / 5u)", "#define REP_DELAY 0u"),
        ("gallery_app.c", "#define REP_RATE (CLK_HZ / 12u)", "#define REP_RATE (CLK_HZ * 4u)"),
        ("gallery_app.c", "dk == KEY_UP ? 2u : 3u", "dk == KEY_UP ? 3u : 2u"),
        ("gallery_app.c", "rep_next = cycles() + (fresh ? REP_DELAY : REP_RATE);", "rep_next = now + (fresh ? REP_DELAY : REP_RATE);"),
    ]
    muts += [                                                                                                      # where a version 1 picture sits on the fit canvas
        ("gallery_app.c", "pic.rx = (uint16_t)fx; pic.ry = (uint16_t)fy; pic.rw = (uint16_t)fw; pic.rh = (uint16_t)fh; }", "pic.rx = v1l.rx; pic.ry = v1l.ry; pic.rw = v1l.rw; pic.rh = v1l.rh; }"),
        ("gallery_app.c", "gv_fit_rect(v1f.width, v1f.height, GT_CANVAS_W, GT_CANVAS_H, &fx, &fy, &fw, &fh);", "gv_fit_rect(v1f.height, v1f.width, GT_CANVAS_W, GT_CANVAS_H, &fx, &fy, &fw, &fh);"),
        ("gallery_app.c", "gv_fit_rect(v1f.width, v1f.height, GT_CANVAS_W, GT_CANVAS_H, &fx, &fy, &fw, &fh);", "gv_fit_rect(v1f.width, v1f.height, GT_CANVAS_H, GT_CANVAS_W, &fx, &fy, &fw, &fh);"),
        ("gallery_app.c", "pic.rx = (uint16_t)fx; pic.ry = (uint16_t)fy;", "pic.rx = (uint16_t)fy; pic.ry = (uint16_t)fx;"),
    ]
    muts += [                                                                                                      # an index kept on the card (the block cache)
        ("gallery_app.c", "if (v1c_age[i] < v1c_age[victim]) victim = i;", "if (v1c_age[i] > v1c_age[victim]) victim = i;"),
        ("gallery_app.c", "if (v1c_pos[i] == pos) { v1c_age[i] = ++v1c_clock; return v1z_idx + i * v1c_bs; }", "if (v1c_pos[i] == pos) { return v1z_idx + i * v1c_bs; }"),
        ("gallery_app.c", "uint32_t n = v1c_bs - (a - bp); if (n > 12u - done) n = 12u - done;", "uint32_t n = 12u - done;"),
        ("gallery_app.c", "const uint32_t a = off + done, bp = a - a % v1c_bs;", "const uint32_t a = off + done, bp = a - a % 4096u;"),
        ("gallery_app.c", "out[done + k] = b[a - bp + k];", "out[done + k] = b[k];"),
        ("gallery_app.c", "v1c_pos[victim] = pos; v1c_age[victim] = ++v1c_clock; v1c_reads++;", "v1c_age[victim] = ++v1c_clock; v1c_reads++;"),
        ("gallery_app.c", "v1z.idx = 0; v1z.fetch = v1_idx_entry; v1z.fctx = 0; v1c_clock = 0u; for (uint32_t i = 0; i < 16u; i++) { v1c_pos[i] = 0xFFFFFFFFu; v1c_age[i] = 0u; }", "v1z.idx = 0; v1z.fetch = v1_idx_entry; v1z.fctx = 0; v1c_clock = 0u;"),
        ("gallery_app.c", "ir = v1z.idx_len <= V1Z_VERIFY_MAX ? tgy_verify_index(&v1fz, &v1z, v1_read_idx, 0, v1z_idx, V1Z_IDX_MAX) : TH_OK;", "ir = TH_OK;"),
        ("gallery_app.c", "const uint32_t end = v1z.idx_off + v1z.idx_len, len = end - pos < v1c_bs ? end - pos : v1c_bs;", "const uint32_t end = v1z.idx_off + v1z.idx_len, len = v1c_bs;"),
    ]
    muts += [                                                                                                      # the details page's histograms
        ("gallery_app.c", "gfx_rect(x + DET_BX + i, y + DET_PH - DET_BB - bh, step == 1u ? 1u : step - 1u, bh, bar_col[h]);", "gfx_rect(x + DET_BX + i + 1u, y + DET_PH - DET_BB - bh, step == 1u ? 1u : step - 1u, bh, bar_col[h]);"),
        ("gallery_app.c", "gfx_rect(x + DET_BX + i, y + DET_PH - DET_BB - bh, step == 1u ? 1u : step - 1u, bh, bar_col[h]);", "gfx_rect(x + DET_BX + i, y + DET_PH - DET_BB - bh + 1u, step == 1u ? 1u : step - 1u, bh, bar_col[h]);"),
        ("gallery_app.c", "gfx_rect(x + DET_BX + i, y + DET_PH - DET_BB - bh, step == 1u ? 1u : step - 1u, bh, bar_col[h]);", "gfx_rect(x + DET_BX + i, y + DET_PH - DET_BB - bh, step == 1u ? 2u : step, bh, bar_col[h]);"),
        ("gallery_app.c", "{ 0xFFFFu, 0xF800u, 0x07E0u, 0x001Fu }", "{ 0xFFFFu, 0x07E0u, 0xF800u, 0x001Fu }"),
        ("gallery_app.c", "const uint32_t mx = hs_plot_max(hist, h), step = hist->is_fine ? 1u : 8u;", "const uint32_t mx = hs_plot_max(hist, 0u), step = hist->is_fine ? 1u : 8u;"),
        ("gallery_app.c", "gh_clear(h);" if False else "    hs_clear(h); const hs_rect_t rc = head.version == 1u ? (hs_rect_t){ 0u, 0u, HS_TW, 96u } : hs_rect(pw, ph);", "    const hs_rect_t rc = head.version == 1u ? (hs_rect_t){ 0u, 0u, HS_TW, 96u } : hs_rect(pw, ph);"),
        ("gallery_app.c", "if (!hist) { gfx_text(x + 6u, y + 34u, GD_HIST_NOTE, DIM, BG); continue; }", ""),
        ("gallery_app.c", "if (hs_inside(&rc, row, col)) hs_add(h, (uint16_t)(v & 0xFFFFu));", "hs_add(h, (uint16_t)(v & 0xFFFFu));"),
        ("gallery_app.c", "if (hs_inside(&rc, row, col + 1u)) hs_add(h, (uint16_t)(v >> 16));", "if (hs_inside(&rc, row, col)) hs_add(h, (uint16_t)(v >> 16));"),
        ("gallery_app.c", ": hs_rect(pw, ph);", ": hs_rect(ph, pw);"),
        ("gallery_app.c", "thumb_hist(gx_thumb_off(&head, e->thumb), &det_hist, e->width, e->height)", "thumb_hist(gx_thumb_off(&head, e->thumb), &det_hist, 0u, 0u)"),
        ("gallery_app.c", "row = px / HS_TW, col = px % HS_TW;", "row = px / HS_TW, col = (px % HS_TW) + 1u;"),
        ("gallery_app.c", "gfx_text(6, 324, hist ? (from_dec ? GD_HIST_SRC_DEC : GD_HIST_SRC) : GD_HIST_NOTE, DIM, BG);", ""),
    ]
    muts += [                                                                                                      # the version 1 path (open_v1, the hardware decoder)
        ("gallery_app.c", "REG_WR(R_LAND_BASE, GAL_LAND_BASE); REG_WR(R_LAND_CTRL, 1u);", "REG_WR(R_LAND_BASE, GAL_LAND_BASE + 2u); REG_WR(R_LAND_CTRL, 1u);"),
        ("gallery_app.c", "if (!tgy_hw_present()) return 7;", ""),
        ("gallery_app.c", "if (r == TH_HWERR) return 9;", "if (r == TH_HWERR) return 8;"),
        ("gallery_app.c", "if (r == TH_CRC || r == TH_MAGIC || r == TH_VERSION || r == TH_SHORT) return 3;", ""),
        ("gallery_app.c", "h4[4] == 1u && h4[5] == 0u) { const int rv = open_v1(show_progress);", "h4[4] == 1u && h4[5] == 7u) { const int rv = open_v1(show_progress);"),
        ("gallery_app.c", ": tgy_hw_show(&v1f, &v1l, GAL_PLANE_BASE * 2u, v1_tick, &show);", ": tgy_hw_show(&v1f, &v1l, GAL_LAND_BASE * 2u, v1_tick, &show);"),
        ("gallery_app.c", "const uint32_t fsize = (uint32_t)h4[48] | (uint32_t)h4[49] << 8 | (uint32_t)h4[50] << 16 | (uint32_t)h4[51] << 24, base = GAL_LAND_BASE * 2u;", "const uint32_t fsize = (uint32_t)h4[48] | (uint32_t)h4[49] << 8 | (uint32_t)h4[50] << 16 | (uint32_t)h4[51] << 24, base = GAL_LAND_BASE * 2u + 2u;"),
    ]
    muts += [                                                                                                      # Gallery 1.0: folders, grid sizes, rounded corners, selection ring, resume in a folder
        ("gallery_app.c", "ent[k].thumb = f.cover;", "ent[k].thumb = f.first + 1u;"),
        ("gallery_app.c", "if (GR.rad && level == 0u) gfx_corners(x, y, GR.cw, GR.ch, GR.rad, BG);", "if (0) gfx_corners(x, y, GR.cw, GR.ch, GR.rad, BG);"),
        ("gallery_app.c", "gfx_corners(x, y, GR.cw, GR.ch, GR.rad, BG);\n        }\n    }", "}\n    }"),
        ("gallery_app.c", "if (GR.rad) { gfx_corners(x, y, w, h, GR.rad, c); if (c != BG) gfx_corners(x - t, y - t, w + 2u * t, h + 2u * t, GR.rad + t, BG); }", "if (GR.rad) { if (c != BG) gfx_corners(x - t, y - t, w + 2u * t, h + 2u * t, GR.rad + t, BG); }"),
        ("gallery_app.c", "if (GR.rad) { gfx_corners(x, y, w, h, GR.rad, c); if (c != BG) gfx_corners(x - t, y - t, w + 2u * t, h + 2u * t, GR.rad + t, BG); }", "if (GR.rad) { gfx_corners(x, y, w, h, GR.rad, c); }"),
        ("gallery_app.c", "#define STASH_Y 384u", "#define STASH_Y 100u"),
        ("gallery_app.c", "{ 5, 4, 72, 72, 6, 78, 4, 76, 2, 8, 0 }", "{ 5, 4, 72, 72, 6, 78, 4, 76, 2, 8, 1 }"),
        ("gallery_app.c", "{ 6, 4, 64, 64, 2, 66, 10, 74, 2, 6, 1 }", "{ 6, 4, 64, 64, 2, 66, 10, 74, 2, 6, 2 }"),
        ("gallery_app.c", "{ 8, 6, 48, 48, 1, 49, 3, 51, 1, 4, 2 }", "{ 8, 6, 48, 48, 1, 49, 3, 51, 2, 4, 2 }"),
        ("gallery_app.c", "gfx_dsblit(STASH_X, STASH_Y, x, y, 96u, 96u, GR.ratio, GR.ratio);", "gfx_dsblit(STASH_X, STASH_Y, x, y, 96u, 96u, GR.ratio, 0u);"),
        ("gallery_app.c", "if (i < f->first) hi = mid; else if (i >= f->first + f->count) lo = mid + 1u; else { *k = mid; return 1; }", "if (i < f->first) lo = mid + 1u; else if (i >= f->first + f->count) lo = mid + 1u; else { *k = mid; return 1; }"),
        ("gallery_app.c", "if (i < f->first) hi = mid; else if (i >= f->first + f->count) lo = mid + 1u; else { *k = mid; return 1; }", "if (i < f->first) hi = mid; else if (i >= f->first + f->count) hi = mid; else { *k = mid; return 1; }"),
        ("gallery_app.c", "static uint32_t grid_setting(void) { return gset.grid & 3u; }", "static uint32_t grid_setting(void) { return 0u; }"),
        ("gallery_app.c", "if (level != 0u || head.version == 0u || gi == grid_setting()) return;", "return;"),
        ("gallery_app.c", "set_grid(grid_setting()); page = (sel - cur_first) / pp; loaded_page = 0xFFFFFFFFu;", "set_grid(grid_setting()); page = 0u; loaded_page = 0xFFFFFFFFu;"),
        ("gallery_app.c", "else if (edge & KEY_B) { if (level == 0u && head.folder_count > 1u) { enter_folders(); draw_page(); } rep_key = 0u; }", "else if (0) { }"),
        ("gallery_app.c", "if (head.folder_count > 1u) enter_folders(); else enter_folder(&f, 0u);", "enter_folder(&f, 0u);"),
        ("gallery_app.c", "sel = cur_folder; page = sel / pp; loaded_page = 0xFFFFFFFFu;", "sel = 0u; page = 0u; loaded_page = 0xFFFFFFFFu;"),
        ("gallery_app.c", "level = 0u; cur_first = f->first; cur_count = f->count; cur_folder = k;", "level = 0u; cur_first = f->first; cur_count = f->count; cur_folder = 0u;"),
        ("gallery_app.c", "gfx_rect(x, y + GR.ch - 16u, GR.cw, 16u, dark_bg()); gfx_text(x + 2u, y + GR.ch - 16u, b, FG, dark_bg());", "gfx_text(x + 2u, y + GR.ch - 16u, b, FG, dark_bg());"),
        ("gallery_app.c", "*q++ = ' '; q = put_u(q, n); *q = 0;", "*q = 0;"),
        ("gallery_app.c", "if (gx_parse_entry_h(&head, buf + k * head.entry_bytes, head.entry_bytes, &ent[k])) return 0;", "if (gx_parse_entry_h(&head, buf + k * 160u, head.entry_bytes, &ent[k])) return 0;"),
    ]
    muts += [                                                                                                      # the turned view (K7)
        ("gallery_app.c", ": gr_turn(rot_o, 1); mask_i = 0u;", ": gr_turn(rot_o, 0); mask_i = 0u;"),
        ("gallery_app.c", "(rot_pan + ROT_STEP > rot_pan_max ? rot_pan_max : rot_pan + ROT_STEP)", "(rot_pan + ROT_STEP)"),
        ("gallery_app.c", "(rot_pan > ROT_STEP ? rot_pan - ROT_STEP : 0u)", "(rot_pan - ROT_STEP)"),
        ("gallery_app.c", "rot_view(); prev = REG_RD(R_INPUT) & 0xFFFFu; held = 0u; next_tick = cycles(); continue;", "prev = REG_RD(R_INPUT) & 0xFFFFu; held = 0u; next_tick = cycles(); continue;"),
        ("gallery_app.c", "sel = n; rot_o = 0u;", "sel = n;"),
        ("gallery_app.c", "            REG_WR(R_VIEW_STRIDE, GAL_LINE_WORDS16); REG_WR(R_VIEW_BASE, GAL_PLANE_BASE);                                              /* the upright plane (the previous picture may have been turned: the view pointed at the turned plane) */\n", ""),
        ("gallery_app.c", "    rot_o = 0u; mask_i = 0u; mask_painted = 0u;\n    const int r = open_timed(sel, 1);", "    mask_i = 0u; mask_painted = 0u;\n    const int r = open_timed(sel, 1);"),
        ("gallery_app.c", "rot_pan = rot_pan_max / 2u;", "rot_pan = 0u;"),
        ("gallery_app.c", "px = (GT_CANVAS_W - pw) / 2u,", "px = 0u,"),
        ("gallery_app.c", "rot_clear(rows); rot_pan_max", "rot_pan_max"),
        ("gallery_app.c", "gr_dims(o, cw, ch, &pw, &ph);", "pw = cw; ph = ch;"),
        ("gallery_app.c", "if (rot_o == 0u) { REG_WR(R_VIEW_STRIDE, GAL_LINE_WORDS16); REG_WR(R_VIEW_BASE, GAL_PLANE_BASE); } else rot_view();", "rot_view();"),
        ("gallery_app.c", "            if (!v1_on) continue;\n            rot_o = (keys", "            rot_o = (keys"),
    ]
    muts += [                                                                                                      # the details pages
        ("gallery_app.c", "if (head.version == 1u && e->details_len) {", "if (0) {"),
        ("gallery_app.c", "read_bytes(SLOT_IDX, gx_details_abs(&head, e), e->details_len, db)", "read_bytes(SLOT_IDX, head.details_off, e->details_len, db)"),
        ("gallery_app.c", "gd_meta_parse(db, e->details_len, &m);", "gd_meta_parse(db, 0u, &m);"),
        ("gallery_app.c", "i == p->cur ? ACC : DIM);", "DIM);"),
        ("gallery_app.c", "    gfx_text(x, 2, b, DIM, BG);\n", ""),
        ("gallery_app.c", "const uint32_t n = id == GDP_INFO ? gdi_rows(d, m, r) : gds_rows(m, r);", "const uint32_t n = gdi_rows(d, m, r);"),
        ("gallery_app.c", "gdp_t pg; gdp_open(&pg, &m);", "gdp_t pg; m.has &= ~GDM_SHOT; gdp_open(&pg, &m);"),
    ]
    muts += [                                                                                                      # the crop masks
        ("gallery_app.c", "mask_i = gm_next(mask_i);", "mask_i = mask_i + 1u;"),
        ("gallery_app.c", "mask_p = h > w; }", "mask_p = 0u; }"),
        ("gallery_app.c", "if ((edge & KEY_START) && !(mask_i && gm_can_turn(mask_i))) continue;", ""),
        ("gallery_app.c", "if (mask_painted) { const int r = view_reopen();", "if (0) { const int r = view_reopen();"),
        ("gallery_app.c", "mask_black(base, 0u, GT_CANVAS_W, 0u, y0); mask_black(base, 0u, GT_CANVAS_W, y1, rows);", "mask_black(base, 0u, GT_CANVAS_W, y1, rows);"),
        ("gallery_app.c", "mask_black(base, 0u, GT_CANVAS_W, y1, rows); mask_black(base, 0u, x0, y0, y1);", "mask_black(base, 0u, x0, y0, y1);"),
        ("gallery_app.c", "mask_black(base, 0u, x0, y0, y1); mask_black(base, x1, GT_CANVAS_W, y0, y1);", "mask_black(base, x1, GT_CANVAS_W, y0, y1);"),
        ("gallery_app.c", "mask_black(base, 0u, x0, y0, y1); mask_black(base, x1, GT_CANVAS_W, y0, y1);\n", "mask_black(base, 0u, x0, y0, y1);\n"),
        ("gallery_app.c", "    mask_painted = 0u;\n    if (rot_o == 0u) { REG_WR(R_VIEW_STRIDE, GAL_LINE_WORDS16);", "    if (rot_o == 0u) { REG_WR(R_VIEW_STRIDE, GAL_LINE_WORDS16);"),
        ("gallery_app.c", "sel = n; rot_o = 0u; mask_i = 0u; mask_painted = 0u;", "sel = n; rot_o = 0u; mask_painted = 0u;"),
        ("gallery_app.c", ": gr_turn(rot_o, 1); mask_i = 0u;", ": gr_turn(rot_o, 1);"),
        ("gallery_app.c", "*x = rot_px + (xa < xb ? xa : xb);", "*x = (xa < xb ? xa : xb);"),
        ("gallery_app.c", "const uint32_t base = rot_o ? GAL_ROT_BASE : GAL_PLANE_BASE, rows = rot_o ? rot_rows : GT_CANVAS_H;", "const uint32_t base = GAL_PLANE_BASE, rows = rot_o ? rot_rows : GT_CANVAS_H;"),
        ("gallery_app.c", "gm_crop(mask_i, mask_p, rx, ry, rw, rh, &x0, &y0, &x1, &y1);", "gm_crop(mask_i, 0u, rx, ry, rw, rh, &x0, &y0, &x1, &y1);"),
    ]
    muts += [                                                                                                      # the colour presets
        ("gallery_app.c", "static void lut_wait(void) { while (REG_RD(R_PIX_CTRL) & 0x100u) { } }", "static void lut_wait(void) { }"),
        ("gallery_app.c", "        REG_WR(R_PIX_CTRL, d);  ", "        (void)d;  "),
        ("gallery_app.c", "REG_WR(R_PIX_CTRL, d | (p != 0u ? 4u : 0u));", "REG_WR(R_PIX_CTRL, d);"),
        ("gallery_app.c", "if (p != 0u && p != lut_loaded) {", "if (p != 0u) {"),
        ("gallery_app.c", "if (p != 0u && p != lut_loaded) {", "if (p != 0u && lut_loaded == 0u) {"),
        ("gallery_app.c", "for (uint32_t ch = 0; ch < 3u; ch++)", "for (uint32_t ch = 0; ch < 2u; ch++)"),
        ("gallery_app.c", "for (uint32_t i = 0; i < 256u; i++) { lut_wait();", "for (uint32_t i = 0; i < 255u; i++) { lut_wait();"),
        ("gallery_app.c", "const uint32_t d = gset.dither % 3u,", "const uint32_t d = 0u * gset.dither,"),
        ("gallery_app.c", "lut_wait(); lut_loaded = p;", "lut_wait();"),
        ("gallery_app.c", "REG_WR(R_PIX_LUT, gl_word(ch, i, gl_value(p, ch, i)))", "REG_WR(R_PIX_LUT, gl_word(0u, i, gl_value(p, ch, i)))"),
        ("gallery_app.c", "REG_WR(R_PIX_LUT, gl_word(ch, i, gl_value(p, ch, i)))", "REG_WR(R_PIX_LUT, gl_word(ch, i, gl_value(p, 1u, i)))"),
    ]
    muts += [                                                                                                      # the histograms of the decoder
        ("gallery_app.c", "(uint32_t)(fx + fw), (uint32_t)(fy + fh));", "GT_CANVAS_W, (uint32_t)(fy + fh));"),
        ("gallery_app.c", "tgy_hw_hist_begin((uint32_t)fx, (uint32_t)fy,", "tgy_hw_hist_begin((uint32_t)fx, 0u,"),
        ("gallery_app.c", "if (hist_hw) { tgy_hw_hist_end(); hist_counted = (r == TH_OK); }", "if (hist_hw) { hist_counted = (r == TH_OK); }"),
        ("gallery_app.c", "if (rv == 0 && hist_counted) hist_for = i;", ""),
        ("gallery_app.c", "if (hist_for == sel && !tgy_hw_hist_read256(c, &n) && n != 0u)", "if (!tgy_hw_hist_read256(c, &n) && n != 0u)"),
        ("gallery_app.c", "(REG_RD(R_VIEW_CAPS) & 4u) != 0u;", "(REG_RD(R_VIEW_CAPS) & 2u) != 0u;"),
        ("gallery_app.c", "hist_ok = 1; from_dec = 1; }", "hist_ok = 1; from_dec = 0; }"),
        ("gallery_app.c", "hist_for = 0xFFFFFFFFu; hist_counted = 0u;", "hist_counted = 0u;"),
        ("tgy_hw.c", "REG_WR(R_HIST_CTRL, 2u); hist_wait();", "REG_WR(R_HIST_CTRL, 0u); hist_wait();"),
        ("tgy_hw.c", "static void hist_wait(void) { for (uint32_t i = 0; i < 100000u && (REG_RD(R_HIST_CTRL) & 0x100u); i++) { } }", "static void hist_wait(void) { }"),
        ("tgy_hw.c", "REG_WR(R_HIST_RECT1, (y1 << 16) | x1);", "REG_WR(R_HIST_RECT1, (y1 << 16) | x0);"),
        ("tgy_hw.c", "REG_WR(R_HIST_RECT0, (y0 << 16) | x0);", "REG_WR(R_HIST_RECT0, (0u << 16) | x0);"),
        ("tgy_hw.c", "out[k][i >> 3] += v & 0xFFFFFFu;", "out[k][i >> 4] += v & 0xFFFFFFu;"),
        ("tgy_hw.c", "REG_WR(R_HIST_IDX, (k << 8) | i);", "REG_WR(R_HIST_IDX, ((k ^ 1u) << 8) | i);"),
        ("tgy_hw.c", "if (g_hist_on) REG_WR(R_HIST_ORG, (y0 << 16) | x0);", "if (g_hist_on) REG_WR(R_HIST_ORG, (0u << 16) | x0);"),
        ("tgy_hw.c", "if (g_hist_on) REG_WR(R_HIST_ORG, (y0 << 16) | x0);", "if (0) REG_WR(R_HIST_ORG, (y0 << 16) | x0);"),
        ("tgy_hw.c", "REG_WR(R_HIST_CTRL, 1u); g_hist_on = 1u;", "REG_WR(R_HIST_CTRL, 1u); g_hist_on = 0u;"),
    ]
    muts += [                                                                                                      # the mirror keys and the Controls rows
        ("gallery_app.c", "rot_o = (keys & KEY_START) ? ((edge & KEY_Y) ? gr_flip(rot_o) : gr_mirror(rot_o)) : gr_turn(rot_o, 1);", "rot_o = gr_turn(rot_o, 1);"),
        ("gallery_app.c", "rot_o = (keys & KEY_START) ? ((edge & KEY_Y) ? gr_flip(rot_o) : gr_mirror(rot_o)) :", "rot_o = (keys & KEY_START) ? ((edge & KEY_Y) ? gr_mirror(rot_o) : gr_mirror(rot_o)) :"),
        ("gallery_app.c", "rot_o = (keys & KEY_START) ? ((edge & KEY_Y) ? gr_flip(rot_o) : gr_mirror(rot_o)) :", "rot_o = (keys & KEY_START) ? ((edge & KEY_Y) ? gr_flip(rot_o) : gr_flip(rot_o)) :"),
        ("gallery_app.c", "rot_o = (keys & KEY_START) ? ((edge & KEY_Y) ? gr_flip(rot_o) : gr_mirror(rot_o)) :", "rot_o = (keys & KEY_L1) ? ((edge & KEY_Y) ? gr_flip(rot_o) : gr_mirror(rot_o)) :"),
        ("gallery_app.c", "rot_o = (keys & KEY_START) ? ((edge & KEY_Y) ? gr_flip(rot_o) : gr_mirror(rot_o)) : gr_turn(rot_o, 1); mask_i = 0u;", "rot_o = (keys & KEY_START) ? ((edge & KEY_Y) ? gr_flip(rot_o) : gr_mirror(rot_o)) : gr_turn(rot_o, 1);"),
    ]
    muts += [                                                                                                      # zoom from a turned or masked view goes upright (owner decision B)
        ("gallery_app.c", "            if (zn == 0u) continue;                                                                                                   /* no zoom level", "            if (0) continue;                                                                                                   /* no zoom level"),
        ("gallery_app.c", "            rot_o = 0u; mask_i = 0u;\n            const int r = view_reopen(); if (r) { gal_view_off();", "            mask_i = 0u;\n            const int r = view_reopen(); if (r) { gal_view_off();"),
        ("gallery_app.c", "            rot_o = 0u; mask_i = 0u;\n            const int r = view_reopen(); if (r) { gal_view_off();", "            rot_o = 0u;\n            const int r = view_reopen(); if (r) { gal_view_off();"),
        ("gallery_app.c", "            rot_o = 0u; mask_i = 0u;\n            const int r = view_reopen(); if (r) { gal_view_off(); loaded_page = 0xFFFFFFFFu; draw_page(); say(open_msg[r], BAD); return; }\n        }", "            rot_o = 0u; mask_i = 0u;\n        }"),
        ("gallery_app.c", "if ((rot_o != 0u || mask_i != 0u) && (edge & (KEY_A | KEY_R1))) {", "if ((rot_o != 0u || mask_i != 0u) && (edge & KEY_A)) {"),
        ("gallery_app.c", "if ((rot_o != 0u || mask_i != 0u) && (edge & (KEY_A | KEY_R1))) {", "if (rot_o != 0u && (edge & (KEY_A | KEY_R1))) {"),
        ("gallery_app.c", "if ((rot_o != 0u || mask_i != 0u) && (edge & (KEY_A | KEY_R1))) {", "if (mask_i != 0u && (edge & (KEY_A | KEY_R1))) {"),
    ]
    muts += [                                                                                                      # the Settings page layout
        ("gallery_app.c", "gfx_text(6, PAGE_Y0 + 2u * GSP_ROWS * LINE_H + 4u, sp->row == GSP_RESUME", "gfx_text(6, PAGE_Y0 + 6u * LINE_H + 4u, sp->row == GSP_RESUME"),      # the old position: over the fourth row
    ]
    muts += [                                                                                                      # X: the colour filter in the viewer; the preview and the key reminder on the details pages; the 256 level histograms
        ("gallery_app.c", "gset.lut = (gset.lut + 1u) % GL_PRESETS; fx_apply(); last_act = cycles();", "gset.lut = (gset.lut + 1u); fx_apply(); last_act = cycles();"),
        ("gallery_app.c", "gset.lut = (gset.lut + 1u) % GL_PRESETS; fx_apply(); last_act = cycles();", "gset.lut = (gset.lut + 2u) % GL_PRESETS; fx_apply(); last_act = cycles();"),
        ("gallery_app.c", "gset.lut = (gset.lut + 1u) % GL_PRESETS; fx_apply(); last_act = cycles();", "gset.lut = (gset.lut + 1u) % GL_PRESETS; last_act = cycles();"),
        ("gallery_app.c", "if ((edge & KEY_X) && !(keys & KEY_START)) {", "if (edge & KEY_X) {"),
        ("gallery_app.c", "if ((edge & KEY_X) && !(keys & KEY_START)) {", "if ((edge & KEY_X) && (keys & KEY_START)) {"),
        ("gallery_app.c", "draw_preview(DET_VX, DET_VY_INFO, thumb_off);", "draw_preview(DET_VX, DET_VY_INFO + 1u, thumb_off);"),
        ("gallery_app.c", "draw_preview(DET_VX, DET_VY_HIST, thumb_off);", "draw_preview(DET_VX, DET_VY_HIST + 1u, thumb_off);"),
        ("gallery_app.c", "for (uint32_t i = 0; i < GD_KEYS_N; i++) gfx_text(DET_KX,", "for (uint32_t i = 0; i < GD_KEYS_N - 2u; i++) gfx_text(DET_KX,"),
        ("gallery_app.c", "*put_u(b, hist->n) = 0;", "*put_u(b, hist->n + 1u) = 0;"),
        ("gallery_app.c", "hist->is_fine ? HS_FINE : HS_BINS", "hist->is_fine ? HS_BINS : HS_FINE"),
        ("gallery_app.c", "const uint32_t mx = hs_plot_max(hist, h), step = hist->is_fine ? 1u : 8u;", "const uint32_t mx = hs_plot_max(hist, h), step = 1u;"),
    ]
    muts += [                                                                                                      # the view is hidden while a turn or a mask redraws the plane; the details page's histograms from the grid
        ("gallery_app.c", "            hide_view();\n            const int r = view_reopen();", "            const int r = view_reopen();"),
        ("gallery_app.c", "            hide_view();\n            if (edge & KEY_L1) {", "            if (edge & KEY_L1) {"),
        ("gallery_app.c", "    if (!view_hidden) { if (rot_o == 0u)", "    if (1) { if (rot_o == 0u)"),
        ("gallery_app.c", "            if (mask_i) mask_apply();\n            show_view();", "            if (mask_i) mask_apply();\n            view_hidden = 0u;"),
        ("gallery_app.c", "            show_view();\n            continue;\n        }\n        if (rot_o != 0u && (edge & (KEY_UP | KEY_DOWN)))", "            view_hidden = 0u;\n            continue;\n        }\n        if (rot_o != 0u && (edge & (KEY_UP | KEY_DOWN)))"),
        ("gallery_app.c", "if (from_grid && hist_for != sel && head.version == 1u) {", "if (0 && hist_for != sel && head.version == 1u) {"),
        ("gallery_app.c", "if (from_grid && hist_for != sel && head.version == 1u) {", "if (from_grid && head.version == 1u) {"),
        ("gallery_app.c", "if (from_grid && hist_for != sel && head.version == 1u) {", "if (from_grid && hist_for != sel) {"),
    ]
    muts += [                                                                                                      # the label bar
        ("gallery_app.c", "if (lab_on && (edge || (uint32_t)(cycles() - lab_t0) > 2u * CLK_HZ)) lab_clear();", "if (lab_on && ((uint32_t)(cycles() - lab_t0) > 2u * CLK_HZ)) lab_clear();"),
        ("gallery_app.c", "    lab_copy(0);\n    for (uint32_t i = 0; i < 64u; i++)", "    for (uint32_t i = 0; i < 64u; i++)"),
        ("gallery_app.c", "glb_cat(glb_cat(b, \"FILTER \"), gl_names[gset.lut % GL_PRESETS])", "glb_cat(glb_cat(b, \"FILTER \"), gl_names[(gset.lut + 1u) % GL_PRESETS])"),
        ("gallery_app.c", "mask_black(view, 0u, GT_CANVAS_W, GLB_Y0, GLB_Y0 + GLB_H); gfx_fence();", "gfx_fence();"),
        ("gallery_app.c", "for (uint32_t k = 0; k < GLB_SCALE; k++) {", "for (uint32_t k = 0; k < 2u; k++) {"),
        ("gallery_app.c", "const uint32_t view = rot_o ? GAL_ROT_BASE + rot_pan * GAL_LINE_WORDS16 : GAL_PLANE_BASE;", "const uint32_t view = GAL_PLANE_BASE;"),
        ("gallery_app.c", "if (mask_i && mask_p && gm_can_turn(mask_i)) glb_cat(q, \" TALL\");", "if (mask_i && gm_can_turn(mask_i)) glb_cat(q, \" TALL\");"),
        ("gallery_app.c", "            show_view(); lab_turn();\n", "            show_view();\n"),
        ("gallery_app.c", "            lab_filter();\n            continue;", "            continue;"),
        ("gallery_app.c", "            show_view(); lab_mask();\n", "            show_view();\n"),
        ("gallery_app.c", "glb_cat(b, glb_orient[rot_o & 7u]); lab_show(b);", "glb_cat(b, glb_orient[(rot_o + 1u) & 7u]); lab_show(b);"),
    ]
    muts += [                                                                                                      # the 2x and 4x zoom, B and A in a zoom
        ("gallery_app.c", "tgy_hw_scatter(1, (int32_t)(3u * z2_s), (int32_t)(z2_s * pitch));", "tgy_hw_scatter(1, 3, (int32_t)(z2_s * pitch));"),
        ("gallery_app.c", "tgy_hw_scatter(1, (int32_t)(3u * z2_s), (int32_t)(z2_s * pitch));", "tgy_hw_scatter(1, (int32_t)(3u * z2_s), (int32_t)pitch);"),
        ("gallery_app.c", "dst + a * 3u + b * pitch, pitch,", "dst + a * 3u, pitch,"),
        ("gallery_app.c", "dst + a * 3u + b * pitch, pitch,", "dst + b * pitch, pitch,"),
        ("gallery_app.c", "for (uint32_t k = 0; k < z2_s * z2_s && !r; k++) {", "for (uint32_t k = 0; k < z2_s * z2_s - 1u && !r; k++) {"),
        ("gallery_app.c", "(uint32_t)(vx - (int32_t)(z2_wtx[p] * z2_ot())) * 3u / 2u);", "(uint32_t)(vx - (int32_t)(z2_wtx[p] * z2_ot())) * 3u);"),
        ("gallery_app.c", "if (b_on && !(keys & KEY_B)) { b_on = 0u; nlevel = z2_s == 4u ? 2 : -1; }", "if (b_on && !(keys & KEY_B)) { b_on = 0u; nlevel = -1; }"),
        ("gallery_app.c", "else if (b_on && (uint32_t)(cycles() - b_t0) > B_LONG) { ret = 2; break; }", "else if (b_on && (uint32_t)(cycles() - b_t0) > B_LONG) { ret = 0; break; }"),
        ("gallery_app.c", "if (edge & KEY_L1) nlevel = z2_s == 4u ? 2 : -1;", "if (edge & KEY_L1) nlevel = -1;"),
        ("gallery_app.c", "if (edge & KEY_A) { if (z2_s == 2u) nlevel = 4; }", "if (edge & KEY_A) { }"),
        ("gallery_app.c", "if (b_on && (uint32_t)(cycles() - b_t0) > B_LONG) { b_on = 0u; zoom_leave(); gal_view_off();", "if (0 && (uint32_t)(cycles() - b_t0) > B_LONG) { b_on = 0u; zoom_leave(); gal_view_off();"),
        ("gallery_app.c", "else if (zl.layout == 2u && z_shown) { zoom_to_grid = 0u; z2_enter();", "else if (zl.layout == 2u) { zoom_to_grid = 0u; z2_enter();"),
        ("gallery_app.c", "    if (r == 0) zoom_open(k, cx - GV_SCREEN_W / 2, cy - GV_SCREEN_H / 2);", "    if (r == 0) zoom_open(k, 0, 0);"),
        ("gallery_app.c", "z2_tx = s == 2u ? 5u : 4u;", "z2_tx = s == 2u ? 5u : 3u;"),
        ("gallery_app.c", "if (w == -2) { if (++z2_fail >= 5u) { ret = -1; break; } wait_ms(100u); }", "if (w == -2) { ret = -1; break; }"),
        ("gallery_app.c", "if ((edge & KEY_SELECT) && !(keys & (KEY_LEFT | KEY_RIGHT))) {                                                         /* Select: the details pages", "if (0) {                                                         /* Select: the details pages"),
        ("gallery_app.c", "lab_show_at(z2_s == 2u ? \"2X ZOOM\" : \"4X ZOOM\", z2_disp_view, z2_disp_stride); last_pct = 101u;", "last_pct = 101u;"),
        ("gallery_app.c", "if (!z2_shown && z2_disp_ok && !view_hidden) {", "if (0) {"),
        ("gallery_app.c", "lab_show_at(z2_s == 2u ? \"2X ZOOM\" : \"4X ZOOM\", z2_disp_view, z2_disp_stride);", "lab_show_at(\"2X ZOOM\", z2_disp_view, z2_disp_stride);"),
        ("gallery_app.c", "z2_disp_stride = GV_WIN_STRIDE_WORDS; z2_disp_ok = 1u;", "z2_disp_stride = GV_WIN_STRIDE_WORDS; z2_disp_ok = 0u;"),
        ("gallery_app.c", "if (++z2_job.k >= z2_s * z2_s) {", "if (++z2_job.k >= z2_s * z2_s - 1u) {"),
        ("gallery_app.c", "while ((int32_t)(now - next_tick) >= 0 && frames < 4) { next_tick += ZOOM_FRAME; frames++; }", "if ((int32_t)(now - next_tick) >= 0) { next_tick += ZOOM_FRAME; frames = 1; }"),
        ("gallery_app.c", "if (from != tp && z2_isres(from, bx, by)) { z2_copy(from, tp, bx, by); return 1; }", "if (0 && z2_isres(from, bx, by)) { z2_copy(from, tp, bx, by); return 1; }"),
        ("gallery_app.c", "if (edge & KEY_START) { ret = 3; break; }", "if (edge & KEY_START) { ret = 0; break; }"),
        ("gallery_app.c", "    if (!z2_hold) {info.zt_n", "    if (1) {info.zt_n"),
        ("gallery_app.c", "static void zoom_goto(int k)\n{\n    z2_hold = 0u;\n", "static void zoom_goto(int k)\n{\n"),
        ("gallery_app.c", "if (r == 0 || r == 3) zoom_open(k, cx - GV_SCREEN_W / 2, cy - GV_SCREEN_H / 2);", "if (r == 0) zoom_open(k, cx - GV_SCREEN_W / 2, cy - GV_SCREEN_H / 2);"),
        ("gallery_app.c", "static void z2_stats_reset(void) { info.zt_n", "static void z2_stats_reset(void) { info.zoom_ms = 0u; info.zt_n"),
    ]
    muts += [                                                                                                      # the decoder's histogram read in two passes
        ("gallery_app.c", "h->fine[k][i] = hs_scale(v, s); }", "h->fine[k][i] = (uint16_t)v; }"),
        ("gallery_app.c", "const uint32_t s = hs_shift(mx), n = tgy_hw_hist_n(); if (n == 0u) return 0;", "const uint32_t s = 0u, n = tgy_hw_hist_n(); if (n == 0u) return 0;"),
    ]
    only = sys.argv[sys.argv.index("--only") + 1] if "--only" in sys.argv else None                                   # --only TEXT: just the mutants whose original code contains TEXT (Tier B of CLAUDE.md section 4: the mutants of the code you changed)
    if only: muts = [m for m in muts if only in m[1]]; assert muts, "no mutant matches --only"
    if "--idx" in sys.argv:                                                                                       # --idx 6,7,58: just these mutants (by their position in the list)
        ids = [int(x) for x in sys.argv[sys.argv.index("--idx") + 1].split(",")]; muts = [muts[i] for i in ids]
    if tail: muts = muts[-tail:]                                                                                # --v1-mutants: only the version 1 path's
    bad = 0
    for i, (f, a, b) in enumerate(muts):
        text = (ROOT / "fw2/gallery" / f).read_text(); assert a in text, (i, a)
        w = Path(tempfile.mkdtemp())
        if not build(w, {f: text.replace(a, b, 1)}): print("killed   (does not build)", i); continue
        rc, out = run(w, setdir); ok = rc == 0 and "PASSED" in out
        print("SURVIVED" if ok else "killed  ", i, a[:60]); bad += ok
    return bad

def quick(names):
    """--scenario a,b: build once and run just these scenarios (Tier A of CLAUDE.md section 4: the edit loop), each with the set it needs. Nothing else runs."""
    w = Path(tempfile.mkdtemp())
    if not build(w): return 1
    sets = {}
    def get(kind):
        if kind not in sets:
            d = Path(tempfile.mkdtemp()); {"base": make_set, "v1": make_v1_set, "v1i": make_v1idx_set, "lib1": make_lib1_set, "libbig": make_libbig_set, "big": make_big_set, "zoom": make_zoom_set}[kind](d); sets[kind] = d
        return sets[kind]
    rc = 0
    for sc in names:
        kind = "v1i" if sc == "v1gridhist" else "v1" if sc.startswith("v1") else "lib1" if sc.startswith("lib1") else "libbig" if sc.startswith("libbig") or sc == "colour" else "big" if sc.startswith("big") else "zoom" if sc == "zoom" else "base"
        r = subprocess.run([str(w / "t"), str(get(kind)), sc], capture_output=True, text=True, timeout=900)
        bad = [l for l in r.stdout.splitlines() if "FAIL" in l]
        print(("FAIL " if r.returncode else "ok   ") + sc + ("  " + " | ".join(bad[:4]) + " " + r.stderr[-300:] if r.returncode else ""))
        rc |= r.returncode != 0
    return rc

if __name__ == "__main__":
    if "--scenario" in sys.argv: sys.exit(quick(sys.argv[sys.argv.index("--scenario") + 1].split(",")))
    setdir = Path(tempfile.mkdtemp()); make_set(setdir)
    if "--mutants" in sys.argv or "--only" in sys.argv or "--idx" in sys.argv: sys.exit(1 if mutants(setdir) else 0)
    if "--v1-mutants" in sys.argv: sys.exit(1 if mutants(setdir, int(sys.argv[sys.argv.index("--v1-mutants") + 1]) if len(sys.argv) > sys.argv.index("--v1-mutants") + 1 else 7) else 0)
    w = Path(tempfile.mkdtemp())
    if not build(w): sys.exit(1)
    rc, out = run(w, setdir); print("\n".join(l for l in out.splitlines() if "FAIL" in l or "PASSED" in l or "(exit" in l)); (rc) and print(out[-7000:]); sys.exit(rc)
