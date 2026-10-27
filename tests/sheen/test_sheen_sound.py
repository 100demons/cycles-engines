#!/usr/bin/env python3
"""SHEEN host checks (§9.1-§9.3), python3 + numpy only.

Every check prints the measured value.  A check that cannot run is printed NOT RUN, never PASS.

The host renderer writes the value the OS's voice loop leaves after its arithmetic >>1.  In that halved domain
1 << 30 is full scale, i.e. the Q31 sample the DSP wrote before the OS halved it.  Tests state this explicitly.

    python3 tests/sheen/test_sheen_sound.py [--only hero,macros,recipes]
"""
import argparse
import csv as _csv
import pathlib
import subprocess
import sys
import wave

import numpy as np

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent.parent
SRC = HERE / "sheen_host.c"
INC = ROOT / "engines" / "sheen"
OUT = ROOT / "build" / "sheen-test"
FS = 1 << 30
SR = 48000

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
    exe = OUT / "sheen_host"
    base = ["cc", "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror", f"-I{INC}", str(SRC), "-o", str(exe)]
    r = subprocess.run(base, capture_output=True, text=True)
    if r.returncode != 0:
        sdks = sorted(pathlib.Path("/Library/Developer/CommandLineTools/SDKs").glob("MacOSX*.sdk"),
                      key=lambda p: p.name, reverse=True)
        for sdk in sdks:
            if sdk.name == "MacOSX.sdk":
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
SIZE = int(subprocess.run([str(HOST), "--dump-size"], capture_output=True, text=True).stdout.strip())


class Render:
    def __init__(self, x, tr):
        self.x = x
        self.tr = tr

    def col(self, name):
        return np.array([float(r[name]) for r in self.tr])


def render(name, blocks=700, trigs=(32,), ends=(), resets=(), trace=True, tv=(), tp=(), **kw):
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
    lines += [f"trigvel {a} {b}" for a, b in tv]
    lines += [f"trigpunch {a} {b}" for a, b in tp]
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
    return float(np.sqrt(np.mean(np.square(x)))) if len(x) else 0.0


def rms_db(x):
    return db(rms(x))


def peak_db(x):
    return db(np.max(np.abs(x))) if len(x) else -300.0


def band_db(x, lo, hi, start=0, n=None):
    if n is None:
        n = len(x) - start
    seg = x[start:start + n]
    if len(seg) < 8:
        return -300.0
    seg = seg * np.hanning(len(seg))
    X = np.fft.rfft(seg)
    f = np.fft.rfftfreq(len(seg), 1 / SR)
    m = (f >= lo) & (f < hi)
    return 10 * np.log10(np.sum(np.abs(X[m]) ** 2) + 1e-30)


def band_frac(x, lo, hi, start, n):
    seg = x[start:start + n] * np.hanning(n)
    X = np.abs(np.fft.rfft(seg)) ** 2
    f = np.fft.rfftfreq(n, 1 / SR)
    return float(np.sum(X[(f >= lo) & (f < hi)]) / (np.sum(X) + 1e-30))


def flatness(x, lo, hi, start, n):
    seg = x[start:start + n] * np.hanning(n)
    X = np.abs(np.fft.rfft(seg)) ** 2
    f = np.fft.rfftfreq(n, 1 / SR)
    p = X[(f >= lo) & (f < hi)] + 1e-30
    return float(np.exp(np.mean(np.log(p))) / np.mean(p))


def centroid(x, start, n):
    seg = x[start:start + n]
    if len(seg) < 8:
        return 0.0
    X = np.abs(np.fft.rfft(seg * np.hanning(len(seg))))
    f = np.fft.rfftfreq(len(seg), 1 / SR)
    return float(np.sum(f * X) / (np.sum(X) + 1e-30))


def rms_env(x, win=240):
    k = np.ones(win) / win
    return np.sqrt(np.convolve(x * x, k, mode="same"))


