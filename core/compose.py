#!/usr/bin/env python3
"""Composes added engines into one build at build time.

An engine is a package: engines/<id>/ with its sources and a manifest.json (schema cycles-mod/0.1). The
composer takes the packages the user names, in that order, and:

  1. places each package's committed object (engines/<id>/<id>.o) at its address with core/relocate.py, the
     values that depend on the build resolved as symbols (its "patch_points": the machine index and its first
     descriptor, and later the picture index);
  2. lays them out in the payload window (0x43000000, PAY_ALONE): code and tables one after the other from the
     window's start, variables one after the other from +0x36000, links each one there, and refuses a layout
     that crosses the runtime or a package's RAM claim;
  3. registers them all at once (the registrar runtime in core/registrar/registrar.json): the tables of names,
     update/render, records and descriptors, the bounds, the jumps into the committed runtime, its parameter
     block for this build, and the boot stub from the committed template;
  4. computes what the result cannot be installed with (declared conflicts; the composer refuses a build and
     says why — there are no hand-written exclusions);
  5. emits one tweak and builds the .syx with core/build.py (writes checked against the stock bytes, caves
     checked, repacked and re-signed, read back).

This is the reference implementation. docs/flasher/composer.js must give the same bytes; a test compares them
on every change. They are always changed in the same commit.

    python3 core/compose.py --cycles model-cycles_OS1.13.syx --machines lixr [--machines feral9,lixr] \
        [--out build/] [--emit path.json]

Runs with python3 alone (the committed objects, runtime and stub; no toolchain). Carried over from
tools/compose.py of the old repo (100demons/cycles-flasher, MIT); the stock-OS addresses it reads are in
core/os113.py.
"""
import argparse
import hashlib
import hmac
import json
import pathlib
import struct
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(HERE))

import aplib_grow                # noqa: E402
import build as mcb              # noqa: E402
import os113                     # noqa: E402
import relocate                  # noqa: E402
import sprites                   # noqa: E402

BASE = mcb.BASE
MACHINES = ROOT / "engines"
BUILD = ROOT / "build"
REGISTRAR = HERE / "registrar" / "registrar.json"


def sha(b):
    return hashlib.sha256(b).hexdigest()


def load_manifest(mid):
    path = MACHINES / mid / "manifest.json"
    if not path.is_file():
        sys.exit(f"!! no package {mid}: {path} is missing")
    m = json.loads(path.read_text(encoding="utf-8"))
    if m.get("schema") != "cycles-mod/0.1":
        sys.exit(f"!! {path}: schema {m.get('schema')!r}, expected cycles-mod/0.1")
    if m["id"] != mid:
        sys.exit(f"!! {path}: id {m['id']!r} is not the folder's name")
    m["_dir"] = MACHINES / mid
    m["_machine"] = next((u for u in m["uses"] if u["point"] == "machine"), None)
    if not m["_machine"]:
        sys.exit(f"!! {mid}: no 'machine' entry in uses: not an added machine")
    return m


def align4(x):
    return (x + 3) & ~3


def patch_values(m, values):
    """The build's values for the package's patch points, as symbols for the relocator."""
    defines, symbols = [], {}
    for pp in m["build"].get("patch_points", []):
        if pp["kind"] not in values:
            sys.exit(f"!! {m['id']}: patch point kind {pp['kind']!r} unknown")
        if "symbol" in pp:
            symbols[pp["symbol"]] = values[pp["kind"]]
        elif "define" in pp:
            defines.append(f"-D{pp['define']}={values[pp['kind']]}")
        else:
            sys.exit(f"!! {m['id']}: a patch point needs 'symbol' or 'define'")
    return defines, symbols


def object_path(m):
    objs = m["build"].get("objects", [])
    if len(objs) != 1:
        sys.exit(f"!! {m['id']}: build.objects must name one object (ld -r of the sources), has {objs}")
    return m["_dir"] / objs[0]


def place_package(m, code_at, bss_at, code_end, values, obj=None):
    """Places the package's committed object at code_at and bss_at with core/relocate.py, the patch points
    resolved to the build's values."""
    b = m["build"]
    defines, symbols = patch_values(m, values)
    if defines:
        sys.exit(f"!! {m['id']}: patch points as defines cannot be applied to a committed object: use symbols")
    if obj is None:
        path = object_path(m)
        if not path.is_file():
            sys.exit(f"!! {m['id']}: {path} is missing: run core/build_engine.py (it writes the object)")
        obj = path.read_bytes()
    return relocate.place(obj, b["symbols"]["update"], b["symbols"]["render"], b.get("keep", []), code_at, bss_at,
                          code_end, symbols, m["id"])


