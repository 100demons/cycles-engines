#!/usr/bin/env python3
"""Load Rhythmicon V5 Cloud's host core (ctypes) and render it.

The shared library is built from tests/host/r5_host.c against the same headers the Cycles build uses.
"""
import ctypes
import pathlib
import subprocess

import numpy as np

ROOT = pathlib.Path(__file__).resolve().parents[2]
HERE = ROOT / "build" / "rhythmicon5"
SRC = ROOT / "engines" / "rhythmicon5"
LIB = HERE / "libr5host.dylib"
SDK = "/Library/Developer/CommandLineTools/SDKs/MacOSX15.4.sdk"


def build():
    HERE.mkdir(parents=True, exist_ok=True)
    subprocess.run(["cc", "-O2", "-std=c99", "-Wall", "-Wextra", "-Werror",
                    "-isysroot", SDK, "-I", str(SRC), "-shared", "-fPIC",
                    str(ROOT / "tests" / "host" / "r5_host.c"), "-o", str(LIB)], check=True)
    return LIB


def load():
    if not LIB.is_file() or LIB.stat().st_mtime < (ROOT / "tests" / "host" / "r5_host.c").stat().st_mtime:
        build()
    lib = ctypes.CDLL(str(LIB))
    lib.r5h_reset.argtypes = [ctypes.c_int]
    lib.r5h_reset_dry.argtypes = []
    lib.r5h_set.argtypes = [ctypes.c_int] * 9
    lib.r5h_dry.argtypes = [ctypes.POINTER(ctypes.c_int)]
    lib.r5h_wet.argtypes = [ctypes.POINTER(ctypes.c_int), ctypes.c_int]
    lib.r5h_shape_q16.restype = ctypes.c_int
    lib.r5h_rate.argtypes = [ctypes.c_int]
    lib.r5h_rate.restype = ctypes.c_uint
    lib.r5h_ring.argtypes = [ctypes.c_int]
    lib.r5h_ring.restype = ctypes.c_int
    return lib


class Host:
    def __init__(self, lib=None):
        self.lib = lib or load()
        self.out = (ctypes.c_int * 32)()

    def reset(self, t=0):
        self.lib.r5h_reset(t)

    def reset_dry(self):
        self.lib.r5h_reset_dry()

    def set(self, note, pitch, fine, color, shape, tim, cld, freeze, trig):
        self.lib.r5h_set(int(note), int(pitch), int(fine), int(color), int(shape), int(tim), int(cld),
                         int(freeze), int(trig))

    def dry(self):
        self.lib.r5h_dry(self.out)
        return np.frombuffer(self.out, dtype=np.int32).astype(np.int64).copy()

    def wet(self, x, dry):
        for i in range(32):
            self.out[i] = int(x[i])
        self.lib.r5h_wet(self.out, int(dry))
        return np.frombuffer(self.out, dtype=np.int32).astype(np.int64).copy()


def knob8(v):
    return int(v) * 256


def render(knobs, note=60, blocks=200, trig_at=(0,), pitch=64, fine=64, freeze=0, wet=True,
           shape_curve=None, tim=None, cld=None, lib=None):
    """Render blocks of 32. knobs = (color, shape, tim, cld); shape_curve(b) can override SHAPE per block."""
    h = Host(lib)
    h.reset(0)
    color, shape, tim0, cld0 = knobs
    tim = tim0 if tim is None else tim
    cld = cld0 if cld is None else cld
    out = []
    for b in range(blocks):
        sh = shape_curve(b) if shape_curve is not None else shape
        h.set(note << 16, pitch << 8, fine << 8, knob8(color), knob8(sh), tim, cld, freeze, 1 if b in trig_at else 0)
        x = h.dry()
        if wet:
            x = h.wet(x, 1)
        out.append(x)
    return np.concatenate(out) if out else np.zeros(0, np.int64)


def t60_measured(x, f0=1046.5, fs=48000, start=0.002, end=None, expected=0.1):
    """Fit the exponential envelope of one ping. Band-pass around f0 removes the DC-blocker tail."""
    x = np.asarray(x, dtype=np.float64) / 2 ** 30
    n = len(x)
    X = np.fft.fft(x)
    f = np.fft.fftfreq(n, 1 / fs)
    X[(np.abs(f) < f0 * 0.6) | (np.abs(f) > f0 * 1.4)] = 0
    env = np.abs(np.fft.ifft(X))
    if end is None:
        end = min(0.7 * expected, 0.45, 0.9 * n / fs)
    i0 = int(start * fs)
    i1 = int(end * fs)
    t = np.arange(i0, i1) / fs
    e = np.maximum(env[i0:i1], 1e-12)
    a = np.polyfit(t, np.log(e), 1)[0]
    return -np.log(1000.0) / a