def length_40(x, o):
    env = rms_env(x)
    seg = env[o:o + int(0.6 * SR) if len(env) > o + int(0.6 * SR) else len(env)]
    if len(seg) == 0:
        return None
    pk = np.max(seg)
    if pk <= 0:
        return None
    above = np.where(seg >= pk * 10 ** (-40 / 20))[0]
    return None if len(above) == 0 else above[-1] / SR


def length_40_long(x, o, seconds):
    env = rms_env(x)
    seg = env[o:o + int(seconds * SR)]
    pk = np.max(seg)
    if pk <= 0:
        return None
    above = np.where(seg >= pk * 10 ** (-40 / 20))[0]
    return None if len(above) == 0 else above[-1] / SR


def clean(r, name):
    x = r.x
    o = onset(x)
    step = float(np.max(np.abs(np.diff(x)))) if len(x) > 1 else 0.0
    mean = float(abs(np.mean(x[o:o + int(0.3 * SR)])))
    last = -1
    if r.tr:
        for i, row in enumerate(r.tr):
            if (int(row["body_env"]) > 0 or int(row["sizz_env"]) > 0 or int(row["chik_env"]) > 0
                    or int(row["tick_env"]) > 0):
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
    closed = render("hero_closed", blocks=900, trigs=(32,), trace=True)
    x = closed.x
    o = onset(x)
    check("9.1.1 peak -6..-0.5 dBFS", -6 <= peak_db(x) <= -0.5, f"{peak_db(x):.2f} dBFS")
    frac = band_frac(x, 5000, 24000, o, int(0.060 * SR))
    check("9.1.2 >=70% energy above 5 kHz", frac >= 0.70, f"{100 * frac:.1f} %")
    lo = band_db(x, 100, 1000, o, int(0.060 * SR))
    mid = band_db(x, 6000, 11000, o, int(0.060 * SR))
    check("9.1.2 100-1000 Hz >=24 dB below 6-11 kHz", mid - lo >= 24, f"{mid - lo:.2f} dB")
    lowmid = band_db(x, 2000, 4500, o, int(0.060 * SR))
    check("9.1.2 2-4.5 kHz >=4 dB below 6-11 kHz", mid - lowmid >= 4, f"{mid - lowmid:.2f} dB")
    air = band_db(x, 12000, 18000, o, int(0.060 * SR))
    check("9.1.3 air 3..15 dB below 6-11 kHz", 3 <= mid - air <= 15, f"{mid - air:.2f} dB")
    pk_i = int(np.argmax(np.abs(x)))
    check("9.1.4 peak within 2 ms", (pk_i - o) / SR <= 0.002, f"{(pk_i - o) / SR * 1000:.2f} ms")
    ln = length_40(x, o)
    check("9.1.5 -40 dB time 40..120 ms", ln is not None and 0.040 <= ln <= 0.120,
          f"{ln * 1000:.1f} ms" if ln is not None else "never")
    clean(closed, "9.1.13")

    # round robin at COLOR 96, trigs 300 ms apart
    rr = render("hero_rr", blocks=800, trigs=(32, 32 + int(0.300 * SR)), color=96, trace=False)
    a = rr.x[32:32 + int(0.030 * SR)]
    b = rr.x[32 + int(0.300 * SR):32 + int(0.330 * SR)]
    corr = float(np.corrcoef(a, b)[0, 1])
    drms = abs(rms_db(a) - rms_db(b))
    check("9.1.6 round-robin correlation <0.95", corr < 0.95, f"{corr:.3f}")
    check("9.1.6 round-robin RMS within 1 dB", drms <= 1.0, f"{drms:.2f} dB")

    openr = render("hero_open", blocks=2200, trigs=(32,), punch=1, trace=True)
    xo = openr.x
    oo = onset(xo)
    lno = length_40_long(xo, oo, 1.6)
    check("9.1.7 open -40 dB time 400..1100 ms", lno is not None and 0.400 <= lno <= 1.100,
          f"{lno * 1000:.1f} ms" if lno is not None else "never")
    e15 = rms(xo[oo + int(0.015 * SR):oo + int(0.040 * SR)])
    e0 = rms(xo[oo:oo + int(0.010 * SR)])
    bloom = db(e15) - db(e0)
    check("9.1.8 bloom 15-40 ms within 6 dB of 0-10 ms", bloom >= -6, f"{bloom:+.2f} dB")
    c20 = centroid(xo, oo + int(0.020 * SR), int(0.080 * SR))
    c300 = centroid(xo, oo + int(0.300 * SR), int(0.100 * SR))
    check("9.1.9 centroid falls >=5% by 300-400 ms", c300 <= 0.95 * c20, f"{c20:.0f} -> {c300:.0f} Hz")
    frac_o = band_frac(xo, 5000, 24000, oo, int(0.200 * SR))
    lo_o = band_db(xo, 100, 1000, oo, int(0.200 * SR))
    mid_o = band_db(xo, 6000, 11000, oo, int(0.200 * SR))
    check("9.1.10 open >=65% energy above 5 kHz", frac_o >= 0.65, f"{100 * frac_o:.1f} %")
    check("9.1.10 open 100-1000 Hz >=20 dB below 6-11 kHz", mid_o - lo_o >= 20, f"{mid_o - lo_o:.2f} dB")
    clean(openr, "9.1.13 open")

    # choke: open at 0, closed at 200 ms
    ch = render("choke", blocks=700, trigs=(32, 32 + int(0.200 * SR)), tv=((32 + int(0.200 * SR), 100),),
                tp=((32, 1), (32 + int(0.200 * SR), 0)), trace=False)
    w0 = 32 + int(0.280 * SR)
    w1 = 32 + int(0.320 * SR)
    alone = rms_db(xo[w0:w1])
    choked = rms_db(ch.x[w0:w1])
    check("9.1.11 choke >=25 dB", alone - choked >= 25, f"{alone - choked:.2f} dB")

    # roll: 16 closed trigs 25 ms apart, velocity 40 -> 120
    # the voice loop triggers once per 32-frame block; put each 25 ms step on its nearest block start
    roll_t = [32 + int(round(i * 0.025 * SR / 32.0)) * 32 for i in range(16)]
    roll_v = [(t, int(round(40 + i * (80 / 15)))) for i, t in enumerate(roll_t)]
    rl = render("roll", blocks=800, trigs=roll_t, tv=roll_v, trace=False)
    peaks = []
    for i, t in enumerate(roll_t):
        seg = rl.x[t:t + int(0.020 * SR)]
        peaks.append(peak_db(seg))
    dips = 0
    dipmax = 0.0
    for i in range(1, len(peaks)):
        if peaks[i] < peaks[i - 1]:
            dips += 1
            dipmax = max(dipmax, peaks[i - 1] - peaks[i])
    check("9.1.12 roll peaks rise (one dip <=1.5 dB)", dips <= 1 and dipmax <= 1.5,
          f"{dips} dips, max {dipmax:.2f} dB")
    s1 = np.max(np.abs(np.diff(rl.x[roll_t[0]:roll_t[0] + int(0.005 * SR)])))
    steps = [np.max(np.abs(np.diff(rl.x[t - int(0.002 * SR):t + int(0.002 * SR)]))) for t in roll_t[1:]]
    check("9.1.12 roll step <=1.2 x first onset", max(steps) <= 1.2 * s1,
          f"{max(steps) / s1:.3f} x")

    a = render("det1", blocks=300, trigs=(32,), trace=False)
    b = render("det2", blocks=300, trigs=(32,), trace=False)
    check("9.1.14 deterministic", np.array_equal(a.x, b.x), "identical" if np.array_equal(a.x, b.x) else "DIFFER")
    return closed, openr


