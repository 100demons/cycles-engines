#!/usr/bin/env python3
"""Feral's SHAPE knob shows the model names (NYB, GND, ...) instead of numbers.

Asks the OS's own state lookup (state_at, 0x4004df40) for Feral's SHAPE descriptor, runs the formatter the returned
object carries for each model value, and checks the text. Also checks the other knobs keep the generic object.

    python3 tests/emu/test_feral_names.py <model-cycles_OS1.13.syx>
"""
import struct
import sys

from harness import check, FAIL, main_os_from_syx, built, engine

NAMES = ["NYB", "GND", "BNJ", "RYT", "FRS", "RNG", "PLS", "SCN", "KOS"]
STATE_AT = 0x4004df40
BUF = 0x80000400


def main():
    stock = main_os_from_syx(sys.argv[1])
    for ids in (["feral9"], ["feral9", "punch", "blast"]):
        tweak, info, img, runtime = built(stock, ids)
        e = engine(img, runtime, tweak)
        obj = e.call(STATE_AT, 77)                       # descriptor 77 = Feral's SHAPE (slot 7)
        check(obj >= 0x43000000, f"{'+'.join(ids)}: SHAPE has Feral's own state object")
        for d in (76, 78, 79, 80):
            check(e.call(STATE_AT, d) < 0x43000000, f"{'+'.join(ids)}: descriptor {d} keeps the generic object")
        fmt = struct.unpack(">I", bytes(e.uc.mem_read(obj + 0x20, 4)))[0]
        got = []
        for v in range(9):
            e.uc.mem_write(BUF, b"\0" * 32)
            e.call(fmt, 0, v << 8, BUF)
            got.append(bytes(e.uc.mem_read(BUF, 8)).split(b"\0")[0].decode("latin-1"))
        check(got == NAMES, f"{'+'.join(ids)}: SHAPE text {got}")
    print("\nALL OK" if not FAIL else f"\n{len(FAIL)} FAILED")
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
