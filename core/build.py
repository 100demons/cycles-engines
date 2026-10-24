"""Build a Model:Cycles firmware from your own copy of the official OS, applying byte patches.

No Elektron firmware is distributed here. This module applies the write tables of composed tweaks (the
byte patches from core/compose.py) to section 3 (MAIN OS) of your .syx, then rebuilds and re-signs the
container (checksums + HMAC-SHA256).

The low-level engine (mtlib) comes from drumkilla/elektron-model-tweaks (MIT); see core/mtlib/LICENSE.
Carried over from tools/build.py of the old repo (100demons/cycles-flasher, MIT), trimmed to the parts
the composer needs. Only section 3 is touched: bootloader and updater are kept, so the STARTUP MENU
(MIDI IN) recovery stays available.
"""
import hashlib
import re
import sys

from mtlib import aplib, container            # noqa: F401  (aplib, container re-exported)
from mtlib.syx import unwrap, wrap, BYTES_PER_MSG  # noqa: F401  (re-exported)

BASE = 0x40000400                             # VA of the first byte of section 3 (MAIN OS)
END_LIMIT = 0x40200000                        # the grown image must end below the bootstrap's work area

HEX = re.compile(r"(?:[0-9a-fA-F]{2})*")


def sha(b):
    return hashlib.sha256(b).hexdigest()


def unhex(s):
    """bytes.fromhex but without spaces (two hex digits per byte), like fromHex of docs/flasher/builder.js."""
    if not isinstance(s, str) or not HEX.fullmatch(s):
        raise SystemExit(f"!! invalid hex: {str(s)[:24]}")
    return bytes.fromhex(s)


def check_conflicts(chosen):
    ids = {t["id"] for t in chosen}
    for t in chosen:
        for other in t.get("conflicts", []):
            if other in ids:
                raise SystemExit(f"!! {t['id']} and {other} are incompatible (they change the same bytes)")
        for need in t.get("requires", []):
            if need not in ids:
                raise SystemExit(f"!! {t['id']} also needs {need}")


def cave_zones(main_os, chosen):
    """0xFF zones of the original image (or a zone a tweak writes) that a tweak writes into: [(lo, hi)] in offsets."""
    zones = {}
    for t in chosen:
        for w in t["writes"]:
            old = bytes.fromhex(w["old"])
            if len(old) < 2 or old.count(0xFF) != len(old):
                continue
            lo, hi = w["off"], w["off"] + len(old)
            while lo > 0 and main_os[lo - 1] == 0xFF:
                lo -= 1
            zones[lo] = max(hi, zones.get(lo, hi))
    return sorted(zones.items())


def refs_into(main_os, zones):
    """References in the ORIGINAL MAIN OS into the zones [(lo, hi)] (offsets). Returns (sure, doubtful)."""
    import array
    w = array.array("H", main_os[:len(main_os) & ~1])
    if sys.byteorder == "little":
        w.byteswap()
    vz = [(lo + BASE, hi + BASE) for lo, hi in zones]
    lo_all, hi_all = min(a for a, _ in vz), max(b for _, b in vz)

    def inside(t):
        return lo_all <= t < hi_all and any(a <= t < b for a, b in vz)

    def s16(x):
        return x - 0x10000 if x & 0x8000 else x

    sure, doubt = [], []
    n = len(w)
    for i in range(n - 1):
        x, va = w[i], BASE + 2 * i
        v = (x << 16) | w[i + 1]
        if inside(v):
            sure.append((va, v, "32-bit constant"))
        t = None
        if (x & 0x3F) == 0x3A and ((x & 0xF1FF) == 0x41FA or x in (0x487A, 0x4EFA, 0x4EBA)
                                    or ((x & 0xC000) == 0 and (x & 0x3000))):
            t, kind = va + 2 + s16(w[i + 1]), "addressing (d16,PC)"
        elif (x & 0xF000) == 0x6000:
            d8 = x & 0xFF
            if d8 == 0:
                t = va + 2 + s16(w[i + 1])
            elif d8 == 0xFF:
                t = va + 2 + ((((w[i + 1] << 16) | (w[i + 2] if i + 2 < n else 0)) ^ 0x80000000) - 0x80000000)
            else:
                t = va + 2 + (d8 - 0x100 if d8 & 0x80 else d8)
            kind = "branch"
        if t is not None and inside(t):
            doubt.append((va, t, kind))
    return sure, doubt


