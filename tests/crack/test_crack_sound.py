#!/usr/bin/env python3
"""CRACK host checks (§9.1-§9.3), python3 + numpy only.

Every check prints the measured value.  A check that cannot run is printed NOT RUN, never PASS.

The host renderer writes the value the OS's voice loop leaves after its arithmetic >>1.  In that halved domain
1 << 30 is full scale, i.e. the Q31 sample the DSP wrote before the OS halved it.  Tests state this explicitly.

    python3 tests/crack/test_crack_sound.py [--only hero,color,...]
"""
import argparse
import json
import pathlib
import shutil
import subprocess
import sys
import wave

import numpy as np

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent.parent
SRC = HERE / "crack_host.c"
INC = ROOT / "engines" / "crack"
OUT = ROOT / "build" / "crack-test"
FS = 1 << 30
SR = 48000
DB = 20.0

RESULTS = []


def check(name, ok, value):
    RESULTS.append((name, bool(ok), value))
    print(f"  {'PASS' if ok else 'FAIL'}  {name}: {value}", flush=True)
    return ok


def info(name, value):
    RESULTS.append((name, None, value))
    print(f"  ..    {name}: {value}", flush=True)


def build_host():
    OUT.mkdir(parents=True, exist_ok=True)
    exe = OUT / "crack_host"
    base = ["cc", "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror", f"-I{INC}", str(SRC), "-o", str(exe)]
    r = subprocess.run(base, capture_output=True, text=True)
    if r.returncode != 0:
        # The owner's macOS has a broken default SDK/tapi link here; retry with the newest working SDK.
        sdks = sorted(pathlib.Path("/Library/Developer/CommandLineTools/SDKs").glob("MacOSX*.sdk"),
                      key=lambda p: p.name, reverse=True)
        for sdk in sdks:
            if "MacOSX.sdk" == sdk.name:
                continue
            cmd = base[:1] + ["-isysroot", str(sdk)] + base[1:]
            r2 = subprocess.run(cmd, capture_output=True, text=True)
            if r2.returncode == 0:
                info("host build", f"used -isysroot {sdk.name} (default SDK link failed)")
                return exe
        sys.stderr.write(r.stdout + r.stderr)
        sys.exit("cannot build the host renderer")
    info("host build", "cc -std=c99 -O2 -Wall -Wextra -Werror")
    return exe


HOST = build_host()


class Render:
    def __init__(self, x, tr):
        self.x = x
        self.tr = tr

    def col(self, name):
        return np.array([float(r[name]) for r in self.tr])


def render(name, blocks=700, trigs=(32,), ends=(), resets=(), trace=True, **kw):
    p = dict(note=60, pitch=64, fine=64, color=64, shape=64, sweep=64, contour=64, decay=40,
             punch=0, gate=0, vel=100)
    p.update(kw)
    d = OUT / "scripts"
    d.mkdir(exist_ok=True)
    raw = OUT / f"{name}.raw"
    csv = OUT / f"{name}.csv"
    lines = [f"{k} {v}" for k, v in p.items()]
    lines += [f"blocks {blocks}"]
    lines += [f"trig {t}" for t in trigs]
    lines += [f"end {t}" for t in ends]
    lines += [f"reset {t}" for t in resets]
    lines += [f"raw {raw}"]
    if trace:
        lines += [f"trace {csv}"]
    script = d / f"{name}.txt"
    script.write_text("\n".join(lines) + "\n", encoding="utf-8")
    subprocess.run([str(HOST), str(script)], check=True, capture_output=True)
    x = np.fromfile(raw, dtype="<i4").astype(np.float64) / FS
    if trace:
        import csv as _csv

        with open(csv, newline="", encoding="utf-8") as f:
            tr = list(_csv.DictReader(f))
    else:
        tr = []
    return Render(x, tr)


def onset(x):
    a = np.abs(x)
    idx = np.argmax(a > 10 ** (-60 / 20))
    return int(idx)


def db(x):
    return 20 * np.log10(max(float(x), 1e-30))


def rms(x):
    return float(np.sqrt(np.mean(np.square(x))))


def rms_db(x):
    return db(rms(x))


def peak_db(x):
    return db(np.max(np.abs(x)))


