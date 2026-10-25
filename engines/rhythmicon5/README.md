# Rhythmicon V5 Cloud

**FIFTH's octave Rhythmicon, swept by one SHAPE macro, into HALO New's granular cloud.**

This is the fifth Rhythmicon (the FIFTH engine) with the fifths group removed and the granular cloud from HALO New
added after it. The cloud is a verbatim copy in `r5_cloud.h` / `r5_cloud_tables.h`: same controls, curves, defaults,
internal settings, buffer and Freeze behaviour. The proof compiles it side by side with HALO New's own
`halo_dsp.h` (an unmodified reference copy is in `tests/halo/`) and compares every output sample.

Status: experimental · Version 1.0 · For: Model:Cycles OS 1.13 · MIT

## Controls

| Knob | Job |
|---|---|
| **COLOR** | Octaves: 1, 2, 4, 8, 16 × the note (0 → 1 partial, 64 → 3, 127 → 5). Default **63**. |
| **SHAPE** | The macro: turning up adds sweep and shortens the ring; turning down does the opposite. Default **27**. |
| **SWEEP** | Cloud TIME, exactly as HALO New. Default **70**. |
| **CONTOUR** | Cloud amount, exactly as HALO New. Default **40**. |
| **PUNCH** | Cloud Freeze, exactly as HALO New. |
| **DECAY** | Stock amp envelope. Default **80** (FIFTH's startup value). |
| **PITCH / FINE / GATE** | As on TONE / FIFTH. |

The Rhythmicon's old SWEEP (ping rate) is now SHAPE. Its old CONTOUR (ring length) is now SHAPE's decay; FIFTH's
CONTOUR had no other job, so nothing else had to be frozen. The Rhythmicon's old PUNCH function (FREE) conflicts with
the cloud's Freeze, so the cloud keeps PUNCH and the Rhythmicon's FREE is frozen at its startup default, **off**:
every trig resets the ping phases, as FIFTH did with PUNCH 0.

## SHAPE

`SHAPE` is slewed one-pole in Q16, 1/16 per 32-frame block (about 10.7 ms, the same style as the VX engines), so
knob turns and p-locks never step. It drives two things:

- **Sweep**: `old_sweep = SHAPE × 76 / 127`, kept in Q16 and fed into FIFTH's existing `T_RATE` curve. SHAPE 0 is
  old SWEEP 0, SHAPE 27 is old SWEEP 16, SHAPE 127 is old SWEEP 76 exactly; all 128 values are distinct and rising.
- **Ring**: `T60 = 325 ms × (20 / 325)^(SHAPE / 127)`, with the pole radius computed at the resonator's **real 24 kHz
  update rate** (16 internal steps per 32-frame block). FIFTH's old `T_RING` was generated as if the loop ran at
  96 kHz, so its real rings were about four times its documentation; the new table follows the real rate.

Measured T60 at the five check points: 324.3, 179.3, 80.0, 39.2 and 19.9 ms (table: 325, 179.7, 79.7, 39.5, 20 ms).

There is no fifth oscillator group any more: the only strike loop is the octave stack (`f × 2^i`). At SHAPE 0 the
output is sample-for-sample FIFTH with SHAPE 64 (its no-fifths setting), so the deletion is exact.

## Signal flow and level

`Rhythmicon (Q30) → OS AMP_ENV/VCA (no PUNCH) → cloud (Q31) → out (Q30)`.

HALO's cloud expects Q31; FIFTH's dry path is Q30. The glue therefore shifts the dry up by one bit (a +6.0 dB format
conversion, saturating at full scale) before the cloud and shifts back by one bit after it. At CONTOUR 0 this round
trip leaves the dry signal exactly as FIFTH produced it. No further gain trim was added: FIFTH's peak (~0.75 Q30) is
only about 2.4 dB below the loudest HALO resonator (~0.99), so it is not "far" from what the cloud was fed. The
worst case (SHAPE 0, TIME 127, CONTOUR 127, Freeze after the buffer filled) peaks at 0.27 Q30, with no clipping or
fixed-point wraparound.

## Build and tests

```sh
M68K_CROSS=$HOME/m68k-elf-16.2/bin/m68k-elf- python3 core/build_engine.py rhythmicon5
PATH=$HOME/m68k-elf-16.2/bin:$PATH python3 build/rhythmicon5/build_and_prove.py
python3 build/rhythmicon5/make_demos.py
```

The proof checks the official OS hash, the cloud copy against HALO New, the SHAPE sweep mapping, T60 from host
renders, the no-fifths deletion, defaults, the emulator (boot, dispatch, sound, extremes, stock machines), host vs
emulator bit-exactness, CPU against TONE, and the usual `tests/emu/test_engines.py` suite. `build/rhythmicon5/prove.log`
is the last run: **ALL OK**.

CPU (instructions per 32-frame block, one voice, TONE 5,291 in the same run): default 3,899 (0.74×), heavy 4,665
(0.88×), heavy + Freeze 4,351 (0.82×), dry 3,147 (0.59×). The one-time first strike after choosing the machine is
7,374 (1.39×), the same one-block initialization pattern as HALO (1.14×) and FIFTH; every sustained block is under
TONE.

## Files

| File | What |
|---|---|
| `rhythmicon5.c` | voice-loop glue |
| `rhythmicon5_dsp.h` | Rhythmicon dry half, SHAPE macro, clouds glue |
| `r5_cloud.h` | HALO New's cloud, copied verbatim |
| `r5_common.h` | EMAC arithmetic |
| `r5_tables.h` | Feral's sine/note tables, FIFTH's T_RATE, the new T60 table |
| `r5_cloud_tables.h` | HALO New's cloud tables, copied verbatim |
| `manifest.json` | package recipe |
| `rhythmicon5.o` | committed m68k relocatable object |

The standalone build is at slot 7 (OS machine 6), like FIFTH, so it is a single-engine OS of its own. The package is
not added to `engines/slots.json` or the web flasher; it can be composed by id.

## Credits

Engine, glue and tests: **100demons and Claude**, MIT. The Rhythmicon is Feral's RYT reduced to octaves (FIFTH).
The cloud is HALO New / Mutable Instruments Clouds (Émilie Gillet, MIT). The Rhythmicon idea is Léon Theremin and
Henry Cowell's (1931).
