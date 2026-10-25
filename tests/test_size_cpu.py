#!/usr/bin/env python3
"""Size and CPU report for every engine and the full build (ARCHITECTURE.md §7.3).

Size is measured from the composed layout (code + tables, variables, and the bytes added to the image against the
image budget). CPU is the voice-loop cost measured in the emulator, carried in each engine's manifest (cost.emulation,
measured in the old repo's voice-loop counter) and reported against the stock TONE machine.

    python3 tests/test_size_cpu.py [model-cycles_OS1.13.syx]
"""
import json
import pathlib
import sys

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(ROOT / "core"))

import compose                    # noqa: E402
import os113                      # noqa: E402

ENGINES = ROOT / "engines"
BUDGET = 0x40200000 - (os113.BASE + os113.IMAGE_LEN)      # end_limit - image_end, ~344 KB


def engine_ids():
    return sorted(p.name for p in ENGINES.iterdir() if p.is_dir() and (p / "manifest.json").is_file()
                  and (p / "card.json").is_file())


def main():
    cycles = sys.argv[1] if len(sys.argv) > 1 else None
    ids = engine_ids()
    if not ids:
        sys.exit("!! no engine packages with a card.json")
    img = os113.cycles_main(cycles) if cycles else None

    print("engine            code+tables   variables   image added")
    total_img = 0
    for mid in ids:
        m = json.loads((ENGINES / mid / "manifest.json").read_text(encoding="utf-8"))
        if img is not None:
            _tweak, info = compose.compose(img, [mid])
            pkg = next((p for p in info["packages"] if p["id"] == mid), None)
            added = info["image"]
        else:
            pkg = None
            added = 0
        cpu = m.get("cost", {}).get("emulation", {})
        tone = (cpu.get("reference") or {}).get("TONE")
        print(f"  {mid:12s} {pkg['code'] if pkg else 0:8d}   {pkg['bss'] if pkg else 0:8d}   {added:8d}   "
              f"CPU {cpu.get('per_voice', {}).get('max', '?')} vs TONE {tone or '?'}")
        total_img += added

    if img is not None:
        _tweak, info = compose.compose(img, ids)
        print(f"\nall {len(ids)} engines together: {info['image']} bytes added to the image, "
              f"{info['image']} / {BUDGET} of the image budget used ({100 * info['image'] / BUDGET:.1f}%)")
        if info["image"] > BUDGET:
            sys.exit(f"!! the full build ({info['image']} bytes) exceeds the image budget ({BUDGET} bytes)")
        print("size report OK: the full build fits the image budget")
    else:
        print("\n(pass the official model-cycles_OS1.13.syx to also measure the composed sizes)")


if __name__ == "__main__":
    main()
