#!/usr/bin/env python3
"""Places a package's committed object at its address in the payload, without the toolchain (notes/60 §3, B2a).

A package's object is one relocatable m68k ELF file (ELF32, big endian, type REL; `m68k-elf-ld -r` of the package's
compiled sources, written by its generator and committed next to its manifest). It is compiled with
-ffunction-sections -fdata-sections, so every function and every table is a section of its own, and with -fno-pic,
so the only relocation type in it is R_68K_32: an absolute 32-bit address, symbol + addend, to write where the code
or a table refers to another section. This module does what the linker script of the B1 path (compose.LINK) did:

  1. reads the sections, the symbol table and the .rela.* tables;
  2. keeps only what the roots reach (--gc-sections): the sections holding the update and render entries and the
     "keep" symbols of the manifest, then everything their relocations point to;
  3. places the kept sections in the linker script's order: the update entry's section, the render entry's
     section, the other .text* in file order, then .rodata*, then .data*, each at its own alignment, from code_at;
     the .bss* from bss_at (not emitted: the boot hook zeroes the payload);
  4. resolves every symbol: a section symbol to its section's address, a defined symbol to its section's address
     plus its value, an undefined symbol to the value the composer gives for that patch point (the machine index,
     the first descriptor: what `ld --defsym` would do), anything else is an error naming the symbol;
  5. writes symbol + addend, 4 bytes big endian, at each relocation's place.

place() returns what compose.compile_package returns, (bytes from code_at to the end of the data, symbols, end of
the variables), so the composer uses one or the other. With the same object the two give the same bytes: that is
what compose.py --compare-ld checks, and the proof of this module.

Steps 1 to 4 depend on the object only, not on where it goes, as long as code and variables start 4-aligned and no
section asks for more (B3a, notes/61 §2): layout() does them once, from offset 0, and keeps each relocation as
(position, base, addend); apply() writes step 5 at the build's addresses; place() is the two together. bundle() is a
layout as the web flasher embeds it, and docs/flasher/composer.js places it the same way (tools/webcompose_check.sh).

    python3 tools/relocate.py tools/machines/refmachine/refmachine.o      # lists sections, symbols, relocations
"""
import struct
import sys

R_68K_32 = 1
SHT_PROGBITS, SHT_SYMTAB, SHT_STRTAB, SHT_RELA, SHT_NOBITS = 1, 2, 3, 4, 8
SHF_ALLOC = 2
SHN_UNDEF, SHN_ABS, SHN_COMMON = 0, 0xfff1, 0xfff2
STT_SECTION = 3
EM_68K = 4
IGNORED = (".comment", ".note", ".eh_frame", ".rela", ".symtab", ".strtab", ".shstrtab", ".debug", ".m68k")


class ObjectError(SystemExit):
    pass


