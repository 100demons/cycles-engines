"""The stock Model:Cycles OS 1.13 addresses and the helpers the composer needs.

Everything here is extracted from the old repository's generators (MIT, credits kept): the reverse-engineered
OS 1.13 virtual addresses were never re-derived for this repo, they come from the old repo
(100demons/cycles-flasher, a fork of durchgemacht/model27, downstream of 18nelli18/Modded-Cycles). See
ARCHITECTURE.md §1.4 and §8.

Sources in the old repo:
  - gen_sdvintage_exact.py : BASE, IMAGE_LEN, CAVE, HOOK, HOOK_OLD (the boot hook at the freed sprite mask)
  - gen_sdvintage_7th.py   : the descriptor / table addresses, BOUNDS, JUMPS, CALLS, be32, refs32, cycles_main
  - gen_syntakt_engines.py : the payload layout (LAYOUT, ROWS7), set_base/set_rows/check_room/segments, MAP
  - gen_syntakt_machines.py: JUMPS8 (the detour sites for 8+ machines)
  - gen_macro.py           : BSS_OFF (the variables window offset)

Only the names and values compose.py actually reads are kept; the generators' own DSP/Syntakt analysis code is
not needed by the composer and is not carried over.
"""
import json
import pathlib

from mtlib import aplib, container          # noqa: F401  (cycles_main)
from mtlib.syx import unwrap                # noqa: F401  (cycles_main)

BASE = 0x40000400                            # VA of the first byte of section 3 (MAIN OS)
IMAGE_LEN = 0x1a9d40                         # MAIN OS Cycles 1.13: the payload starts right after

# --- the boot hook, at the freed sprite mask (gen_sdvintage_exact.py, tools/sprites.py) ---------------------
CAVE = 0x4016cae8                            # freed sprite mask: the boot stub goes here
STUB_END = 0x4016cba8                        # the boot stub must end before this (the next sprite mask)
HOOK = 0x400004b2                            # the Cycles BSS reset
HOOK_OLD = "4feffff048d700f0"                # lea -16(sp),sp ; movem.l d4-d7,(sp)

# --- descriptors, tables and bounds (gen_sdvintage_7th.py) ---------------------------------------------------
DESC, NDESC, DSTRIDE = 0x4010dce0, 76, 0x38  # parameter descriptors
SNARE_DESC = 51                              # COLOR, SHAPE, SWEEP, CONTOUR of SNARE, then its Amp Decay (55)
ALG_DESC = 41                                # "Algorithm": the machine choice
ROWS, CCROWS = 0x40a79418, 0x40a7ada4         # per-machine BSS tables (6 x 32 o), built by 0x4005a274
NAMES = 0x401177e4                           # 6 machine names (MACHINES screen)
RENDER_TAB, UPDATE_TAB, ENGINE_MAP = 0x40118610, 0x40118628, 0x40118640
MAP = ENGINE_MAP                             # machine -> update/render table entry (8 o)

# Descriptor-count bounds (moveq #76 / #75) of the table accessors, from the disassembly. Excluded:
# 0x4004df68 / 0x4004df82 (a 76-byte sound structure, NOT the descriptor count) and 0x4004df40 / 0x4004dfa2
# (per-descriptor state: the state_at / state_connect detours).
BOUNDS = (
    0x4000a93e, 0x4000a95e, 0x4000a984, 0x4000aa52, 0x4000b208, 0x4000b22a, 0x4000b3e4, 0x4000b476,
    0x4000b51a, 0x4000b65a, 0x4000b6ec, 0x4000b788, 0x4000b946, 0x4001d5e2, 0x4001d8ee, 0x4001d90e,
    0x4001e026, 0x4001f326, 0x40022a7c, 0x40022afe, 0x40029e78, 0x40029e98, 0x4002b430, 0x40046d5c,
    0x4004e31e, 0x4004e3ba, 0x4005a368, 0x4005a4e8, 0x4005a50a, 0x4005a52c, 0x4005a556, 0x4005a582,
    0x4005a5cc, 0x4005a5fe, 0x4005a628, 0x4005a65c, 0x4005a76a, 0x4005a7aa, 0x4005a7d0, 0x4005a7f8,
    0x4005a81a, 0x4005a83c, 0x4005a85e,
)
# (address, original bytes, detour symbol): "jmp detour" in place of whole instructions
JUMPS = (
    (0x4004df40, "704c222f0004", "state_at"),
    (0x4004dfa2, "704c222f0004", "state_connect"),
    (0x4005a8f0, "7405b4816532", "cc_bounds"),
    (0x400a2638, "eb8c48780001", "drum_icons"),
    (0x400a4dc4, "700541e8000a", "small_icon"),
    (0x4004df5c, "7206202f0004", "record_at"),    # record per machine (machine index + 1)
    (0x4004df76, "7205202f0004", "record_of"),    # record per machine (machine index)
)
# (address, original bytes, symbol): "jsr detour ; nop" in place of two instructions (8 o)
CALLS = (
    (0x4001e8da, "202f0020226a0068", "knob_vec"),  # knob -> descriptor: machine -> record
)
# detour sites for the 8th machine and beyond (gen_syntakt_machines.py)
JUMPS8 = (
    (0x4005a50a, "724c202f0004", "desc_machine"),
    (0x4005a340, "7005b085643e", "builder_row"),
)

