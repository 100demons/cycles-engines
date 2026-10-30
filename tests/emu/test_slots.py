#!/usr/bin/env python3
"""Stable slots test (Phase 3): a build holds slots 7 .. the highest ticked engine, every unticked slot below filled
with the shared silent `----` placeholder. In the real OS voice loop (Unicorn):

  - LiXR built alone is slot 8 (machine 7), with slot 7 as the silent placeholder;
  - the placeholder track renders silence;
  - Feral + LiXR puts Feral at slot 7 (machine 6) and LiXR at slot 8 (machine 7);
  - LiXR keeps the same machine number in both builds (so a saved sound finds it again).

    python3 tests/emu/test_slots.py --cycles model-cycles_OS1.13.syx [--blocks 60]
"""
import argparse
import struct
import sys

import numpy as np

from harness import (check, FAIL, main_os_from_syx, boot_hook_ok, bootstrap_depack_ok, built, engine, E, os113)


def names_of(runtime):
    """The MACHINES page's name table, read from the payload's data block: the added machines' names (the stock six
    point into the stock OS, so they read as '<stock>')."""
    n = runtime[os113.STUBS + 0x400 - os113.PAY_ALONE]     # the parameter block's .byte n
    nm = os113.FIRST + n
    data_off = os113.DATA - os113.PAY_ALONE
    names = []
    for i in range(os113.FIRST, nm):
        ptr = struct.unpack_from(">I", runtime, data_off + 4 * i)[0]
        off = ptr - os113.PAY_ALONE
        if os113.DATA <= ptr < os113.PAY_ALONE + len(runtime):
            end = runtime.index(b"\0", off)
            names.append(runtime[off:end].decode("ascii"))
        else:
            names.append("<stock>")
    return names


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--cycles", required=True)
    ap.add_argument("--blocks", type=int, default=60)
    args = ap.parse_args()
    stock = main_os_from_syx(args.cycles)

    # LiXR alone: slot 8 (machine 7), slot 7 is the placeholder
    tweak, info, img, runtime = built(stock, ["lixr"])
    print("\nlixr alone: " + ", ".join(f"{p['id']} = machine {p['index']}" for p in info["packages"]))
    check(bootstrap_depack_ok(args.cycles, stock, tweak), "lixr alone: bootstrap depack")
    check(boot_hook_ok(img, runtime), "lixr alone: boot hook")
    e = engine(img, runtime, tweak)
    check(not e.unmapped, "lixr alone: voice loop starts, no unmapped access")
    # the placeholder (machine 6) is silent
    e.set(0, machine=6, note=60, pitch=64, finetune=64, color=64, shape=64, sweep=64, contour=64, punch=0, gate=0,
          decay=64)
    x = e.render(args.blocks, trig_at=(1,), track=0)
    check(int(np.abs(x).max()) == 0 and not e.unmapped, f"lixr alone: placeholder (machine 6) is silent (peak {int(np.abs(x).max())})")
    # LiXR (machine 7) makes sound
    e.set(0, machine=7, note=36, pitch=64, finetune=64, color=64, shape=64, sweep=64, contour=48, punch=0, gate=0,
          decay=50)
    x = e.render(args.blocks, trig_at=(1,), track=0)
    check(int(np.abs(x).max()) > 1_000_000 and not e.unmapped,
          f"lixr alone: LiXR (machine 7) makes sound (peak {int(np.abs(x).max())})")
    names = names_of(runtime)
    check("----" in names, f"lixr alone: the MACHINES page name table holds ---- (got {names})")

    # Feral + LiXR: Feral at slot 7 (machine 6), LiXR at slot 8 (machine 7)
    tweak2, info2, img2, runtime2 = built(stock, ["feral", "lixr"])
    print("\nferal+lixr: " + ", ".join(f"{p['id']} = machine {p['index']}" for p in info2["packages"]))
    check(bootstrap_depack_ok(args.cycles, stock, tweak2), "feral+lixr: bootstrap depack")
    check(boot_hook_ok(img2, runtime2), "feral+lixr: boot hook")
    e2 = engine(img2, runtime2, tweak2)
    check(not e2.unmapped, "feral+lixr: voice loop starts, no unmapped access")
    for p in info2["packages"]:
        if p["id"] == "placeholder":
            continue
        e2.set(0, machine=p["index"], note=36, pitch=64, finetune=64, color=64, shape=64, sweep=64, contour=64,
               punch=0, gate=0, decay=50)
        x = e2.render(args.blocks, trig_at=(1,), track=0)
        check(int(np.abs(x).max()) > 1_000_000 and not e2.unmapped,
              f"feral+lixr: {p['id']} (machine {p['index']}) makes sound (peak {int(np.abs(x).max())})")
    # the stable-slot property: LiXR is machine 7 in both builds
    lixr_alone = next(p for p in info["packages"] if p["id"] == "lixr")
    lixr_pair = next(p for p in info2["packages"] if p["id"] == "lixr")
    check(lixr_alone["index"] == lixr_pair["index"] == 7,
          f"stable slot: LiXR is machine 7 alone ({lixr_alone['index']}) and with Feral ({lixr_pair['index']})")

    print("\nALL OK" if not FAIL else f"\n{len(FAIL)} FAILED: " + "; ".join(FAIL[:8]))
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
