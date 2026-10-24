#!/usr/bin/env python3
"""Generates the registrar's committed data, core/registrar/registrar.json, and the frame, core/registrar/frame.json.

The registrar runtime (registrar.S) and the boot stub template (stub.S, its PACK form) are assembled once with the
m68k toolchain; their code bytes and the stub's immediates' offsets are committed, so the composer and the web
flasher need no assembler (core/compose.py, docs/flasher/composer.js). With --cycles, the frame of the registrar's
part for n = 1 to MAX_ENGINES machines (compose.frame()), read from the official MAIN OS, is also committed:
frame.json, which the web flasher fills like compose.fill().

    python3 core/gen_registrar.py [--cycles model-cycles_OS1.13.syx] [--check]
"""
import argparse
import json
import pathlib
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import build_engine                     # noqa: E402  (CROSS, run)
import compose                          # noqa: E402
import os113                            # noqa: E402

OUT = HERE / "registrar" / "registrar.json"
FRAME = HERE / "registrar" / "frame.json"
SRC = HERE / "registrar" / "registrar.S"
STUB = HERE / "registrar" / "stub.S"
REGS_OFF = 0x400                       # the parameter block: REGS = STUBS + 0x400, room for MAX_ENGINES entries
ENTRY = 16                             # one entry per machine: 16 bytes
MAX_ENGINES = os113.MAX_ENGINES        # the one capacity constant (core/registrar/layout.json)
ENTRIES = ("state_at", "state_connect", "cc_bounds", "drum_icons", "small_icon", "record_at", "record_of", "marks",
           "knob_vec", "desc_machine", "builder_row")
PLACEHOLDERS = {"src": 0x7a5a5a51, "dst": 0x7b5b5b52, "longs": 0x7c5c5c53}


def assemble(tmp, name, src, at, defs=()):
    """Assemble src (a .S file) at address at: (bytes, symbols)."""
    o, e, b = (tmp / (name + x) for x in (".o", ".elf", ".bin"))
    build_engine.run([build_engine.CROSS + "gcc", "-mcpu=54418", *defs, "-c", str(src), "-o", str(o)])
    build_engine.run([build_engine.CROSS + "ld", "-Ttext", f"{at:#x}", "-o", str(e), str(o)])
    build_engine.run([build_engine.CROSS + "objcopy", "-O", "binary", "-j", ".text", str(e), str(b)])
    syms = {p[-1]: int(p[0], 16) for p in (l.split() for l in build_engine.run([build_engine.CROSS + "nm", str(e)]).splitlines())
            if len(p) == 3}
    return b.read_bytes(), syms


def stub_template(tmp):
    """The PACK stub assembled at CAVE with placeholder immediates: (code bytes, {name: offset}, segs offset)."""
    (tmp / "segs.inc").write_text("")
    blob, _ = assemble(tmp, "stub", STUB, os113.CAVE, [
        f"-DPAYLOAD_SRC={PLACEHOLDERS['src']:#x}", f"-DPAYLOAD_DST={PLACEHOLDERS['dst']:#x}",
        f"-DPAYLOAD_LONGS={PLACEHOLDERS['longs']:#x}", "-DPACK", f"-I{tmp}"])
    if blob[-8:] != bytes(8):
        raise SystemExit("!! the stub does not end with the empty segment table")
    code = blob[:-8]
    offs = {}
    for name, v in PLACEHOLDERS.items():
        b = v.to_bytes(4, "big")
        i = code.find(b)
        if i < 0 or code.find(b, i + 1) >= 0:
            raise SystemExit(f"!! placeholder {name} found {code.count(b)} times in the stub")
        offs[name] = i
    return code, offs, len(code)


