#!/usr/bin/env python3
"""A real folder of hundreds of pictures through tools/gallery/tgy_convert.py (docs/M1_DESIGN.md, the open item "real folders of photos"). Builds a folder of N pictures (default 240; --quick 24) of every shape a
phone or a download folder has (landscape, portrait, panorama, tiny, an EXIF-rotated one, names that differ only by case, a name that is not Latin-1) beside what is not a picture or is damaged (a text file, an
AppleDouble junk file, an empty .jpg, a name too long for the card path, pictures in a subfolder), runs the real converter as a child process and checks what a user needs: the run finishes, every good picture is in the
index exactly once and in order, every thumbnail in the index is the one inside its .tgy, nothing is cut, nothing half-written is left, a second run gives the same bytes, and the cost (time, memory, disk) is printed.
`--mutants` edits copies of the converter; every edit must make this test fail."""
import json, os, resource, struct, subprocess, sys, tempfile, time
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools/gallery"))
import tgy
from PIL import Image

LONG = "L" * 70
def make_folder(src, n):
    src.mkdir(parents=True)
    shapes = [(640, 480), (480, 640), (1200, 300), (96, 72), (900, 900)]
    good = []
    for i in range(n):
        w, h = shapes[i % len(shapes)]
        im = Image.new("RGB", (w, h), ((i * 37) % 256, (i * 91) % 256, (i * 53) % 256)); im.paste((255, 255, 255), (i % 5, i % 7, i % 5 + 8, i % 7 + 8))
        name = f"IMG_{i:04d}"
        if i % 40 == 0 and i: name = f"IMG_{i:04d}_中文"                    # not Latin-1: the display name gets '?'
        im.save(src / f"{name}.jpg", quality=80); good.append(name)
    for nm in ("caf\u00e9", "\u4e2d\u6587", "\u65e5\u672c"): Image.new("RGB", (40, 30), (3, 3, 3)).save(src / (nm + ".jpg")); good.append(nm)                  # accented (Latin-1) and two different names with no ASCII at all
    rot = Image.new("RGB", (600, 400), (10, 200, 30)); ex = Image.Exif(); ex[0x0112] = 6; rot.save(src / "ROTATED.jpg", exif=ex.tobytes()); good.append("ROTATED")      # EXIF orientation 6: shown as 400 x 600
    Image.new("RGB", (50, 50), (1, 2, 3)).save(src / "PIC.jpg"); Image.new("RGB", (60, 40), (4, 5, 6)).save(src / "pic.png"); good += ["PIC", "pic"]                  # the same name but for the case and the extension (a case-insensitive disk would merge two files that differ only by case): both must survive
    (src / "notes.txt").write_text("not a picture"); (src / "empty.jpg").write_bytes(b""); (src / "._IMG_0000.jpg").write_bytes(b"\0\5\x16\7" + bytes(60))
    Image.new("RGB", (80, 60), (7, 8, 9)).save(src / (LONG + ".png"))                                                                                              # a card path over the 95-byte limit
    (src / "sub").mkdir(); Image.new("RGB", (80, 60), (9, 9, 9)).save(src / "sub" / "inner.jpg")                                                                   # not converted: the converter does not descend (said in the report)
    return good

def run_conv(conv, src, out):
    t0 = time.time(); r0 = resource.getrusage(resource.RUSAGE_CHILDREN)
    r = subprocess.run([sys.executable, str(conv), str(src), str(out), "--index-format", "v0"], capture_output=True, text=True)      # the version 0 (flat, 96 x 72) index: this test is the regression of that format; version 1 is tested in sim/test_tgy_convert_v1.py
    dt = time.time() - t0; peak = resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss / (1024 * 1024)      # macOS reports bytes
    ev = []
    for l in r.stdout.splitlines():
        try: ev.append(json.loads(l))
        except ValueError: pass
    return r, ev, dt, peak

fails = 0
def check(c, what, extra=""):
    global fails
    print(("ok   " if c else "FAIL ") + what + ("" if c else "  " + str(extra)[:300]))
    if not c: fails += 1

