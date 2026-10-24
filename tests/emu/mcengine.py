"""Émulation (Unicorn, m68k « ANY » = ColdFire + EMAC corrigée) du moteur de synthèse du
Model:Cycles OS 1.13 : la vraie boucle des 6 voix (0x400a7d4a), bloc par bloc (32 trames, 48 kHz).

Aucune image n'est fournie : on passe le MAIN OS (section 3) de TON .syx, éventuellement patché.
Voir notes/14 §3-4 pour le contrat (voix, paramètres, tables de machines).
"""
import hashlib
import os
import struct
import tempfile

import numpy as np
from unicorn import Uc, UC_ARCH_M68K, UC_MODE_BIG_ENDIAN, UC_HOOK_MEM_UNMAPPED, UC_HOOK_CODE
from unicorn import m68k_const as mk

import emac

BASE = 0x40000400
VOICE_LOOP = 0x400a7d4a          # boucle des 6 voix : (out, params, trig_mask, release_mask)
VOICE_RESET = 0x400a7ab8         # remise à zéro d'une voix (au boot et au changement de machine)
TRACK_BASE = 0x80001858          # 6 blocs mono de 32 x int32 (sortie des voix)
PMOD = 0x80001830                # 6 x int32 : note du trig, demi-tons << 16
PARAMS = 0x40700000              # zone libre (BSS) pour les paramètres des 6 pistes
STOP = 0x40780000
STACK = 0x90010000
VOICE0, VSTRIDE = 0x42308828, 0x31c
CODE_RANGE = (BASE, 0x40100000)  # code de l'OS où chercher les instructions EMAC
MACH = {"KICK": 0, "SNARE": 1, "METAL": 2, "PERC": 3, "TONE": 4, "CHORD": 5}
# slot du descripteur -> mot 8.8 à p + 2*slot (notes/14 §1)
SLOT = {"machine": 0x09, "pitch": 0x0a, "color": 0x0b, "shape": 0x0c, "sweep": 0x0d,
        "contour": 0x0e, "punch": 0x0f, "gate": 0x10, "finetune": 0x11, "decay": 0x12}
# défauts d'origine par machine (color, shape, sweep, contour, decay), table 0x4010dd08
DEFAULTS = {0: (10, 16, 16, 24, 28), 1: (0, 127, 8, 0, 40), 2: (46, 48, 0, 20, 20),
            3: (38, 100, 64, 26, 26), 4: (40, 38, 52, 42, 42), 5: (3, 43, 24, 64, 64)}

IMAGE_LEN = 0x1a9d40             # MAIN OS 1.13 d'origine ; au-delà : charge utile d'un tweak « append »
PAYLOAD_DST = 0x43000000         # où le crochet de démarrage la recopie (notes/17)
PAYLOAD_CODE = ((0x43000000, 0x43007298), (0x43031000, 0x43032000))   # zones de code de la charge utile
# Tweaks de gen_syntakt_engines.py (notes/28, notes/29) : le début de la charge utile est l'image de deux zones de
# SRAM (transit, exécution, taille), recopiées au démarrage (stub.S) : code du Syntakt (il s'exécute dans la 1re),
# tables et états de voix.
SRAM_BANKS = ((0x43000000, 0x80001c5c, 0x5858), (0x43005858, 0x8000cac0, 0x2020))
SRAM_CODE = SRAM_BANKS[0]
# (SD VINTAGE seul s'arrête à 0x4300603c ; SD + CP VINTAGE à 0x430065a4, notes/19 ; jusqu'à SY TOY à
#  0x430068c8, notes/20 ; jusqu'à SY BITS à 0x43006da8, notes/21 ; jusqu'à SY SWARM à 0x43007298, notes/22.
#  Décoder un peu de données comme du code ne fait que poser des crochets EMAC à des adresses jamais exécutées.)

_EMAC_CACHE = {}


def _emac_instrs(main_os, ranges):
    key = (hashlib.sha256(main_os).hexdigest(), tuple(ranges))
    if key not in _EMAC_CACHE:
        fd, path = tempfile.mkstemp(suffix=".bin")
        try:
            os.write(fd, main_os)
            os.close(fd)
            instrs = {}
            for lo, hi in ranges:
                instrs.update(emac.disasm(path, BASE, lo, hi))
        finally:
            os.unlink(path)
        _EMAC_CACHE[key] = instrs
    return _EMAC_CACHE[key]


