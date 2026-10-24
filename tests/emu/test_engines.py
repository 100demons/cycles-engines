#!/usr/bin/env python3
"""Emulator tests for the composed engines (ARCHITECTURE.md §7.2), in the real OS voice loop (Unicorn).

For each selection (each engine alone, then all of them), on the official OS 1.13:
  1. start-up: the bootstrap's own aPLib depacker reads the grown OS back; the boot hook copies the packed
     payload to its place and clears the BSS;
  2. every installed slot dispatches to its own update/render and makes sound through the stock amp chain;
  3. the six stock machines are unchanged: SNARE and KICK render sample for sample like the stock OS;
  4. no access outside mapped memory.

The emulator (mcengine.py, emac.py) is carried over from the old repo (tools/emu/), generalised so it checks any
slot. Needs Unicorn and numpy, and m68k-elf-objdump on the PATH (see AGENTS.md).

    python3 tests/emu/test_engines.py --cycles model-cycles_OS1.13.syx [--machines lixr,feral9,refmachine] \
        [--blocks 100]
"""
import argparse
import pathlib
import struct
import sys

import numpy as np

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent.parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(ROOT / "core"))

import aplib_grow                    # noqa: E402
import build as mcb                  # noqa: E402
import compose                       # noqa: E402
import os113                         # noqa: E402
import mcengine as E                 # noqa: E402

FAIL = []


def check(ok, msg):
    print(("  ok    " if ok else "  FAIL  ") + msg, flush=True)
    if not ok:
        FAIL.append(msg)


def main_os_from_syx(path):
    stream, _ = mcb.unwrap(pathlib.Path(path).read_bytes())
    c = mcb.container.parse(stream)
    s3 = next(s for s in c["sections"] if s["id"] == 3)
    return mcb.aplib.depack(c["blob"][s3["off"]:s3["off"] + s3["size"]])[0]


def boot_hook_ok(os_img, payload):
    """Run the real BSS reset 0x400004b2 of the patched OS: the hook copies the payload to 0x43000000, then the
    BSS is zeroed and the function returns."""
    from unicorn import Uc, UC_ARCH_M68K, UC_MODE_BIG_ENDIAN
    from unicorn import m68k_const as mk
    uc = Uc(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN)
    uc.ctl_set_cpu_model(mk.UC_CPU_M68K_ANY)
    uc.mem_map(0x40000000, 0x02400000)
    uc.mem_map(E.PAYLOAD_DST, 0x00100000)
    uc.mem_map(0x80000000, 0x00010000)
    uc.mem_map(0x90000000, 0x00020000)
    uc.mem_write(E.BASE, os_img)
    sram = bytearray(0x10000)
    sram[0:0x74c0] = os_img[0x4019b590 - E.BASE:0x401a2a50 - E.BASE]
    sram[0x8000:0x8000 + 0x76f0] = os_img[0x401a2a50 - E.BASE:0x401aa140 - E.BASE]
    uc.mem_write(0x80000000, bytes(sram))
    uc.mem_write(E.STOP, b"\x4e\x71\x4e\x71")
    sp = 0x90010000
    uc.mem_write(sp, struct.pack(">I", E.STOP))
    uc.reg_write(mk.UC_M68K_REG_A7, sp)
    uc.emu_start(0x400004b2, E.STOP, count=20_000_000)
    copied = bytes(uc.mem_read(E.PAYLOAD_DST, len(payload))) == payload
    src = E.BASE + E.IMAGE_LEN
    cleared = not any(uc.mem_read(src, len(payload))) and not any(uc.mem_read(0x42338000, 0xb0))
    back = uc.reg_read(mk.UC_M68K_REG_A7) == sp + 4
    ok = copied and cleared and back
    print(f"  {'ok    ' if ok else 'FAIL  '} boot hook: payload copied {'intact' if copied else 'WRONG'}, "
          f"BSS zeroed {'yes' if cleared else 'NO'}, return {'normal' if back else 'ABNORMAL'}", flush=True)
    return ok


def bootstrap_depack_ok(cycles_syx, stock, tweak):
    """Run the bootstrap's own aPLib depacker (section 2, 0x800006bc) on the grown section 3 and check it reads
    the OS back identically."""
    from unicorn import Uc, UC_ARCH_M68K, UC_MODE_BIG_ENDIAN, UC_HOOK_MEM_UNMAPPED
    from unicorn import m68k_const as mk
    stream, _ = mcb.unwrap(pathlib.Path(cycles_syx).read_bytes())
    c = mcb.container.parse(stream)
    sec = {x["id"]: c["blob"][x["off"]:x["off"] + x["size"]] for x in c["sections"]}
    boot = mcb.aplib.depack(sec[2])[0]
    _stock, ops = mcb.aplib.depack(sec[3])
    patched, dirty = mcb.apply_writes(stock, [tweak])
    payload, _ = mcb.build_payload([tweak], stock)
    want = bytes(patched) + payload
    comp = aplib_grow.repack_grow(want, ops, dirty + bytearray(b"\x01") * len(payload), len(stock))
    STAGE, DEST, STOP, SP = 0x40200000, 0x40000400, 0x8001f000, 0x8001e000
    uc = Uc(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN)
    uc.ctl_set_cpu_model(mk.UC_CPU_M68K_ANY)
    uc.mem_map(0x80000000, 0x20000)
    uc.mem_write(0x800003fc, boot)
    uc.mem_map(0x40000000, 0x00400000)
    uc.mem_write(STAGE, comp)
    uc.mem_write(STOP, b"\x4e\x71\x4e\x71")
    uc.mem_write(SP, struct.pack(">III", STOP, STAGE, DEST))
    uc.reg_write(mk.UC_M68K_REG_A7, SP)
    bad = []
    uc.hook_add(UC_HOOK_MEM_UNMAPPED, lambda u, a, addr, size, v, d: bad.append(addr) or False)
    uc.emu_start(0x800006bc, STOP, count=400_000_000)
    same = bytes(uc.mem_read(DEST, len(want))) == want
    fits = DEST + len(want) <= STAGE
    ok = same and fits and not bad and uc.reg_read(mk.UC_M68K_REG_D0) == len(want)
    print(f"  {'ok    ' if ok else 'FAIL  '} bootstrap depacker: grown OS read back {'identically' if same else 'DIFFERENTLY'}"
          f" ({len(comp)} -> {len(want)} o, end {DEST + len(want):#x} < {STAGE:#x}: {'yes' if fits else 'NO'})", flush=True)
    return ok


