"""Emulation exacte de l'EMAC ColdFire (MAC 32x32, modes fractionnaire/entier, signe) pour Unicorn.

L'EMAC d'Unicorn (QEMU) est fausse en mode fractionnaire signe (MACSR = 0xa0, celui du moteur
audio du M:C) : l'enveloppe d'ampli (-1.0 x -0.9947) y donne +0.50 au lieu de -0.9947, les sons
s'eteignent en quelques ms. On intercepte donc chaque instruction EMAC (decodee par
m68k-linux-gnu-objdump) et on l'execute ici : produit signe exact, precision de l'accumulateur
(8 bits sous le LSB), saturation (OMC), chargement parallele des MAC-with-load.
Formes utilisees par l'OS 1.13 : mac/msac.l Ry,Rx[,<ea>,Rw],ACCn ; movclr.l ; move.l ACC<->Rn ;
move.l #imm,MACSR. Voir notes/14 §4."""
import re, shutil, subprocess
from unicorn import UC_HOOK_CODE
from unicorn import m68k_const as mk

# the cross binutils as the generators find them: Debian's m68k-linux-gnu-, or m68k-elf- (Homebrew)
OBJDUMP = next((c + "objdump" for c in ("m68k-linux-gnu-", "m68k-elf-") if shutil.which(c + "objdump")),
               "m68k-linux-gnu-objdump")

REG = {f"%d{i}": getattr(mk, f"UC_M68K_REG_D{i}") for i in range(8)}
REG.update({f"%a{i}": getattr(mk, f"UC_M68K_REG_A{i}") for i in range(8)})
REG["%fp"] = mk.UC_M68K_REG_A6
REG["%sp"] = mk.UC_M68K_REG_A7
M32 = 0xffffffff


def s32(v):
    v &= M32
    return v - (1 << 32) if v & 0x80000000 else v


def disasm(binfile, base, start, stop, arch="m68k:isa-c:emac"):
    out = subprocess.run([OBJDUMP, "-D", "-b", "binary", "-m", arch,
                          f"--adjust-vma={base:#x}", f"--start-address={start:#x}",
                          f"--stop-address={stop:#x}", binfile],
                         capture_output=True, text=True, check=True).stdout
    res = {}
    for line in out.splitlines():
        m = re.match(r"\s*([0-9a-f]+):\t([0-9a-f ]+)\t(\S+)\s*(.*)$", line)
        if not m:
            continue
        addr = int(m.group(1), 16)
        size = len(m.group(2).replace(" ", "")) // 2
        res[addr] = (size, m.group(3), m.group(4).strip())
    return res