def runtime(tmp):
    """registrar.S assembled at STUBS for the alone layout: (code bytes, {entry: address}, regs)."""
    os113.set_base(os113.PAY_ALONE)
    regs = os113.STUBS + REGS_OFF
    blob, syms = assemble(tmp, "registrar", SRC, os113.STUBS,
                          [f"-DREGS={regs:#x}", f"-DDESCN={os113.DESCN:#x}", f"-DMARKS_ROW={os113.MARKS_ROW}"])
    if os113.STUBS + len(blob) > regs:
        raise SystemExit(f"!! the runtime ({len(blob)} bytes) runs into its parameter block at {regs:#x}")
    missing = [e for e in ENTRIES if e not in syms]
    if missing:
        raise SystemExit(f"!! entries missing from registrar.S: {missing}")
    return blob, {e: syms[e] for e in ENTRIES}, regs


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--cycles", help="official model-cycles_OS1.13.syx: also writes frame.json")
    ap.add_argument("--check", action="store_true", help="checks that the committed files match")
    args = ap.parse_args()
    with tempfile.TemporaryDirectory() as d:
        code, offs, segs_at = stub_template(pathlib.Path(d))
        rt, entries, regs = runtime(pathlib.Path(d))
    data = {
        "schema": "model27-registrar/0.1",
        "description": [
            "The registrar's committed data: what core/compose.py writes without the toolchain.",
            "stub: the PACK boot stub of core/registrar/stub.S assembled at the freed sprite mask",
            "(CAVE), its three build-dependent immediates at the given offsets (4 bytes, big endian: the payload's",
            "source at the end of the image, its destination, its length in longs), and the segment table appended",
            "at segs_at (address, longs per segment, then 0, 0).",
            "runtime: registrar.S assembled at STUBS of the alone layout (0x43033000), the detours of the added",
            "machines reading the parameter block at regs_at (see registrar.S for its layout: 16 bytes of header,",
            "then entry_size bytes per machine, MAX_ENGINES at most); entries are the addresses the OS's jumps",
            "and call are pointed at (JUMPS, JUMPS8, CALLS). Generated by core/gen_registrar.py.",
        ],
        "stub": {
            "at": f"{os113.CAVE:#x}",
            "end_limit": f"{os113.STUB_END:#x}",
            "hex": code.hex(),
            "src_at": offs["src"], "dst_at": offs["dst"], "longs_at": offs["longs"],
            "segs_at": segs_at,
        },
        "runtime": {
            "at": f"{os113.STUBS:#x}",
            "hex": rt.hex(),
            "entries": {e: f"{a:#x}" for e, a in entries.items()},
            "regs_at": f"{regs:#x}",
            "regs_header": 16, "entry_size": ENTRY, "max_machines": MAX_ENGINES,
            "descn": f"{os113.DESCN:#x}", "marks_row": os113.MARKS_ROW,
        },
    }
    text = json.dumps(data, indent=1) + "\n"
    print(f"  stub: {len(code)} bytes of code, immediates at {offs}, segment table at {segs_at}")
    print(f"  runtime: {len(rt)} bytes at {os113.STUBS:#x}, parameter block at {regs:#x} "
          f"({16 + ENTRY * MAX_ENGINES} bytes for {MAX_ENGINES} machines), {len(entries)} entries")
    OUT.parent.mkdir(parents=True, exist_ok=True)
    bad = 0
    if args.check:
        if OUT.read_text(encoding="utf-8") != text:
            print("!! core/registrar/registrar.json is not up to date: rerun core/gen_registrar.py")
            bad = 1
    else:
        OUT.write_text(text)
        print(f"  wrote {OUT.relative_to(HERE.parent)}")
    if args.cycles:
        img = os113.cycles_main(args.cycles)
        if len(img) != os113.IMAGE_LEN:
            raise SystemExit("!! not the official MAIN OS 1.13")
        frs = compose.frames(img, data)
        print(f"  frame: {len(frs['writes'])} writes for every n, "
              f"{', '.join(f'{len(v['writes'])} for n = {n}' for n, v in frs['by_n'].items())}")
        ftext = json.dumps(frs, indent=1) + "\n"
        if args.check:
            if FRAME.read_text(encoding="utf-8") != ftext:
                print("!! core/registrar/frame.json is not up to date: rerun core/gen_registrar.py --cycles ...")
                bad = 1
        else:
            FRAME.write_text(ftext)
            print(f"  wrote {FRAME.relative_to(HERE.parent)}")
    if bad:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
