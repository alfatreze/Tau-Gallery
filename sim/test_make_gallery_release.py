#!/usr/bin/env python3
"""Host test of tools/make_gallery_release.py: a rehearsal on the tree with a made-up RBF (the real firmware is built once and reused), the refusals (wrong RBF hash, a tag that is
not core.json's version, a CHANGELOG without the heading or with another date, a bitstream the ROM does not accept, a dirty tree, a package without its build-bound files) and the
mutants of the script (each must make a test fail). Run: python3 sim/test_make_gallery_release.py [--mutants]"""
import hashlib, json, os, shutil, subprocess, sys, tempfile, zipfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
SCRIPT = ROOT / "tools/make_gallery_release.py"
fails = 0
def check(name, ok):
    global fails
    print(("ok   " if ok else "FAIL ") + name); fails += 0 if ok else 1
VER, DATE = "1.0.0", "2026-11-20"                      # a rehearsal of the first release: a copy of dist/ with core.json bumped (the tree's dist/ still says 0.1.0)
TAG = "v" + VER
def make_dist(d):
    dist = d / "dist"; shutil.copytree(ROOT / "dist", dist, ignore=shutil.ignore_patterns("._*", ".DS_Store"))
    p = dist / "Cores/alfatreze.Tau Gallery/core.json"; j = json.loads(p.read_text()); j["core"]["metadata"].update(version=VER, date_release=DATE); p.write_text(json.dumps(j, indent=4) + "\n")
    return dist

def setup():
    d = Path(tempfile.mkdtemp()); rbf = d / "ap_core.rbf"; rbf.write_bytes(bytes((i * 7 + 3) & 255 for i in range(5000)))
    cl = d / "CHANGELOG.md"; cl.write_text(f"# Changelog\n\n## {TAG} — {DATE.split('-')[2].lstrip('0')} November 2026\n\nTest.\n\n- Omega: nothing changed.\n")
    return d, rbf, hashlib.sha256(rbf.read_bytes()).hexdigest(), cl

def rom_build(d):
    env = dict(os.environ, APP="gallery", TGY="1", TAU_BUILD_OUT=str(d / "fw"))
    r = subprocess.run(["bash", "fw2/build_gallery.sh"], cwd=ROOT, env=env, capture_output=True, text=True)
    return d / "fw/gallery-app" if r.returncode == 0 else None

def run(script, d, rbf, sha, cl, rom, extra=(), tag=TAG, bsv="4D50331E", clean_out=True, registry=None):
    out, work = d / "out", d / "work"
    if clean_out:
        shutil.rmtree(out, ignore_errors=True); shutil.rmtree(work, ignore_errors=True)
    args = [sys.executable, str(script), "--rbf", str(rbf), "--rbf-sha256", sha, "--release", tag, "--no-previous", "--bitstream-version", bsv,
            "--rom-dir", str(rom), "--out", str(out), "--work", str(work), "--changelog", str(cl), "--dist", str(d / "dist")] + (["--registry", str(registry)] if registry else []) + list(extra)
    return subprocess.run(args, cwd=ROOT, capture_output=True, text=True)

def inproc(script, d, rbf, sha, cl, rom, tag, registry=None):
    """Run the script's main() in this process with the tree taken as clean (the publish command is printed only for a clean tree); returns what it printed."""
    import importlib.util, io, contextlib
    spec = importlib.util.spec_from_file_location("mgr_" + script.stem, script); mod = importlib.util.module_from_spec(spec); spec.loader.exec_module(mod)
    real = mod.tau_compat.source_state; mod.tau_compat.source_state = lambda root=ROOT: dict(real(root), dirty=False)
    shutil.rmtree(d / "out", ignore_errors=True); shutil.rmtree(d / "work", ignore_errors=True)
    args = ["--rbf", str(rbf), "--rbf-sha256", sha, "--release", tag, "--no-previous", "--bitstream-version", "4D50331E", "--rom-dir", str(rom), "--out", str(d / "out"), "--work", str(d / "work"),
            "--changelog", str(cl), "--dist", str(d / "dist")] + (["--registry", str(registry)] if registry else [])
    buf = io.StringIO()
    try:
        with contextlib.redirect_stdout(buf): mod.main(args)
    except SystemExit as e: buf.write(f"EXIT {e}")
    except Exception as e: buf.write(f"ERROR {e}")
    finally: mod.tau_compat.source_state = real
    return buf.getvalue()

