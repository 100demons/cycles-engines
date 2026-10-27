#!/usr/bin/env python3
"""Render the 16 CRACK recipes and the macro sweeps into build/crack-demos/ (32-bit PCM WAV, 48 kHz).

    python3 tests/crack/render_demos.py
"""
import pathlib
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent.parent
OUT = ROOT / "build" / "crack-demos"
SRC = HERE / "crack_host.c"
INC = ROOT / "engines" / "crack"


def build():
    OUT.mkdir(parents=True, exist_ok=True)
    exe = ROOT / "build" / "crack-test" / "crack_host"
    exe.parent.mkdir(parents=True, exist_ok=True)
    base = ["cc", "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror", f"-I{INC}", str(SRC), "-o", str(exe)]
    r = subprocess.run(base, capture_output=True, text=True)
    if r.returncode:
        for sdk in sorted(pathlib.Path("/Library/Developer/CommandLineTools/SDKs").glob("MacOSX*.sdk"),
                          key=lambda p: p.name, reverse=True):
            if sdk.name == "MacOSX.sdk":
                continue
            r2 = subprocess.run(base[:1] + ["-isysroot", str(sdk)] + base[1:], capture_output=True, text=True)
            if r2.returncode == 0:
                return exe
        sys.stderr.write(r.stdout + r.stderr)
        sys.exit("cannot build the host renderer")
    return exe


RECIPES = [
    (1, "hero-snare", dict()),
    (2, "hero-rimshot", dict(punch=1)),
    (3, "ghost-note", dict(vel=35)),
    (4, "trap-clap-snare", dict(pitch=70, color=100, shape=60, sweep=70, contour=90, decay=45)),
    (5, "808-style-snare", dict(color=40, shape=32, sweep=30, contour=20, decay=50)),
    (6, "909-style-snare", dict(color=50, shape=36, sweep=64, contour=40, decay=44)),
    (7, "brush", dict(color=56, shape=64, sweep=0, contour=10, decay=30, vel=60)),
    (8, "gated-80s-snare", dict(pitch=60, color=70, shape=48, sweep=64, contour=70, decay=115)),
    (9, "kick", dict(pitch=40, color=0, shape=0, sweep=64, contour=80, decay=60)),
    (10, "tom", dict(pitch=52, color=8, shape=40, sweep=40, contour=40, decay=70)),
    (11, "clap", dict(color=127, shape=64, sweep=50, contour=60, decay=50)),
    (12, "metallic-hat", dict(pitch=100, color=80, shape=120, sweep=80, contour=50, decay=8)),
    (13, "metal-hit", dict(color=30, shape=127, sweep=64, contour=96, decay=60)),
    (14, "zap", dict(color=0, shape=64, sweep=127, contour=64, decay=30)),
    (15, "crushed-lo-fi-snare", dict(color=64, shape=64, sweep=64, contour=127, decay=40)),
]


def render(exe, name, params, blocks=900, ends=()):
    p = dict(note=60, pitch=64, fine=64, color=64, shape=64, sweep=64, contour=64, decay=40,
             punch=0, gate=0, vel=100)
    p.update(params)
    script = OUT / f"{name}.txt"
    wav = OUT / f"{name}.wav"
    lines = [f"{k} {v}" for k, v in p.items()]
    lines += [f"blocks {blocks}", "trig 32"] + [f"end {e}" for e in ends] + [f"out {wav}"]
    script.write_text("\n".join(lines) + "\n", encoding="utf-8")
    subprocess.run([str(exe), str(script)], check=True, capture_output=True)


def main():
    exe = build()
    for num, name, params in RECIPES:
        render(exe, f"{num:02d}-{name}", params)
    # sustained bass: one note held across four steps (480 ms)
    render(exe, "16-sustained-bass",
           dict(pitch=40, color=0, shape=10, sweep=20, contour=40, decay=64, gate=1),
           blocks=1200, ends=(32 + int(0.48 * 48000),))
    # macro sweeps: 0 / 32 / 64 / 96 / 127
    for macro, key in (("wires", "color"), ("body", "shape"), ("snap", "sweep"), ("heat", "contour")):
        for v in (0, 32, 64, 96, 127):
            render(exe, f"macro-{macro}-{v:03d}", {key: v}, blocks=600)
    for v in (0, 40, 64, 100, 115):
        render(exe, f"macro-decay-{v:03d}", {"decay": v}, blocks=1200)
    files = sorted(p.name for p in OUT.glob("*.wav"))
    print(f"wrote {len(files)} WAVs to {OUT}")
    for f in files:
        print("  " + f)


if __name__ == "__main__":
    main()