class EMAC:
    def __init__(self, uc):
        self.uc = uc
        self.acc = [0, 0, 0, 0]          # valeur exacte, unite 2^-62 (fract.) ou 1 (entier)
        self.macsr = 0
        self.mask = 0xffffffff
        self.ops = {}
        self.fast = {}
        self.next = {}
        self.count = 0

    # --- modes ---
    @property
    def frac(self): return bool(self.macsr & 0x20)
    @property
    def omc(self): return bool(self.macsr & 0x80)
    @property
    def unsigned(self): return bool(self.macsr & 0x40)
    @property
    def round(self): return bool(self.macsr & 0x10)

    def install(self, instrs):
        n = 0
        for addr, (size, mn, ops) in instrs.items():
            if mn in ("macl", "msacl", "macw", "msacw", "movclrl") or \
               (mn == "movel" and re.search(r"%acc|%macsr|%mask", ops)):
                self.ops[addr] = (size, mn, ops)
                f = self._compile(addr, size, mn, ops)
                if f is not None:
                    self.fast[addr] = f
                self.uc.hook_add(UC_HOOK_CODE, self._hook, begin=addr, end=addr)
                n += 1
        return n

    def r(self, name):
        return self.uc.reg_read(REG[name]) & M32

    def w(self, name, v):
        self.uc.reg_write(REG[name], v & M32)

    def _acc_to_reg(self, i):
        a = self.acc[i]
        if self.frac:
            v = a >> 31 if not self.round else (a + (1 << 30)) >> 31
        else:
            v = a
        if self.omc:
            v = max(-(1 << 31), min((1 << 31) - 1, v))
        return v & M32

    def _ea(self, spec):
        """Retourne (adresse, effet de bord) pour %aN@, %aN@+, %aN@-, %aN@(d)."""
        m = re.match(r"(%a[0-7]|%fp|%sp)@(\+|-|\((-?\d+)\))?(&?)$", spec)
        if not m:
            raise NotImplementedError("EA " + spec)
        an, mode, disp, amp = m.group(1), m.group(2), m.group(3), m.group(4)
        base = self.r(an)
        if mode == "-":
            base = (base - 4) & M32
            post = lambda: self.w(an, base)
        elif mode == "+":
            post = lambda: self.w(an, base + 4)
        else:
            post = lambda: None
            if disp:
                base = (base + int(disp)) & M32
        addr = base & self.mask if amp else base
        return addr, post

    def _hook(self, uc, addr, size, ud):
        f = self.fast.get(addr)
        if f is not None:
            f()
            uc.reg_write(mk.UC_M68K_REG_PC, self.next[addr])
            return
        self._slow(uc, addr)

    def _compile(self, addr, sz, mn, ops):
        """Fermetures rapides pour les formes du moteur : mac/msac.l R,R,accN et movclr/move."""
        uc = self.uc
        rr, rw = uc.reg_read, uc.reg_write
        args = [a.strip() for a in ops.split(",")]
        self.next[addr] = addr + sz
        if mn in ("macl", "msacl") and len(args) == 3:
            ra, rb, i = REG[args[0]], REG[args[1]], int(args[2][4])
            sign = 1 if mn == "macl" else -1
            acc = self.acc
            def f():
                a = rr(ra) & M32; b = rr(rb) & M32
                if not self.unsigned:
                    a = a - 0x100000000 if a & 0x80000000 else a
                    b = b - 0x100000000 if b & 0x80000000 else b
                p = a * b
                if self.frac:
                    p = (p >> 23) << 23
                acc[i] += sign * p
            return f
        if mn == "movclrl":
            i, rd = int(args[0][4]), REG[args[1]]
            def f():
                rw(rd, self._acc_to_reg(i)); self.acc[i] = 0
            return f
        if mn == "movel" and args[0].startswith("%acc") and args[0][4].isdigit() and args[1] in REG:
            i, rd = int(args[0][4]), REG[args[1]]
            return lambda: rw(rd, self._acc_to_reg(i))
        if mn == "movel" and args[1].startswith("%acc") and args[1][4].isdigit() and args[0] in REG:
            i, rs = int(args[1][4]), REG[args[0]]
            def f():
                v = rr(rs) & M32
                v = v - 0x100000000 if v & 0x80000000 else v
                self.acc[i] = (v << 31) if self.frac else v
            return f
        return None

    def _slow(self, uc, addr):
        sz, mn, ops = self.ops[addr]
        self.count += 1
        args = [a.strip() for a in ops.split(",")]
        if mn in ("macl", "msacl", "macw", "msacw"):
            acc = int(args[-1][4])
            ry, rx = args[0], args[1]
            rest = args[2:-1]
            shift = 0
            if rest and rest[0] in ("<<", ">>"):
                shift = 1 if rest[0] == "<<" else -1
                rest = rest[1:]
            if mn.endswith("w"):
                def half(n):
                    v = self.r(n[:-1]) if n[-1] in "ul" else self.r(n)
                    v = (v >> 16) if n[-1] == "u" else v
                    v &= 0xffff
                    return v if self.unsigned else (v - 0x10000 if v & 0x8000 else v)
                a, b = half(ry), half(rx)
                p = a * b
                if self.frac:
                    p <<= 32          # Q15*Q15 = Q30 -> meme echelle 2^-62
            else:
                a, b = self.r(ry), self.r(rx)
                if not self.unsigned:
                    a, b = s32(a), s32(b)
                p = a * b
            if shift == 1:
                p <<= 1
            elif shift == -1:
                p >>= 1
            if self.frac:
                p = (p >> 23) << 23   # precision de l'accumulateur (8 bits sous le LSB)
            if rest:                   # MAC avec chargement : <ea>,Rw
                ea, rw = rest[0], rest[1]
                ad, post = self._ea(ea)
                val = int.from_bytes(uc.mem_read(ad, 4), "big")
            self.acc[acc] += p if mn.startswith("mac") else -p
            if rest:
                self.w(rw, val)
                post()
        elif mn == "movclrl":
            i = int(args[0][4])
            self.w(args[1], self._acc_to_reg(i))
            self.acc[i] = 0
        elif mn == "movel":
            src, dst = args
            if src.startswith("%acc") and dst.startswith("%acc"):
                self.acc[int(dst[4])] = self.acc[int(src[4])]
            elif src.startswith("%acc") and src[4].isdigit():
                self.w(dst, self._acc_to_reg(int(src[4])))
            elif dst.startswith("%acc") and dst[4].isdigit():
                v = s32(self.r(src)) if src.startswith("%") else s32(int(src[1:]))
                self.acc[int(dst[4])] = (v << 31) if self.frac else v
            elif dst == "%macsr":
                self.macsr = (int(src[1:]) if src.startswith("#") else self.r(src)) & 0xff
            elif src == "%macsr":
                self.w(dst, self.macsr)
            elif dst == "%mask":
                self.mask = (int(src[1:]) if src.startswith("#") else self.r(src)) | 0xffff0000
            elif src == "%mask":
                self.w(dst, self.mask)
            else:
                raise NotImplementedError(f"{addr:#x} {mn} {ops}")
        else:
            raise NotImplementedError(f"{addr:#x} {mn} {ops}")
        uc.reg_write(mk.UC_M68K_REG_PC, addr + sz)