def _emac_payload(payload, dst=PAYLOAD_DST, extra=()):
    """Instructions EMAC de la charge utile copiée à dst (mêmes décalages que PAYLOAD_CODE et SRAM_CODE depuis
    PAYLOAD_DST : gen_syntakt_engines.LAYOUT). extra : [(début, fin)] d'autre code de la charge utile, aux adresses
    en mémoire (ACID au-dessus de MACRO avec les moteurs du Syntakt, notes/54 §11)."""
    key = ("payload", dst, hashlib.sha256(payload).hexdigest(), tuple(extra))
    if key not in _EMAC_CACHE:
        fd, path = tempfile.mkstemp(suffix=".bin")
        try:
            os.write(fd, payload)
            os.close(fd)
            instrs = {}
            for lo, hi in PAYLOAD_CODE:
                lo, hi = lo - PAYLOAD_DST + dst, hi - PAYLOAD_DST + dst
                if lo - dst < len(payload):
                    instrs.update(emac.disasm(path, dst, lo, min(hi, dst + len(payload))))
            for lo, hi in extra:
                instrs.update(emac.disasm(path, dst, lo, min(hi, dst + len(payload))))
            stage, run, n = SRAM_CODE                # mêmes octets, aux adresses d'exécution en SRAM
            if stage - PAYLOAD_DST < len(payload):
                base = run - (stage - PAYLOAD_DST)
                instrs.update(emac.disasm(path, base, run, run + min(n, len(payload) - (stage - PAYLOAD_DST))))
        finally:
            os.unlink(path)
        _EMAC_CACHE[key] = instrs
    return _EMAC_CACHE[key]


