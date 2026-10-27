#!/usr/bin/env python3
"""SHEEN emulator checks (§9.4): device code in the real OS 1.13 voice loop.

The host renderer and the emulator run the same script.  The host writes the value after the OS's arithmetic >>1, so
the two must be identical sample for sample.  CPU is measured in the same composed build against stock TONE.

    python3 tests/sheen/test_sheen_emu.py --cycles firmware/model-cycles_OS1.13.syx [--only exact,cpu,level,six]
"""
import argparse
import pathlib
import struct
import sys

import numpy as np

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent.parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(ROOT / "tests" / "emu"))
sys.path.insert(0, str(ROOT / "core"))

import harness                        # noqa: E402
import mcengine as E                  # noqa: E402
import test_sheen_sound as S          # noqa: E402

FAIL = []


def check(ok, msg):
    print(("  ok    " if ok else "  FAIL  ") + msg, flush=True)
    if not ok:
        FAIL.append(msg)


def main_os(path):
    return harness.main_os_from_syx(path)


def event_val(events, pos, default):
    for p, v in events:
        if pos <= p < pos + 32:
            return v
    return default


CASES = []


def case(name, blocks=200, trigs=(32,), ends=(), resets=(), tv=(), tp=(), **kw):
    CASES.append(dict(name=name, blocks=blocks, trigs=trigs, ends=ends, resets=resets, tv=tv, tp=tp, **kw))


def build_cases():
    case("hero_closed", 300)
    case("hero_open", 900, punch=1)
    case("choke_pair", 700, trigs=(32, 32 + int(0.200 * 48000)),
         tv=((32 + int(0.200 * 48000), 100),), tp=((32, 1), (32 + int(0.200 * 48000), 0)))
    case("soft_hit", 300, vel=30)
    roll = [32 + int(round(i * 0.025 * 48000 / 32.0)) * 32 for i in range(16)]
    case("roll", 500, trigs=tuple(roll),
         tv=tuple((t, int(round(40 + i * 80 / 15))) for i, t in enumerate(roll)))
    # every recipe of §5.5
    for num, name, kw in S.RECIPES:
        blocks = 600
        ends = ()
        if name == "note_length_open":
            ends = (32 + int(0.250 * 48000),)
            blocks = 1000
        elif name == "reverse_cymbal":
            blocks = 2000
        elif name in ("ride", "crash", "gong"):
            blocks = 1000
        case(f"recipe{num:02d}_{name}", blocks=blocks, ends=ends, **kw)
    case("machine_change", 500, trigs=(32,), resets=(3200,))


def run_host(c):
    r = S.render(c["name"] + "_emu", blocks=c["blocks"], trigs=c["trigs"], ends=c["ends"],
                 resets=c["resets"], tv=c["tv"], tp=c["tp"], trace=False,
                 **{k: v for k, v in c.items() if k in
                    ("note", "pitch", "fine", "color", "shape", "sweep", "contour", "decay", "punch", "gate", "vel")})
    return (S.OUT / f"{c['name']}_emu.raw").read_bytes()


