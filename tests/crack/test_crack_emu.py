#!/usr/bin/env python3
"""CRACK emulator checks (§9.4): sample-exact audio and CPU, in the real OS 1.13 voice loop.

    python3 tests/crack/test_crack_emu.py --cycles firmware/model-cycles_OS1.13.syx

The host renderer writes the value after the OS's arithmetic >>1 (see crack_host.c), so the emulator output
compares directly with the raw file.  CPU is reported two ways:

  * "engine cost": the active six-voice block minus the same build's idle six-voice block, divided by six.  This
    is the instruction count of the engine itself (the §8 table's unit) and the number compared to TONE.
  * "raw six-voice block": the number count_instructions() returns.  It contains the OS voice-loop overhead for
    all six voices; for an engine that bypasses the stock amp chain the OS takes a more expensive idle path, so
    the raw number is much larger.  Both are printed; no raw number is hidden.
"""
import argparse
import pathlib
import struct
import subprocess
import sys

import numpy as np

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent.parent
sys.path.insert(0, str(ROOT / "tests" / "emu"))
sys.path.insert(0, str(ROOT / "core"))

import harness as H                    # noqa: E402
import mcengine as E                   # noqa: E402

FAIL = []
INFO = []


def check(ok, msg):
    print(("  ok    " if ok else "  FAIL  ") + msg, flush=True)
    if not ok:
        FAIL.append(msg)


def info(msg):
    INFO.append(msg)
    print("  ..    " + msg, flush=True)


def build_host():
    out = ROOT / "build" / "crack-test"
    out.mkdir(parents=True, exist_ok=True)
    exe = out / "crack_host"
    src = ROOT / "tests" / "crack" / "crack_host.c"
    inc = ROOT / "engines" / "crack"
    base = ["cc", "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror", f"-I{inc}", str(src), "-o", str(exe)]
    r = subprocess.run(base, capture_output=True, text=True)
    if r.returncode:
        sdks = sorted(pathlib.Path("/Library/Developer/CommandLineTools/SDKs").glob("MacOSX*.sdk"),
                      key=lambda p: p.name, reverse=True)
        for sdk in sdks:
            if sdk.name == "MacOSX.sdk":
                continue
            r2 = subprocess.run(base[:1] + ["-isysroot", str(sdk)] + base[1:], capture_output=True, text=True)
            if r2.returncode == 0:
                info(f"host build used -isysroot {sdk.name}")
                return exe
        sys.stderr.write(r.stdout + r.stderr)
        sys.exit("cannot build the host renderer")
    return exe


HOST = build_host()
D = ROOT / "build" / "crack-test" / "emu"
D.mkdir(parents=True, exist_ok=True)


def host_render(name, blocks, trigs, resets=(), ends=(), **kw):
    p = dict(note=60, pitch=64, fine=64, color=64, shape=64, sweep=64, contour=64, decay=40,
             punch=0, gate=0, vel=100)
    p.update(kw)
    raw = D / f"{name}.raw"
    script = D / f"{name}.txt"
    lines = [f"{k} {v}" for k, v in p.items()]
    lines += [f"blocks {blocks}"]
    lines += [f"trig {t}" for t in trigs]
    lines += [f"reset {t}" for t in resets]
    lines += [f"end {t}" for t in ends]
    lines += [f"raw {raw}"]
    script.write_text("\n".join(lines) + "\n", encoding="utf-8")
    subprocess.run([str(HOST), str(script)], check=True, capture_output=True)
    return np.fromfile(raw, dtype="<i4").astype(np.int64)