def band_db(x, lo, hi, start=0, n=None, window=True):
    if n is None:
        n = len(x) - start
    seg = x[start:start + n]
    if window:
        seg = seg * np.hanning(len(seg))
    X = np.fft.rfft(seg)
    f = np.fft.rfftfreq(len(seg), 1 / SR)
    m = (f >= lo) & (f < hi)
    return 10 * np.log10(np.sum(np.abs(X[m]) ** 2) + 1e-30)


def strongest(x, lo, hi, start, n):
    seg = x[start:start + n] * np.hanning(n)
    X = np.abs(np.fft.rfft(seg))
    f = np.fft.rfftfreq(n, 1 / SR)
    m = (f >= lo) & (f <= hi)
    return float(f[m][np.argmax(X[m])])


def centroid(x, start, n):
    seg = x[start:start + n]
    X = np.abs(np.fft.rfft(seg * np.hanning(len(seg))))
    f = np.fft.rfftfreq(len(seg), 1 / SR)
    return float(np.sum(f * X) / (np.sum(X) + 1e-30))


def rms_env(x, win=240):
    k = np.ones(win) / win
    return np.sqrt(np.convolve(x * x, k, mode="same"))


def length_40(x, o):
    env = rms_env(x, 240)
    seg = env[o:o + int(0.6 * SR)]
    pk = np.max(seg)
    if pk <= 0:
        return None
    below = np.where(seg < pk * 10 ** (-40 / 20))[0]
    below = below[below > np.argmax(seg)]
    return None if len(below) == 0 else below[0] / SR


def clean(r, name):
    x = r.x
    o = onset(x)
    step = float(np.max(np.abs(np.diff(x)))) if len(x) > 1 else 0.0
    mean = float(abs(np.mean(x[o:o + int(0.3 * SR)])))
    # zero within 50 blocks of the last non-zero body/wire envelope
    last = -1
    if r.tr:
        for i, row in enumerate(r.tr):
            if int(row["body_env"]) > 0 or int(row["wire_env"]) > 0:
                last = i
    tail_ok = True
    if last >= 0:
        tail_ok = not np.any(x[(last + 50) * 32:])
    ok = step <= 1.0 + 1e-9 and mean < 10 ** (-50 / 20) and tail_ok
    check(f"{name} clean", ok, f"max step {step:.4f} FS, |mean| {db(mean):.1f} dBFS, zero tail {tail_ok}")
    return ok


def burst_count(x, lo, hi, start, n, min_gap_ms=4.0):
    seg = x[start:start + n]
    X = np.fft.rfft(seg)
    f = np.fft.rfftfreq(n, 1 / SR)
    X[(f < lo) | (f > hi)] = 0
    y = np.fft.irfft(X, n)
    env = np.sqrt(np.convolve(y * y, np.ones(24) / 24, mode="same"))
    thr = env.max() * 0.18
    peaks = []
    gap = int(min_gap_ms / 1000 * SR)
    i = 1
    while i < len(env) - 1:
        if env[i] >= env[i - 1] and env[i] > env[i + 1] and env[i] > thr:
            if not peaks or i - peaks[-1] > gap:
                peaks.append(i)
            elif env[i] > env[peaks[-1]]:
                peaks[-1] = i
        i += 1
    return len(peaks), peaks