# --- capacity and payload layout (ARCHITECTURE.md §3; the single source is core/registrar/layout.json) ----------
END_LIMIT = 0x40200000                        # the grown image must end below the bootstrap's work area
MARKS_ROW = 7                                 # marks per row on the MACHINES screen (two rows = 14 machines)
_layout = json.loads((pathlib.Path(__file__).resolve().parent / "registrar" / "layout.json").read_text(encoding="utf-8"))
MAX_ENGINES = _layout["MAX_ENGINES"]          # the one machine-count constant
FIRST = _layout["first"]                      # the stock machines (0..5); added engines are first..first+MAX_ENGINES-1
PAY_ALONE = int(_layout["pay"], 16)
BSS_OFF = int(_layout["bss_off"], 16)         # the variables window, above the moved tables
# the reserved table region, offsets from PAY_ALONE: the registrar runtime, the parameter block, the data block,
# the descriptors (76 + 5 x MAX_ENGINES), the parameter and CC rows (32 x (first + MAX_ENGINES)), the records
# (0x60 x MAX_ENGINES). Everything fits before BSS_OFF.
_OFFSET_KEYS = ("stubs", "data", "descn", "rowsn", "ccrowsn", "recs", "rec_atk")
LAYOUT = {k.upper(): int(_layout[k], 16) for k in _OFFSET_KEYS}

PAY = PAY_ALONE


def set_base(pay):
    """Place the payload at pay: the LAYOUT constants become pay + offset."""
    global PAY
    PAY = pay
    for k, off in LAYOUT.items():
        globals()[k] = pay + off


def set_rows(n):
    """Compatibility shim: the rows live at fixed addresses sized for MAX_ENGINES, so nothing moves per build."""
    if n > MAX_ENGINES:
        raise SystemExit(f"!! {n} machines: the room holds {MAX_ENGINES}")


def check_room(n, nm):
    """The tables of n added machines (nm machines in all) fit in their place (fixed, sized for MAX_ENGINES)."""
    if n > MAX_ENGINES:
        raise SystemExit(f"!! {n} machines: the room holds {MAX_ENGINES}")
    ndesc = NDESC + 5 * n
    for what, end, limit in (("descriptors", DESCN + ndesc * DSTRIDE, ROWSN),
                             ("rows", ROWSN + 32 * nm, CCROWSN),
                             ("CC rows", CCROWSN + 32 * nm, RECS),
                             ("records", RECS + 0x60 * n, REC_ATK)):
        if end > limit:
            raise SystemExit(f"!! {what} to {end:#x}, past {limit:#x}")


def segments(parts, size):
    """Payload chunks to pack into the image: the union of parts (runtime addresses) to 64 o, aligned on 4."""
    rs = []
    for p_ in parts:
        a = int(p_["dest"], 16)
        n = len(p_["hex"]) // 2 if "hex" in p_ else (lambda r: int(r[1], 16) - int(r[0], 16))(p_["cycles"])
        rs.append((a & ~3, (a + n + 3) & ~3))
    out = []
    for lo, hi in sorted(rs):
        if out and lo <= out[-1][1] + 64:
            out[-1][1] = max(out[-1][1], hi)
        else:
            out.append([lo, hi])
    if out[0][0] < PAY or out[-1][1] > PAY + size:
        raise SystemExit("!! chunks outside the payload")
    return [(lo, hi - lo) for lo, hi in out]


# --- small byte helpers (gen_sdvintage_7th.py) ----------------------------------------------------------------
def be32(x):
    import struct
    return struct.pack(">I", x & 0xffffffff)


def cycles_main(path):
    """The decompressed MAIN OS (section 3) of an official model-cycles_OS1.13.syx."""
    stream, _ = unwrap(pathlib.Path(path).read_bytes())
    c = container.parse(stream)
    s3 = next(s for s in c["sections"] if s["id"] == 3)
    return aplib.depack(c["blob"][s3["off"]:s3["off"] + s3["size"]])[0]


def refs32(img, v):
    """Virtual addresses of every 32-bit reference to v in the MAIN OS."""
    b, out, i = be32(v), [], img.find(be32(v))
    while i >= 0:
        if i % 2 == 0:
            out.append(BASE + i)
        i = img.find(b, i + 1)
    return out


set_base(PAY_ALONE)
