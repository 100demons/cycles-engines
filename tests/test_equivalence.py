#!/usr/bin/env python3
"""Equivalence test: docs/flasher/composer.js == core/compose.py, byte for byte.

This is the gate of ARCHITECTURE.md §7.1. It writes a reference (compose.json: each package placed at a set of
addresses; composed.json: the composed tweaks and the built MAIN OS for every selection, and fill() for synthetic
machines) from the Python composer, then runs tests/webcompose_check.js in node, which composes the same thing from
the committed frame.json and the package bundles and compares it to the reference, byte for byte.

    python3 tests/test_equivalence.py [model-cycles_OS1.13.syx]

Part 1 (relocate.place) runs without the firmware file; part 2 (compose + build + fill) needs the official file.
"""
import argparse
import contextlib
import io
import json
import pathlib
import random
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(ROOT / "core"))

import compose                    # noqa: E402
import os113                      # noqa: E402
import relocate                   # noqa: E402

ENGINES = ROOT / "engines"
FRAME = ROOT / "core" / "registrar" / "frame.json"
CODE_END = 0x48000000             # no limit here: the composer checks the room, not this comparison
COMPARED = ("id", "order", "name", "version", "conflicts", "writes", "append", "symbols")


def packages():
    """(id, manifest, object bytes) of every engine package with exactly one committed object (not the placeholder)."""
    for d in sorted(p for p in ENGINES.iterdir() if p.is_dir()):
        mf = d / "manifest.json"
        if not mf.is_file():
            continue
        m = json.loads(mf.read_text(encoding="utf-8"))
        if m["id"] == "placeholder":
            continue
        objs = m.get("build", {}).get("objects", [])
        if len(objs) == 1 and (d / objs[0]).is_file():
            yield m["id"], m, (d / objs[0]).read_bytes()


def part1(out):
    rnd = random.Random(61)
    ref = {}
    for mid, m, obj in packages():
        b = m["build"]
        upd, rnd_sym, keep = b["symbols"]["update"], b["symbols"]["render"], b.get("keep", [])
        names = [upd, rnd_sym, *keep]
        lay = relocate.layout(obj, upd, rnd_sym, keep, mid)
        points = [pp["symbol"] for pp in b.get("patch_points", []) if "symbol" in pp]
        spots = [(0x43000000, 0x43036000), (0x43000518, 0x430360c0), (0x43007740, 0x4304a038)]
        spots += [(0x43000000 + 4 * rnd.randrange(0x8000), 0x43036000 + 4 * rnd.randrange(0x8000)) for _ in range(20)]
        cases = []
        for code_at, bss_at in spots:
            values = {p: rnd.randrange(200) for p in points}
            blob, syms, end = relocate.place(obj, upd, rnd_sym, keep, code_at, bss_at, CODE_END, values, mid)
            cases.append({"code_at": code_at, "bss_at": bss_at, "values": values, "blob": blob.hex(),
                          "symbols": {k: syms[k] for k in names}, "bss_end": end})
        ref[mid] = {"bundle": relocate.bundle(lay, names), "cases": cases}
        print(f"{mid}: {len(lay['code'])} bytes of code and tables, {len(lay['relocs'])} relocations, "
              f"{len(cases)} placements")
    (out / "compose.json").write_text(json.dumps(ref), encoding="utf-8")


def selections(ids):
    """each engine alone, all engines, and 20 random subsets (the architecture's §7.1 list)."""
    rnd = random.Random(63)
    out = [[i] for i in ids] + [ids]
    for _ in range(20):
        sub = [i for i in ids if rnd.random() < 0.5]
        if not sub:
            sub = [rnd.choice(ids)]
        rnd.shuffle(sub)
        if sub not in out:
            out.append(sub)
    return out


def part2(out, cycles):
    raw = pathlib.Path(cycles).read_bytes()
    img = os113.cycles_main(cycles)
    if len(img) != os113.IMAGE_LEN:
        sys.exit("!! not the official MAIN OS 1.13")
    frs = json.loads(FRAME.read_text(encoding="utf-8"))
    regen = compose.frames(img, compose.load_registrar())
    if json.dumps(frs, sort_keys=True) != json.dumps(regen, sort_keys=True):
        sys.exit("!! core/registrar/frame.json is not up to date: run core/gen_registrar.py --cycles ...")
    ids = [p for p, _, _ in packages()]
    cases, pkgs = [], {}
    for sel in selections(ids):
        tweak, info = compose.compose(img, sel)
        with contextlib.redirect_stdout(io.StringIO()):
            _syx, patched = compose.build_syx(raw, [tweak])
        for i in sel:
            pkgs.setdefault(i, compose.web_package(i))
        cases.append({"ids": sel, "tweak": {k: tweak[k] for k in COMPARED}, "main_os": compose.sha(patched)})
        print(f"{'+'.join(sel)}: {len(tweak['writes'])} writes, {info['image']} bytes added, MAIN OS {compose.sha(patched)[:12]}")
    # fill() for 1 to max_machines synthetic machines
    rnd, lay, fills = random.Random(62), frs["layout"], []
    for n in range(1, frs["runtime"]["max_machines"] + 1):
        machines, blobs, at = [], [], lay["pay"]
        for i in range(n):
            size = 4 * rnd.randrange(1, 64)
            blobs.append((at, bytes(rnd.randrange(256) for _ in range(size))))
            machines.append(dict(name="Tone" if i == 1 else f"MACH{i}", image=rnd.randrange(8), decay=rnd.randrange(128),
                                 knobs=[("Tone", "TONE", 127), ("Start", "STRT", 0, 0, 120), (f"Knob{i}", f"K{i}", 64),
                                        ("Level", "LEVL", 100, 1, 127)],
                                 update=at, render=at + 2 * rnd.randrange(size // 2)))
            at += size + 4 * rnd.randrange(24)
        bss_end = lay["pay"] + lay["bss_off"]
        if n % 2 == 0:
            blobs.append((bss_end, bytes(rnd.randrange(256) for _ in range(4 * rnd.randrange(1, 32)))))
            bss_end += len(blobs[-1][1])
        bss_end += 4 * rnd.randrange(1, 0x400)
        writes, append = compose.fill(compose.frame_of(frs, n), img, machines, blobs, bss_end)
        fills.append({"machines": machines, "blobs": [[a, b.hex()] for a, b in blobs], "bss_end": bss_end,
                      "writes": writes, "append": append})
    print(f"fill: {len(fills)} synthetic builds of 1 to {len(fills)} machines")
    device = json.loads((ROOT / "core" / "os113_dev.json").read_text(encoding="utf-8"))
    slots = compose.load_slots()
    placeholder = compose.web_package("placeholder")
    (out / "composed.json").write_text(json.dumps({
        "device": device, "frames": frs, "packages": pkgs, "cases": cases, "fills": fills,
        "slots": slots, "placeholder": placeholder}), encoding="utf-8")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("cycles", nargs="?", help="official model-cycles_OS1.13.syx")
    args = ap.parse_args()
    with tempfile.TemporaryDirectory() as d:
        out = pathlib.Path(d)
        part1(out)
        if args.cycles:
            part2(out, args.cycles)
        cmd = ["node", str(HERE / "webcompose_check.js"), str(out)]
        if args.cycles:
            cmd.append(str(pathlib.Path(args.cycles).resolve()))
        r = subprocess.run(cmd)
        sys.exit(r.returncode)


if __name__ == "__main__":
    main()