def web_package(mid):
    """A package as the web flasher reads it: the manifest entries the composer reads, and its committed object
    laid out once (relocate.layout, relocate.bundle). docs/flasher/composer.js composes from it what compose()
    composes from the manifest and the object."""
    m = load_manifest(mid)
    b = m["build"]
    upd, rnd, keep = b["symbols"]["update"], b["symbols"]["render"], b.get("keep", [])
    pps = b.get("patch_points", [])
    if any("symbol" not in pp for pp in pps):
        sys.exit(f"!! {mid}: patch points as defines cannot be applied to a committed object: use symbols")
    path = object_path(m)
    if not path.is_file():
        sys.exit(f"!! {mid}: {path} is missing: run core/build_engine.py (it writes the object)")
    lay = relocate.layout(path.read_bytes(), upd, rnd, keep, mid)
    uses = [{k: v for k, v in u.items() if k != "why"} for u in m["uses"]
            if u["point"] in ("machine", "memory.ram", "descriptor.state") or "hook" in u or "vtable" in u]
    return {"id": mid, "name": m["name"], "order": m["order"], "version": m["version"],
            "symbols": {"update": upd, "render": rnd}, "keep": keep, "patch_points": pps, "uses": uses,
            "conflicts_declared": [c["with"] for c in m.get("compat", {}).get("conflicts_declared", [])],
            "bundle": relocate.bundle(lay, [upd, rnd, *keep])}


def load_registrar():
    """The registrar's committed data (core/registrar/registrar.json): the boot stub template and the runtime."""
    if not REGISTRAR.is_file():
        sys.exit(f"!! {REGISTRAR} is missing: run core/gen_registrar.py")
    reg = json.loads(REGISTRAR.read_text(encoding="utf-8"))
    if reg.get("schema") != "model27-registrar/0.1":
        sys.exit(f"!! {REGISTRAR}: schema {reg.get('schema')!r}")
    return reg