def verify(conv, n, report=True):
    w = Path(tempfile.mkdtemp()); src = w / "src"; good = make_folder(src, n); out = w / "out"
    r, ev, dt, peak = run_conv(conv, src, out)
    check(r.returncode == 0 and ev and ev[-1]["event"] == "done", "the converter finishes with exit 0 and a done event", (r.returncode, r.stderr[-300:]))
    if not ev or ev[-1]["event"] != "done": return w
    conv_ev = [e for e in ev if e["event"] == "converted"]; skips = [e for e in ev if e["event"] == "skip"]
    check(len(conv_ev) == len(good) == ev[-1]["pictures"], f"{len(good)} good pictures converted, none lost", (len(conv_ev), len(good)))
    check(sorted(e["file"] for e in skips) == sorted(["._IMG_0000.jpg", "empty.jpg", LONG + ".png"]) and ev[-1]["skipped"] == 3, "exactly the junk file, the empty file and the too-long name were skipped, and counted", [e["file"][:20] for e in skips])
    check(any("shorten" in e["error"] for e in skips if e["file"].startswith("LLL")), "the too-long name's skip says what to do")
    tgys = sorted(p.name for p in out.glob("*.tgy"))
    check(len(tgys) == len(good) and not list(out.glob("*.part")), "one .tgy per good picture and no .part file is left", (len(tgys), list(out.glob("*.part"))[:2]))
    check(all(p.stat().st_size == 1748992 for p in out.glob("*.tgy")), "every .tgy is 1,748,992 bytes")
    check(not (out / "inner.tgy").exists(), "pictures in a subfolder are not converted (the converter does not descend)")
    idx = (out / "tau-library.tdb").read_bytes(); ents = tgy.parse_index(idx)
    check(len(ents) == len(good), "the index lists every good picture once", (len(ents), len(good)))
    paths = [e["path"] for e in ents]
    check(all(b < 0x80 for p in paths for b in p.encode("utf-8")), "every card path is ASCII only (KB-041: the Pocket cannot open a path with a byte of 0x80 or more)", [p for p in paths if any(b >= 0x80 for b in p.encode())][:2])
    byname = {e["name"]: e["path"] for e in ents}
    check(byname.get("caf\u00e9", "").startswith("Assets/tau_gallery/common/photos/caf_") and "caf\u00e9" in byname, "an accented name keeps its Latin-1 display name and gets an ASCII file name", byname.get("caf\u00e9"))
    zz = [e["path"] for e in ents if e["name"] in ("??", "\u4e2d\u6587", "\u65e5\u672c")]
    check(len(zz) == 2 and len(set(zz)) == 2, "two different names with no ASCII at all get two different file names", zz)
    check(len(set(paths)) == len(paths) and all(len(p.encode()) < tgy.PATH_BYTES for p in paths), "paths are unique and each fits the 95-byte limit")
    check(len({p.lower() for p in paths}) == len(paths), "no two paths are the same once case is ignored (FAT has no case)")
    src_order = sorted(p.stem for p in (src).iterdir() if p.suffix.lower() in tgy_convert_ext() and p.is_file() and p.stat().st_size and not p.name.startswith("._") and p.stem != LONG)
    disp = lambda s: "".join(c if ord(c) < 256 else "?" for c in s)[:39]
    check(ents[0]["name"] == disp(src_order[0]) and ents[-1]["name"] == disp(src_order[-1]) and [e["name"] for e in ents[:5]] == [disp(s) for s in src_order[:5]], "the index follows the file name order (by display name: first five and the last)", ([e["name"] for e in ents[:2]], ents[-1]["name"], src_order[-1]))
    bad = 0
    for i, e in enumerate(ents):
        d = (out / (Path(e["path"]).name)).read_bytes()
        if d[4096:4096 + tgy.THUMB_BYTES] != e["thumb"] or e["size"] != len(d): bad += 1
    check(bad == 0, "every thumbnail in the index is the one inside its .tgy, and every size matches", bad)
    rot = [e for e in ents if e["name"] == "ROTATED"]
    check(len(rot) == 1 and (rot[0]["width"], rot[0]["height"]) == (400, 600), "the EXIF-rotated picture is stored as 400 x 600", rot and (rot[0]["width"], rot[0]["height"]))
    cn = sorted(e["name"] for e in ents if e["name"].lower().startswith("pic"))
    check(cn == ["PIC", "pic_2"], "PIC.jpg and pic.png both survive, the second as pic_2", cn)
    zh = [e for e in ents if "?" in e["name"]]
    check(all(e["name"].startswith("IMG_") or set(e["name"]) == {"?"} for e in zh) and (n < 41 or len(zh) >= 1), "a name that is not Latin-1 shows '?' and is not cut at the wrong place", [e["name"] for e in zh][:2])
    size_ok = len(idx) >= 32 + len(ents) * 160 + len(ents) * tgy.THUMB_BYTES
    check(size_ok, f"the index holds all entries and thumbnails ({len(idx) / 1e6:.2f} MB)")
    r2, ev2, _, _ = run_conv(conv, src, out2 := w / "out2")
    check(r2.returncode == 0 and (out2 / "tau-library.tdb").read_bytes() == idx and (out2 / tgys[0]).read_bytes() == (out / tgys[0]).read_bytes(), "a second run gives the same index and the same first picture, byte for byte")
    if report:
        total = sum(p.stat().st_size for p in out.glob("*")) / 1e6
        print(f"report: {len(good)} pictures in {dt:.1f} s ({dt / len(good) * 1000:.0f} ms each), peak memory {peak:.0f} MB, output {total:.0f} MB, index {len(idx) / 1e6:.2f} MB; 1,000 pictures would be {1000 * 1.749:.0f} MB on the card and {(32 + 1000 * (160 + tgy.THUMB_BYTES)) / 1e6:.1f} MB of index")
    return w