def code_ranges(tweak):
    """The payload's code as the composer placed it: the packages' code and tables, and the runtime."""
    return [(int(p["dest"], 16), int(p["dest"], 16) + len(p["hex"]) // 2) for p in tweak["append"]["parts"]
            if "hex" in p and int(p["dest"], 16) <= os113.STUBS]


def built(stock, ids):
    """The composed tweak, the patched MAIN OS followed by its packed payload, and the payload in memory."""
    tweak, info = compose.compose(stock, ids)
    patched, _ = mcb.apply_writes(stock, [tweak])
    runtime = mcb.payload_runtime(tweak, stock, None)
    packed = mcb.payload_image(tweak, runtime)
    return tweak, info, bytes(patched) + packed, runtime


def engine(img, runtime, tweak):
    return E.Engine(img, payload=(E.PAYLOAD_DST, runtime), payload_code=code_ranges(tweak))


def play(e, track, machine, knobs, decay, blocks, note=60):
    e.set(track, machine=machine, note=note, pitch=64, finetune=64, color=knobs[0], shape=knobs[1], sweep=knobs[2],
          contour=knobs[3], punch=0, gate=0, decay=decay)
    return e.render(blocks, trig_at=(1,), track=track)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--cycles", required=True, help="official model-cycles_OS1.13.syx")
    ap.add_argument("--machines", default="lixr,feral9,refmachine", help="engine ids, comma-separated")
    ap.add_argument("--blocks", type=int, default=100, help="blocks of 32 frames per sound")
    args = ap.parse_args()
    ids = [i.strip() for i in args.machines.split(",") if i.strip()]
    stock = main_os_from_syx(args.cycles)
    selections = [[i] for i in ids] + [ids]

    builds = {}
    for sel in selections:
        key = "+".join(sel)
        tweak, info, img, runtime = built(stock, sel)
        builds[key] = (tweak, img, runtime, sel)
        added = [p for p in info["packages"] if p["id"] != "placeholder"]
        print(f"\n{key}: " + ", ".join(f"{p['id']} = machine {p['index']}" for p in added)
              + f"; payload {len(runtime)} bytes, {len(img) - E.IMAGE_LEN} in the image")
        check(bootstrap_depack_ok(args.cycles, stock, tweak), f"{key}: bootstrap depack")
        check(boot_hook_ok(img, runtime), f"{key}: boot hook")
        e = engine(img, runtime, tweak)
        check(not e.unmapped, f"{key}: voice loop starts, no unmapped access")
        for p in added:
            man = compose.load_manifest(p["id"])
            mc = man["_machine"]
            knobs = [k["default"] for k in mc["knobs"]]
            e.set(0, machine=p["index"], note=36, pitch=64, finetune=64, color=knobs[0], shape=knobs[1],
                  sweep=knobs[2], contour=knobs[3], punch=0, gate=0, decay=mc["decay_default"])
            x = np.stack([e.block(1 if b in (1, 120) else 0) for b in range(160)])[:, 0]
            peak = int(np.abs(x).max())
            check(peak > 1_000_000 and not e.unmapped, f"{key}: {mc['name']} (machine {p['index']}) makes sound (peak {peak})")
            check(peak < 0x7ffffff0, f"{key}: {mc['name']} (machine {p['index']}) no hard clipping (peak {peak})")

    # the six stock machines are unchanged, sample for sample
    print("\nstock machines unchanged")
    for name, slot, params in (("SNARE", 1, (127, 8, 0)), ("KICK", 0, (10, 16, 16))):
        color, shape, sweep = params
        e = E.Engine(stock)
        x = play(e, 0, slot, (color, shape, sweep, 0), 40, args.blocks)
        for key, (tweak, img, runtime, _sel) in builds.items():
            f = engine(img, runtime, tweak)
            y = play(f, 0, slot, (color, shape, sweep, 0), 40, args.blocks)
            check(np.array_equal(x, y) and np.max(np.abs(x)) > 1e6 and not f.unmapped,
                  f"{key}: {name} on the patched OS identical to the stock OS, sample for sample")

    print("\nALL OK" if not FAIL else f"\n{len(FAIL)} FAILED: " + "; ".join(FAIL[:8]))
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