def known_ref(known, chosen, zones, hit):
    va, tgt, _ = hit
    zlo, zhi = next((lo, hi) for lo, hi in zones if lo + BASE <= tgt < hi + BASE)
    for k in known or ():
        if int(k["ref"], 16) != va:
            continue
        lo, hi = int(k["lo"], 16) - BASE, int(k["hi"], 16) - BASE
        spans = [(w["off"], w["off"] + len(bytes.fromhex(w["old"]))) for t in chosen for w in t["writes"]]
        if all(lo <= a and b <= hi for a, b in spans if a < zhi and b > zlo):
            return k
    return None


def check_caves(main_os, chosen, force, dirty=None, patched=None, known=None):
    """Refuse to write into a 0xFF zone that the original image references (it may not be free)."""
    zones = cave_zones(main_os, chosen)
    if not zones:
        return
    for lo, hi in zones:
        print(f"  used 0xFF zone: 0x{lo + BASE:08x}..0x{hi + BASE - 1:08x} ({hi - lo} o)")
    sure, doubt = refs_into(main_os, zones)
    if dirty is not None:
        def rewritten(hit):
            o = hit[0] - BASE
            if not any(dirty[o:o + 4]):
                return False
            if patched is not None and hit[2].startswith("32-bit"):
                v = int.from_bytes(patched[o:o + 4], "big")
                if any(lo + BASE <= v < hi + BASE for lo, hi in zones):
                    return False
            return True
        gone = [h for h in sure + doubt if rewritten(h)]
        sure = [h for h in sure if not rewritten(h)]
        doubt = [h for h in doubt if not rewritten(h)]
        for va, t, kind in gone:
            print(f"  reference rewritten by a tweak (neutralised): 0x{va:08x} -> 0x{t:08x} ({kind})")
    if known:
        acked = [(h, known_ref(known, chosen, zones, h)) for h in sure]
        for (va, t, _), k in acked:
            if k:
                print(f"  known reference, writes in its free part: 0x{va:08x} -> 0x{t:08x} ({k['why']})")
        sure = [h for h, k in acked if not k]
    starts = {lo + BASE for lo, _ in zones}

    def show(hits):
        for va, t, kind in hits:
            z = max(s for s in starts if s <= t)
            note = " (first byte of the zone: may also be the end of the previous object)" if t == z else ""
            print(f"     0x{va:08x} : {kind} -> 0x{t:08x} (zone +0x{t - z:x}){note}")

    if doubt:
        print("  check in the disassembly (may be data that looks like code):")
        show(doubt)
    if not sure:
        print("  references (32-bit constants) to these zones in the original image: none")
        return
    print("!! the original image points into a 0xFF zone a tweak wants to write:")
    show(sure)
    if not force:
        raise SystemExit("   This zone may not be free: refusing to write.\n"
                         "   Check these references by hand, then rerun with --force-cave if they are harmless.")
    print("   --force-cave: continuing anyway.")


def apply_writes(main_os, tweaks_selected):
    data = bytearray(main_os)
    dirty = bytearray(len(main_os))
    for t in tweaks_selected:
        for w in t["writes"]:
            off = w["off"]
            old = unhex(w["old"])
            new = unhex(w["new"])
            if not isinstance(off, int) or isinstance(off, bool) or off < 0 or off + len(old) > len(data):
                raise SystemExit(f"!! {t['id']} : write outside section 3 (off {off})")
            cur = bytes(data[off:off + len(old)])
            if cur == new and cur != old:
                continue
            if cur != old:
                raise SystemExit(
                    f"!! {t['id']} @ {off}: expected 'old' bytes {old.hex()} "
                    f"but found {cur.hex()} — refusing to write")
            data[off:off + len(new)] = new
            for k in range(off, off + len(new)):
                dirty[k] = 1
    return bytes(data), dirty