def emulate(e, c, track=0, machine=13):
    blocks = c["blocks"]
    # the OS sets v+0x38 / v+0x3c in the block after the trig / note end, and the bridge reads them in the
    # next update (as PUNCH does), so the OS is fed one block earlier than the host's script
    trig_blocks = {t // 32 - 1 for t in c["trigs"]}
    end_blocks = {t // 32 for t in c["ends"]}
    reset_blocks = {t // 32 for t in c["resets"]}
    base = dict(machine=machine, note=c.get("note", 60), pitch=c.get("pitch", 64), finetune=c.get("fine", 64),
                color=c.get("color", 64), shape=c.get("shape", 64), sweep=c.get("sweep", 64),
                contour=c.get("contour", 64), decay=c.get("decay", 40), punch=c.get("punch", 0),
                gate=c.get("gate", 0))
    e.set(track, **base)
    e.solo(track)
    out = []
    for b in range(blocks):
        if b in reset_blocks:
            e.call(E.VOICE_RESET, E.VOICE0 + track * E.VSTRIDE)
        trig = (1 << track) if b in trig_blocks else 0
        rel = (1 << track) if b in end_blocks else 0
        if trig:
            pos = (b + 1) * 32        # the host trig block (the OS trig is fed one block earlier)
            vel = event_val(c["tv"], pos, c.get("vel", 100))
            e.uc.mem_write(0x40fe4cdc + 2 * track, struct.pack(">H", (vel & 0x7f) << 8))
            punch = event_val(c["tp"], pos, c.get("punch", 0))
            e.set(track, punch=punch)
        out.append(e.block(trig, rel)[track])
    return np.concatenate(out).astype(np.int32)


def exact(builds):
    print("\n§9.4 sample-exact (host vs emulator, after the OS's >>1)")
    show = builds["sheen"]
    e = harness.engine(show[0], show[1], show[2])
    for c in CASES:
        e.call(E.VOICE_RESET, E.VOICE0)
        e.uc.mem_write(0x40fe4cdc, struct.pack(">H", 100 << 8))
        emu = emulate(e, c)
        host = np.frombuffer(run_host(c), dtype="<i4").astype(np.int32)
        n = min(len(emu), len(host))
        same = np.array_equal(emu[:n], host[:n]) and len(emu) == len(host)
        if same:
            check(True, f"{c['name']}: {len(host)} samples identical")
        else:
            d = np.where(emu[:n] != host[:n])[0]
            i = int(d[0]) if len(d) else 0
            check(False, f"{c['name']}: first difference at {i} (emu {emu[i]} host {host[i]}), "
                         f"{len(d)} samples differ, max {int(np.max(np.abs(emu[:n] - host[:n])))}")


def measure(e, blocks=8, trig_blocks=(1,), track=0, hist=False):
    if hist:
        e.count_instructions()
    vals = []
    for b in range(blocks):
        if hist:
            e.instructions = 0
        e.block((1 << track) if b in trig_blocks else 0)
        vals.append(e.instructions if hist else 0)
    return vals


def cpu_and_level(builds, stock):
    print("\n§9.4 CPU and level")
    # TONE reference, same run/build style
    e = E.Engine(stock)
    e.solo(0)
    e.machine_defaults(0, "TONE")
    e.set(0, note=60)
    e.count_instructions()
    tone = []
    for b in range(6):
        e.instructions = 0
        e.block(1 if b == 0 else 0)
        tone.append(e.instructions)
    tone_trig, tone_steady = tone[0], min(tone[1:])
    print(f"  ..    TONE trig {tone_trig}, steady {tone_steady} instructions/block")

    # SHEEN worst case: everything active and every knob changing on the trig block
    e = harness.engine(*builds["sheen"])
    e.solo(0)
    e.set(0, machine=13, note=60, pitch=64, finetune=64, color=64, shape=127, sweep=127,
          contour=127, decay=115, punch=1, gate=1)
    e.uc.mem_write(0x40fe4cdc, struct.pack(">H", 127 << 8))
    e.count_instructions()
    vals = []
    for b in range(6):
        e.instructions = 0
        e.block(1 if b == 1 else 0)
        vals.append(e.instructions)
    worst = vals[2]
    # steady hero closed
    e2 = harness.engine(*builds["sheen"])
    e2.solo(0)
    e2.set(0, machine=13, note=60, pitch=64, finetune=64, color=64, shape=64, sweep=64,
           contour=64, decay=40, punch=0, gate=0)
    e2.uc.mem_write(0x40fe4cdc, struct.pack(">H", 100 << 8))
    e2.count_instructions()
    e2.instructions = 0
    e2.block(1)
    hero_trig = e2.instructions
    steady = []
    for b in range(2, 6):
        e2.instructions = 0
        e2.block(0)
        steady.append(e2.instructions)
    # silent
    e3 = harness.engine(*builds["sheen"])
    e3.solo(0)
    e3.set(0, machine=13, note=60, pitch=64, finetune=64, color=64, shape=64, sweep=64,
           contour=64, decay=40, punch=0, gate=0)
    e3.count_instructions()
    for b in range(10):
        e3.block(0)
    e3.instructions = 0
    e3.block(0)
    silent = e3.instructions
    es = E.Engine(stock)
    es.solo(0)
    es.machine_defaults(0, "TONE")
    es.set(0, note=60)
    es.count_instructions()
    for b in range(10):
        es.block(0)
    es.instructions = 0
    es.block(0)
    silent_tone = es.instructions
    print(f"  ..    SHEEN worst {worst} = {worst / tone_steady:.3f} x TONE steady, hero trig {hero_trig}, "
          f"steady {min(steady)}, silent {silent} (TONE silent {silent_tone}, marginal {silent - silent_tone})")
    check(worst <= tone_steady, f"worst {worst} <= TONE steady {tone_steady} ({worst / tone_steady:.3f} x)")
    check(worst <= int(0.90 * tone_steady), f"worst <= 0.90 x TONE ({worst / tone_steady:.3f} x)")
    check(silent - silent_tone < 300, f"silent marginal {silent - silent_tone} < 300 over TONE silent")

    # level: hero closed peak vs stock METAL, same run
    def peak(e, machine, **kw):
        e.solo(0)
        e.set(0, machine=machine, note=60, pitch=64, finetune=64, color=64, shape=64, sweep=64,
              contour=64, punch=0, gate=0, decay=40)
        for k, v in kw.items():
            e.set(0, **{k: v})
        e.uc.mem_write(0x40fe4cdc, struct.pack(">H", 100 << 8))
        x = e.render(100, trig_at=(1,), track=0)
        return int(np.max(np.abs(x)))

    metal = peak(E.Engine(stock), "METAL")
    sheen = peak(harness.engine(*builds["sheen"]), 13)
    ratio = 20 * np.log10((sheen + 1e-9) / (metal + 1e-9))
    check(abs(ratio) <= 3, f"hero peak {sheen} vs METAL {metal}: {ratio:+.2f} dB")


def six_voices(builds, stock):
    print("\n§9.4 six voices")
    # six SHEEN tracks at the worst setting
    e = harness.engine(*builds["sheen"])
    for t in range(6):
        e.set(t, machine=13, note=60, pitch=64, finetune=64, color=64, shape=127, sweep=127,
              contour=127, decay=115, punch=1, gate=1)
        e.uc.mem_write(0x40fe4cdc + 2 * t, struct.pack(">H", 127 << 8))
    e.count_instructions()
    vals = []
    for b in range(6):
        e.instructions = 0
        e.block(0x3f if b == 1 else 0)
        vals.append(e.instructions)
    six_sheen = max(vals[2:])
    # six TONE tracks, defaults, all triggered
    e2 = E.Engine(stock)
    for t in range(6):
        e2.machine_defaults(t, "TONE")
        e2.set(t, note=60)
    e2.count_instructions()
    vals = []
    for b in range(6):
        e2.instructions = 0
        e2.block(0x3f if b == 1 else 0)
        vals.append(e2.instructions)
    six_tone = max(vals[1:])
    print(f"  ..    six SHEEN {six_sheen}, six TONE {six_tone} ({six_sheen / six_tone:.3f} x)")
    check(six_sheen <= six_tone, f"six SHEEN {six_sheen} <= six TONE {six_tone}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cycles", required=True)
    ap.add_argument("--only", default="exact,cpu,level,six")
    ap.add_argument("--cases", default="", help="only cases whose name contains this text")
    args = ap.parse_args()
    only = [s.strip() for s in args.only.split(",") if s.strip()]
    build_cases()
    if args.cases:
        global CASES
        CASES = [c for c in CASES if args.cases in c["name"]]
    stock = main_os(args.cycles)
    print(f"\nbuilding SHEEN at machine 13")
    tweak, info, img, runtime = harness.built(stock, ["sheen"])
    builds = {"sheen": (img, runtime, tweak)}
    for p in info["packages"]:
        if p["id"] == "sheen":
            print(f"  ..    sheen: machine {p['index']}, code {p['code']} bytes, variables {p['bss']} bytes")
    if "exact" in only:
        exact(builds)
    if "cpu" in only or "level" in only:
        cpu_and_level(builds, stock)
    if "six" in only:
        six_voices(builds, stock)
    print("\nALL OK" if not FAIL else f"\n{len(FAIL)} FAILED: " + "; ".join(FAIL[:8]))
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