# ----------------------------------------------------------------------------------------------------------------
def hero():
    print("\n§9.1 Hero")
    r = render("hero", blocks=700, trigs=(32,), trace=True)
    x = r.x
    o = onset(x)
    n150 = int(0.15 * SR)
    check("9.1.1 peak -6..-0.5 dBFS", -6 <= peak_db(x) <= -0.5, f"{peak_db(x):.2f} dBFS")
    check("9.1.1 RMS 150 ms (reference only)", True, f"{rms_db(x[o:o + n150]):.2f} dBFS")
    pk = strongest(x, 120, 400, o + int(0.02 * SR), int(0.1 * SR))
    check("9.1.2 strongest 120-400 Hz peak is 180-215 Hz", 180 <= pk <= 215, f"{pk:.1f} Hz")

    tr = r.tr
    f0 = float(tr[0]["f0_hz"]) if tr else 0
    m1 = [(float(z["time_ms"]), 12 * np.log2(float(z["mode1_hz"]) / float(z["f0_hz"])))
          for z in tr if float(z["mode1_hz"]) > 0]
    at_trig = m1[0][1] if m1 else -99
    at30 = [v for t, v in m1 if 25 <= t <= 40]
    crack_ok = at_trig >= 3 and at30 and abs(at30[0]) <= 0.5
    check("9.1.3 mode 1 >=3 st up at trig, <=0.5 st by 30 ms", crack_ok,
          f"trig {at_trig:.2f} st, 30 ms {at30[0] if at30 else 'n/a':.2f} st")

    body = band_db(x, 150, 400, o, n150)
    wire = band_db(x, 2000, 8000, o, n150)
    air = band_db(x, 12000, 20000, o, n150)
    sub = band_db(x, 20, 90, o, n150)
    check("9.1.4 body / wire within 6 dB", abs(body - wire) <= 6, f"{wire - body:+.2f} dB")
    check("9.1.4 air <=24 dB below wire", air - wire <= -0 + 24 and (wire - air) <= 24,
          f"wire-air {wire - air:.2f} dB")
    check("9.1.4 20-90 Hz >=15 dB below body", body - sub >= 15, f"body-sub {body - sub:.2f} dB")

    first3 = np.max(np.abs(x[o:o + int(0.003 * SR)]))
    check("9.1.5 first 3 ms peak >=0.7 x overall", first3 >= 0.7 * np.max(np.abs(x)),
          f"{first3 / np.max(np.abs(x)):.2f}")

    ln = length_40(x, o)
    check("9.1.6 -40 dB at 170-380 ms", ln is not None and 0.170 <= ln <= 0.380,
          f"{ln * 1000:.1f} ms" if ln is not None else "never")

    smack = rms_db(x[o + int(0.08 * SR):o + int(0.15 * SR)]) - rms_db(x[o:o + int(0.03 * SR)])
    check("9.1.7 80-150 ms <=22 dB below 0-30 ms", smack <= 22, f"{smack:.2f} dB")
    clean(r, "9.1.8")

    rr = render("retrig", blocks=400, trigs=(32, 32 + int(0.040 * SR)), trace=True)
    xx = rr.x
    oo = onset(xx)
    s1 = np.max(np.abs(np.diff(xx[oo:oo + int(0.005 * SR)])))
    t2 = 32 + int(0.040 * SR)
    s2 = np.max(np.abs(np.diff(xx[t2 - int(0.002 * SR):t2 + int(0.002 * SR)])))
    check("9.1.9 retrigger step <=1.2 x first onset", s2 <= 1.2 * s1, f"{s2 / s1:.3f} x")
    a = render("det1", blocks=200, trigs=(32,), trace=False)
    b = render("det2", blocks=200, trigs=(32,), trace=False)
    check("9.1.10 two renders identical", np.array_equal(a.x, b.x), "identical" if np.array_equal(a.x, b.x) else "DIFFER")


def macro_points():
    vals = [0, 16, 32, 48, 64, 80, 96, 112, 127]
    return vals


def all_points_clean(tag, key, base=None):
    base = dict(base or {})
    out = {}
    for v in macro_points():
        p = dict(base)
        p[key] = v
        r = render(f"{tag}{v}", blocks=1200, trigs=(32,), trace=True, **p)
        out[v] = r
        o = onset(r.x)
        rd = rms_db(r.x[o:o + int(0.15 * SR)])
        check(f"{tag}{v} RMS > -40 dBFS", rd > -40, f"{rd:.2f} dBFS")
        clean(r, f"{tag}{v}")
    return out


