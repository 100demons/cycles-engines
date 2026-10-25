#!/usr/bin/env python3
"""Build Rhythmicon V5 Cloud as a standalone .syx and run every proof.

    PATH=~/m68k-elf-16.2/bin:$PATH python3 core/build_engine.py rhythmicon5
    PATH=~/m68k-elf-16.2/bin:$PATH python3 build/rhythmicon5/build_and_prove.py

Checks:
  - the official OS hash;
  - the copy of HALO New's cloud, bit for bit, at min / default / max / Freeze;
  - SHAPE -> old SWEEP mapping (0 -> 0, 27 -> 16, 127 -> 76), against FIFTH's own T_RATE;
  - T60 at SHAPE 0, 27, 64, 96, 127, measured from host renders;
  - no fifth oscillator group: the source has none, and SHAPE 0 is sample-for-sample FIFTH's no-fifths build;
  - the machine defaults;
  - the emulator: boot, dispatch, sound, stock machines, host vs emulator bit-exact, CPU against TONE.
"""
import ctypes
import hashlib
import json
import pathlib
import re
import struct
import subprocess
import sys
import wave

import numpy as np

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / "core"))
sys.path.insert(0, str(ROOT / "tests" / "emu"))
sys.path.insert(0, str(ROOT / "tests" / "host"))

import os113                       # noqa: E402
import compose                     # noqa: E402
import build as mcb                # noqa: E402
import mcengine as E               # noqa: E402
import test_engines as T           # noqa: E402
import r5_host as H                # noqa: E402

from unicorn import UC_HOOK_CODE   # noqa: E402
from unicorn import m68k_const as mk  # noqa: E402

CYCLES = ROOT / "firmware" / "model-cycles_OS1.13.syx"
STOCK_SHA = "44fe586269631a0ca7da25a3383fc6733c314809505fc3cc52f1e0ed9800640c"
ID = "rhythmicon5"
SLOT = 7
OFF = 7
MARK = 0x52354331
OUT = HERE / "model-cycles_OS1.13-rhythmicon5.syx"
WAV = HERE / "wav"
CC = "/private/var/folders/t3/xm7ns55x7gs7whyhm0ppm4n80000gn/T/opencode/halonew/tools/machines/halo"
HALO_FALLBACK = ROOT.parent / "model27" / "tools" / "machines" / "halo"

FAIL = []


def check(ok, msg):
    print(("  ok    " if ok else "  FAIL  ") + msg, flush=True)
    if not ok:
        FAIL.append(msg)


def sha(b):
    return hashlib.sha256(b).hexdigest()


def json_defaults():
    m = json.loads((ROOT / "engines" / ID / "manifest.json").read_text())
    knobs = m["_machine"]["knobs"] if "_machine" in m else m["uses"][0]["knobs"]
    return m, m["uses"][0], {k["short"]: k["default"] for k in knobs}


# ------------------------------------------------------------------------------------------------ cloud copy
def cloud_copy():
    print("\ncloud: the copied code vs HALO New's cloud, sample for sample")
    halo = pathlib.Path(CC)
    if not (halo / "halo_dsp.h").is_file():
        halo = HALO_FALLBACK
    exe = HERE / "cloud_compare"
    sdk = "/Library/Developer/CommandLineTools/SDKs/MacOSX15.4.sdk"
    subprocess.run(["cc", "-O2", "-std=c99", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function",
                    "-isysroot", sdk, "-I", str(ROOT / "engines" / ID), "-I", str(halo),
                    str(ROOT / "tests" / "host" / "cloud_compare.c"),
                    str(ROOT / "tests" / "host" / "cloud_ref_wrap.c"),
                    str(ROOT / "tests" / "host" / "cloud_ours_wrap.c"), "-o", str(exe)], check=True)
    out = subprocess.run([str(exe)], capture_output=True, text=True)
    print(out.stdout.rstrip())
    check(out.returncode == 0 and "ALL OK" in out.stdout, "the cloud copy is bit-identical to HALO New's")


# ------------------------------------------------------------------------------------------------ sweep
def emac_qmul(a, b):
    v = ((a * b) >> 23) >> 8
    return max(-0x80000000, min(0x7fffffff, v))


