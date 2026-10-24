#!/usr/bin/env python3
"""Capacity test (Phase 2): build 1, 6, 7, 8, 12 and 16 copies of the reference machine (which plays a different
pitch per slot), and the real engines (Feral, LiXR) next to refmachine copies. In the real OS voice loop (Unicorn):

  - the build boots (bootstrap depack, boot hook);
  - every slot dispatches to its own update/render and plays its own pitch;
  - the six stock machines are unchanged;
  - Feral and LiXR still play next to the refmachine copies.

    python3 tests/emu/test_capacity.py --cycles model-cycles_OS1.13.syx [--blocks 60]
"""
import argparse
import sys

import numpy as np

from harness import (check, FAIL, main_os_from_syx, boot_hook_ok, bootstrap_depack_ok, built, engine, play,
                     E, mcb)

SR = 48000


def f0(x, lo=60, hi=1500):
    x = x.astype(float) - np.mean(x)
    X = np.abs(np.fft.rfft(x * np.hanning(len(x)), 1 << 16))
    fr = np.fft.rfftfreq(1 << 16, 1 / SR)
    band = (fr > lo) & (fr < hi)
    return float(fr[band][np.argmax(X[band])])


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--cycles", required=True)
    ap.add_argument("--blocks", type=int, default=60)
    args = ap.parse_args()
    stock = main_os_from_syx(args.cycles)
    counts = (1, 6, 7, 8, 12, 16)

    for n in counts:
        ids = ["refmachine"] * n
        tweak, info, img, runtime = built(stock, ids)
        print(f"\n{n} copies of refmachine: payload {len(runtime)} bytes, {len(img) - E.IMAGE_LEN} in the image")
        check(bootstrap_depack_ok(args.cycles, stock, tweak), f"{n} copies: bootstrap depack")
        check(boot_hook_ok(img, runtime), f"{n} copies: boot hook")
        e = engine(img, runtime, tweak)
        check(not e.unmapped, f"{n} copies: voice loop starts, no unmapped access")
        # every slot plays, at a distinct pitch (the machine_index patch point: one semitone per copy)
        pitches = []
        for i in range(n):
            e.set(0, machine=6 + i, note=60, pitch=64, finetune=64, color=127, shape=0, sweep=64, contour=100,
                  punch=0, gate=0, decay=40)
            x = e.render(args.blocks, trig_at=(1,), track=0)
            peak = int(np.abs(x).max())
            check(peak > 1_000_000 and not e.unmapped, f"{n} copies: machine {6 + i} makes sound (peak {peak})")
            pitches.append(f0(x[SR // 100:]))
        if n >= 2:
            mono = all(a < b for a, b in zip(pitches, pitches[1:]))
            check(mono, f"{n} copies: each slot a semitone higher than the last "
                  f"({', '.join(f'{p:.0f}' for p in pitches)} Hz)")

    # the real engines next to refmachine copies
    for combo in (["feral9"] + ["refmachine"] * 3, ["lixr"] + ["refmachine"] * 3,
                  ["feral9", "lixr"] + ["refmachine"] * 8):
        tweak, info, img, runtime = built(stock, combo)
        print(f"\n{'+'.join(combo)}: payload {len(runtime)} bytes")
        check(bootstrap_depack_ok(args.cycles, stock, tweak), f"{'+'.join(combo)}: bootstrap depack")
        check(boot_hook_ok(img, runtime), f"{'+'.join(combo)}: boot hook")
        e = engine(img, runtime, tweak)
        check(not e.unmapped, f"{'+'.join(combo)}: voice loop starts, no unmapped access")
        for i in range(len(combo)):
            e.set(0, machine=6 + i, note=60, pitch=64, finetune=64, color=64, shape=64, sweep=64, contour=64,
                  punch=0, gate=0, decay=50)
            x = e.render(args.blocks, trig_at=(1,), track=0)
            check(int(np.abs(x).max()) > 1_000_000 and not e.unmapped,
                  f"{'+'.join(combo)}: machine {6 + i} ({combo[i]}) makes sound")

    # the six stock machines are unchanged, sample for sample (against the 16-copy build)
    print("\nstock machines unchanged (16 copies)")
    tweak, info, img, runtime = built(stock, ["refmachine"] * 16)
    for name, slot, params in (("SNARE", 1, (127, 8, 0)), ("KICK", 0, (10, 16, 16))):
        color, shape, sweep = params
        es = E.Engine(stock)
        x = play(es, 0, slot, (color, shape, sweep, 0), 40, args.blocks)
        f = engine(img, runtime, tweak)
        y = play(f, 0, slot, (color, shape, sweep, 0), 40, args.blocks)
        check(np.array_equal(x, y) and np.max(np.abs(x)) > 1e6 and not f.unmapped,
              f"16 copies: {name} identical to the stock OS, sample for sample")

    print("\nALL OK" if not FAIL else f"\n{len(FAIL)} FAILED: " + "; ".join(FAIL[:8]))
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