def macro_points():
    return [0, 16, 32, 48, 64, 80, 96, 112, 127]


def all_points_clean(tag, key, base=None, blocks=1200):
    base = dict(base or {})
    out = {}
    for v in macro_points():
        p = dict(base)
        p[key] = v
        r = render(f"{tag}{v}", blocks=blocks, trigs=(32,), trace=True, **p)
        out[v] = r
        o = onset(r.x)
        rd = rms_db(r.x[o:o + int(0.060 * SR)])
        check(f"{tag}{v} RMS > -40 dBFS", rd > -40, f"{rd:.2f} dBFS")
        clean(r, f"{tag}{v}")
    return out


def macros():
    print("\n§9.2 Macros")
    c = all_points_clean("color", "color")
    flat = {v: flatness(r.x, 4000, 16000, onset(r.x), int(0.060 * SR)) for v, r in c.items()}
    check("9.2 COLOR flatness 127 < 64 < 0", flat[127] < flat[64] < flat[0],
          f"0={flat[0]:.3f} 64={flat[64]:.3f} 127={flat[127]:.3f}")
    check("9.2 COLOR 127 oscillators 1-4 at 0", int(c[127].tr[1]["osc14"]) == 0,
          f"osc14 mask {c[127].tr[1]['osc14']}")

    s = all_points_clean("size", "shape")
    cen = {v: centroid(r.x, onset(r.x), int(0.100 * SR)) for v, r in s.items()}
    lens = {v: (length_40(r.x, onset(r.x)) or 9.0) for v, r in s.items()}
    seq = [cen[v] for v in macro_points()]
    rises = sum(1 for i in range(1, len(seq)) if seq[i] > seq[i - 1] * 1.03)
    check("9.2 SIZE centroid falls 0->127 (one rise <=3%)", cen[127] < cen[0] and rises <= 1,
          f"0={cen[0]:.0f} 64={cen[64]:.0f} 127={cen[127]:.0f} Hz, rises {rises}")
    check("9.2 SIZE -40 dB time rises 0->127", lens[127] > lens[0],
          f"0={lens[0] * 1000:.0f} ms 127={lens[127] * 1000:.0f} ms")

    t = all_points_clean("tick", "sweep")
    rat = {}
    for v, r in t.items():
        o = onset(r.x)
        e0 = band_db(r.x, 20, 12000, o, int(0.002 * SR))
        e1 = band_db(r.x, 20, 12000, o + int(0.002 * SR), int(0.018 * SR))
        rat[v] = e0 - e1
    check("9.2 TICK onset/tail energy rises 0->96", rat[96] > rat[0],
          f"0={rat[0]:.2f} 64={rat[64]:.2f} 96={rat[96]:.2f} dB")
    n, _ = burst_count(t[0].x, 4000, 10000, onset(t[0].x), int(0.025 * SR))
    check("9.2 TICK 0 has >=2 bursts", n >= 2, f"{n} bursts")
    mh = float(t[127].tr[1]["base_hz"]) if len(t[127].tr) > 1 else 0
    check("9.2 TICK 127 metal base >=2 octaves up", mh >= 205.3 * 4, f"{mh:.1f} Hz")

    h = all_points_clean("heat", "contour")
    hero = render("heat_hero", blocks=300, trigs=(32,), trace=False)
    ho = onset(hero.x)
    href = rms_db(hero.x[ho:ho + int(0.060 * SR)])
    rmsv = {v: rms_db(r.x[onset(r.x):onset(r.x) + int(0.060 * SR)]) for v, r in h.items()}
    ok = all(abs(v - href) <= 3 for v in rmsv.values())
    check("9.2 HEAT RMS within +-3 dB of hero", ok,
          " ".join(f"{v}:{rmsv[v] - href:+.1f}" for v in macro_points()))
    crest = {v: peak_db(r.x[onset(r.x):onset(r.x) + int(0.060 * SR)])
             - rms_db(r.x[onset(r.x):onset(r.x) + int(0.060 * SR)]) for v, r in h.items()}
    check("9.2 HEAT crest falls 0->96", crest[96] < crest[0],
          f"0={crest[0]:.1f} 64={crest[64]:.1f} 96={crest[96]:.1f} dB")
    check("9.2 HEAT 127 hold 3 / bits 7", int(h[127].tr[1]["crush_hold"]) == 3 and int(h[127].tr[1]["crush_bits"]) == 7,
          f"hold {h[127].tr[1]['crush_hold']}, bits {h[127].tr[1]['crush_bits']}")
    peaks = {v: peak_db(r.x[onset(r.x):onset(r.x) + int(0.060 * SR)]) for v, r in h.items()}
    check("9.2 HEAT peak within +-3 dB of hero",
          all(abs(peaks[v] - peak_db(hero.x[ho:ho + int(0.060 * SR)])) <= 3 for v in macro_points()),
          " ".join(f"{v}:{peaks[v] - peak_db(hero.x[ho:ho + int(0.060 * SR)]):+.1f}" for v in macro_points()))

    dc = all_points_clean("decay", "decay")
    dc[100] = render("decay100", blocks=2500, trigs=(32,), trace=True, decay=100)
    for v in (101, 115, 127):
        dc[v] = render(f"decay{v}", blocks=3000, trigs=(32,), trace=True, decay=v)
    dc_o = all_points_clean("decayo", "decay", base={"punch": 1}, blocks=3000)
    dc_o[100] = render("decayo100", blocks=4500, trigs=(32,), trace=True, decay=100, punch=1)
    lens = {v: (length_40(r.x, onset(r.x)) or 9.0) for v, r in dc.items()}
    check("9.2 DECAY closed -40 dB rises 0->100", lens[100] > lens[0],
          f"0={lens[0] * 1000:.0f} ms 100={lens[100] * 1000:.0f} ms")
    lens_o = {v: (length_40_long(r.x, onset(r.x), 5.0) or 9.0) for v, r in dc_o.items()}
    check("9.2 DECAY open -40 dB rises 0->100", lens_o[100] > lens_o[0],
          f"0={lens_o[0] * 1000:.0f} ms 100={lens_o[100] * 1000:.0f} ms")
    for v in (101, 115, 127):
        r = dc[v]
        body = np.array([int(z["body_env"]) for z in r.tr])
        pk = body.max()
        peak_i = int(np.argmax(body))
        fall = np.where(body < pk * 0.05)[0]
        fall = fall[fall > peak_i]
        cut_ms = (fall[0] - peak_i) / 24.0 if len(fall) else 99.0
        audio_pk = int(np.argmax(np.abs(r.x)))
        check(f"9.2 DECAY {v} trace rises then cuts <=5 ms", pk > 1000 and peak_i > 10 and cut_ms <= 5.0,
              f"peak block {peak_i}, cut {cut_ms:.1f} ms, audio peak {audio_pk / SR * 1000:.0f} ms")
    ao = int(np.argmax(np.abs(dc[101].x)))
    check("9.2 DECAY 101 audio peak >=60 ms after trig", ao >= 32 + int(0.060 * SR), f"{ao / SR * 1000:.0f} ms")

    off = render("punch_off", blocks=900, trigs=(32,), trace=False)
    on = render("punch_on", blocks=2200, trigs=(32,), trace=False, punch=1)
    lc = length_40(off.x, onset(off.x)) or 0.0
    lo_ = length_40_long(on.x, onset(on.x), 1.6) or 0.0
    check("9.2 PUNCH open >=5 x closed", lo_ >= 5 * lc, f"{lo_ * 1000:.0f} / {lc * 1000:.0f} ms")

    v30 = render("vel30", blocks=500, trigs=(32,), trace=False, vel=30)
    v127 = render("vel127", blocks=500, trigs=(32,), trace=False, vel=127)
    r30 = rms_db(v30.x[onset(v30.x):onset(v30.x) + int(0.060 * SR)])
    r127 = rms_db(v127.x[onset(v127.x):onset(v127.x) + int(0.060 * SR)])
    c30 = centroid(v30.x, onset(v30.x), int(0.030 * SR))
    c127 = centroid(v127.x, onset(v127.x), int(0.030 * SR))
    check("9.2 velocity 30 vs 127 RMS >=10 dB", abs(r127 - r30) >= 10, f"{r127 - r30:.2f} dB")
    check("9.2 velocity centroid higher at 127", c127 > c30, f"{c30:.0f} -> {c127:.0f} Hz")

    end = 32 + int(0.120 * SR)
    g0 = render("gate0", blocks=600, trigs=(32,), ends=(end,), trace=True, punch=1, gate=0)
    g1 = render("gate1", blocks=600, trigs=(32,), ends=(end,), trace=True, punch=1, gate=1)
    a = end + int(0.020 * SR)
    b = end + int(0.040 * SR)
    d = rms_db(g0.x[a:b]) - rms_db(g1.x[a:b])
    check("9.2 GATE >=20 dB lower after note end", d >= 20, f"{d:.2f} dB")


