#!/usr/bin/env python3
"""Add the pictures of a package to a card's existing TGIX library instead of replacing it (docs/CARD_INSTALL_PROCEDURE.md: the installer copies the package's tau-library.tdb over the card's).
  merge_index.py --old CARD/Assets/tau_gallery/common/tau-library.tdb --pkg PKG/Assets/tau_gallery/common [--suffix -v1] [--label " (v1)"] [--card-root CARD]
Every picture of the package's own index gets SUFFIX before its extension (a package picture with the name of one already on the card would overwrite it: the card's old firmware cannot draw a version 1 file), LABEL
after its display name; the package's photos are renamed to match; the package's tau-library.tdb is rewritten as the card's entries (unchanged, thumbnails included) plus the package's, sorted by path. With
--card-root every entry of the old index must exist on the card. Only the package directory is changed; the card is only read. Prints a summary."""
import argparse, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent)); import tgy

def merge(old_tdb, pkg_common, suffix="-v1", label=" (v1)", card_root=None):
    old = tgy.parse_index(Path(old_tdb).read_bytes()); pk = Path(pkg_common); new = tgy.parse_index((pk / "tau-library.tdb").read_bytes())
    if card_root is not None:
        gone = [e["path"] for e in old if not (Path(card_root) / e["path"]).is_file()]
        if gone: raise SystemExit("the old index lists %d files that are not on the card, e.g. %s" % (len(gone), gone[0]))
    have = {e["path"] for e in old}; out = []
    for e in new:
        p = Path(e["path"]); np_ = str(p.with_name(p.stem + suffix + p.suffix)); src = pk / "photos" / p.name; dst = pk / "photos" / (p.stem + suffix + p.suffix)
        if np_ in have: raise SystemExit("renamed path %s is already in the old index" % np_)
        if src.is_file(): src.rename(dst)
        elif not dst.is_file(): raise SystemExit("package picture missing: %s" % src)
        out.append(dict(e, path=np_, name=(e["name"] + label)[:tgy.NAME_BYTES - 1]))
    allents = sorted(old + out, key=lambda e: e["path"])
    (pk / "tau-library.tdb").write_bytes(tgy.pack_index(allents, [e["thumb"] for e in allents]))
    return len(old), len(out), len(allents)

if __name__ == "__main__":
    ap = argparse.ArgumentParser(); ap.add_argument("--old", required=True); ap.add_argument("--pkg", required=True); ap.add_argument("--suffix", default="-v1"); ap.add_argument("--label", default=" (v1)"); ap.add_argument("--card-root")
    a = ap.parse_args(); o, n, t = merge(a.old, a.pkg, a.suffix, a.label, a.card_root); print("merged index: %d old + %d new = %d entries" % (o, n, t))