def evaluate(script):
    """Every test against `script`; returns the number of failures (the global counter is reset by the caller)."""
    global fails
    d, rbf, sha, cl = setup(); make_dist(d); rom = ROM
    r = run(script, d, rbf, sha, cl, rom, ["--allow-dirty"])
    check("a rehearsal on the tree succeeds", r.returncode == 0)
    zips = sorted((d / "out").glob("*.zip")); z = zips[0] if len(zips) == 1 else None
    check("one zip named Author.Core_Version_Date.zip", z is not None and z.name == f"alfatreze.Tau_Gallery_{VER}_{DATE}.zip")
    check("tau-compat.json and SHA256SUMS.txt are written, the sums are right", (d / "out/tau-compat.json").is_file() and z is not None
          and set((d / "out/SHA256SUMS.txt").read_text().split("\n")[:-1]) == {f"{hashlib.sha256(z.read_bytes()).hexdigest()}  {z.name}", f"{hashlib.sha256((d / 'out/tau-compat.json').read_bytes()).hexdigest()}  tau-compat.json"})
    if z:
        with zipfile.ZipFile(z) as f:
            names = f.namelist(); core = json.loads(f.read("Cores/alfatreze.Tau Gallery/core.json"))["core"]["metadata"]
            rev = rbf.read_bytes().translate(bytes(int(f"{x:08b}"[::-1], 2) for x in range(256)))
            check("the zip holds only Cores, Platforms and Assets, and the files a core needs", {n.split("/")[0] for n in names} == {"Cores", "Platforms", "Assets"}
                  and all(n in names for n in ("Cores/alfatreze.Tau Gallery/bitstream.rbf_r", "Cores/alfatreze.Tau Gallery/data.json", "Platforms/tau_gallery.json", "Platforms/_images/tau_gallery.bin",
                                               "Assets/tau_gallery/alfatreze.Tau Gallery/tau.rom", "Assets/tau_gallery/alfatreze.Tau Gallery/tau-cold.bin", "Assets/tau_gallery/alfatreze.Tau Gallery/tau-loading.bin")))
            check("the bitstream is the bit-reversed RBF", f.read("Cores/alfatreze.Tau Gallery/bitstream.rbf_r") == rev)
            check("core.json carries the release version and date", core["version"] == VER and core["date_release"] == DATE)
            rom_in = f.read("Assets/tau_gallery/alfatreze.Tau Gallery/tau.rom")
            check("the ROM is the build's, with the release version stamped in", rom_in != (rom / "tau.rom").read_bytes() and VER.encode() in rom_in and b"TAUFWPAIR:" in rom_in)
            check("no metadata files and no pictures in the zip", not any(n.endswith(".DS_Store") or "/._" in "/" + n or n.endswith(".tgy") for n in names))
        mf = d / "out/tau-compat.json"; pk = json.loads(mf.read_text())["packages"][0] if mf.is_file() else {}
        check("the manifest says CORE_VERSION 4D50331E and names the ROM's version and the zip's hash", str(pk.get("bitstream_core_version", "")).upper().endswith("4D50331E") and pk.get("zip_sha256") == hashlib.sha256(z.read_bytes()).hexdigest())
    check("a rehearsal prints no publish command", "gh release create" not in r.stdout)
    # a pre-release of the same core: the version of core.json and of the zip carry the label, the registry names the tag that ships the ids
    RC = TAG + "-rc.1"; clr = d / "CL_rc.md"; clr.write_text(f"# Changelog\n\n## {RC} \u2014 {DATE.split('-')[2].lstrip('0')} November 2026\n\nx\n")
    reg = json.loads((ROOT / "tools/persist_registry.json").read_text())
    for v in reg["ids"].values(): v["since"] = RC
    rp = d / "registry_rc.json"; rp.write_text(json.dumps(reg))
    r = run(script, d, rbf, sha, clr, rom, ["--allow-dirty"], tag=RC, registry=rp); zr = sorted((d / "out").glob("*.zip"))
    check("a pre-release rehearsal succeeds and its zip carries the label", r.returncode == 0 and len(zr) == 1 and zr[0].name == f"alfatreze.Tau_Gallery_{VER}-rc.1_{DATE}.zip")
    if zr:
        with zipfile.ZipFile(zr[0]) as f: check("core.json of the pre-release says 1.0.0-rc.1", json.loads(f.read("Cores/alfatreze.Tau Gallery/core.json"))["core"]["metadata"]["version"] == VER + "-rc.1")
    # the publish command, with the tree taken as clean
    out = inproc(script, d, rbf, sha, cl, rom, TAG); out2 = inproc(script, d, rbf, sha, clr, rom, RC, registry=rp)
    check("the publish command names the Gallery repository, the zip, the manifest and the sums", "gh release create v1.0.0 --repo alfatreze/Tau-Gallery" in out and "tau-compat.json" in out and "SHA256SUMS.txt" in out and ".zip" in out)
    check("a pre-release is published with --prerelease, a release without", " --prerelease" in out2 and " --prerelease" not in out)
    # the refusals
    r = run(script, d, rbf, "0" * 64, cl, rom, ["--allow-dirty"]); check("a wrong RBF hash is refused", r.returncode != 0 and "hash mismatch" in r.stderr)
    r = run(script, d, rbf, sha, cl, rom, ["--allow-dirty"], tag="v9.9.9"); check("a tag that is not core.json's version is refused", r.returncode != 0 and "core.json version" in r.stderr)
    r = run(script, d, rbf, sha, cl, rom, ["--allow-dirty"], tag="not-a-tag"); check("a tag that is not vX.Y.Z is refused", r.returncode != 0 and "must look like" in r.stderr)
    r = run(script, d, rbf, sha, cl, rom, ["--allow-dirty"], tag=TAG + "-dev.4"); check("a dev tag is refused", r.returncode != 0 and "dev builds" in r.stderr)
    cl2 = d / "CL2.md"; cl2.write_text("# Changelog\n\n## v0.0.1 — 1 January 2026\n\nx\n"); r = run(script, d, rbf, sha, cl2, rom, ["--allow-dirty"])
    check("a CHANGELOG without the tag's heading is refused", r.returncode != 0)
    cl3 = d / "CL3.md"; cl3.write_text(f"# Changelog\n\n## {TAG} — 31 December 2030\n\nx\n"); r = run(script, d, rbf, sha, cl3, rom, ["--allow-dirty"])
    check("a CHANGELOG date that differs from core.json's is refused", r.returncode != 0 and "differs" in r.stderr)
    r = run(script, d, rbf, sha, cl, rom, ["--allow-dirty"], bsv="4D50331A"); check("a bitstream the ROM does not accept is refused (the black-screen check)", r.returncode != 0 and "PAIR MISMATCH" in (r.stdout + r.stderr))
    nb = d / "nobuild"; nb.mkdir(); (nb / "tau.rom").write_bytes((rom / "tau.rom").read_bytes()); r = run(script, d, rbf, sha, cl, nb, ["--allow-dirty"])
    check("a firmware build without its cold image is refused", r.returncode != 0 and "missing" in r.stderr)
    r = run(script, d, rbf, sha, cl, rom, [])
    dirty = subprocess.run(["git", "status", "--porcelain", "--", ".", ":(exclude)dist", ":(exclude)release"], cwd=ROOT, capture_output=True, text=True).stdout.strip()
    if dirty: check("an uncommitted tree is refused without --allow-dirty", r.returncode != 0 and "uncommitted" in r.stderr)
    else: check("a clean tree gives the publish command with the repository, the zip and the manifest", r.returncode == 0 and "gh release create" in r.stdout and "--repo alfatreze/Tau-Gallery" in r.stdout and "tau-compat.json" in r.stdout)
    return fails

