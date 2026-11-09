#!/usr/bin/env python3
"""The converter's modes the Tau Gallery Converter app uses (tools/gallery/tgy_convert.py): one picture or a folder as the source, --card-layout (the card's own tree) and --merge (add to a library already there). Runs the real
converter as a child process. `--mutants` edits copies of the converter; every edit must make a check fail."""
import json, shutil, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools/gallery"))
import tgy
from PIL import Image

CARD = "Assets/tau_gallery/common"

def pic(path, w, h, col):
    Image.new("RGB", (w, h), col).save(path)

def run(conv, *args):
    r = subprocess.run([sys.executable, str(conv), *map(str, args)], capture_output=True, text=True)
    ev = []
    for l in r.stdout.splitlines():
        try: ev.append(json.loads(l))
        except ValueError: pass
    return r.returncode, ev

def checks(conv):
    bad = []
    def check(ok, what):
        if not ok: bad.append(what)
    d = Path(tempfile.mkdtemp()); src = d / "in"; src.mkdir()
    pic(src / "b.jpg", 900, 600, (200, 10, 10)); pic(src / "a.png", 600, 900, (10, 200, 10)); (src / "notes.txt").write_text("x")
    # default: the same layout as always (the pictures and the index side by side in OUT), plan event first
    rc, ev = run(conv, src, d / "o0")
    check(rc == 0 and ev[0] == {"event": "plan", "files": 2, "kept": 0} and ev[-1]["event"] == "done" and ev[-1]["pictures"] == 2, "a folder converts and says its plan first")
    check(sorted(p.name for p in (d / "o0").iterdir()) == ["a.tgy", "b.tgy", "tau-library.tdb"], "default layout: the pictures and the index sit in OUT")
    # card layout
    rc, ev = run(conv, src, d / "o1", "--card-layout")
    check(rc == 0 and sorted(p.name for p in (d / "o1" / CARD / "photos").iterdir()) == ["a.tgy", "b.tgy"] and (d / "o1" / CARD / "tau-library.tdb").is_file(), "--card-layout: the pictures in Assets/tau_gallery/common/photos, the index in Assets/tau_gallery/common")
    check(not (d / "o1" / "tau-library.tdb").exists(), "--card-layout leaves nothing loose in OUT")
    idx = tgy.parse_index((d / "o1" / CARD / "tau-library.tdb").read_bytes())
    check([e["path"] for e in idx] == [CARD + "/photos/a.tgy", CARD + "/photos/b.tgy"], "the index holds the card paths of the pictures")
    # one picture
    rc, ev = run(conv, src / "b.jpg", d / "o2", "--card-layout")
    idx2 = tgy.parse_index((d / "o2" / CARD / "tau-library.tdb").read_bytes())
    check(rc == 0 and ev[0]["files"] == 1 and [e["name"] for e in idx2] == ["b"] and (d / "o2" / CARD / "photos" / "b.tgy").is_file(), "one picture file converts to a library of one")
    for name in ("notes.txt", "missing.jpg"):
        rc, ev = run(conv, src / name, d / "o3")
        check(rc == 2 and ev and ev[-1]["event"] == "error" and not (d / "o3").exists(), f"{name}: a file that is not a picture, or not there, is an error with exit 2 and writes nothing")
    # merge: add one picture to a library
    before = {e["name"]: e for e in tgy.parse_index((d / "o1" / CARD / "tau-library.tdb").read_bytes())}
    pic(d / "c.png", 500, 500, (10, 10, 200))
    rc, ev = run(conv, d / "c.png", d / "o1", "--card-layout", "--merge")
    after = tgy.parse_index((d / "o1" / CARD / "tau-library.tdb").read_bytes())
    check(rc == 0 and ev[0]["kept"] == 2 and ev[-1]["pictures"] == 3 and ev[-1]["converted"] == 1 and [e["name"] for e in after] == ["a", "b", "c"], "--merge adds one picture and keeps the two already there, in name order")
    check(all(e["thumb"] == before[e["name"]]["thumb"] and e["size"] == before[e["name"]]["size"] for e in after if e["name"] in before), "the kept pictures keep their thumbnails and sizes")
    check(sorted(p.name for p in (d / "o1" / CARD / "photos").iterdir()) == ["a.tgy", "b.tgy", "c.tgy"], "the kept pictures' files are still there")
    # merge: the same name replaces
    pic(d / "c.png", 700, 300, (1, 2, 3))
    rc, ev = run(conv, d / "c.png", d / "o1", "--card-layout", "--merge")
    again = tgy.parse_index((d / "o1" / CARD / "tau-library.tdb").read_bytes())
    check(rc == 0 and ev[-1]["converted"] == 1 and [e["name"] for e in again] == ["a", "b", "c"] and [e for e in again if e["name"] == "c"][0]["width"] == 700, "--merge: a picture with the same name replaces the old one, it is not listed twice")
    # merge: a picture whose file was removed is dropped
    (d / "o1" / CARD / "photos" / "a.tgy").unlink()
    pic(d / "e.png", 200, 200, (5, 5, 5))
    rc, ev = run(conv, d / "e.png", d / "o1", "--card-layout", "--merge")
    gone = tgy.parse_index((d / "o1" / CARD / "tau-library.tdb").read_bytes())
    check(rc == 0 and [e["name"] for e in gone] == ["b", "c", "e"] and ev[0]["kept"] == 2, "--merge drops a picture whose file is gone")
    # merge: an index that cannot be read is rebuilt without it, said so
    (d / "o1" / CARD / "tau-library.tdb").write_bytes(b"garbage")
    rc, ev = run(conv, d / "e.png", d / "o1", "--card-layout", "--merge")
    check(rc == 0 and any(e["event"] == "warning" for e in ev) and [e["name"] for e in tgy.parse_index((d / "o1" / CARD / "tau-library.tdb").read_bytes())] == ["e"], "--merge with a broken old index: a warning, the library is rebuilt")
    # merge into nothing: works as a plain conversion
    rc, ev = run(conv, src, d / "o4", "--card-layout", "--merge")
    check(rc == 0 and ev[0]["kept"] == 0 and ev[-1]["pictures"] == 2, "--merge into an empty output is a plain conversion")
    # without --merge the old library is replaced
    rc, ev = run(conv, d / "c.png", d / "o1", "--card-layout")
    check(rc == 0 and [e["name"] for e in tgy.parse_index((d / "o1" / CARD / "tau-library.tdb").read_bytes())] == ["c"], "without --merge the library holds only what was converted")
    # a custom card dir with the card layout
    rc, ev = run(conv, src, d / "o5", "--card-layout", "--card-dir", "Assets/x/y/pics")
    check((d / "o5/Assets/x/y/pics/a.tgy").is_file() and (d / "o5/Assets/x/y/tau-library.tdb").is_file(), "--card-dir moves the pictures and the index with it")
    shutil.rmtree(d)
    return bad

