#!/usr/bin/env python3
"""Compiles an engine package's sources to its committed relocatable object (ld -r).

A package is engines/<id>/ with a manifest.json (schema cycles-mod/0.1). The manifest's build recipe says which
sources to compile, with which flags, and which one object to commit. This tool compiles them and joins them with
`ld -r` into one relocatable m68k ELF (every function and table a section of its own, the patch points still
undefined symbols), exactly as core/relocate.py expects, and as core/compose.py places it.

The object is committed; this tool's --check verifies it is up to date with the sources (CI, and before a commit).
The composer and the build never need the toolchain: they place the committed object with core/relocate.py.

    python3 core/build_engine.py <id>            # rebuild engines/<id>/<id>.o from its sources
    python3 core/build_engine.py <id> --check    # verify the committed object is up to date
    python3 core/build_engine.py --all [--check] # every package under engines/

Needs an m68k toolchain: set M68K_CROSS to the prefix (e.g. /path/to/bin/m68k-elf-), or have m68k-elf-gcc on the PATH.
"""
import argparse
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent
ENGINES = ROOT / "engines"

CROSS = os.environ.get("M68K_CROSS") or next(
    (c for c in ("m68k-linux-gnu-", "m68k-elf-") if shutil.which(c + "gcc")), "m68k-linux-gnu-")


def run(cmd, **kw):
    return subprocess.run(cmd, check=True, capture_output=True, text=True, **kw).stdout


def build_object(mid):
    """The package's object as it is committed: the sources compiled with the manifest's recipe, joined with ld -r."""
    d = ENGINES / mid
    mf = d / "manifest.json"
    if not mf.is_file():
        sys.exit(f"!! no package {mid}: {mf} is missing")
    m = json.loads(mf.read_text(encoding="utf-8"))
    if m.get("schema") != "cycles-mod/0.1" or m["id"] != mid:
        sys.exit(f"!! {mf}: bad schema or id")
    b = m["build"]
    objs = b.get("objects", [])
    if len(objs) != 1:
        sys.exit(f"!! {mid}: build.objects must name one object (ld -r of the sources), has {objs}")
    defs = [f"-D{k}={v}" for k, v in b.get("defines", {}).items()]
    incs = [f"-I{d / i}" for i in b.get("include", [])]
    with tempfile.TemporaryDirectory() as td:
        tmp = pathlib.Path(td)
        parts = []
        for rel in b["sources"]:
            src = d / rel
            if not src.is_file():
                sys.exit(f"!! {mid}: source {src} is missing")
            o = tmp / f"{mid}-{src.stem}.o"
            run([CROSS + "gcc", *b["cflags"], *defs, *incs, "-c", str(src), "-o", str(o)])
            parts.append(o)
        out = tmp / f"{mid}.o"
        run([CROSS + "ld", "-r", "-o", str(out), *map(str, parts)])
        return out.read_bytes(), mf, (d / objs[0])


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("ids", nargs="*", help="package ids under engines/ (or --all)")
    ap.add_argument("--all", action="store_true", help="every package that has a manifest")
    ap.add_argument("--check", action="store_true", help="verify the committed object matches the sources")
    args = ap.parse_args()
    ids = args.ids
    if args.all:
        ids = [p.name for p in sorted(ENGINES.iterdir()) if p.is_dir() and (p / "manifest.json").is_file()]
    if not ids:
        sys.exit("!! name a package, or --all")
    bad = 0
    for mid in ids:
        data, mf, committed = build_object(mid)
        if args.check:
            ok = committed.is_file() and committed.read_bytes() == data
            print(f"  {mid:12s} {committed.name} {'is up to date' if ok else 'DOES NOT MATCH'}")
            bad += 0 if ok else 1
        else:
            committed.write_bytes(data)
            print(f"  wrote {committed.relative_to(ROOT)} ({len(data)} bytes)")
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