MUTANTS = [
    ("the RBF hash is not checked", "    if sha(rbf) != rbf_sha256:", "    if False:"),
    ("the tag is not compared with core.json", "    if m.group(1) != version:", "    if False:"),
    ("the CHANGELOG date is not compared", "        if cl_date and cl_date != date:", "        if False:"),
    ("the dirty tree is accepted", "        if not allow_dirty and tau_compat.source_state(root)[\"dirty\"]:", "        if False:"),
    ("the pairing check is skipped", "    sh([sys.executable, \"tools/check_fw_bitstream_pair.py\", pkg, \"--bitstream-version\", core_version], root)\n", ""),
    ("the version is not stamped into the ROM", "    stamped = tau_version.stamp_file(assets / \"tau.rom\", release[1:])", "    stamped = 'none'"),
    ("the bitstream is copied as it is", ".write_bytes(rbf.read_bytes().translate(bytes(int(f\"{x:08b}\"[::-1], 2) for x in range(256))))", ".write_bytes(rbf.read_bytes())"),
    ("core.json keeps its version", "    cj[\"core\"][\"metadata\"][\"version\"] = release[1:]\n", ""),
    ("the cold image is not required", "BUILD_BOUND = (\"tau.rom\", \"tau-cold.bin\")", "BUILD_BOUND = (\"tau.rom\",)"),
    ("a rehearsal prints the publish command", "    if args.allow_dirty:\n        print(\"\\n(rehearsal", "    if False:\n        print(\"\\n(rehearsal"),
    ("the wrong repository", "REPO = \"alfatreze/Tau-Gallery\"", "REPO = \"alfatreze/Tau-Alpha\""),
    ("a pre-release is not marked", "pre = \" --prerelease\" if \"-\" in args.release else \"\"", "pre = \"\""),
    ("the sums file lists only the zip", "    sums = [f\"{sha(z)}  {z.name}\", f\"{sha(compat)}  {compat.name}\"]", "    sums = [f\"{sha(z)}  {z.name}\"]"),
]

if __name__ == "__main__":
    tmp = Path(tempfile.mkdtemp()); ROM = rom_build(tmp)
    if ROM is None: print("the firmware did not build"); sys.exit(1)
    evaluate(SCRIPT); print(f"make-gallery-release: {fails} failure(s)")
    if fails: sys.exit(1)
    if "--mutants" in sys.argv:
        src = SCRIPT.read_text(); surv = 0
        for name, a, b in MUTANTS:
            assert a in src, name
            m = ROOT / "tools/_mut_make_gallery_release.py"; m.write_text(src.replace(a, b, 1))
            try:
                fails = 0
                import io, contextlib
                with contextlib.redirect_stdout(io.StringIO()): n = evaluate(m)
            finally: m.unlink()
            print("killed  " if n else "SURVIVED", name); surv += not n
        sys.exit(1 if surv else 0)
