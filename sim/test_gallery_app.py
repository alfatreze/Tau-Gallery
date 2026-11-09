#!/usr/bin/env python3
"""Driver of sim/test_gallery_app.c: writes 18 small pictures and their index with the real converter library, builds the firmware (gallery_app.c, gallery_fmt.c, gallery_hw.c) against the hardware model and runs it.
`--mutants` edits copies of the firmware sources; every edit must make the run fail."""
import os, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools/gallery"))
import tgy
from PIL import Image

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
    for f in (ROOT / "fw2/gallery").iterdir():
        if f.suffix in (".c", ".h"): (inc / f.name).write_text((fw or {}).get(f.name, f.read_text()))
    (workdir / "sim").mkdir(exist_ok=True); (workdir / "sim/t.c").write_text(text)
    env = dict(os.environ); sdk = subprocess.run([sys.executable, str(ROOT / "tools/toolchain.py"), "sdkroot"], capture_output=True, text=True).stdout.strip()
    if sdk: env["SDKROOT"] = sdk
    r = subprocess.run(["cc", "-O1", "-w", "-o", str(workdir / "t"), str(workdir / "sim/t.c")], capture_output=True, text=True, env=env)
    if r.returncode: print(r.stderr[:3000]); return False
    return True

def run(workdir, setdir):
    r = subprocess.run([str(workdir / "t"), str(setdir)], capture_output=True, text=True, timeout=600)
    return r.returncode, r.stdout

def mutants(setdir):
    muts = [("gallery_app.c", "REG_WR(R_LAND_BASE, GAL_PLANE_BASE);", "REG_WR(R_LAND_BASE, GAL_PLANE_BASE + 2u);"),
            ("gallery_app.c", "0x40000000u + c, len,", "0x40000000u, len,"),
            ("gallery_app.c", "t.canvas_off + c, 0x40000000u", "t.canvas_off, 0x40000000u"),
            ("gallery_app.c", "const uint32_t total = (t.canvas_len + 4095u) & ~4095u;", "const uint32_t total = t.canvas_len;"),
            ("gallery_app.c", "if (REG_RD(R_LAND_OVF) != 0u ||", "if ("),
            ("gallery_app.c", "|| REG_RD(R_LAND_WORDS) != total / 4u) return 6;", ") return 6;"),
            ("gallery_app.c", "GX0 2u", "GX0 4u") if False else ("gallery_app.c", "#define GX0 2u", "#define GX0 4u"),
            ("gallery_app.c", "#define GYS 76u", "#define GYS 75u"),
            ("gallery_app.c", "const uint32_t rel = a - off, row = rel / 192u, col = (rel % 192u) / 2u;", "const uint32_t rel = a - off, row = rel / 192u, col = (rel % 192u) / 2u + 1u;"),
            ("gallery_app.c", "if (a < off || a >= off + GX_THUMB_BYTES) continue;", "if (a < off) continue;"),
            ("gallery_app.c", "path[0] = '/';", "path[0] = 'x';"),
            ("gallery_app.c", "if (edge & KEY_B) { gal_view_off();", "if (edge & KEY_B) { "),
            ("gallery_app.c", "(sel + 1u) % head.count : (sel + head.count - 1u) % head.count;", "(sel + 1u) % head.count : (sel + head.count - 1u);"),
            ("gallery_app.c", "if (np != page) { page = np; if (!load_page())", "if (np != page) { if (!load_page())"),
            ("gallery_app.c", "else if (edge & KEY_R1) { select_to(sel + PER_PAGE < head.count ? sel + PER_PAGE : head.count - 1u); }", "else if (edge & KEY_R1) { select_to(sel + PER_PAGE < head.count ? sel + PER_PAGE : sel); }"),
            ("gallery_app.c", "gfx_text(6, 338, ent[sel - page * PER_PAGE].name, FG, BG);", "gfx_text(6, 338, ent[0].name, FG, BG);"),
            ("gallery_app.c", "if (!gal_view_on()) { say(\"VIEWER DID NOT START\", BAD); return; }", "gal_view_on();"),
            ("gallery_app.c", "if (r) { say(open_msg[r], BAD); return; }\n    if (!gal_view_on())", "if (r) { say(open_msg[r], BAD); }\n    if (!gal_view_on())"),
            ("gallery_app.c", "{ run_pages(); draw_page(); prev =", "{ run_pages(); prev ="),
            ("gallery_app.c", "if (r == GP_OPEN_CONTROLS) { gc_t c;", "if (r == GP_OPEN_HELP) { gc_t c;"),
            ("gallery_app.c", "static void page_clear(const char *title) { gfx_rect(0, 0, GFX_W, GFX_H, BG);", "static void page_clear(const char *title) { "),
            ("gallery_app.c", "draw_menu(&m);\n    }\n}", "}\n}"),
            ("gallery_app.c", "else if (edge & KEY_START) {", "else if (edge & KEY_SELECT) {"),
            ("gallery_fmt.c", "return h->entries_off + i * GX_ENTRY_BYTES;" if False else "uint32_t gx_thumb_off(const gx_head_t *h, uint32_t thumb) { return h->atlas_off + thumb * GX_THUMB_BYTES; }", "uint32_t gx_thumb_off(const gx_head_t *h, uint32_t thumb) { return h->atlas_off + thumb * (GX_THUMB_BYTES - 2u); }")]
    bad = 0
    for i, (f, a, b) in enumerate(muts):
        text = (ROOT / "fw2/gallery" / f).read_text(); assert a in text, (i, a)
        w = Path(tempfile.mkdtemp())
        if not build(w, {f: text.replace(a, b, 1)}): print("killed   (does not build)", i); continue
        rc, out = run(w, setdir); ok = rc == 0 and "PASSED" in out
        print("SURVIVED" if ok else "killed  ", i, a[:60]); bad += ok
    return bad

if __name__ == "__main__":
    setdir = Path(tempfile.mkdtemp()); make_set(setdir)
    if "--mutants" in sys.argv: sys.exit(1 if mutants(setdir) else 0)
    w = Path(tempfile.mkdtemp())
    if not build(w): sys.exit(1)
    rc, out = run(w, setdir); print("\n".join(l for l in out.splitlines() if "FAIL" in l or "PASSED" in l)); sys.exit(rc)
