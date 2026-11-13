#!/usr/bin/env python3
"""Build a Tau Gallery release: ONE core, `alfatreze.Tau Gallery` on platform `tau_gallery`, in one zip.

What it does (the gallery's counterpart of tools/make_release.py, which is the music player's):
  1. refuses early: the RBF's SHA-256, a tag that is not vX.Y.Z or vX.Y.Z-label, core.json that does not carry the tag's version, a CHANGELOG
     without the tag's heading or with another date than core.json's date_release, an uncommitted tree (the tree-derived fields of the manifest
     must come from a committed tree);
  2. builds the firmware (APP=gallery TGY=1 bash fw2/build_gallery.sh, the 128 KB link of the TGY core) unless --rom-dir names a build;
  3. assembles the package from dist/ (core files, platform json and artwork, boot splash) with the fresh ROM and cold image, the bit-reversed
     bitstream, the release version and date in core.json, and the version stamped into the ROM;
  4. checks the pairing (tools/check_fw_bitstream_pair.py: the ROM must accept the bitstream's CORE_VERSION, the black-screen check), the data slots'
     files, the zip (base folders, no metadata files, bitstream bit-reversed, ROM byte for byte, platform files), and the package against its own
     tau-compat.json (tools/tau_compat.py build, verify, check-card);
  5. writes release/<Author>.<Core>_<Version>_<Date>.zip, release/tau-compat.json and release/SHA256SUMS.txt and PRINTS the gh command. It never
     publishes and never touches a card.

  python3 tools/make_gallery_release.py --rbf PATH_TO_RAW.rbf --rbf-sha256 HASH --release v1.0.0 --no-previous
  python3 tools/make_gallery_release.py ... --release v1.0.0-rc.1 --no-previous     (a pre-release of the same core: gh --prerelease)

Bump first: `dist/Cores/alfatreze.Tau Gallery/core.json` version (X.Y.Z) and date_release, and a `## vX.Y.Z[-label] - D Month YYYY` heading in CHANGELOG.md.
A pictures-free package: the user converts their own pictures (tools/gallery-converter or tgy_convert.py); the library index is theirs.
--rom-dir, --dist, --registry, --out, --work, --changelog and --allow-dirty exist for the host test and for rehearsals: a run with --allow-dirty never prints the publish command.
"""
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import tau_compat  # noqa: E402
import tau_layout  # noqa: E402
import tau_version  # noqa: E402
import make_release as mr  # noqa: E402  (make_zip and verify are the player release's, generic over one core)

CORE_ID, PLATFORM = "alfatreze.Tau Gallery", "tau_gallery"
REPO = "alfatreze/Tau-Gallery"
BUILD_BOUND = ("tau.rom", "tau-cold.bin")          # from the firmware build; tau-loading.bin (the boot splash) comes from dist/


class ReleaseError(Exception):
    pass


