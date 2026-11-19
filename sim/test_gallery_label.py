#!/usr/bin/env python3
"""Host test of fw2/gallery/gallery_label.h (sim/test_gallery_label.c). The unmodified header must pass; the source mutants (scale, bit order, advance, offsets, bounds) must each fail."""
import os, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
HDR = ROOT / "fw2/gallery/gallery_label.h"
def run(hdr_text=None):
    w = Path(tempfile.mkdtemp()); (w / "sim").mkdir(); (w / "fw2/gallery").mkdir(parents=True)
    for f in ("gallery_lut.h", "gallery_mask.h"): (w / "fw2/gallery" / f).write_text((ROOT / "fw2/gallery" / f).read_text())
    (w / "fw2/gallery/gallery_label.h").write_text(hdr_text if hdr_text is not None else HDR.read_text())
    (w / "sim/t.c").write_text((ROOT / "sim/test_gallery_label.c").read_text())
    env = dict(os.environ); sdk = subprocess.run([sys.executable, str(ROOT / "tools/toolchain.py"), "sdkroot"], capture_output=True, text=True).stdout.strip()
    if sdk: env["SDKROOT"] = sdk
    r = subprocess.run(["cc", "-O1", "-w", "-o", str(w / "t"), str(w / "sim/t.c"), "-lm"], capture_output=True, text=True, env=env)
    if r.returncode: return 1, r.stderr
    r = subprocess.run([str(w / "t")], capture_output=True, text=True); return r.returncode, r.stdout
MUT = [
    ("const uint32_t ci = x / GLB_ADV, within = (x % GLB_ADV) / GLB_SCALE, row = y / GLB_SCALE;", "const uint32_t ci = x / GLB_ADV, within = (x % GLB_ADV) / GLB_SCALE, row = y / 2u;"),
    ("const uint32_t ci = x / GLB_ADV, within = (x % GLB_ADV) / GLB_SCALE, row = y / GLB_SCALE;", "const uint32_t ci = x / GLB_ADV, within = 4u - (x % GLB_ADV) / GLB_SCALE, row = y / GLB_SCALE;"),
    ("return (glb_cols(s[ci])[within] >> row) & 1u;", "return (glb_cols(s[ci])[within] >> (6u - row)) & 1u;"),
    ("if (ci >= glb_len(s) || ci >= GLB_MAXCH || within >= 5u || row >= 7u) return 0;", "if (ci >= glb_len(s) || within >= 5u || row >= 7u) return 0;"),
    ("if (ci >= glb_len(s) || ci >= GLB_MAXCH || within >= 5u || row >= 7u) return 0;", "if (ci >= glb_len(s) || ci >= GLB_MAXCH || within >= 6u || row >= 7u) return 0;"),
    ("#define GLB_ADV 18u", "#define GLB_ADV 17u"),
    ("#define GLB_TX 16u", "#define GLB_TX 15u"),
    ("const uint8_t v = (x >= GLB_TX && glb_on(s, x - GLB_TX, ty)) ? 255u : 0u;", "const uint8_t v = (x >= GLB_TX && glb_on(s, x - GLB_TX, ty)) ? 254u : 0u;"),
    ("rb[3u * x] = v; rb[3u * x + 1u] = v; rb[3u * x + 2u] = v;", "rb[3u * x] = v; rb[3u * x + 1u] = 0u; rb[3u * x + 2u] = v;"),
    ("{ 'A', { 0x7E, 0x11, 0x11, 0x11, 0x7E } }", "{ 'A', { 0x7E, 0x11, 0x11, 0x11, 0x7F } }"),
    ("{ '/', { 0x20, 0x10, 0x08, 0x04, 0x02 } }", "{ '/', { 0x02, 0x04, 0x08, 0x10, 0x20 } }"),
    ("{ ':', { 0x00, 0x36, 0x36, 0x00, 0x00 } }", "{ ':', { 0x00, 0x36, 0x00, 0x00, 0x00 } }"),
    ("static const char *const glb_orient[8] = { \"UPRIGHT\", \"MIRRORED\",", "static const char *const glb_orient[8] = { \"UPRIGHT\", \"MIRRORED~\","),
    ("static inline char *glb_cat(char *d, const char *s) { while (*s) *d++ = *s++; *d = 0; return d; }", "static inline char *glb_cat(char *d, const char *s) { while (*s) *d++ = *s++; *d = 0; return d + 1; }"),
]
if __name__ == "__main__":
    rc, out = run(); print("\n".join(l for l in out.splitlines() if "FAIL" in l or "PASSED" in l or "ok" in l[:3]))
    if rc: sys.exit(1)
    if "--mutants" in sys.argv:
        src = HDR.read_text(); bad = 0
        for a, b in MUT:
            assert a in src, a[:60]
            rc, _ = run(src.replace(a, b, 1)); print("killed  " if rc else "SURVIVED", a[:60]); bad += rc == 0
        sys.exit(1 if bad else 0)