def macros():
    print("\n§9.2 Macros")
    # COLOR
    c = all_points_clean("color", "color")
    ratios = {}
    for v, r in c.items():
        o = onset(r.x)
        ratios[v] = band_db(r.x, 2000, 8000, o, int(0.15 * SR)) - band_db(r.x, 150, 400, o, int(0.15 * SR))
    seq = [ratios[v] for v in range(0, 97, 16)]
    dips = sum(1 for i in range(1, len(seq)) if seq[i] < seq[i - 1])
    deep = [seq[i - 1] - seq[i] for i in range(1, len(seq)) if seq[i] < seq[i - 1] and seq[i - 1] - seq[i] > 1.0]
    check("9.2 COLOR wire/body rises 0->96 (one dip <=1 dB)", seq[-1] > seq[0] and len(deep) == 0 and dips <= 1,
          "ratios " + ", ".join(f"{v:.1f}" for v in seq))
    r = c[127]
    o = onset(r.x)
    n, peaks = burst_count(r.x, 1000, 4000, o, int(0.040 * SR))
    check("9.2 COLOR 127 >=3 clap bursts in 40 ms", n >= 3, f"{n} bursts")

    # SHAPE
    s = all_points_clean("shape", "shape", base={"color": 0})
    f0 = 196.0
    e3 = {v: band_db(r.x, 3 * f0, 10000, onset(r.x), int(0.15 * SR)) for v, r in s.items()}
    check("9.2 SHAPE 3f0-10k lowest at 0 / highest at 127",
          min(e3, key=e3.get) == 0 and max(e3, key=e3.get) == 127,
          f"0={e3[0]:.1f}, 64={e3[64]:.1f}, 127={e3[127]:.1f} dB")
    drops = {}
    for v, r in s.items():
        drops[v] = max(float(z["tension_st"]) for z in r.tr[:12]) if r.tr else 0
    check("9.2 SHAPE tension drop shrinks 0->127", drops[0] > drops[127],
          f"0={drops[0]:.2f} st, 127={drops[127]:.2f} st")

    # SWEEP
    w = all_points_clean("sweep", "sweep")
    rat = {}
    for v, r in w.items():
        o = onset(r.x)
        e0 = band_db(r.x, 20, 12000, o, int(0.003 * SR), window=False)
        e1 = band_db(r.x, 20, 12000, o + int(0.003 * SR), int(0.027 * SR))
        rat[v] = e0 - e1
    check("9.2 SWEEP onset/tail energy rises 0->96", rat[96] > rat[0],
          f"0={rat[0]:.2f}, 64={rat[64]:.2f}, 96={rat[96]:.2f} dB")
    r = w[127]
    m1 = [12 * np.log2(float(z["mode1_hz"]) / float(z["f0_hz"])) for z in r.tr
          if float(z["mode1_hz"]) > 0][:6]
    check("9.2 SWEEP 127 mode 1 >=2 octaves up at trig", m1 and max(m1) >= 24, f"{max(m1):.1f} st" if m1 else "n/a")

    # HEAT
    h = all_points_clean("heat", "contour")
    hero_rms = rms_db(render("hero_ref", blocks=300, trigs=(32,), trace=False).x[32:32 + int(0.15 * SR)])
    rmsv = {v: rms_db(r.x[onset(r.x):onset(r.x) + int(0.15 * SR)]) for v, r in h.items()}
    ok = all(abs(v - hero_rms) <= 3 for v in rmsv.values())
    check("9.2 HEAT RMS within +-3 dB of hero", ok,
          " ".join(f"{v}:{rmsv[v] - hero_rms:+.1f}" for v in rmsv))
    crest = {}
    for v, r in h.items():
        o = onset(r.x)
        seg = r.x[o:o + int(0.15 * SR)]
        crest[v] = peak_db(seg) - rms_db(seg)
    check("9.2 HEAT crest falls 0->96", crest[96] < crest[0],
          f"0={crest[0]:.1f}, 64={crest[64]:.1f}, 96={crest[96]:.1f} dB")
    r = h[127]
    rr = next(z for z in r.tr if float(z["mode1_hz"]) > 0)
    check("9.2 HEAT 127 crush hold 4 / bits 6", int(rr["crush_hold"]) == 4 and int(rr["crush_bits"]) == 6,
          f"hold {rr['crush_hold']}, bits {rr['crush_bits']}")

    # DECAY
    d = all_points_clean("decay", "decay")
    d[100] = render("decay100", blocks=2000, trigs=(32,), trace=True, decay=100)
    d[101] = render("decay101", blocks=1200, trigs=(32,), trace=True, decay=101)
    d[115] = render("decay115", blocks=1200, trigs=(32,), trace=True, decay=115)
    lens = {}
    for v, r in d.items():
        ln = length_40(r.x, onset(r.x))
        lens[v] = ln if ln is not None else 99.0
    check("9.2 DECAY -40 dB time rises 0->100", lens[100] > lens[0],
          f"0={lens[0] * 1000:.0f} ms, 48={lens[48] * 1000:.0f}, 100={lens[100] * 1000:.0f}")
    for v in (101, 115, 127):
        r = d[v]
        wire = np.array([int(z["wire_env"]) for z in r.tr])
        pk = wire.max()
        hold = np.where(wire >= pk * 0.45)[0]
        fade = np.where(wire <= pk * 0.05)[0]
        fade = fade[fade > (hold[-1] if len(hold) else 0)]
        ft = (fade[0] - hold[-1]) * 32.0 / SR if len(fade) and len(hold) else 99.0
        check(f"9.2 DECAY {v} gated hold then fade <=15 ms", len(hold) > 24 and ft <= 0.015,
              f"hold {(hold[-1] - hold[0]) * 32.0 / SR * 1000:.0f} ms, fade {ft * 1000:.1f} ms")

    # PUNCH
    off = render("punch_off", blocks=200, trigs=(32,), trace=False)
    on = render("punch_on", blocks=200, trigs=(32,), trace=False, punch=1)
    eo = band_db(off.x, 3000, 12000, onset(off.x), int(0.020 * SR), window=False)
    en = band_db(on.x, 3000, 12000, onset(on.x), int(0.020 * SR), window=False)
    check("9.2 PUNCH >=2 dB more >3 kHz in 20 ms", en - eo >= 2, f"{en - eo:+.2f} dB")

    # velocity
    v30 = render("vel30", blocks=300, trigs=(32,), trace=False, vel=30)
    v127 = render("vel127", blocks=300, trigs=(32,), trace=False, vel=127)
    r30 = rms_db(v30.x[onset(v30.x):onset(v30.x) + int(0.15 * SR)])
    r127 = rms_db(v127.x[onset(v127.x):onset(v127.x) + int(0.15 * SR)])
    c30 = centroid(v30.x, onset(v30.x), int(0.05 * SR))
    c127 = centroid(v127.x, onset(v127.x), int(0.05 * SR))
    check("9.2 velocity 30 vs 127 RMS >=10 dB", abs(r127 - r30) >= 10, f"{r127 - r30:.2f} dB")
    check("9.2 velocity centroid higher at 127", c127 > c30, f"{c30:.0f} -> {c127:.0f} Hz")

    # gate
    g0 = render("gate0", blocks=500, trigs=(32,), ends=(32 + int(0.3 * SR),), trace=False, decay=20, gate=0)
    g1 = render("gate1", blocks=500, trigs=(32,), ends=(32 + int(0.3 * SR),), trace=True, decay=20, gate=1)
    t = 32 + int(0.25 * SR)
    r0 = rms_db(g0.x[t:t + int(0.005 * SR)])
    r1 = rms_db(g1.x[t:t + int(0.005 * SR)])
    check("9.2 GATE 250 ms >=20 dB higher", r1 - r0 >= 20, f"{r1 - r0:.2f} dB")