def old_rate_table():
    text = (ROOT / "engines" / "fifth" / "fifth_tables.h").read_text()
    m = re.search(r"static const u32 T_RATE\[\d+\] = \{(.*?)\};", text, re.S)
    return [int(x, 0) for x in re.findall(r"-?(?:0x[0-9a-fA-F]+|\d+)", m.group(1))]


def lutku(t, k):
    i = k >> 8
    return (t[i] + emac_qmul(t[i + 1] - t[i], (k & 0xff) << 23)) & 0xffffffff


def sweep_proof(lib):
    print("\nsweep: SHAPE -> FIFTH's old SWEEP curve")
    t = old_rate_table()
    # the generated table is FIFTH's curve, unchanged
    text = (ROOT / "engines" / ID / "r5_tables.h").read_text()
    m = re.search(r"static const u32 T_RATE\[\d+\] = \{(.*?)\};", text, re.S)
    new = [int(x, 0) for x in re.findall(r"-?(?:0x[0-9a-fA-F]+|\d+)", m.group(1))]
    check(new == t, "T_RATE is FIFTH's own sweep curve, value for value")
    r0, r27, r127 = lib.r5h_rate(0), lib.r5h_rate(27 * 256), lib.r5h_rate(127 * 256)
    check(r0 == lutku(t, 0), f"SHAPE 0 gives old SWEEP 0 (rate {r0})")
    check(r27 == lutku(t, 16 * 256), f"SHAPE 27 gives old SWEEP 16 (rate {r27})")
    check(r127 == lutku(t, 76 * 256), f"SHAPE 127 gives old SWEEP 76 (rate {r127})")
    vals = [lib.r5h_rate(s * 256) for s in range(128)]
    check(all(vals[i] < vals[i + 1] for i in range(127)) and len(set(vals)) == 128,
          "128 SHAPE values give 128 distinct, rising sweep values (no repeats, no jumps)")
    worst = max(abs(vals[s] - lutku(t, (s * 256 * 76) // 127)) for s in range(128))
    check(worst <= 1, f"every SHAPE value follows SHAPE x 76 / 127 (worst {worst} LSB from the 8.8 reference)")


# ------------------------------------------------------------------------------------------------ T60
def t60_proof(lib):
    print("\ndecay: T60 measured from host renders (note C5, one octave)")
    ok = True
    for s in (0, 27, 64, 96, 127):
        expected = 0.325 * (0.020 / 0.325) ** (s / 127.0)
        x = H.render((0, s, 70, 0), note=84, blocks=3000, lib=lib)
        got = H.t60_measured(x, expected=expected)
        err = (got / expected - 1) * 100
        good = abs(err) <= 10
        ok &= good
        check(good, f"SHAPE {s:3d}: T60 {got * 1000:7.1f} ms, table {expected * 1000:7.1f} ms ({err:+.1f} %)")
    return ok


# ------------------------------------------------------------------------------------------------ no fifths
def no_fifths(lib):
    print("\nno fifths")
    src = (ROOT / "engines" / ID / "rhythmicon5_dsp.h").read_text()
    bad = [w for w in ("FIFTH_MAX", "amt", "sub", "OCT_MAX + j") if w in src]
    check(not bad, "the fifth oscillator group is gone from the DSP source (no fifth amount, no fifth stack)")

    # at SHAPE 0 the new ring equals FIFTH's CONTOUR 10 and the sweep equals old SWEEP 0: the output must be
    # sample-for-sample FIFTH's own no-fifths build (SHAPE 64).
    sdk = "/Library/Developer/CommandLineTools/SDKs/MacOSX15.4.sdk"
    so = HERE / "libfifthref.dylib"
    subprocess.run(["cc", "-O2", "-std=c99", "-Wall", "-Wextra", "-Werror", "-isysroot", sdk,
                    "-I", str(ROOT / "engines" / "fifth"), "-shared", "-fPIC",
                    str(ROOT / "tests" / "host" / "fifth_ref.c"), "-o", str(so)], check=True)
    f5 = ctypes.CDLL(str(so))
    f5.f5_reset.argtypes = []
    f5.f5_set.argtypes = [ctypes.c_int] * 6
    f5.f5_block.argtypes = [ctypes.c_void_p]
    out = (ctypes.c_int * 32)()
    f5.f5_reset()
    old = []
    for b in range(3000):
        f5.f5_set(48 << 16, 127 * 256, 64 * 256, 0, 10 * 256, 1 if b == 0 else 0)
        f5.f5_block(out)
        old.append(np.frombuffer(out, dtype=np.int32).copy())
    old = np.concatenate(old).astype(np.int64)
    h = H.Host(lib)
    h.reset(0)
    new = []
    for b in range(3000):
        h.set(48 << 16, 64 << 8, 64 << 8, 127 * 256, 0, 70, 0, 0, 1 if b == 0 else 0)
        new.append(h.dry())
    new = np.concatenate(new)
    check(np.array_equal(old, new), "SHAPE 0 is bit-identical to FIFTH with SHAPE 64 (never a fifth)")

    # and no fifth group can appear at any SHAPE: the engine only ever strikes 1, 2, 4, 8, 16 x the note.
    check("for (i = 0; i < oct; i++)" in src and "fp = inc << i" in src,
          "the only strike loop is the octave stack (f x 2^i)")


# ------------------------------------------------------------------------------------------------ host <-> emulator
class Probe:
    def __init__(self, eng, sym):
        self.eng, self.rec = eng, {}
        uc = eng.uc
        arg = lambda k: struct.unpack(">I", uc.mem_read(uc.reg_read(mk.UC_M68K_REG_A7) + 4 * k, 4))[0]

        def track(v):
            return (v - E.VOICE0) // E.VSTRIDE if (v - E.VOICE0) % E.VSTRIDE == 0 else -1

        def upd(uc_, a, s, u):
            if track(arg(2)) != 0:
                return
            vb = bytes(uc.mem_read(arg(2), E.VSTRIDE))
            pb = bytes(uc.mem_read(arg(3), 0x40))
            v32 = lambda o: struct.unpack_from(">i", vb, o)[0]
            p16 = lambda o: struct.unpack_from(">h", pb, o)[0]
            self.rec.update(mark=v32(0x2c) != MARK, trig=int(v32(0x38) != 0), v34=int(v32(0x34) != 0),
                            pmod=struct.unpack(">i", struct.pack(">I", arg(1)))[0],
                            pitch=p16(0x14), fine=p16(0x22), color=p16(0x16), shape=p16(0x18),
                            sweep=p16(0x1a), contour=p16(0x1c), punch=p16(0x1e), decay=struct.unpack_from(">b", pb, 0x24)[0])

        def rnd(uc_, a, s, u):
            if track(arg(2)) == 0:
                self.rec["out_at"] = arg(1)

        def amp(uc_, a, s, u):
            if track(arg(1)) == 0 and "out_at" in self.rec:
                self.rec["dry"] = np.frombuffer(bytes(uc.mem_read(self.rec["out_at"], 128)), ">i4").astype(np.int64)

        def wet(uc_, a, s, u):
            if self.rec.get("out_at") == arg(1):
                self.rec["wet_in"] = np.frombuffer(bytes(uc.mem_read(arg(1), 128)), ">i4").astype(np.int64)
                self.rec["wet_dry"] = arg(3)

        def ret(uc_, a, s, u):
            if "out_at" in self.rec:
                self.rec["post"] = np.frombuffer(bytes(uc.mem_read(self.rec["out_at"], 128)), ">i4").astype(np.int64)

        for addr, fn in ((sym["rhythmicon5_update"], upd), (sym["rhythmicon5_render"], rnd),
                         (0x400a9252, amp), (sym["rhythmicon5_wet_entry"], wet), (0x400a7e20, ret)):
            uc.hook_add(UC_HOOK_CODE, fn, begin=addr, end=addr + 1)


def prove_case(full, runtime, tweak, info, name, fn, blocks):
    sym = {k: int(v, 16) for k, v in tweak["symbols"].items()}
    eng = E.Engine(full, payload=(E.PAYLOAD_DST, runtime), payload_code=T.code_ranges(tweak))
    for t in range(1, 6):
        eng.uc.mem_write(E.VOICE0 + t * E.VSTRIDE, struct.pack(">II", OFF, OFF))
    probe = Probe(eng, sym)
    h = H.Host()
    h.reset(0)
    bad = [0, 0, 0]
    seen = [0, 0, 0]
    eng.set(0, machine=info["packages"][0]["index"], note=48, pitch=64, finetune=64, color=63, shape=27,
            sweep=70, contour=40, punch=0, gate=1, decay=0)
    for b in range(blocks):
        kw = fn(b)
        eng.set(0, **kw)
        probe.rec.clear()
        fin = eng.block(1 if b in (1,) else 0)[0]
        if "pitch" not in probe.rec:
            continue
        r = probe.rec
        if r["mark"]:
            h.reset(0)
        h.set(48 << 16, r["pitch"], r["fine"], r["color"], r["shape"], r["sweep"] >> 8, r["contour"] >> 8,
              r["punch"] != 0, r["trig"])
        x = h.dry()
        if "dry" in r:
            seen[0] += 1
            if not np.array_equal(x, r["dry"]):
                bad[0] += 1
        if "wet_in" in r:
            seen[1] += 1
            if not np.array_equal(h.wet(r["wet_in"], r["wet_dry"]), r["post"]):
                bad[1] += 1
        if "post" in r:
            seen[2] += 1
            if not np.array_equal(fin, r["post"] >> 1):
                bad[2] += 1
    check(all(x == 0 for x in bad) and all(x > 20 for x in seen) and not eng.unmapped,
          f"{name}: host vs emulator bit-exact (dry {seen[0]}, cloud {seen[1]}, final {seen[2]} blocks)")
    return sum(bad)


def emulator_proof(raw, img, tweak, info):
    print("\nemulator: the real OS 1.13 voice loop (Unicorn)")
    patched, _ = mcb.apply_writes(img, [tweak])
    runtime = mcb.payload_runtime(tweak, img, None)
    packed = mcb.payload_image(tweak, runtime)
    full = bytes(patched) + packed
    check(T.bootstrap_depack_ok(str(CYCLES), img, tweak), "bootstrap depacker reads the grown OS back")
    check(T.boot_hook_ok(full, runtime), "boot hook copies the payload and clears the BSS")

    p = info["packages"][0]
    m = json.loads((ROOT / "engines" / ID / "manifest.json").read_text())
    defaults = [k["default"] for k in m["uses"][0]["knobs"]]

    def engine():
        return E.Engine(full, payload=(E.PAYLOAD_DST, runtime), payload_code=T.code_ranges(tweak))

    e = engine()
    check(not e.unmapped, "voice loop starts, no unmapped access")
    e.set(0, machine=p["index"], note=60, pitch=64, finetune=64, color=defaults[0], shape=defaults[1],
          sweep=defaults[2], contour=defaults[3], punch=0, gate=0, decay=m["uses"][0]["decay_default"])
    x = np.stack([e.block(1 if b == 1 else 0) for b in range(48)])[:, 0]
    check(int(np.abs(x).max()) > 1_000_000 and not e.unmapped, f"makes sound at the defaults (peak {int(np.abs(x).max())})")

    # extremes, including worst-case clipping: SHAPE 0, cloud at max, Freeze on
    worst = 0
    for knobs, punch in (((0, 0, 0, 0), 0), ((127, 127, 127, 127), 0), ((127, 0, 127, 127), 1),
                         ((0, 0, 127, 127), 1)):
        for note in (30, 84):
            q = engine()
            q.set(0, machine=p["index"], note=note, pitch=64, finetune=64, color=knobs[0], shape=knobs[1],
                  sweep=knobs[2], contour=knobs[3], punch=punch, gate=0, decay=50)
            y = np.stack([q.block(1 if b in (1, 30) else 0) for b in range(120)])[:, 0]
            worst = max(worst, int(np.abs(y).max()))
            if q.unmapped:
                e.unmapped.extend(q.unmapped)
    check(worst < 0x7ffffff0 and not e.unmapped, f"extremes: peak {worst}, no hard clipping, no wraparound")

    print("\nstock machines unchanged, sample for sample:")
    for name, slot, params in (("SNARE", 1, (127, 8, 0)), ("KICK", 0, (10, 16, 16))):
        color, shape, sweep = params
        base = T.play(E.Engine(img), 0, slot, (color, shape, sweep, 0), 40, 100)
        q = engine()
        got = T.play(q, 0, slot, (color, shape, sweep, 0), 40, 100)
        check(np.array_equal(base, got) and np.max(np.abs(base)) > 1e6 and not q.unmapped,
              f"{name} on the patched OS identical to the stock OS")

    print("\nhost vs emulator, bit for bit:")
    bad = 0
    bad += prove_case(full, runtime, tweak, info, "default", lambda b: {}, 200)
    bad += prove_case(full, runtime, tweak, info, "SHAPE sweep + cloud + Freeze",
                      lambda b: dict(shape=(b * 127 // 160) & 127,
                                     sweep=40 + (b * 80 // 160) % 88,
                                     contour=20 + (b * 100 // 160) % 108,
                                     punch=1 if 90 <= b < 130 else 0), 200)
    bad += prove_case(full, runtime, tweak, info, "heavy (SHAPE 0, cloud max)",
                      lambda b: dict(color=127, shape=0, sweep=127, contour=127,
                                     punch=1 if 100 <= b < 150 else 0), 220)
    check(bad == 0, "host and emulator agree sample for sample")

    return full, runtime, p


# ------------------------------------------------------------------------------------------------ CPU
def cpu_proof(full, runtime, tweak, info):
    print("\nCPU, instructions per 32-frame block, one voice (whole loop minus the no-voice loop)")
    p = info["packages"][0]

    def engine():
        return E.Engine(full, payload=(E.PAYLOAD_DST, runtime), payload_code=T.code_ranges(tweak))

    def costs(machine, knobs, blocks=600, trig=True, freeze=0, decay=50):
        eng = engine()
        for t in range(1, 6):
            eng.uc.mem_write(E.VOICE0 + t * E.VSTRIDE, struct.pack(">II", OFF, OFF))
        eng.set(0, machine=machine, note=36, pitch=64, finetune=64, color=knobs[0], shape=knobs[1],
                sweep=knobs[2], contour=knobs[3], punch=freeze, gate=0, decay=decay)
        if machine == OFF:
            eng.uc.mem_write(E.VOICE0, struct.pack(">II", OFF, OFF))
        eng.count_instructions()
        per = []
        for b in range(blocks):
            i0 = eng.instructions
            eng.block(1 if (trig and b == 1) else 0)
            per.append(eng.instructions - i0)
        return np.array(per)

    zero = int(np.median(costs(OFF, (0, 0, 0, 0), blocks=20, trig=False)))
    tone = int(np.median(costs(4, (40, 38, 52, 42), blocks=80)[6:30])) - zero
    rows = []
    for name, kn, fr in (("default", (63, 27, 70, 40), 0),
                         ("heavy", (127, 0, 127, 127), 0),
                         ("heavy + Freeze", (127, 0, 127, 127), 1),
                         ("dry (CLOUD 0)", (127, 27, 70, 0), 0)):
        c = costs(p["index"], kn, freeze=fr) - zero
        steady = c[20:]
        rows.append((name, int(np.median(steady)), int(np.percentile(steady, 95)), int(c.max()), int(c[1])))
    print(f"  {'TONE':18s} {tone:6d}  (1.00 x TONE)")
    for name, med, p95, mx, first in rows:
        print(f"  {name:18s} {med:6d}  med {med / tone:.2f}, p95 {p95 / tone:.2f}, "
              f"first strike {first / tone:.2f} x TONE")
    med = max(r[1] for r in rows if r[0] != "dry (CLOUD 0)")
    steady = max(r[2] for r in rows if r[0] != "dry (CLOUD 0)")
    check(med < tone and steady <= tone,
          f"CPU under one TONE voice: worst median {med} ({med / tone:.2f}), worst steady {steady} "
          f"({steady / tone:.2f}) vs TONE {tone}")
    return tone, rows



def worst_case(lib):
    print("\nlevel: worst case and the Rhythmicon's own loudness")
    worst = 0
    for note in (30, 36, 48, 84):
        h = H.Host(lib)
        h.reset(0)
        y = []
        for b in range(6000):
            h.set(note << 16, 64 << 8, 64 << 8, 127 << 8, 0, 127, 127, 1 if b >= 3500 else 0,
                  1 if b in (1, 3000) else 0)
            x = h.dry()
            x = h.wet(x, 1)
            y.append(x)
        y = np.concatenate(y)
        peak = int(np.max(np.abs(y)))
        worst = max(worst, peak)
        check(peak < 0x7ffffff0, f"note {note}: SHAPE 0, cloud max, Freeze after the cloud filled: peak {peak}")
    check(worst < 0x7ffffff0, f"no hard clipping or fixed-point wraparound (worst peak {worst})")

    # the dry path must keep FIFTH's loudness: compare with FIFTH's own no-fifths build at the same sweep/ring
    x = H.render((63, 27, 70, 0), note=48, blocks=1500, trig_at=(1, 600, 1200), lib=lib).astype(np.float64) / 2 ** 30
    print(f"  Rhythmicon dry (SHAPE 27, CLOUD 0): peak {np.max(np.abs(x)):.3f}, "
          f"RMS {np.sqrt(np.mean(x * x)):.3f}")
    check(np.max(np.abs(x)) < 0.95, "the dry path stays below full scale, like FIFTH")

# ------------------------------------------------------------------------------------------------ main
def main():
    print("Rhythmicon V5 Cloud: build and proof")
    raw = CYCLES.read_bytes()
    got = sha(raw)
    check(got == STOCK_SHA, f"official model-cycles_OS1.13.syx SHA-256 {got}")

    m, machine, defaults = json_defaults()
    check(defaults["OCT"] == 63 and defaults["SHP"] == 27 and defaults["TIM"] == 70 and defaults["CLD"] == 40,
          "defaults: COLOR 63, SHAPE 27, cloud SWEEP 70, cloud CONTOUR 40 (HALO New's own defaults)")
    check(machine["decay_default"] == 80, "default DECAY 80 (FIFTH's startup value)")

    lib = H.build() and H.load()
    cloud_copy()
    sweep_proof(lib)
    t60_proof(lib)
    no_fifths(lib)
    worst_case(lib)

    img = os113.cycles_main(str(CYCLES))
    tweak, info = compose.compose(img, [ID], slots={ID: SLOT})
    print("\nplacement:")
    for q in info["packages"]:
        print(f"  {q['id']:12s} machine {q['index']}, code {q['code']} bytes at {q['code_at']:#x}, "
              f"variables {q['bss']} bytes")
    print(f"  payload {info['pay']:#x}..{info['end']:#x}, {info['image']} bytes added to the image")

    out, patched = compose.build_syx(raw, [tweak])
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_bytes(out)
    OUT.with_suffix(".syx.json").write_text(json.dumps({
        "machines": [ID], "slots": {ID: SLOT},
        "output": {"file": OUT.name, "sha256": sha(out), "bytes": len(out)},
        "main_os": {"sha256": sha(patched), "bytes": len(patched), "end": f"{mcb.BASE + len(patched):#x}"},
    }, indent=1) + "\n")
    print(f"\nwrote {OUT} ({len(out)} bytes)")
    print(f"MAIN OS {len(patched)} bytes, SHA-256 {sha(patched)}")

    full, runtime, p = emulator_proof(raw, img, tweak, info)
    tone, rows = cpu_proof(full, runtime, tweak, info)

    print("\nusual proof suite (tests/emu/test_engines.py):")
    res = subprocess.run([sys.executable, str(ROOT / "tests" / "emu" / "test_engines.py"),
                          "--cycles", str(CYCLES), "--machines", ID, "--blocks", "80"],
                         capture_output=True, text=True)
    tail = [ln for ln in res.stdout.splitlines() if "ALL OK" in ln or "FAIL" in ln or "makes sound" in ln]
    print("\n".join("  " + ln.strip() for ln in tail))
    check(res.returncode == 0 and "ALL OK" in res.stdout, "test_engines.py: ALL OK")

    print()
    if FAIL:
        print(f"{len(FAIL)} FAILURE(S):")
        for f in FAIL:
            print("  - " + f)
        return 1
    print("ALL OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