MUT = [("if (photos / Path(e[\"path\"]).name).is_file(): old[", "if True: old["),
       ("old.pop((stem + \".tgy\").lower(), None)", "pass"),
       ("key=lambda x: x[0])", "key=lambda x: x[1][\"width\"])"),
       ("index_dir = a.out / str(Path(card_dir).parent) if a.card_layout else a.out", "index_dir = a.out"),
       ("photos = a.out / card_dir if a.card_layout else a.out", "photos = a.out"),
       ("if a.src.suffix.lower() not in EXT:", "if False:"),
       ("elif a.src.is_dir(): files", "elif a.src.is_dir() and False: files"),
       ("if a.merge and (index_dir / a.index_name).is_file():", "if (index_dir / a.index_name).is_file():"),
       ("except ValueError as e: print(json.dumps({\"event\": \"warning\"", "except ValueError as e: print(json.dumps({\"event\": \"note\""),
       ("print(json.dumps({\"event\": \"plan\", \"files\": len(files), \"kept\": len(old)}))", "print(json.dumps({\"event\": \"plan\", \"files\": len(files), \"kept\": 0}))"),
       ("thumbs = [th for _, _, th in both]", "thumbs = thumbs + [th for _, _, th in both][len(thumbs):]"),
       ("\"converted\": len(entries) - len(old)", "\"converted\": len(entries)"),
       ("else: print(json.dumps({\"event\": \"error\", \"error\": \"no such file or folder: \" + str(a.src)})); return 2", "else: files = []")]

def mutants():
    src = (ROOT / "tools/gallery/tgy_convert.py").read_text(); bad = 0
    for i, (a, b) in enumerate(MUT):
        if a not in src: print("MISSING mutant text", i, a[:60]); bad += 1; continue
        d = Path(tempfile.mkdtemp()); shutil.copy(ROOT / "tools/gallery/tgy.py", d / "tgy.py"); (d / "tgy_convert.py").write_text(src.replace(a, b, 1))
        try: killed = bool(checks(d / "tgy_convert.py"))
        except Exception: killed = True
        print("killed  " if killed else "SURVIVED", i, a[:60]); bad += not killed
        shutil.rmtree(d)
    return bad

if __name__ == "__main__":
    if "--mutants" in sys.argv: sys.exit(1 if mutants() else 0)
    bad = checks(ROOT / "tools/gallery/tgy_convert.py")
    for b in bad: print("FAIL", b)
    print("PASSED" if not bad else f"FAILED {len(bad)}"); sys.exit(1 if bad else 0)