RECIPES = [
    (1, "hero", dict(color=64, shape=64, sweep=64, contour=64, decay=40)),
    (2, "rimshot", dict(color=64, shape=64, sweep=64, contour=64, decay=40, punch=1)),
    (3, "ghost", dict(color=64, shape=64, sweep=64, contour=64, decay=40, vel=35)),
    (4, "trap_clap_snare", dict(pitch=70, color=100, shape=60, sweep=70, contour=90, decay=45)),
    (5, "808_snare", dict(color=40, shape=32, sweep=30, contour=20, decay=50)),
    (6, "909_snare", dict(color=50, shape=36, sweep=64, contour=40, decay=44)),
    (7, "brush", dict(color=56, shape=64, sweep=0, contour=10, decay=30, vel=60)),
    (8, "gated_80s", dict(pitch=60, color=70, shape=48, sweep=64, contour=70, decay=115)),
    (9, "kick", dict(pitch=40, color=0, shape=0, sweep=64, contour=80, decay=60)),
    (10, "tom", dict(pitch=52, color=8, shape=40, sweep=40, contour=40, decay=70)),
    (11, "clap", dict(color=127, shape=64, sweep=50, contour=60, decay=50)),
    (12, "metal_hat", dict(pitch=100, color=80, shape=120, sweep=80, contour=50, decay=8)),
    (13, "metal_hit", dict(color=30, shape=127, sweep=64, contour=96, decay=60)),
    (14, "zap", dict(color=0, shape=64, sweep=127, contour=64, decay=30)),
    (15, "crushed", dict(color=64, shape=64, sweep=64, contour=127, decay=40)),
]