def sha(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()


def sh(cmd, root, **kw):
    print("+", " ".join(str(c) for c in cmd), flush=True)
    r = subprocess.run([str(c) for c in cmd], cwd=root, **kw)
    if r.returncode:
        raise ReleaseError(f"step failed ({r.returncode}): {' '.join(str(c) for c in cmd)}")


def check_inputs(release, rbf, rbf_sha256, root, dist, changelog, previous, previous_release, allow_dirty, bitstream_version):
    """Everything that can be refused before anything is built. Returns (version, date, core_version)."""
    if sha(rbf) != rbf_sha256:
        raise ReleaseError(f"RBF hash mismatch: expected {rbf_sha256}, got {sha(rbf)}")
    meta = json.loads((dist / "Cores" / CORE_ID / "core.json").read_text())["core"]["metadata"]
    version, date = meta["version"], meta["date_release"]
    import re
    m = re.fullmatch(r"v(\d+\.\d+\.\d+)(-[0-9A-Za-z.]+)?", release)
    if not m:
        raise ReleaseError(f"--release must look like v1.0.0 or v1.0.0-rc.1: {release!r}")
    if "-dev." in release:
        raise ReleaseError("dev builds are not published: use tools/package_dev_build.py --number")
    if m.group(1) != version:
        raise ReleaseError(f"--release {release} but core.json version is {version} (bump core.json first)")
    try:
        tau_compat.changelog_section(release, changelog)
        cl_date = tau_compat.changelog_releases(changelog).get(release)
        if cl_date and cl_date != date:
            raise ReleaseError(f"core.json date_release {date} differs from the CHANGELOG heading's date {cl_date}: set one of them")
        if previous and not previous_release:
            raise ReleaseError("--previous needs --previous-release")
        core_version = tau_compat.bitstream_core_version(rbf, bitstream_version)
        if not allow_dirty and tau_compat.source_state(root)["dirty"]:
            raise ReleaseError("the tree has uncommitted changes outside dist/ and release/: commit them before a release")
    except tau_compat.CompatError as e:
        raise ReleaseError(f"tau-compat: {e}")
    return version, date, core_version


def build_firmware(root, build_out):
    env = dict(os.environ, APP="gallery", TGY="1", TAU_BUILD_OUT=str(build_out))
    sh(["bash", "fw2/build_gallery.sh"], root, env=env)
    return build_out / "gallery-app"


def assemble(dist, pkg, rbf, rom_dir, release, date):
    """The package directory (base folders only): dist's core files and platform files, the fresh ROM and cold image, the reversed bitstream."""
    if pkg.exists():
        shutil.rmtree(pkg)
    shutil.copytree(dist / "Cores" / CORE_ID, pkg / "Cores" / CORE_ID, ignore=shutil.ignore_patterns("._*", ".DS_Store"))
    (pkg / "Platforms/_images").mkdir(parents=True)
    for n in (f"{PLATFORM}.json",):
        shutil.copy2(dist / "Platforms" / n, pkg / "Platforms" / n)
    shutil.copy2(dist / f"Platforms/_images/{PLATFORM}.bin", pkg / f"Platforms/_images/{PLATFORM}.bin")
    assets = tau_layout.core_dir(pkg, PLATFORM, CORE_ID)
    assets.mkdir(parents=True)
    for n in BUILD_BOUND:
        if not (rom_dir / n).is_file():
            raise ReleaseError(f"{rom_dir / n} is missing")
        shutil.copy2(rom_dir / n, assets / n)
    splash = dist / "Assets" / PLATFORM / CORE_ID / "tau-loading.bin"
    if not splash.is_file():
        raise ReleaseError(f"{splash} is missing (the boot splash)")
    shutil.copy2(splash, assets / "tau-loading.bin")
    (pkg / "Cores" / CORE_ID / "bitstream.rbf_r").write_bytes(rbf.read_bytes().translate(bytes(int(f"{x:08b}"[::-1], 2) for x in range(256))))
    cj_path = pkg / "Cores" / CORE_ID / "core.json"
    cj = json.loads(cj_path.read_text())
    cj["core"]["metadata"]["version"] = release[1:]
    cj["core"]["metadata"]["date_release"] = date
    cj_path.write_text(json.dumps(cj, indent=4) + "\n")
    stamped = tau_version.stamp_file(assets / "tau.rom", release[1:])
    return assets / "tau.rom", stamped


def check_slots(pkg):
    """Every data slot that names a file the package is supposed to carry has it (the picture library and the media are the user's)."""
    slots = json.loads((pkg / "Cores" / CORE_ID / "data.json").read_text())["data"]["data_slots"]
    missing = []
    for sl in slots:
        fn = sl.get("filename")
        if fn in tau_layout.BUILD_BOUND:
            if not (tau_layout.core_dir(pkg, PLATFORM, CORE_ID) / fn).is_file():
                missing.append(fn)
    if missing:
        raise ReleaseError(f"data.json names build-bound files the package lacks: {missing}")
    return len(slots)


def run(args):
    root = args.root
    rbf = args.rbf if args.rbf.is_absolute() else root / args.rbf
    version, date, core_version = check_inputs(args.release, rbf, args.rbf_sha256, root, args.dist, args.changelog, args.previous,
                                               args.previous_release, args.allow_dirty, args.bitstream_version)
    print(f"release {args.release}: core.json v{version} ({date}), bitstream CORE_VERSION {core_version}")
    work = args.work
    rom_dir = args.rom_dir or build_firmware(root, work / "fw")
    pkg = work / "pocket"
    rom, stamped = assemble(args.dist, pkg, rbf, rom_dir, args.release, date)
    print(f"version {stamped} -> {rom}")
    sh([sys.executable, "tools/check_fw_bitstream_pair.py", pkg, "--bitstream-version", core_version], root)
    print(f"data slots ok ({check_slots(pkg)})")
    out = args.out
    out.mkdir(parents=True, exist_ok=True)
    mr.OUT = out
    z = mr.make_zip(pkg, tau_layout.zip_name(CORE_ID, args.release[1:], date))
    n = mr.verify(z, CORE_ID, args.release[1:], rbf.read_bytes(), rom)
    print(f"  {z.name}: {n} files, {z.stat().st_size:,} bytes, checks ok")
    kw = dict(release=args.release, zips=[z], previous=None if args.no_previous else args.previous, rbf=rbf,
              bitstream_version=args.bitstream_version, previous_release=args.previous_release, changelog=args.changelog, root=root,
              registry_file=args.registry)
    compat = out / "tau-compat.json"
    try:
        compat.write_text(tau_compat.dumps(tau_compat.build(**kw)))
        errs = tau_compat.verify(compat, **kw)
    except tau_compat.CompatError as e:
        raise ReleaseError(f"tau-compat: {e}")
    if errs:
        raise ReleaseError("tau-compat.json does not match the zip:\n  " + "\n  ".join(errs))
    try:
        unpacked = work / "unzipped"
        if unpacked.exists():
            shutil.rmtree(unpacked)
        with zipfile.ZipFile(z) as f:
            f.extractall(unpacked)
        errs = tau_compat.check_card(json.loads(compat.read_text()), unpacked, CORE_ID)
    except tau_compat.CompatError as e:
        raise ReleaseError(f"tau-compat check-card: {e}")
    bad = [e for e in errs if e[0] == "error"]
    if bad:
        raise ReleaseError(f"the unpacked zip does not match its manifest: {bad[:3]}")
    sums = [f"{sha(z)}  {z.name}", f"{sha(compat)}  {compat.name}"]
    (out / "SHA256SUMS.txt").write_text("\n".join(sums) + "\n")
    print("\n".join(sums))
    if args.allow_dirty:
        print("\n(rehearsal with --allow-dirty: no publish command)")
        return z
    pre = " --prerelease" if "-" in args.release else ""
    print(f"\nPublish (owner approval first):\n  gh release create {args.release} --repo {REPO}{pre} --title '{args.release}' --notes-file <notes> "
          f"{out / z.name} {out / 'tau-compat.json'} {out / 'SHA256SUMS.txt'}")
    return z


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--rbf", required=True, type=Path, help="the raw (not reversed) RBF")
    ap.add_argument("--rbf-sha256", required=True, help="expected SHA-256 of --rbf (refuses an unaudited bitstream)")
    ap.add_argument("--release", required=True, help="the tag: v1.0.0, or v1.0.0-rc.1 for a pre-release (needs a matching CHANGELOG heading)")
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--previous", action="append", type=Path, help="the previous release's zip (persist_ids_changed)")
    g.add_argument("--no-previous", action="store_true", help="the first release: persist_ids_changed = []")
    ap.add_argument("--previous-release", help="the tag of the --previous zip")
    ap.add_argument("--bitstream-version", help="CORE_VERSION of --rbf when it has no fit manifest (<rbf>.json)")
    ap.add_argument("--rom-dir", type=Path, help="use this firmware build (tau.rom and tau-cold.bin) instead of building one")
    ap.add_argument("--out", type=Path, default=ROOT / "release", help="where the zip, tau-compat.json and SHA256SUMS.txt go")
    ap.add_argument("--work", type=Path, default=ROOT / "work/gallery-release", help="scratch folder (package, build, unzipped copy)")
    ap.add_argument("--dist", type=Path, default=ROOT / "dist", help="the folder holding Cores, Platforms and Assets of the core (the tree's dist/)")
    ap.add_argument("--registry", type=Path, default=tau_compat.REGISTRY_FILE, help="the persist registry (tools/persist_registry.json): what each persisted id means and since which release")
    ap.add_argument("--changelog", type=Path, default=ROOT / "CHANGELOG.md")
    ap.add_argument("--allow-dirty", action="store_true", help="rehearsal on an uncommitted tree: no publish command is printed")
    args = ap.parse_args(argv)
    args.root = ROOT
    try:
        run(args)
    except ReleaseError as e:
        sys.exit(f"make_gallery_release: {e}")


if __name__ == "__main__":
    main()
