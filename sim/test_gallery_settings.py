#!/usr/bin/env python3
"""Host test of fw2/gallery/gallery_settings.h (sim/test_gallery_settings.c). The unmodified build must pass; the six built-in mutants (-DBUG=1..6: clamping instead of wrapping in the menu, Controls and Help, the menu
entries swapped) must each fail. The source mutants then drop the window fix and the close keys from copies of the header."""
import os, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
def build(extra, hdr=None):
    w = Path(tempfile.mkdtemp()); (w / "fw2/gallery").mkdir(parents=True); (w / "fw2/cores").mkdir(); (w / "sim").mkdir()
    (w / "fw2/gallery/gallery_settings.h").write_text(hdr if hdr is not None else (ROOT / "fw2/gallery/gallery_settings.h").read_text())
    (w / "sim/t.c").write_text((ROOT / "sim/test_gallery_settings.c").read_text())
    env = dict(os.environ); sdk = subprocess.run([sys.executable, str(ROOT / "tools/toolchain.py"), "sdkroot"], capture_output=True, text=True).stdout.strip()
    if sdk: env["SDKROOT"] = sdk
    r = subprocess.run(["cc", "-O1", "-w", *extra, "-o", str(w / "t"), str(w / "sim/t.c")], capture_output=True, text=True, env=env)
    if r.returncode: print(r.stderr[:2000]); return None
    return w
def run(w):
    r = subprocess.run([str(w / "t")], capture_output=True, text=True); return r.returncode, r.stdout
if __name__ == "__main__":
    w = build([]); 
    if not w: sys.exit(1)
    rc, out = run(w); print("\n".join(l for l in out.splitlines() if "FAIL" in l or "PASSED" in l))
    if rc: sys.exit(1)
    if "--mutants" in sys.argv:
        bad = 0
        for n in (1, 2, 3, 4, 5, 6):
            w = build([f"-DBUG={n}"]); rc, _ = run(w) if w else (1, ""); print("killed  " if rc else "SURVIVED", f"BUG={n}"); bad += rc == 0
        src = (ROOT / "fw2/gallery/gallery_settings.h").read_text()
        for a, b in (("w[GS_W_LAYOUT] = (w[GS_W_LAYOUT] & ~(15u << 27)) | (GS_LAYOUT_VERSION << 27);", "w[GS_W_LAYOUT] = (w[GS_W_LAYOUT] & ~(15u << 27));"),
                     ("w[GS_W_CRC]    = (w[GS_W_CRC] & 0x80000000u) | (v->last_crc & GS_M31);", "w[GS_W_CRC]    = (v->last_crc & GS_M31);"),
                     ("w[GS_W_INDEX]  = (w[GS_W_INDEX] & 0x80000000u) | (v->last_idx & GS_M31);", "w[GS_W_INDEX]  = (w[GS_W_INDEX] & 0x80000000u) | (v->last_idx & 0xFFFFu);"),
                     ("v.last_crc = w[GS_W_CRC] & GS_M31;", "v.last_crc = w[GS_W_CRC];"), ("v.pal = w[GS_W_LOOK] & 31u; v.theme = (w[GS_W_LOOK] >> 5) & 63u; v.pol = (w[GS_W_LOOK] >> 11) & 1u;", "v.pal = w[GS_W_LOOK] & 31u; v.theme = (w[GS_W_LOOK] >> 5) & 31u; v.pol = (w[GS_W_LOOK] >> 11) & 1u;"), ("a->theme == b->theme && a->pol == b->pol", "a->theme == b->theme"), ("a->pol == b->pol && a->pal == b->pal; }", "a->pol == b->pol; }"),
                     ("v.resume = (w[GS_W_RESUME] & 1u) ^ 1u;", "v.resume = (w[GS_W_RESUME] & 1u);"), ("w[GS_W_RESUME] = (w[GS_W_RESUME] & ~1u) | ((v->resume & 1u) ^ 1u);", "w[GS_W_RESUME] = (w[GS_W_RESUME] & ~1u) | (v->resume & 1u);"),
                     ("static const uint8_t gs_write_order[] = { GS_W_LOOK, GS_W_RESUME, GS_W_CRC, GS_W_INDEX, GS_W_LAYOUT };", "static const uint8_t gs_write_order[] = { GS_W_LAYOUT, GS_W_LOOK, GS_W_RESUME, GS_W_CRC, GS_W_INDEX };"),
                     ("if ((crc_at_saved & GS_M31) != v->last_crc) return 0u;", "if (crc_at_saved != v->last_crc) return 0u;"),
                     ("return changed && (int32_t)(now - last_activity) >= (int32_t)hz;", "return (int32_t)(now - last_activity) >= (int32_t)hz;"),
                     ("(int32_t)(now - last_activity) >= (int32_t)hz;\n#endif", "now >= last_activity + hz;\n#endif")):
            assert a in src, a; w = build([], src.replace(a, b, 1)); rc, _ = run(w) if w else (1, ""); print("killed  " if rc else "SURVIVED", a[:60]); bad += rc == 0
        sys.exit(1 if bad else 0)