def payload_runtime(t, main_os, st_img):
    """A tweak's "append" payload as it lies in memory at run time (from append.dest)."""
    ap_ = t["append"]
    dest, size = int(ap_["dest"], 16), ap_["size"]
    out = bytearray(size)
    for part in ap_["parts"]:
        at = int(part["dest"], 16) - dest
        if "syntakt" in part:
            lo, hi = (int(x, 16) for x in part["syntakt"])
            chunk = st_img[lo - BASE:hi - BASE]
        elif "cycles" in part:
            lo, hi = (int(x, 16) for x in part["cycles"])
            chunk = main_os[lo - BASE:hi - BASE]
        else:
            chunk = unhex(part["hex"])
        out[at:at + len(chunk)] = chunk
    for va, old, new in ap_["reloc"]:
        at = int(va, 16) - dest
        o, n = unhex(old), unhex(new)
        if len(o) != 4 or len(n) != 4 or not 0 <= at <= len(out) - 4:
            raise SystemExit(f"!! relocation {va} : 4 bytes expected in the payload")
        if out[at:at + 4] != o:
            raise SystemExit(f"!! relocation {va} : {old} expected, {out[at:at + 4].hex()} found")
        out[at:at + 4] = n
    return bytes(out)


def payload_image(t, runtime):
    """What goes into the image: the whole payload, packed in chunks (append.pack)."""
    ap_ = t["append"]
    if "compress" in ap_:
        if ap_["compress"] != "aplib":
            raise SystemExit(f"!! {t['id']} : unknown compression {ap_['compress']!r}")
        import aplib_grow
        return aplib_grow.pack(runtime)
    if "pack" not in ap_:
        return runtime
    dest, out, kept = int(ap_["dest"], 16), bytearray(), bytearray(runtime)
    for a, n in ap_["pack"]:
        at = int(a, 16) - dest
        out += runtime[at:at + n]
        kept[at:at + n] = bytes(n)
    if any(kept):
        raise SystemExit(f"!! {t['id']} : non-zero bytes outside the packed chunks")
    return bytes(out)


def build_payload(chosen, main_os, syntakt_path=None):
    """Payloads of the "append" tweaks, one after the other in tweak order. Returns (bytes, last tweak)."""
    apps = sorted((t for t in chosen if t.get("append")), key=lambda t: t["order"])
    if not apps:
        return b"", None
    img = None
    if any("syntakt" in t["append"] for t in apps):
        raise SystemExit("!! a Syntakt-source tweak needs tools/emu/syntakt.py, not carried into this repo")
    out = bytearray()
    for t in apps:
        ap_ = t["append"]
        if BASE + len(main_os) + len(out) != int(ap_["at"], 16):
            raise SystemExit(f"!! {t['id']} : the image does not end at {ap_['at']}")
        rt = payload_runtime(t, main_os, img)
        chunk = payload_image(t, rt)
        out += chunk
        where = "in place" if ap_["dest"] == ap_["at"] else f"copied to {ap_['dest']} at boot"
        packed = (f", compressed to {len(chunk)} o" if "compress" in ap_
                  else f", packed to {len(chunk)} o" if "pack" in ap_ else "")
        print(f"  payload {t['id']} : {ap_['size']} o from the tweak alone, {len(ap_['reloc'])} relocations, {where}{packed}")
    if BASE + len(main_os) + len(out) > END_LIMIT:
        raise SystemExit(f"!! the grown OS would pass {END_LIMIT:#x}")
    return bytes(out), apps[-1]