class Engine:
    """extra_code : [(va, taille)] de code ajouté hors de CODE_RANGE (caves), pour l'EMAC.
    payload : (adresse, octets) de la charge utile telle qu'en mémoire, si elle n'est pas simplement la fin de
    l'image (version combinée avec Model-TG : rangée en morceaux, recopiée à 0x46700000, notes/31).
    payload_code : [(début, fin)] de code de la charge utile hors de PAYLOAD_CODE, pour l'EMAC."""

    def __init__(self, main_os, extra_code=(), payload=None, payload_code=()):
        self.img = bytes(main_os)
        uc = self.uc = Uc(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN)
        uc.ctl_set_cpu_model(mk.UC_CPU_M68K_ANY)
        uc.mem_map(0x40000000, 0x00800000)     # image + BSS/globales
        uc.mem_map(0x42000000, 0x00400000)     # SDRAM (états des voix)
        uc.mem_map(0x80000000, 0x00020000)     # SRAM interne (buffers, tables)
        uc.mem_map(0x90000000, 0x00020000)     # pile
        uc.mem_map(0xfc070000, 0x1000)         # minuteur DMA 0 (lu par le régulateur de charge, notes/25) : 0
        uc.mem_write(BASE, self.img)
        uc.mem_write(STOP, b"\x4e\x71\x4e\x71")
        self.unmapped = []
        uc.hook_add(UC_HOOK_MEM_UNMAPPED, self._unmapped)
        self.emac = emac.EMAC(uc)
        ranges = [CODE_RANGE] + [(va, va + n) for va, n in extra_code]
        self.n_emac = self.emac.install(_emac_instrs(self.img, ranges))
        dst, payload = payload or (PAYLOAD_DST, self.img[IMAGE_LEN:])
        if payload:                              # ce que fait le crochet de démarrage 0x400004b2 (notes/17)
            uc.mem_map(dst, (len(payload) + 0xfffff) & ~0xfffff)
            uc.mem_write(dst, payload)
            self.n_emac += self.emac.install(_emac_payload(payload, dst, payload_code))
        # boot : copie ROM -> SRAM (0x4000045c) puis init des voix (0x4005974c)
        uc.mem_write(0x80000000, self.img[0x4019b590 - BASE:0x401a2a50 - BASE])
        uc.mem_write(0x80008000, self.img[0x401a2a50 - BASE:0x401aa140 - BASE])
        chord = struct.unpack_from(">I", self.img, 0x40118594 - BASE)[0]
        if payload and not 0x80000000 <= chord < 0x80010000:
            # tables d'ondes de CHORD sorties de la SRAM (notes/28) : le crochet de démarrage (stub.S) y recopie
            # le début de la charge utile, code du Syntakt compris
            for stage, run, n in SRAM_BANKS:
                uc.mem_write(run, payload[stage - PAYLOAD_DST:stage - PAYLOAD_DST + n])
        for t in range(6):
            v = VOICE0 + t * VSTRIDE
            uc.mem_write(v + 0x318, struct.pack(">I", 0x8000bd98 + t * 0x30))
            self.call(VOICE_RESET, v)
        self.trk = [dict(machine=0, pitch=64, note=60, color=0, shape=0, sweep=0, contour=0,
                         punch=0, gate=0, finetune=64, decay=40) for _ in range(6)]
        self.instructions = None

    def _unmapped(self, uc, access, addr, size, value, ud):
        self.unmapped.append((uc.reg_read(mk.UC_M68K_REG_PC), addr))
        return False

    def call(self, fn, *args):
        sp = STACK - 0x200
        self.uc.mem_write(sp, struct.pack(">I" + "I" * len(args), STOP, *[a & 0xffffffff for a in args]))
        self.uc.reg_write(mk.UC_M68K_REG_A7, sp)
        self.uc.emu_start(fn, STOP, count=5_000_000)
        return self.uc.reg_read(mk.UC_M68K_REG_D0)

    def set(self, t, **kw):
        for k, v in kw.items():
            if k == "machine" and isinstance(v, str):
                v = MACH[v]
            self.trk[t][k] = v

    def machine_defaults(self, t, m):
        m = MACH.get(m, m)
        c, s, w, ct, d = DEFAULTS[m]
        self.set(t, machine=m, color=c, shape=s, sweep=w, contour=ct, decay=d)

    def solo(self, track):
        """Coupe le rendu des autres voix (index de machine hors 0..5 -> pas de rendu)."""
        for t in range(6):
            if t != track:
                self.uc.mem_write(VOICE0 + t * VSTRIDE, struct.pack(">I", 6))

    def count_instructions(self):
        """Compte les instructions exécutées (lent : à n'activer que pour une mesure)."""
        self.instructions = 0

        def hk(uc, a, s, u):
            self.instructions += 1
        self.uc.hook_add(UC_HOOK_CODE, hk)

    def _write_params(self):
        for t in range(6):
            self.uc.mem_write(PMOD + 4 * t, struct.pack(">i", int(round(self.trk[t]["note"] * 65536))))
            p = PARAMS + 0xe + t * 0x42
            for k, slot in SLOT.items():
                self.uc.mem_write(p + slot * 2, struct.pack(">h", int(round(self.trk[t][k] * 256))))

    def block(self, trig_mask=0, release_mask=0):
        self._write_params()
        sp = STACK - 0x100
        self.uc.mem_write(sp, struct.pack(">IIIII", STOP, TRACK_BASE, PARAMS, trig_mask, release_mask))
        self.uc.reg_write(mk.UC_M68K_REG_A7, sp)
        self.uc.emu_start(VOICE_LOOP, STOP, count=5_000_000)
        raw = self.uc.mem_read(TRACK_BASE, 6 * 0x80)
        return np.frombuffer(bytes(raw), dtype=">i4").reshape(6, 32).astype(np.int64)

    def render(self, blocks, trig_at=(0,), track=0, on_block=None):
        out = []
        for b in range(blocks):
            if on_block:
                on_block(self, b)
            out.append(self.block((1 << track) if b in trig_at else 0)[track])
        return np.concatenate(out)


def wav(path, x, sr=48000):
    import wave
    y = np.asarray(x, dtype=np.float64)
    peak = np.max(np.abs(y)) or 1
    y = (y / peak * 0.9 * 32767).astype("<i2")
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(sr)
        w.writeframes(y.tobytes())
