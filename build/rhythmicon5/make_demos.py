#!/usr/bin/env python3
"""Render the four Rhythmicon V5 Cloud demo WAVs with the real host DSP.

The dry core and the cloud are the same fixed-point code as the Cycles build. A simple gate envelope stands in for
the OS amp chain: it is applied BEFORE the cloud, exactly where the OS amp chain sits, so the cloud tail outlives the
note. No audio is taken from the firmware.

    PATH=~/m68k-elf-16.2/bin:$PATH python3 build/rhythmicon5/make_demos.py
"""
import math
import pathlib
import sys
import wave

import numpy as np

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / "tests" / "host"))
import r5_host as H  # noqa: E402

FS = 48000
BLK = 32
OUT = HERE / "wav"


def env(t, start, end, attack=0.008, release=0.30):
    if t < start or t > end + release * 8:
        return 0.0
    if t < start + attack:
        return (t - start) / attack
    if t <= end:
        return 1.0
    return math.exp(-(t - end) / (release / 6.0))


def render(path, seconds, notes, shape_fn, tim, cld, punch_fn=None, color=63, decay=80):
    h = H.Host()
    h.reset(0)
    blocks = int(seconds * FS / BLK)
    out = []
    for b in range(blocks):
        t = b * BLK / FS
        trig = any(abs(t - n[0]) < BLK / FS / 2 for n in notes)
        # one voice: the last note that started before t
        active = [n for n in notes if n[0] <= t]
        midi = active[-1][1] if active else 48
        shape = shape_fn(t) if shape_fn else 27
        punch = punch_fn(t) if punch_fn else 0
        h.set(midi << 16, 64 << 8, 64 << 8, color << 8, int(shape) << 8, tim, cld, punch, 1 if trig else 0)
        x = h.dry()
        g = sum(env(t + i / FS, n[0], n[0] + n[2]) for i in range(0, BLK, 8) for n in notes) / (BLK // 8)
        g = min(g, 1.0)
        x = (x.astype(np.float64) * g).astype(np.int64)
        x = h.wet(x, 1)
        out.append(x)
    y = np.concatenate(out).astype(np.float64) / 2 ** 30
    n = int(0.010 * FS)
    if len(y) > 2 * n:
        y[:n] *= np.linspace(0, 1, n)
        y[-n:] *= np.linspace(1, 0, n)
    peak = np.max(np.abs(y)) or 1.0
    y = y / peak * 0.9
    OUT.mkdir(parents=True, exist_ok=True)
    pcm = (np.clip(y, -1, 1) * 32767).astype("<i2")
    with wave.open(str(OUT / path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(FS)
        w.writeframes(pcm.tobytes())
    print(f"{path}: {seconds:.1f} s, peak {peak:.2f} (before normalising), {len(pcm)} frames")


def main():
    # 1. default patch: a short modal phrase
    notes = [(0.0, 48, 0.55), (1.2, 55, 0.55), (2.4, 60, 0.55), (3.6, 63, 0.75),
             (5.0, 48, 0.55), (6.2, 55, 0.55), (7.4, 60, 0.55), (8.6, 67, 1.10)]
    render("rhythmicon5-default.wav", 11.0, notes, None, 70, 40)

    # 2. a slow SHAPE sweep, cloud dry (CONTOUR 0)
    notes = [(i * 0.80, 48 if i % 4 else 43, 0.35) for i in range(12)]
    render("rhythmicon5-shape-sweep-dry.wav", 10.0, notes, lambda t: min(127, 127 * t / 9.0), 70, 0)

    # 3. the same sweep, cloud wet
    render("rhythmicon5-shape-sweep-wet.wav", 10.0, notes, lambda t: min(127, 127 * t / 9.0), 90, 70)

    # 4. Freeze in action: let the cloud fill, freeze for 3 s, release
    notes = [(0.0, 48, 0.55), (1.2, 55, 0.55), (2.4, 60, 1.2)]
    render("rhythmicon5-freeze.wav", 9.0, notes, lambda t: 27, 100, 90,
           punch_fn=lambda t: 1 if 3.0 <= t < 6.0 else 0)


if __name__ == "__main__":
    main()