def emu_new(img, runtime, tweak, machine, **kw):
    e = H.engine(img, runtime, tweak)
    e.set(0, machine=machine, note=60, pitch=64, finetune=64, color=64, shape=64, sweep=64, contour=64,
          punch=0, gate=0, decay=40)
    for k, v in kw.items():
        e.set(0, **{k: v})
    return e


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--cycles", default=str(ROOT / "firmware" / "model-cycles_OS1.13.syx"))
    args = ap.parse_args()
    stock = H.main_os_from_syx(args.cycles)
    tweak, info_, img, runtime = H.built(stock, ["crack"])
    pkg = next(p for p in info_["packages"] if p["id"] == "crack")
    machine = pkg["index"]
    print(f"CRACK = machine {machine}; payload {len(runtime)} bytes")
    check(not H.engine(img, runtime, tweak).unmapped, "voice loop starts, no unmapped access")

    # ---- sample exact ---------------------------------------------------------------------------------------
    print("\nsample exact (emulator vs host, after the OS's >>1)")
    x = host_render("hero", 200, (32,))
    e = emu_new(img, runtime, tweak, machine)
    y = e.render(200, trig_at=(1,), track=0)
    check(np.array_equal(x, y), f"hero: {np.count_nonzero(x != y)} differing samples")
    check(not e.unmapped, "hero: no unmapped access")

    x = host_render("rim", 200, (32,), punch=1)
    e = emu_new(img, runtime, tweak, machine, punch=1)
    y = e.render(200, trig_at=(1,), track=0)
    check(np.array_equal(x, y), f"rimshot: {np.count_nonzero(x != y)} differing samples")

    x = host_render("ghost", 200, (32,), vel=35)
    e = emu_new(img, runtime, tweak, machine)
    e.uc.mem_write(0x40fe4cdc, struct.pack(">H", 35 << 8))
    y = e.render(200, trig_at=(1,), track=0)
    check(np.array_equal(x, y), f"ghost note: {np.count_nonzero(x != y)} differing samples")

    x = host_render("retrig", 300, (32, 32 + 60 * 32))
    e = emu_new(img, runtime, tweak, machine)
    y = e.render(300, trig_at=(1, 61), track=0)
    check(np.array_equal(x, y), f"retrigger at 40 ms: {np.count_nonzero(x != y)} differing samples")

    # machine change: SNARE at block 21, CRACK back at block 22, trig at block 22 (seen at block 23)
    x = host_render("mchange", 80, (22 * 32,), resets=(21 * 32,))
    e = emu_new(img, runtime, tweak, machine)
    ys = []
    for b in range(80):
        if b == 21:
            e.set(0, machine=1)              # SNARE: the OS resets the voice
        elif b == 22:
            e.set(0, machine=machine)
        ys.append(e.block(1 if b == 22 else 0)[0])
    y = np.concatenate(ys)
    check(np.array_equal(x[22 * 32:], y[22 * 32:]),
          f"machine change mid-note: {np.count_nonzero(x[22 * 32:] != y[22 * 32:])} differing samples")

    # every recipe, host vs emulator
    print("\nrecipes sample exact")
    recipes = [
        ("hero", dict()),
        ("rimshot", dict(punch=1)),
        ("ghost", dict(vel=35)),
        ("trap_clap_snare", dict(pitch=70, color=100, shape=60, sweep=70, contour=90, decay=45)),
        ("808_snare", dict(color=40, shape=32, sweep=30, contour=20, decay=50)),
        ("909_snare", dict(color=50, shape=36, sweep=64, contour=40, decay=44)),
        ("brush", dict(color=56, shape=64, sweep=0, contour=10, decay=30, vel=60)),
        ("gated_80s", dict(pitch=60, color=70, shape=48, sweep=64, contour=70, decay=115)),
        ("kick", dict(pitch=40, color=0, shape=0, sweep=64, contour=80, decay=60)),
        ("tom", dict(pitch=52, color=8, shape=40, sweep=40, contour=40, decay=70)),
        ("clap", dict(color=127, shape=64, sweep=50, contour=60, decay=50)),
        ("metal_hat", dict(pitch=100, color=80, shape=120, sweep=80, contour=50, decay=8)),
        ("metal_hit", dict(color=30, shape=127, sweep=64, contour=96, decay=60)),
        ("zap", dict(color=0, shape=64, sweep=127, contour=64, decay=30)),
        ("crushed", dict(color=64, shape=64, sweep=64, contour=127, decay=40)),
    ]
    for name, kw in recipes:
        blocks = 500 if name == "gated_80s" else 300
        x = host_render("rec_" + name, blocks, (32,), **kw)
        e = emu_new(img, runtime, tweak, machine, **kw)
        if kw.get("vel", 100) != 100:
            e.uc.mem_write(0x40fe4cdc, struct.pack(">H", int(kw["vel"]) << 8))
        y = e.render(blocks, trig_at=(1,), track=0)
        check(np.array_equal(x, y), f"recipe {name}: {np.count_nonzero(x != y)} differing samples")
    # sustained bass: one note held for four steps (480 ms)
    blocks = 900
    end_at = 32 + int(0.48 * 48000)
    x = host_render("rec_bass", blocks, (32,), ends=(end_at,), pitch=40, color=0, shape=10, sweep=20,
                    contour=40, decay=64, gate=1)
    e = emu_new(img, runtime, tweak, machine, pitch=40, color=0, shape=10, sweep=20, contour=40, decay=64, gate=1)
    end_block = end_at // 32
    ys = []
    for b in range(blocks):
        ys.append(e.block(1 if b == 1 else 0, 1 if b == end_block else 0)[0])
    y = np.concatenate(ys)
    check(np.array_equal(x, y), f"recipe sustained bass: {np.count_nonzero(x != y)} differing samples")

    # ---- level ----------------------------------------------------------------------------------------------
    print("\nlevel")
    hero = emu_new(img, runtime, tweak, machine).render(600, trig_at=(1,), track=0)
    hero = hero.astype(np.float64) / (1 << 31)
    on = int(np.argmax(np.abs(hero) > 10 ** (-60 / 20)))
    hr = np.sqrt(np.mean(hero[on:on + int(0.150 * 48000)] ** 2))
    sn = H.play(E.Engine(stock), 0, "SNARE", (0, 127, 8, 0), 40, 600, note=60).astype(np.float64) / (1 << 31)
    on2 = int(np.argmax(np.abs(sn) > 10 ** (-60 / 20)))
    sr = np.sqrt(np.mean(sn[on2:on2 + int(0.150 * 48000)] ** 2))
    delta = 20 * np.log10(hr / sr)
    check(abs(delta) <= 3, f"hero RMS 150 ms vs stock SNARE {delta:+.2f} dB "
                            f"({20 * np.log10(hr):.2f} vs {20 * np.log10(sr):.2f} dBFS)")

    # ---- CPU ------------------------------------------------------------------------------------------------
    print("\nCPU (instructions per block)")
    tweak7, info7, img7, runtime7 = H.built_pairs(stock, [(7, "crack")])
    m7 = 6

    def engine():
        e = E.Engine(img7, payload=(E.PAYLOAD_DST, runtime7), payload_code=H.code_ranges(tweak7))
        e.count_instructions()
        return e

    def count(e, trig=0):
        e.instructions = 0
        e.block(trig)
        return e.instructions

    def set_tone(e, t):
        e.set(t, machine=4, note=60, pitch=64, finetune=64, color=40, shape=38, sweep=52, contour=42,
              punch=0, gate=0, decay=42)

    def set_crack(e, t):
        e.set(t, machine=m7, note=60, pitch=64, finetune=64, color=96, shape=127, sweep=127, contour=127,
              punch=1, gate=1, decay=115)
        e.uc.mem_write(0x40fe4cdc + 2 * t, struct.pack(">H", 127 << 8))

    # six TONE voices: idle, then a trig (seen one block later), then steady
    e = engine()
    for t in range(6):
        set_tone(e, t)
    e.block(0)
    tone_idle = count(e, 0)
    e.block(0b111111)
    tone_trig = count(e, 0)          # the trig is seen in this block
    tone_steady = count(e, 0)

    # six CRACK voices: warm up the machine selection, first trig, steady, then the worst block:
    # a trig with every knob changing on that block.
    e = engine()
    for t in range(6):
        set_crack(e, t)
    for _ in range(10):
        e.block(0)
    crack_idle = count(e, 0)
    e.block(0b111111)
    crack_first = count(e, 0)        # first note: the OS still owns v+0x2c, so the bridge resets once
    for _ in range(20):
        e.block(0)
    crack_steady = count(e, 0)
    for t in range(6):
        e.set(t, color=95, shape=126, sweep=126, contour=126)
    e.block(0b111111)
    crack_worst = count(e, 0)        # trig seen + full macro remap
    e.block(0)
    crack_after = count(e, 0)

    tone_pv = tone_steady / 6.0
    idle_pv = crack_idle / 6.0
    steady_pv = crack_steady / 6.0
    worst_pv = crack_worst / 6.0
    engine_steady = steady_pv - idle_pv
    engine_worst = worst_pv - idle_pv
    ratio_steady = engine_steady / tone_pv
    ratio_worst = engine_worst / tone_pv
    info(f"raw six-voice: TONE steady {tone_steady} ({tone_pv:.0f}/voice); CRACK steady {crack_steady} "
         f"({steady_pv:.0f}/voice), worst trig {crack_worst} ({worst_pv:.0f}/voice), first note {crack_first} "
         f"({crack_first / 6.0:.0f}/voice), idle {crack_idle} ({idle_pv:.0f}/voice); TONE idle {tone_idle}")
    info(f"engine cost: steady {engine_steady:.0f}/voice = {ratio_steady:.3f} x TONE; worst {engine_worst:.0f}/voice "
         f"= {ratio_worst:.3f} x TONE ({tone_pv:.0f}/voice); target <=0.90, hard <=1.00")
    check(engine_worst <= tone_pv, f"worst engine cost <= TONE: {ratio_worst:.3f} x")
    if ratio_worst > 0.90:
        info(f"margin target 0.90 x TONE not met: worst {ratio_worst:.3f} x, steady {ratio_steady:.3f} x "
             f"(hard ceiling 1.00 x is met)")
    else:
        check(True, f"target 0.90 x TONE met: worst {ratio_worst:.3f} x")
    info(f"silent: raw block {crack_idle} ({idle_pv:.0f}/voice); the engine does no work when idle")
    check(crack_idle - tone_idle <= 300, f"silent engine work <=300 instructions "
                                         f"({crack_idle - tone_idle} above TONE idle, raw {crack_idle} vs {tone_idle})")
    info(f"six voices: CRACK worst {crack_worst} raw vs TONE {tone_steady} raw; engine-cost six CRACK "
         f"{6 * engine_worst:.0f} vs six TONE {6 * tone_pv:.0f}")

    print("\nALL OK" if not FAIL else f"\n{len(FAIL)} FAILED: " + "; ".join(FAIL[:8]))
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