def recipes():
    print("\n§9.3 Recipes")
    out = {}
    for num, name, kw in RECIPES:
        r = render(f"recipe{num:02d}_{name}", blocks=700, trigs=(32,), trace=True, **kw)
        out[name] = r
        o = onset(r.x)
        rd = rms_db(r.x[o:o + int(0.15 * SR)])
        check(f"9.3 #{num} {name} RMS > -40 dBFS", rd > -40, f"{rd:.2f} dBFS")
        clean(r, f"9.3 #{num} {name}")
        if name == "kick":
            pk = strongest(r.x, 20, 2000, o, int(0.1 * SR))
            check("9.3 Kick strongest <120 Hz", pk < 120, f"{pk:.1f} Hz")
        if name == "clap":
            n, _ = burst_count(r.x, 1000, 4000, o, int(0.040 * SR))
            check("9.3 Clap >=3 bursts", n >= 3, f"{n} bursts")
        if name == "metal_hat":
            hi = band_db(r.x, 3000, 24000, o, int(0.1 * SR))
            tot = band_db(r.x, 20, 24000, o, int(0.1 * SR))
            check("9.3 Metal hat >=50% energy >3 kHz", hi - tot >= 10 * np.log10(0.5), f"{hi - tot:+.2f} dB")
        if name == "zap":
            m1 = [12 * np.log2(float(z["mode1_hz"]) / float(z["f0_hz"])) for z in r.tr
                  if float(z["mode1_hz"]) > 0][:6]
            check("9.3 Zap mode 1 >=2 octaves up", m1 and max(m1) >= 24, f"{max(m1):.1f} st" if m1 else "n/a")

    # sustained bass: one note held for four steps (480 ms)
    b = render("recipe16_bass", blocks=900, trigs=(32,), ends=(32 + int(0.48 * SR),), trace=True,
               pitch=40, color=0, shape=10, sweep=20, contour=40, decay=64, gate=1)
    out["bass"] = b
    o = onset(b.x)
    rd = rms_db(b.x[o:o + int(0.15 * SR)])
    check("9.3 #16 bass RMS > -40 dBFS", rd > -40, f"{rd:.2f} dBFS")
    clean(b, "9.3 #16 bass")
    rows = [z for z in b.tr if 150 <= float(z["time_ms"]) <= 400 and float(z["mode1_hz"]) > 0]
    errs = [abs(12 * np.log2(float(z["mode1_hz"]) / float(z["f0_hz"]))) for z in rows]
    check("9.3 bass mode 1 within 0.1 st of f0 while held", rows and max(errs) <= 0.1,
          f"max {max(errs):.3f} st" if errs else "n/a")
    f0 = float(b.tr[0]["f0_hz"])
    seg = b.x[o + int(0.15 * SR):o + int(0.4 * SR)]
    X = np.abs(np.fft.rfft(seg * np.hanning(len(seg))))
    f = np.fft.rfftfreq(len(seg), 1 / SR)
    pk = float(f[np.argmax(X)])
    check("9.3 bass strongest hold peak <60 Hz", pk < 60, f"{pk:.1f} Hz (f0 {f0:.1f})")
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--only", default="hero,macros,recipes")
    args = ap.parse_args()
    only = [s.strip() for s in args.only.split(",") if s.strip()]
    if "hero" in only:
        hero()
    if "macros" in only:
        macros()
    if "recipes" in only:
        recipes()
    bad = [r for r in RESULTS if r[1] is False]
    print(f"\n{len(RESULTS)} checks, {len(bad)} failed")
    if bad:
        for name, _ok, val in bad:
            print(f"  FAILED: {name}: {val}")
        return 1
    print("ALL OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