def boot_stub(reg, src, dst, longs, segs):
    """The boot stub for this build, from the template: the three immediates filled in, the segment table
    (address, longs per segment, then 0, 0) appended."""
    st = reg["stub"]
    if int(st["at"], 16) != os113.CAVE:
        sys.exit(f"!! the stub template is for {st['at']}, the boot hook lives at {os113.CAVE:#x}")
    code = bytearray.fromhex(st["hex"])
    for key, v in (("src_at", src), ("dst_at", dst), ("longs_at", longs)):
        struct.pack_into(">I", code, st[key], v)
    table = b"".join(os113.be32(a) + os113.be32(n // 4) for a, n in segs) + bytes(8)
    stub = bytes(code[:st["segs_at"]]) + table
    if os113.CAVE + len(stub) > int(st["end_limit"], 16):
        sys.exit(f"!! boot stub of {len(stub)} bytes ({len(segs)} segments) does not fit before {st['end_limit']}")
    return stub


def parameter_block(fr, machines):
    """The runtime's parameter block for this build (registrar.S): header and one entry per machine."""
    lay, rt = fr["layout"], fr["runtime"]
    n, first = len(machines), lay["first"]
    nm, regs = first + n, rt["regs_at"]
    if n > rt["max_machines"]:
        sys.exit(f"!! {n} machines: the runtime's block holds {rt['max_machines']}")
    out = bytearray()
    out += bytes([n, first, first + n - 1, nm])
    out += os113.be32(76 + 5 * n)                        # last
    out += os113.be32(lay["data"] + 12 * nm)             # knob_tab
    out += os113.be32(regs + rt["regs_header"] + rt["entry_size"] * n)     # machines_end
    for i, m in enumerate(machines):
        e = bytearray(rt["entry_size"])
        e[0:4] = bytes([first + i, m["image"], 76 + 5 * i, 76 + 5 * i - 51])
        e[4:8] = os113.be32(lay["recs"] + 0x60 * i)      # rec
        e[8:12] = os113.be32(m.get("knob_hook", 0))      # knob_hook
        e[12:16] = os113.be32(m.get("state_init", 0))    # state object initializer
        out += e
    if len(out) > rt["regs_header"] + rt["entry_size"] * rt["max_machines"]:
        sys.exit("!! parameter block too big")
    return bytes(out)


def frame(img, reg, n):
    """The registrar's part of a build of n added engines that does not depend on which engines: the writes
    (moved table references, bounds, the jumps and the call into the runtime's fixed entries, the MACHINES
    page's marks), the reloc of the Algorithm parameter's maximum, and the layout, the runtime, the stub template
    and the stock addresses fill() reads."""
    pay, first = os113.PAY_ALONE, 6
    rt = reg["runtime"]
    if int(rt["at"], 16) != os113.STUBS or int(rt["descn"], 16) != os113.DESCN or rt["marks_row"] != os113.MARKS_ROW:
        sys.exit("!! registrar.json was generated for another layout: run core/gen_registrar.py")
    if not 1 <= n <= rt["max_machines"]:
        sys.exit(f"!! {n} machines: the runtime holds 1 to {rt['max_machines']}")
    nm = first + n
    top = nm - 1
    os113.set_rows(n)
    os113.check_room(n, nm)
    names_at, upd_at, rnd_at, map_at = os113.DATA, os113.DATA + 4 * nm, os113.DATA + 8 * nm, os113.DATA + 16 * nm
    entries = {k: int(v, 16) for k, v in rt["entries"].items()}
    writes = []

    def w(va, old, new_):
        if img[va - BASE:va - BASE + len(old)] != old:
            sys.exit(f"!! {va:#x}: {old.hex()} expected, {img[va - BASE:va - BASE + len(old)].hex()} found")
        writes.append({"off": va - BASE, "old": old.hex(), "new": new_.hex()})

    def moveq(va, old, new_, reg_byte):
        w(va, bytes([reg_byte, old]), bytes([reg_byte, new_]))

    def jmp(va):
        return bytes.fromhex("4ef9") + os113.be32(va)

    red = sprites.redirect_write(os113.CAVE)
    w(BASE + red["off"], bytes.fromhex(red["old"]), bytes.fromhex(red["new"]))
    w(os113.HOOK, bytes.fromhex(os113.HOOK_OLD), jmp(os113.CAVE) + bytes.fromhex("4e71"))
    moved = {os113.DESC: os113.DESCN, os113.DESC + 8: os113.DESCN + 8, os113.DESC + 0x20: os113.DESCN + 0x20,
             os113.ROWS: os113.ROWSN, os113.CCROWS: os113.CCROWSN, os113.NAMES: names_at,
             os113.UPDATE_TAB: upd_at, os113.RENDER_TAB: rnd_at, os113.MAP: map_at}
    want = {os113.DESC: 34, os113.DESC + 8: 1, os113.DESC + 0x20: 2, os113.ROWS: 5, os113.CCROWS: 2, os113.MAP: 1,
            os113.RENDER_TAB: 1}
    for old, new_ in moved.items():
        rs = os113.refs32(img, old)
        if len(rs) != want.get(old, 1):
            sys.exit(f"!! references to {old:#x}: {len(rs)}")
        for va in rs:
            w(va, os113.be32(old), os113.be32(new_))
    jumps = os113.JUMPS + os113.JUMPS8
    for va in os113.BOUNDS:
        if va in {j[0] for j in jumps}:
            continue
        b0, b1 = img[va - BASE], img[va - BASE + 1]
        if b0 & 0xf1 != 0x70 or b1 not in (75, 76):
            sys.exit(f"!! {va:#x}")
        moveq(va, b1, b1 + 5 * n, b0)
    for va, reg_byte in ((0x400a7dba, 0x72), (0x400a7df4, 0x70), (0x4005a6a6, 0x72), (0x400147a4, 0x70),
                         (0x400148aa, 0x72), (0x400148b2, 0x70), (0x400a25e0, 0x70)):
        if img[va - BASE] != reg_byte or img[va - BASE + 1] not in (5, 6):
            sys.exit(f"!! bound {va:#x}")
        moveq(va, img[va - BASE + 1], top, reg_byte)
    moveq(0x4005a572, 5, 6, 0x72)
    w(0x4005a2b8, bytes.fromhex("487800c0"), bytes.fromhex("4878") + (32 * nm).to_bytes(2, "big"))
    if nm > os113.MARKS_ROW:
        w(0x400a26a2, bytes.fromhex("7850428545f9"), jmp(entries["marks"]))
    else:
        moveq(0x400a26e8, img[0x400a26e8 - BASE + 1], nm, 0x70)
    for va in (0x4001b69c, 0x400a40a6, 0x400a4fb0):
        moveq(va, 5, 1, 0x70)
    for va, old, sym in jumps:
        w(va, bytes.fromhex(old), jmp(entries[sym]))
    for va, old, sym in os113.CALLS:
        w(va, bytes.fromhex(old), bytes.fromhex("4eb9") + os113.be32(entries[sym]) + bytes.fromhex("4e71"))
    writes.sort(key=lambda x: x["off"])
    lo_, hi_ = os113.DESC - BASE, os113.DESC - BASE + os113.NDESC * os113.DSTRIDE
    final, orig = bytearray(img[lo_:hi_]), img[lo_:hi_]
    alg = os113.ALG_DESC * os113.DSTRIDE + 0x0c
    if struct.unpack_from(">I", final, alg)[0] != (first - 1) << 8:
        sys.exit("!! maximum of the Algorithm parameter")
    struct.pack_into(">I", final, alg, top << 8)
    reloc = [[f"{os113.DESCN + k:#x}", orig[k:k + 4].hex(), final[k:k + 4].hex()]
             for k in range(0, len(final), 4) if final[k:k + 4] != orig[k:k + 4]]
    return {"n": n,
            "layout": {"pay": pay, "bss_off": os113.BSS_OFF, "first": first, "stubs": os113.STUBS,
                       "data": os113.DATA, "descn": os113.DESCN, "recs": os113.RECS, "end_limit": os113.END_LIMIT,
                       "image_end": BASE + os113.IMAGE_LEN, "cave": os113.CAVE,
                       "stub_room": sprites.zone(os113.CAVE)[1], "ndesc": os113.NDESC, "dstride": os113.DSTRIDE},
            "stock": {"names": os113.NAMES, "update": os113.UPDATE_TAB, "render": os113.RENDER_TAB,
                      "map": os113.MAP, "desc": os113.DESC,
                      "snare_desc": os113.DESC + os113.SNARE_DESC * os113.DSTRIDE},
            "runtime": {"at": os113.STUBS, "hex": rt["hex"], "regs_at": int(rt["regs_at"], 16),
                        "regs_header": rt["regs_header"], "entry_size": rt["entry_size"],
                        "max_machines": rt["max_machines"]},
            "stub": reg["stub"],
            "writes": writes, "reloc": reloc}


def frames(img, reg):
    """frame.json: the frames of n = 1 to the runtime's maximum, the writes they share listed once."""
    per = {n: frame(img, reg, n) for n in range(1, reg["runtime"]["max_machines"] + 1)}
    for f in per.values():
        if any(f[k] != per[1][k] for k in ("layout", "stock", "runtime", "stub")):
            sys.exit("!! the frames differ beyond their writes and reloc")
    common = [w for w in per[1]["writes"] if all(w in f["writes"] for f in per.values())]
    out = {"schema": "model27-registrar-frame/0.1",
           "description": [
               "The registrar's part of a build that depends on the number of machines only: for n = 1 to",
               "MAX_ENGINES, the writes (moved tables, bounds, the jumps and the call into the runtime, the marks),",
               "the reloc of the Algorithm parameter's maximum; once, the layout, the runtime, the boot stub template",
               "and the stock addresses whose values fill() reads in the user's own MAIN OS. Written by",
               "core/gen_registrar.py from compose.frame(); docs/flasher/composer.js fills it like compose.fill()."],
           "layout": per[1]["layout"], "stock": per[1]["stock"], "runtime": per[1]["runtime"], "stub": per[1]["stub"],
           "writes": common,
           "by_n": {str(n): {"writes": [w for w in f["writes"] if w not in common], "reloc": f["reloc"]}
                    for n, f in per.items()}}
    for n, f in per.items():
        back = frame_of(out, n)
        if back["writes"] != f["writes"] or back["reloc"] != f["reloc"]:
            sys.exit(f"!! frame.json would not give back the frame of {n} machines")
    return out


def frame_of(frs, n):
    """One n's frame out of frame.json (the composer.js way of reading it)."""
    if str(n) not in frs["by_n"]:
        sys.exit(f"!! no frame for {n} machines")
    fr = {k: frs[k] for k in ("layout", "stock", "runtime", "stub")}
    fr.update(n=n, writes=sorted(frs["writes"] + frs["by_n"][str(n)]["writes"], key=lambda x: x["off"]),
              reloc=frs["by_n"][str(n)]["reloc"])
    return fr


def fill(fr, img, machines, blobs, bss_end):
    """The rest of the registrar's part, what depends on the machines: the data block (names, update and render
    addresses, the strings), five descriptors per machine (SNARE's, read in the official image, with the knob
    fields set), the parameter block, the payload parts in their order, the segments and the boot stub.
    docs/flasher/composer.js does the same from frame.json. Returns (writes, append)."""
    lay, stock, rt = fr["layout"], fr["stock"], fr["runtime"]
    n, first = len(machines), lay["first"]
    if n != fr["n"]:
        sys.exit(f"!! a frame for {fr['n']} machines, {n} given")
    for m in machines:
        if len(m["knobs"]) != 4:
            sys.exit(f"!! {m['name']}: {len(m['knobs'])} knobs; a machine has 4 and DECAY, five descriptors")
    nm = first + n
    u32 = lambda va: struct.unpack_from(">I", img, va - BASE)[0]
    map_at = lay["data"] + 16 * nm
    # --- data (as the old added_machine): names, update/render tables, machine -> record, strings
    strings = [m["name"] for m in machines] + [k[i] for m in machines for k in m["knobs"] for i in (0, 1)]
    at, addr = map_at + ((nm + 3) & ~3), {}
    for s_ in strings:
        if s_ not in addr:
            addr[s_] = at
            at += len(s_) + 1
    names = [u32(stock["names"] + 4 * i) for i in range(first)] + [addr[m["name"]] for m in machines]
    upd = [u32(stock["update"] + 4 * i) for i in range(first)] + [m["update"] for m in machines]
    rnd = [u32(stock["render"] + 4 * i) for i in range(first)] + [m["render"] for m in machines]
    data = bytearray()
    for t in (names, upd, rnd, range(1, nm + 1)):
        data += b"".join(os113.be32(x) for x in t)
    data += img[stock["map"] - BASE:stock["map"] - BASE + first] + bytes(range(first, nm)) + bytes(((nm + 3) & ~3) - nm)
    for s_ in addr:
        data += s_.encode("ascii") + b"\0"
    if lay["data"] + len(data) > lay["descn"]:
        sys.exit("!! data past DESCN")
    # --- 5 descriptors per machine, modelled on SNARE's
    ds, new = lay["dstride"], bytearray()
    for m in machines:
        for k, (long_, short, default, *rng) in enumerate(m["knobs"]):
            e = bytearray(img[stock["snare_desc"] + k * ds - BASE:][:ds])
            e[0x00:0x04] = os113.be32(6)
            if rng:
                e[0x08:0x0c], e[0x0c:0x10] = os113.be32(rng[0] << 8), os113.be32(rng[1] << 8)
            e[0x10:0x14] = os113.be32(default << 8)
            e[0x2c:0x30] = os113.be32(addr[long_])
            e[0x34:0x38] = os113.be32(addr[short])
            new += e
        e = bytearray(img[stock["snare_desc"] + 4 * ds - BASE:][:ds])
        e[0x10:0x14] = os113.be32(m["decay"] << 8)
        new += e
    # --- payload: the packages, the runtime, its parameter block, the data, the descriptors
    regs, code = rt["regs_at"], bytes.fromhex(rt["hex"])
    if lay["stubs"] + len(code) > regs:
        sys.exit("!! the runtime runs into its parameter block")
    block = parameter_block(fr, machines)
    if regs + len(block) > lay["data"]:
        sys.exit("!! parameter block past DATA")
    parts = [{"dest": f"{at_:#x}", "hex": b_.hex()} for at_, b_ in blobs if at_ < lay["stubs"]] + [
        {"dest": f"{lay['stubs']:#x}", "hex": code.hex()},
        {"dest": f"{regs:#x}", "hex": block.hex()},
        {"dest": f"{lay['data']:#x}", "hex": bytes(data).hex()},
        {"dest": f"{lay['descn']:#x}", "cycles": [f"{stock['desc']:#x}", f"{stock['desc'] + lay['ndesc'] * ds:#x}"]},
        {"dest": f"{lay['descn'] + lay['ndesc'] * ds:#x}", "hex": bytes(new).hex()},
    ] + [{"dest": f"{at_:#x}", "hex": b_.hex()} for at_, b_ in blobs if at_ >= lay["stubs"]]
    size = bss_end - lay["pay"]
    segs = os113.segments(parts, size)
    at = lay["image_end"]
    if at + sum(n_ for _, n_ in segs) > lay["end_limit"]:
        sys.exit(f"!! {sum(n_ for _, n_ in segs)} bytes added: the image would pass {lay['end_limit']:#x}")
    stub = boot_stub(fr, at, lay["pay"], size // 4, segs)
    if len(stub) > lay["stub_room"]:
        sys.exit("!! boot stub too big for its mask")
    if img[lay["cave"] - BASE:lay["cave"] - BASE + len(stub)] != b"\xff" * len(stub):
        sys.exit("!! the boot stub's place is taken")
    writes = [dict(w) for w in fr["writes"]] + [{"off": lay["cave"] - BASE, "old": "ff" * len(stub), "new": stub.hex()}]
    writes.sort(key=lambda x: x["off"])
    for a_, b_ in zip(writes, writes[1:]):
        if a_["off"] + len(a_["new"]) // 2 > b_["off"]:
            sys.exit(f"!! writes overlap at {BASE + b_['off']:#x}")
    return writes, {"at": f"{at:#x}", "dest": f"{lay['pay']:#x}", "size": size, "parts": parts,
                    "reloc": [list(r) for r in fr["reloc"]], "pack": [[f"{a_:#x}", n_] for a_, n_ in segs]}


def registrar_writes(img, machines, blobs, bss_end, reg):
    """The registrar's writes and payload for the alone layout, without the toolchain: frame() then fill()."""
    return fill(frame(img, reg, len(machines)), img, machines, blobs, bss_end)


def package_writes(m, img, syms):
    """The package's own hooks (jsr/jmp into its code) and vtable entries, each checked against the stock bytes."""
    writes = []
    for u in m["uses"]:
        if "hook" in u:
            h = u["hook"]
            va, old = int(h["at"], 16), bytes.fromhex(h["stock"])
            target = os113.be32(syms[h["symbol"]])
            new = (bytes.fromhex("4eb9") + target if h["op"] == "jsr"
                   else bytes.fromhex("4ef9") + target + bytes.fromhex("4e71"))
            if len(new) != len(old):
                sys.exit(f"!! {m['id']}: hook at {va:#x}: {len(new)} bytes for {len(old)}")
            if img[va - BASE:va - BASE + len(old)] != old:
                sys.exit(f"!! {m['id']}: {va:#x}: {old.hex()} expected, {img[va - BASE:va - BASE + len(old)].hex()} found")
            writes.append({"off": va - BASE, "old": old.hex(), "new": new.hex()})
        elif "vtable" in u:
            vt = u["vtable"]
            va, method = int(vt["at"], 16), int(vt["stock_ptr"], 16)
            if os113.refs32(img, method) != [va]:
                sys.exit(f"!! {m['id']}: references to {method:#x}: {[hex(r) for r in os113.refs32(img, method)]}")
            writes.append({"off": va - BASE, "old": os113.be32(method).hex(), "new": os113.be32(syms[vt["symbol"]]).hex()})
    return writes


def ram_claims(m):
    return [(int(a, 16), int(b, 16)) for u in m["uses"] if u["point"] == "memory.ram" for a, b in [u["range"]]]


def computed_conflicts(writes, ram, declared):
    """Ids this build cannot be installed with (declared conflicts; no other tweaks are carried into this repo)."""
    return set(declared)


def compose(img, ids):
    """Builds the tweak for the engines named in ids, in that order. Returns (tweak, info)."""
    if not ids:
        sys.exit("!! name at least one engine")
    if len(ids) != len(set(ids)):
        sys.exit("!! an engine is named twice")
    mans = [load_manifest(i) for i in ids]
    pay = os113.PAY_ALONE
    os113.set_base(pay)
    first = 6
    machines, blobs, syms_of, info_pkgs = [], [], {}, []
    with tempfile.TemporaryDirectory() as d:
        tmp = pathlib.Path(d)
        code_cursor, bss_cursor = pay, pay + os113.BSS_OFF
        for i, m in enumerate(mans):
            code_at, bss_at = align4(code_cursor), align4(bss_cursor)
            values = {"machine_index": first + i, "descriptor_base": 76 + 5 * i}
            blob, syms, bss_end = place_package(m, code_at, bss_at, os113.STUBS, values)
            mc = m["_machine"]
            knobs = tuple((k["long"], k["short"], k["default"]) + ((k["min"], k["max"]) if "min" in k else ())
                          for k in mc["knobs"])
            state = next((u for u in m["uses"] if u["point"] == "descriptor.state"), None)
            if state and state.get("descriptor") != 1:
                sys.exit(f"!! {m['id']}: only the SHAPE descriptor (1) can have a state override for now")
            machines.append(dict(name=mc["name"], image=mc["image"], decay=mc["decay_default"], knobs=knobs,
                                 update=syms[m["build"]["symbols"]["update"]],
                                 render=syms[m["build"]["symbols"]["render"]],
                                 state_init=syms[state["init"]] if state else 0))
            blobs.append((code_at, blob))
            syms_of[m["id"]] = syms
            info_pkgs.append(dict(id=m["id"], index=first + i, desc=76 + 5 * i, code_at=code_at, code=len(blob),
                                  bss_at=bss_at, bss=bss_end - bss_at))
            code_cursor, bss_cursor = code_at + len(blob), bss_end
        for m in mans:                       # a package's RAM claim must lie outside the payload
            for lo, hi in ram_claims(m):
                if lo < bss_cursor and pay < hi:
                    sys.exit(f"!! {m['id']}: the payload ({pay:#x}..{bss_cursor:#x}) runs into its RAM claim {lo:#x}..{hi:#x}")
        reg = load_registrar()
        writes, append = registrar_writes(img, machines, blobs, bss_cursor, reg)
    for m in mans:
        writes += package_writes(m, img, syms_of[m["id"]])
    writes.sort(key=lambda x: x["off"])
    for a_, b_ in zip(writes, writes[1:]):
        if a_["off"] + len(a_["new"]) // 2 > b_["off"]:
            sys.exit(f"!! writes overlap at {BASE + b_['off']:#x}")
    ram = [(pay, bss_cursor)] + [r for m in mans for r in ram_claims(m)]
    declared = {c["with"] for m in mans for c in m.get("compat", {}).get("conflicts_declared", [])}
    tid = "+".join(ids)
    conflicts = computed_conflicts(writes, ram, declared) - {tid}
    tweak = {
        "id": tid,
        "order": min(m["order"] for m in mans),
        "name": " + ".join(m["name"] for m in mans) if len(mans) > 1 else mans[0]["name"],
        "description": [
            f"Composed by core/compose.py: {', '.join(f'{p['id']} as machine {p['index']}' for p in info_pkgs)},",
            "registered together through the registrar runtime (registrar.S, a parameter block per build); each",
            "engine's code from engines/<id>/ (manifest.json), placed at build time in the payload (%#x..%#x)." % (pay, bss_cursor),
        ] + [f"{m['id']}: {m['name']}" for m in mans],
        "device": "Model:Cycles",
        "os": "1.13",
        "section": 3,
        "version": "+".join(m["version"] for m in mans),
        "conflicts": sorted(conflicts),
        "writes": writes,
        "append": append,
        "symbols": {k: f"{v:#x}" for m in mans for k, v in syms_of[m["id"]].items()
                    if k in (m["build"]["symbols"]["update"], m["build"]["symbols"]["render"], *m["build"].get("keep", []))},
    }
    info = dict(pay=pay, end=bss_cursor, image=sum(n for _, n in append["pack"]), packages=info_pkgs)
    return tweak, info


def build_syx(raw, tweaks):
    """Applies the tweaks to the stock file with core/build.py's own steps; returns (syx, patched MAIN OS)."""
    stream, info = mcb.unwrap(raw)
    c = mcb.container.parse(stream)
    s3 = next(s for s in c["sections"] if s["id"] == 3)
    main_os, ops = mcb.aplib.depack(c["blob"][s3["off"]:s3["off"] + s3["size"]])
    dev = json.loads((HERE / "os113_dev.json").read_text(encoding="utf-8"))
    if sha(main_os) != dev["section_sha256"]:
        sys.exit("!! the MAIN OS is not the official 1.13")
    mcb.check_conflicts(tweaks)
    tweaks = sorted(tweaks, key=lambda t: t["order"])
    patched, dirty = mcb.apply_writes(main_os, tweaks)
    mcb.check_caves(main_os, tweaks, False, dirty, patched, dev.get("cave_refs_ok"))
    payload, _ = mcb.build_payload(tweaks, main_os)
    orig_len = len(patched)
    patched += payload
    dirty += bytearray(b"\x01") * len(payload)
    new_s3 = aplib_grow.repack_grow(patched, ops, dirty, orig_len)
    if mcb.aplib.depack(new_s3)[0] != patched:
        sys.exit("!! the repacked MAIN OS reads back different")
    msg = c["blob"][:len(c["blob"]) - mcb.container.DIGEST_LEN]
    expect = c["blob"][len(c["blob"]) - mcb.container.DIGEST_LEN:]
    plain = []
    for s in c["sections"]:
        try:
            plain.append(mcb.aplib.depack(c["blob"][s["off"]:s["off"] + s["size"]])[0])
        except Exception:
            pass
    key = mcb.container.find_key(plain, msg, expect)
    if not key:
        sys.exit("!! HMAC key not found")
    blob = mcb.container.rebuild(c, {3: new_s3}, key)
    out = mcb.wrap(mcb.container.build_stream(blob, mcb.BYTES_PER_MSG), info["product"], info["start_seq"])
    stream2, _ = mcb.unwrap(out)
    c2 = mcb.container.parse(stream2)
    s3b = next(s for s in c2["sections"] if s["id"] == 3)
    if mcb.aplib.depack(c2["blob"][s3b["off"]:s3b["off"] + s3b["size"]])[0] != patched:
        sys.exit("!! the written file does not unpack to the patched MAIN OS")
    body = c2["blob"][:len(c2["blob"]) - mcb.container.DIGEST_LEN]
    mac = c2["blob"][len(c2["blob"]) - mcb.container.DIGEST_LEN:]
    if hmac.new(key, body, hashlib.sha256).digest() != mac:
        sys.exit("!! HMAC of the written file does not verify")
    return out, patched


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--cycles", required=True, help="official model-cycles_OS1.13.syx")
    ap.add_argument("--machines", required=True, help="package ids under engines/, in order, comma-separated")
    ap.add_argument("--out", default=str(BUILD), help="folder for the .syx and the report (default: build/)")
    ap.add_argument("--emit", help="also write the composed tweak JSON here")
    args = ap.parse_args()
    raw = pathlib.Path(args.cycles).read_bytes()
    img = os113.cycles_main(args.cycles)
    if len(img) != os113.IMAGE_LEN:
        sys.exit("!! not the official MAIN OS 1.13")
    ids = [i.strip() for i in args.machines.split(",") if i.strip()]
    tweak, info = compose(img, ids)
    for p in info["packages"]:
        print(f"  {p['id']:12s} machine {p['index']}, descriptors {p['desc']}..{p['desc'] + 4}, code {p['code']} bytes at "
              f"{p['code_at']:#x}, variables {p['bss']} bytes at {p['bss_at']:#x}")
    print(f"  tweak {tweak['id']}: {len(tweak['writes'])} writes, payload {info['pay']:#x}..{info['end']:#x}, "
          f"{info['image']} bytes added to the image, {len(tweak['conflicts'])} conflicts computed")
    if args.emit:
        pathlib.Path(args.emit).write_text(json.dumps(tweak, indent=1, ensure_ascii=False) + "\n", encoding="utf-8")
        print(f"  wrote {args.emit}")
    out, patched = build_syx(raw, [tweak])
    outdir = pathlib.Path(args.out)
    outdir.mkdir(parents=True, exist_ok=True)
    name = tweak["id"]
    path = outdir / f"model-cycles_OS1.13-{name}.syx"
    path.write_bytes(out)
    report = {"machines": ids, "stock_sha256": sha(raw),
              "main_os": {"sha256": sha(patched), "bytes": len(patched), "end": f"{BASE + len(patched):#x}"},
              "output": {"file": path.name, "sha256": sha(out), "bytes": len(out)}, "layout": info}
    path.with_suffix(".syx.json").write_text(json.dumps(report, indent=1) + "\n", encoding="utf-8")
    print(f"  wrote {path} ({len(out)} bytes)\n  MAIN OS {len(patched)} bytes, SHA-256 {sha(patched)}")


if __name__ == "__main__":
    main()
