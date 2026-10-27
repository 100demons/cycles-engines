#!/usr/bin/env python3
"""Render SHEEN's demo WAVs with the host DSP (the same sheen_dsp.h as the device).

    python3 tests/sheen/make_demos.py
"""
import pathlib
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent.parent
sys.path.insert(0, str(HERE))
import test_sheen_sound as S          # noqa: E402

OUT = ROOT / "build" / "sheen-demos"
OUT.mkdir(parents=True, exist_ok=True)


def script(name, blocks=1200, trigs=(32,), ends=(), tv=(), tp=(), **kw):
    p = dict(note=60, pitch=64, fine=64, color=64, shape=64, sweep=64, contour=64, decay=40,
             punch=0, gate=0, vel=100)
    p.update(kw)
    lines = [f"{k} {v}" for k, v in p.items()]
    lines += [f"blocks {blocks}"] + [f"trig {t}" for t in trigs]
    lines += [f"trigvel {a} {b}" for a, b in tv] + [f"trigpunch {a} {b}" for a, b in tp]
    lines += [f"end {t}" for t in ends] + [f"out {OUT / (name + '.wav')}"]
    f = S.OUT / "scripts" / f"demo_{name}.txt"
    f.parent.mkdir(exist_ok=True)
    f.write_text("\n".join(lines) + "\n", encoding="utf-8")
    subprocess.run([str(S.HOST), str(f)], check=True)
    print(f"  {OUT / (name + '.wav')}")


def main():
    # 00: the hat groove — 2 bars at 120 BPM of 1/16 notes, open on the off-beat 8ths,
    # plus a 1/32 roll on the last beat.
    step = int(round(0.125 * 48000 / 32)) * 32       # 1/16 note, on the 32-frame grid
    trigs, tv, tp = [], [], []
    for i in range(28):
        t = 32 + i * step
        trigs.append(t)
        vel = 110 if i % 2 == 0 else 70
        tv.append((t, vel))
        tp.append((t, 1 if (i % 4) == 2 else 0))       # the off-beat 8th of each beat is open
    # a 1/32 roll over the last beat (8 notes, 62.5 ms apart), ramping 40 -> 120
    for k in range(8):
        t = 32 + 28 * step + int(round(k * 0.0625 * 48000 / 32)) * 32
        trigs.append(t)
        tv.append((t, int(round(40 + k * 80 / 7))))
        tp.append((t, 0))
    script("00-groove", blocks=6400, trigs=tuple(sorted(trigs)), tv=tuple(tv), tp=tuple(tp))

    for num, name, kw in S.RECIPES:
        blocks = 1200
        ends = ()
        if name in ("ride", "crash", "gong", "reverse_cymbal"):
            blocks = 2600
        if name == "note_length_open":
            ends = (32 + int(0.250 * 48000),)
        script(f"{num:02d}-{name.replace('_', '-')}", blocks=blocks, ends=ends, **kw)

    # four macro sweeps: eight hits, the macro from 0 to 127, to hear each direction
    vals = [0, 16, 32, 48, 64, 80, 96, 112, 127]
    for knob, label in (("color", "color"), ("shape", "size"), ("sweep", "tick"), ("contour", "heat")):
        trigs = tuple(32 + i * int(round(0.250 * 48000 / 32)) * 32 for i in range(len(vals)))
        # the host has one value per parameter, so render short files and concatenate is overkill; instead
        # one file per point keeps the sweep honest and simple
        for v in vals:
            script(f"90-{label}-{v:03d}", blocks=900, trigs=(32,), **{knob: v})


if __name__ == "__main__":
    main()