RECIPES = [
    (1, "hero_closed", dict()),
    (2, "hero_open", dict(punch=1)),
    (3, "note_length_open", dict(punch=1, gate=1, decay=64)),
    (4, "trap_closed", dict(pitch=66, color=50, shape=40, sweep=80, contour=72, decay=24)),
    (5, "808_closed", dict(color=96, shape=64, sweep=40, contour=30, decay=30)),
    (6, "808_open", dict(color=96, shape=64, sweep=40, contour=30, decay=40, punch=1)),
    (7, "909_hat", dict(color=24, shape=52, sweep=64, contour=48, decay=32)),
    (8, "pedal_chick", dict(color=64, shape=70, sweep=20, contour=40, decay=10, vel=60)),
    (9, "shaker", dict(color=0, shape=30, sweep=0, contour=40, decay=22)),
    (10, "tambourine", dict(color=90, shape=16, sweep=8, contour=60, decay=20, punch=1)),
    (11, "ride", dict(color=100, shape=96, sweep=92, contour=50, decay=78, punch=1)),
    (12, "crash", dict(color=48, shape=112, sweep=70, contour=64, decay=80, punch=1)),
    (13, "cowbell", dict(color=127, shape=64, sweep=72, contour=70, decay=30, punch=1)),
    (14, "reverse_cymbal", dict(color=50, shape=100, sweep=64, contour=64, decay=120, punch=1)),
    (15, "crushed", dict(color=64, shape=64, sweep=64, contour=127, decay=40)),
    (16, "laser", dict(color=96, shape=40, sweep=127, contour=64, decay=20)),
    (17, "gong", dict(pitch=40, color=120, shape=127, sweep=30, contour=50, decay=100, punch=1)),
]