def tgy_convert_ext():
    sys.path.insert(0, str(ROOT / "tools/gallery")); import tgy_convert; return tgy_convert.EXT

def mutants():
    src = (ROOT / "tools/gallery/tgy_convert.py").read_text()
    muts = [("base = ascii_stem(p.stem); stem = base;", "base = p.stem; stem = base;"),
            ("if s != stem: s += \"_%04x\" % (zlib.crc32(stem.encode(\"utf-8\")) & 0xFFFF)", "pass"),
            ("if len(path.encode(\"utf-8\")) >= tgy.PATH_BYTES:", "if False:"),
            ("while stem.lower() in used: n += 1; stem = f\"{base}_{n}\"", "pass"),
            ("used.add(stem.lower())", "pass"),
            ("print(json.dumps({\"event\": \"skip\", \"file\": p.name, \"error\": str(e)})); skipped += 1; continue", "raise"),
            ("files = sorted(p for p in a.src.iterdir()", "files = sorted((p for p in a.src.iterdir()"),
            ("os.replace(tmp, path)", "tmp.rename(path.with_name(path.name + '.part'))"),
            ("\"skipped\": skipped", "\"skipped\": 0"),
            ("name=name_of(shown)", "name=name_of(shown)[:5]")]
    bad = 0
    for i, (a, b) in enumerate(muts):
        if a not in src: print("MISSING mutant text", i, a[:50]); bad += 1; continue
        m = src.replace(a, b, 1)
        if i == 4: m = m.replace("if p.suffix.lower() in EXT and p.is_file())", "if p.suffix.lower() in EXT and p.is_file()), reverse=True)", 1)
        d = Path(tempfile.mkdtemp()); (d / "tgy_convert.py").write_text(m)
        for f in ("tgy.py",): (d / f).write_text((ROOT / "tools/gallery" / f).read_text())
        global fails; fails = 0
        import io, contextlib
        with contextlib.redirect_stdout(io.StringIO()):
            try: verify(d / "tgy_convert.py", 24, report=False)
            except Exception: fails += 1
        print("SURVIVED" if fails == 0 else "killed  ", i, a[:60]); bad += fails == 0
    return bad

if __name__ == "__main__":
    if "--mutants" in sys.argv: sys.exit(1 if mutants() else 0)
    verify(ROOT / "tools/gallery/tgy_convert.py", 24 if "--quick" in sys.argv else 240)
    print("PASSED" if not fails else f"FAILED {fails}"); sys.exit(1 if fails else 0)
