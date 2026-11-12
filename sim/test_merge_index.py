#!/usr/bin/env python3
"""Host test for tools/gallery/merge_index.py: the old entries (and their thumbnails) survive unchanged, the package's pictures come in renamed with the suffix and label, sorted by path; a collision after renaming,
a missing package picture and an old index that lists a file the card does not have are refused."""
import sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools/gallery")); import tgy, merge_index as M
fails = 0
def check(name, cond):
    global fails
    if not cond: fails += 1; print("FAIL:", name)
def ent(p, n): return dict(path=p, name=n, size=1000, width=800, height=600)
def thumb(k): return bytes([k]) * tgy.THUMB_BYTES
with tempfile.TemporaryDirectory() as t:
    t = Path(t); card = t / "card"; (card / "Assets/tau_gallery/common/photos").mkdir(parents=True)
    oldp = ["Assets/tau_gallery/common/photos/a.tgy", "Assets/tau_gallery/common/photos/m.tgy", "Assets/tau_gallery/common/photos/z.tgy"]
    for p in oldp: (card / p).write_bytes(b"old")
    (card / "Assets/tau_gallery/common/tau-library.tdb").write_bytes(tgy.pack_index([ent(p, p[-5]) for p in oldp], [thumb(1), thumb(2), thumb(3)]))
    def pkg(names):
        pk = t / ("pkg%d" % len(list(t.iterdir()))); (pk / "photos").mkdir(parents=True)
        for n in names: (pk / "photos" / n).write_bytes(b"new")
        (pk / "tau-library.tdb").write_bytes(tgy.pack_index([ent("Assets/tau_gallery/common/photos/" + n, n[:-4]) for n in names], [thumb(10 + i) for i in range(len(names))])); return pk
    pk = pkg(["m.tgy", "b.tgy"]); old = card / "Assets/tau_gallery/common/tau-library.tdb"
    check("counts", M.merge(old, pk, "-v1", " (v1)", card) == (3, 2, 5))
    got = tgy.parse_index((pk / "tau-library.tdb").read_bytes())
    check("sorted by path", [e["path"] for e in got] == sorted(e["path"] for e in got))
    check("old entries unchanged", [(e["path"], e["thumb"]) for e in got if "-v1" not in e["path"]] == [(oldp[0], thumb(1)), (oldp[1], thumb(2)), (oldp[2], thumb(3))])
    check("new entries renamed with label", sorted(e["name"] for e in got if "-v1" in e["path"]) == ["b (v1)", "m (v1)"])
    check("new thumbnails kept", {e["path"][-8:]: e["thumb"] for e in got if "-v1" in e["path"]} == {"b-v1.tgy": thumb(11), "m-v1.tgy": thumb(10)})
    check("photos renamed", sorted(f.name for f in (pk / "photos").iterdir()) == ["b-v1.tgy", "m-v1.tgy"])
    check("the card's m.tgy name is not reused", not (pk / "photos/m.tgy").exists())
    try: M.merge(old, pkg(["m.tgy"]), "", "", card); check("collision refused", False)
    except SystemExit: check("collision refused", True)
    pk3 = pkg(["q.tgy"]); (pk3 / "photos/q.tgy").unlink()
    try: M.merge(old, pk3, "-v1", "", card); check("missing picture refused", False)
    except SystemExit: check("missing picture refused", True)
    (card / oldp[1]).unlink()
    try: M.merge(old, pkg(["c.tgy"]), "-v1", "", card); check("old index with a missing file refused", False)
    except SystemExit: check("old index with a missing file refused", True)
print("MERGE_INDEX TEST", "PASSED" if not fails else "FAILED %d" % fails); sys.exit(1 if fails else 0)
