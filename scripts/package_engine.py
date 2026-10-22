#!/usr/bin/env python3
"""Pack one engine into a zip that anyone can download and drop into their own project.

Usage (from the repo root):
  python3 scripts/package_engine.py engines/tear

Result: dist/<id>-<version>.zip containing a single folder <id>/ with only the files listed in
that engine's engine.json (plus engine.json itself). The safety check runs first; if it finds
firmware or secrets, no zip is made.
"""
import json
import pathlib
import subprocess
import sys
import zipfile

ROOT = pathlib.Path(__file__).resolve().parent.parent


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    folder = pathlib.Path(sys.argv[1]).resolve()
    card_path = folder / "engine.json"
    if not card_path.exists():
        print(f"No engine.json in {folder}")
        return 1
    card = json.loads(card_path.read_text(encoding="utf-8"))
    name, version = card["id"], card["version"]

    check = subprocess.run([sys.executable, str(ROOT / "scripts" / "check_safety.py"), str(folder)])
    if check.returncode != 0:
        print("Not packaging: the safety check failed.")
        return 1

    files = sorted(set(card["files"]) | {"engine.json"})
    missing = [f for f in files if not (folder / f).exists()]
    if missing:
        print("Files listed in engine.json are missing: " + ", ".join(missing))
        return 1

    dist = ROOT / "dist"
    dist.mkdir(exist_ok=True)
    out = dist / f"{name}-{version}.zip"
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as zf:
        for rel in files:
            info = zipfile.ZipInfo(f"{name}/{rel}", date_time=(2026, 1, 1, 0, 0, 0))  # fixed date: same zip every time
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            zf.writestr(info, (folder / rel).read_bytes())
    print(f"Wrote {out.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