class Elf:
    """A relocatable m68k ELF32 big-endian object: sections, symbols, relocations."""

    def __init__(self, data, name="object"):
        self.data, self.name = data, name
        if data[:4] != b"\x7fELF" or data[4] != 1 or data[5] != 2:
            raise ObjectError(f"!! {name}: not an ELF32 big-endian file")
        (e_type, e_machine, _v, _entry, _phoff, shoff, _flags, _ehsize, _phentsize, _phnum, shentsize, shnum,
         shstrndx) = struct.unpack_from(">HHIIIIIHHHHHH", data, 16)
        if e_type != 1 or e_machine != EM_68K:
            raise ObjectError(f"!! {name}: not a relocatable m68k object (type {e_type}, machine {e_machine})")
        if shentsize != 40:
            raise ObjectError(f"!! {name}: section header size {shentsize}")
        raw = [struct.unpack_from(">10I", data, shoff + 40 * i) for i in range(shnum)]
        shstr = raw[shstrndx]
        names = [self._str(shstr[4], shstr[5], r[0]) for r in raw]
        self.sections = [dict(index=i, name=names[i], type=r[1], flags=r[2], off=r[4], size=r[5], link=r[6],
                              info=r[7], align=max(r[8], 1)) for i, r in enumerate(raw)]
        sym = next((s for s in self.sections if s["type"] == SHT_SYMTAB), None)
        if not sym:
            raise ObjectError(f"!! {name}: no symbol table")
        strtab = self.sections[sym["link"]]
        self.symbols = []
        for i in range(sym["size"] // 16):
            st_name, value, size, info, _other, shndx = struct.unpack_from(">IIIBBH", data, sym["off"] + 16 * i)
            self.symbols.append(dict(index=i, name=self._str(strtab["off"], strtab["size"], st_name), value=value,
                                     size=size, type=info & 0xf, bind=info >> 4, shndx=shndx))
        self.relocs = {}                                 # target section index -> [(offset, symbol index, addend)]
        for s in self.sections:
            if s["type"] != SHT_RELA:
                continue
            if s["link"] != sym["index"]:
                raise ObjectError(f"!! {name}: {s['name']} uses another symbol table")
            rs = []
            for i in range(s["size"] // 12):
                r_offset, r_info, r_addend = struct.unpack_from(">IIi", data, s["off"] + 12 * i)
                if r_info & 0xff != R_68K_32:
                    raise ObjectError(f"!! {name}: relocation type {r_info & 0xff} in {s['name']} at {r_offset:#x}: "
                                      "only R_68K_32 is supported (absolute 32-bit; compile with -fno-pic)")
                rs.append((r_offset, r_info >> 8, r_addend))
            self.relocs[s["info"]] = rs
        self.by_name = {}
        for s_ in self.symbols:
            if s_["name"] and s_["type"] != STT_SECTION:
                self.by_name.setdefault(s_["name"], s_)

    def _str(self, off, size, i):
        end = self.data.index(b"\0", off + i, off + size)
        return self.data[off + i:end].decode("ascii", "replace")

    def comment(self):
        """The compiler's signature (.comment section), e.g. 'GCC: (GNU) 16.2.0'."""
        s = next((s for s in self.sections if s["name"] == ".comment"), None)
        if not s:
            return ""
        return " ".join(p.decode("ascii", "replace") for p in self.data[s["off"]:s["off"] + s["size"]].split(b"\0") if p)

    def alloc_sections(self):
        """The sections that take room in memory, by kind: 'text', 'rodata', 'data', 'bss'."""
        out = []
        for s in self.sections:
            if not s["flags"] & SHF_ALLOC or s["size"] == 0 and s["name"] in (".text", ".data", ".bss"):
                continue
            kind = next((k for k in ("text", "rodata", "data", "bss") if s["name"] == "." + k
                         or s["name"].startswith("." + k + ".")), None)
            if kind is None:
                raise ObjectError(f"!! {self.name}: unexpected section {s['name']} (size {s['size']})")
            if kind == "bss" and s["type"] != SHT_NOBITS or kind != "bss" and s["type"] != SHT_PROGBITS:
                raise ObjectError(f"!! {self.name}: section {s['name']} has type {s['type']}")
            out.append((kind, s))
        return out


def align(x, a):
    return (x + a - 1) & ~(a - 1)


def layout(obj, update, render, keep, name="object"):
    """What depends only on the object, done once: the garbage collection, the linker script's order and alignments,
    the code and tables laid out from offset 0, the variables from offset 0. Returns a dict: code (bytes, each
    relocated word zeroed), relocs [(position, base, addend)] with base "code", "bss", "abs" or the name of an undefined
    symbol (a patch point), symbols {name: (base, offset)}, bss (the variables' size, rounded to 4) and align (the
    largest alignment kept). A section aligned to more than 4 is refused: placed at offset 0, the object would not be
    the same at every 4-aligned address. apply() places it; the web flasher embeds it (bundle(), notes/61 §2)."""
    elf = Elf(obj, name)
    roots = []
    for s_ in (update, render, *keep):
        sym = elf.by_name.get(s_)
        if not sym or sym["shndx"] in (SHN_UNDEF, SHN_ABS, SHN_COMMON):
            raise ObjectError(f"!! {name}: symbol {s_} is not defined in the object")
        roots.append(sym["shndx"])
    # --- garbage collection: what the roots reach through their relocations
    kept, todo = set(), list(roots)
    while todo:
        i = todo.pop()
        if i in kept:
            continue
        kept.add(i)
        for _off, si, _add in elf.relocs.get(i, ()):
            sym = elf.symbols[si]
            if sym["shndx"] not in (SHN_UNDEF, SHN_ABS, SHN_COMMON):
                todo.append(sym["shndx"])
    if any(s["shndx"] == SHN_COMMON for s in elf.symbols):
        raise ObjectError(f"!! {name}: COMMON symbols (compile with -fno-common)")
    # --- placement, in the linker script's order, from offset 0
    secs = [(k, s) for k, s in elf.alloc_sections() if s["index"] in kept]
    big = max([s["align"] for _k, s in secs] + [1])
    if big > 4:
        raise ObjectError(f"!! {name}: a section aligned to {big}: placed once at offset 0, the object would not be the "
                          "same at every 4-aligned address")
    first = [s for s in secs if s[1]["index"] == elf.by_name[update]["shndx"]]
    second = [s for s in secs if s[1]["index"] == elf.by_name[render]["shndx"] and s not in first]
    rest_text = [s for s in secs if s[0] == "text" and s not in first and s not in second]
    groups = [("text", first + second + rest_text)] + [(k, [s for s in secs if s[0] == k]) for k in ("rodata", "data")]
    off, cur = {}, 0
    for _kind, group in groups:
        if not group:
            continue
        cur = align(cur, max(s["align"] for _k, s in group))
        for _k, s in group:
            cur = align(cur, s["align"])
            off[s["index"]] = ("code", cur)
            cur += s["size"]
    code = bytearray(cur)
    cur = 0
    for s in [s for k, s in secs if k == "bss"]:
        cur = align(cur, s["align"])
        off[s["index"]] = ("bss", cur)
        cur += s["size"]
    bss = align(cur, 4)
    for k, s in secs:
        if k != "bss":
            lo = off[s["index"]][1]
            code[lo:lo + s["size"]] = elf.data[s["off"]:s["off"] + s["size"]]
    # --- relocations: where, relative to what, plus what
    relocs = []
    for k, s in secs:
        for r_off, si, add in elf.relocs.get(s["index"], ()):
            if k == "bss":
                raise ObjectError(f"!! {name}: relocation in {s['name']}")
            if r_off + 4 > s["size"]:
                raise ObjectError(f"!! {name}: relocation at {r_off:#x} past the end of {s['name']}")
            sym, pos = elf.symbols[si], off[s["index"]][1] + r_off
            if sym["shndx"] == SHN_UNDEF:
                relocs.append((pos, sym["name"], add))
            elif sym["shndx"] == SHN_ABS:
                relocs.append((pos, "abs", sym["value"] + add))
            elif sym["shndx"] in off:
                base, o = off[sym["shndx"]]
                relocs.append((pos, base, o + sym["value"] + add))
            else:
                raise ObjectError(f"!! {name}: symbol {sym['name'] or si} in a section that was not placed")
            code[pos:pos + 4] = bytes(4)
    symbols = {}
    for s_ in elf.symbols:
        if s_["type"] == STT_SECTION or not s_["name"]:
            continue
        if s_["shndx"] in off:
            base, o = off[s_["shndx"]]
            symbols[s_["name"]] = (base, o + s_["value"])
        elif s_["shndx"] == SHN_ABS:
            symbols[s_["name"]] = ("abs", s_["value"])
    return dict(name=name, code=bytes(code), relocs=relocs, symbols=symbols, bss=bss, align=big)


def apply(lay, code_at, bss_at, code_end, values):
    """Places a layout at code_at (code, tables, data) and bss_at (variables), both 4-aligned, the patch points'
    symbols resolved from values. The code must end before code_end. Returns (blob, symbols, end of the variables),
    like compose.compile_package."""
    name = lay["name"]
    if code_at % 4 or bss_at % 4:
        raise ObjectError(f"!! {name}: placed at {code_at:#x} / {bss_at:#x}: code and variables start 4-aligned")
    if code_at + len(lay["code"]) > code_end:
        raise ObjectError(f"!! {name}: code and tables end at {code_at + len(lay['code']):#x}, past {code_end:#x}")
    bases = {"code": code_at, "bss": bss_at, "abs": 0}
    blob = bytearray(lay["code"])
    for pos, base, add in lay["relocs"]:
        if base in bases:
            v = bases[base] + add
        elif base in values:
            v = values[base] + add
        else:
            raise ObjectError(f"!! {name}: undefined symbol {base}: not one of this build's patch points "
                              f"({', '.join(sorted(values)) or 'none'})")
        struct.pack_into(">I", blob, pos, v & 0xffffffff)
    symbols = {k: bases[b] + o for k, (b, o) in lay["symbols"].items()}
    return bytes(blob), symbols, bss_at + lay["bss"]


def place(obj, update, render, keep, code_at, bss_at, code_end, values, name="object"):
    """Relocates the object (bytes) at code_at (code, tables, data) and bss_at (variables): layout() then apply().
    update and render are the entry symbols, keep the other symbols the composer's writes need (hooks, view methods);
    values maps an undefined symbol (a patch point) to its value. Returns (blob, symbols, end of the variables), like
    compose.compile_package."""
    return apply(layout(obj, update, render, keep, name), code_at, bss_at, code_end, values)


def bundle(lay, names):
    """A layout as the web flasher embeds it (JSON, notes/61 §2): the code in hex, the relocations, the symbols named
    (the entries and what the hooks and views point at) and the variables' size. docs/flasher/composer.js places it
    like apply()."""
    return {"code": lay["code"].hex(), "relocs": [list(r) for r in lay["relocs"]],
            "symbols": {k: list(lay["symbols"][k]) for k in names}, "bss": lay["bss"]}


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    import pathlib
    p = pathlib.Path(sys.argv[1])
    elf = Elf(p.read_bytes(), p.name)
    print(f"{p.name}: {len(elf.sections)} sections, {len(elf.symbols)} symbols, "
          f"{sum(len(r) for r in elf.relocs.values())} relocations (all R_68K_32); {elf.comment()}")
    for k, s in elf.alloc_sections():
        print(f"  {k:6s} {s['name']:32s} {s['size']:6d} bytes, align {s['align']}, {len(elf.relocs.get(s['index'], ()))} relocations")
    undef = sorted(s["name"] for s in elf.symbols if s["shndx"] == SHN_UNDEF and s["name"])
    print(f"  undefined: {', '.join(undef) or 'none'}")


if __name__ == "__main__":
    main()