def strongest_peaks(x, lo, hi, start, n, count=2):
    seg = x[start:start + n] * np.hanning(n)
    X = np.abs(np.fft.rfft(seg))
    f = np.fft.rfftfreq(n, 1 / SR)
    m = (f >= lo) & (f <= hi)
    ff = f[m]
    xx = X[m]
    idx = np.argsort(xx)[::-1]
    out = []
    for i in idx:
        if not out or abs(ff[i] - out[-1]) > 20:
            out.append(float(ff[i]))
        if len(out) >= count:
            break
    return out


def recipes():
    print("\n§9.3 Recipes")
    out = {}
    for num, name, kw in RECIPES:
        blocks = 3000 if name in ("ride", "crash", "gong", "reverse_cymbal") else 900
        r = render(f"recipe{num:02d}_{name}", blocks=blocks, trigs=(32,), trace=True, **kw)
        out[name] = r
        o = onset(r.x)
        rd = rms_db(r.x[o:o + int(0.060 * SR)])
        check(f"9.3 #{num} {name} RMS > -40 dBFS", rd > -40, f"{rd:.2f} dBFS")
        clean(r, f"9.3 #{num} {name}")

    # note-length open hat: gate on vs off after a 250 ms note
    end = 32 + int(0.250 * SR)
    a = render("recipe03_gate_on", blocks=1500, trigs=(32,), ends=(end,), trace=False, punch=1, gate=1, decay=64)
    b = render("recipe03_gate_off", blocks=1500, trigs=(32,), ends=(end,), trace=False, punch=1, gate=0, decay=64)
    w0 = end + int(0.020 * SR)
    w1 = end + int(0.040 * SR)
    d = rms_db(b.x[w0:w1]) - rms_db(a.x[w0:w1])
    check("9.3 note-length open GATE >=20 dB", d >= 20, f"{d:.2f} dB")

    ln = length_40(out["trap_closed"].x, onset(out["trap_closed"].x))
    check("9.3 trap closed -40 dB <=90 ms", ln is not None and ln <= 0.090,
          f"{ln * 1000:.1f} ms" if ln is not None else "never")
    n, _ = burst_count(out["shaker"].x, 3000, 12000, onset(out["shaker"].x), int(0.025 * SR))
    check("9.3 shaker >=2 bursts", n >= 2, f"{n} bursts")
    n, _ = burst_count(out["tambourine"].x, 4000, 18000, onset(out["tambourine"].x), int(0.060 * SR))
    frac = band_frac(out["tambourine"].x, 4000, 24000, onset(out["tambourine"].x), int(0.200 * SR))
    check("9.3 tambourine >=2 bursts and >=60% >4 kHz", n >= 2 and frac >= 0.60,
          f"{n} bursts, {100 * frac:.1f} %")
    lr = length_40_long(out["ride"].x, onset(out["ride"].x), 3.0)
    check("9.3 ride -40 dB >=1.2 s", lr is not None and lr >= 1.2, f"{lr:.2f} s" if lr else "never")
    lc = length_40_long(out["crash"].x, onset(out["crash"].x), 4.0)
    fc = band_frac(out["crash"].x, 4000, 24000, onset(out["crash"].x), int(0.300 * SR))
    check("9.3 crash -40 dB >=1.5 s and >=60% >4 kHz", lc is not None and lc >= 1.5 and fc >= 0.60,
          f"{lc:.2f} s, {100 * fc:.1f} %" if lc else "never")
    pk = strongest_peaks(out["cowbell"].x, 20, 1500, onset(out["cowbell"].x), int(0.200 * SR), 2)
    ok = len(pk) >= 2 and ((abs(pk[0] - 540) <= 10 and abs(pk[1] - 800) <= 15)
                           or (abs(pk[1] - 540) <= 10 and abs(pk[0] - 800) <= 15))
    check("9.3 cowbell peaks 540 +-10 / 800 +-15 Hz", ok, f"{pk}")
    rev = out["reverse_cymbal"]
    pi = int(np.argmax(np.abs(rev.x)))
    check("9.3 reverse peak 150-1500 ms", 0.150 <= pi / SR <= 1.500, f"{pi / SR * 1000:.0f} ms")
    check("9.3 crushed hold 3 / bits 7", int(out["crushed"].tr[1]["crush_hold"]) == 3
          and int(out["crushed"].tr[1]["crush_bits"]) == 7,
          f"hold {out['crushed'].tr[1]['crush_hold']}, bits {out['crushed'].tr[1]['crush_bits']}")
    mh = float(out["laser"].tr[1]["base_hz"]) if len(out["laser"].tr) > 1 else 0
    check("9.3 laser base >=2 octaves up", mh >= 205.3 * 4, f"{mh:.1f} Hz")
    g = out["gong"]
    env = rms_env(g.x)
    pk5 = np.max(env[32:32 + int(0.005 * SR)])
    tail = np.sqrt(np.mean(g.x[int(1.4 * SR):int(1.6 * SR)] ** 2))
    check("9.3 gong tail 1.4-1.6 s within 25 dB", db(tail) - db(pk5) >= -25,
          f"{db(tail) - db(pk5):+.2f} dB")
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--only", default="hero,macros,recipes")
    args = ap.parse_args()
    only = [s.strip() for s in args.only.split(",") if s.strip()]
    info("voice struct", f"{SIZE} bytes")
    if SIZE > 640:
        check("size: voice <=640 bytes", False, f"{SIZE} bytes")
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
