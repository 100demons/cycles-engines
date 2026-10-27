# CRACK

CRACK is a hybrid-rate drum voice for the Elektron Model:Cycles (OS 1.13). At the home settings (all four knobs
at 64, DECAY 40) it is one modern, record-ready snare. The four knobs are macros: each one moves 6–11 internal
parameters at once, and each direction leaves the hero snare, so the same machine also plays kicks, toms, 808/909
snares, claps, rimshots, brushes, gated 80s snares, metal hits, metallic hats, zaps, crushed lo-fi drums and
sustained tones.

- Package id: `crack`; machine name **CRACK**; one machine.
- Knobs: **Wires** (`WIRE`), **Body** (`BODY`), **Snap** (`SNAP`), **Heat** (`HEAT`). Defaults 64/64/64/64.
- `PUNCH` = rimshot; `GATE` sustains; `DECAY` is the engine's own body/wire time (default 40).
- **BODY_TRANSPOSE = −5 semitones.** f0 = trig note + (PITCH − 64) + FINE − 5. The default trig (note 60, PITCH
  64) is **196 Hz (G3)**.

## Signal flow

The core (body, wires, ring, heat) runs at **24 kHz**, 16 samples per 32-frame block. The click and air layer runs
at the full **48 kHz** after a 4-point upsampler. That split is what buys the CPU for three body modes, a real
state-variable wire filter, a tuned comb and a full processing chain.

1. **Body** — three `uint32_t` phase accumulators into a parabolic sine. Mode 1 is the fundamental, mode 2
   phase-modulates mode 1, mode 3 adds the membrane/bar colour. Each mode has its own exponential amplitude
   envelope and its own pitch increment, ramped across the block. Two pitch envelopes (tension and spike) are in
   semitones and decay to zero. A mode above 10 kHz is muted.
2. **Wires** — 32-bit LCG noise into a TPT state-variable filter (Zavalishin/Simper, not the unstable Chamberlin
   form), mixed band-pass/high-pass. The wire envelope has a delay, an attack, an exponential decay and the gated
   80s shape above DECAY 100. The **rattle** term multiplies the wires by `1 − r + r·|mode 1|`, so they buzz at
   the head's pitch. Clap bursts run on the same noise and filter. The cutoff tracks the note at half rate
   (+0.5 octave per octave).
3. **Ring** — a comb on a 128-sample `int16_t` buffer, tuned to f0 × ring ratio, with damped and clamped
   feedback. It is skipped when its level is zero.
4. **Heat** — transient boost, envelope compressor (CRACK knows its own envelopes, so there is no detector or
   lookahead), drive, soft cubic / hard clip, wavefold above HEAT 104, tilt EQ, crush above HEAT 104, DC blocker,
   makeup.
5. **Click and air** — one 48 kHz noise source through a 1-pole high-pass, multiplied by the click envelope plus
   the air envelope (wire envelope × air amount). The click noise is band-limited by a 2-pole 10.6 kHz low-pass so
   a loud click cannot produce a sample-to-sample jump above full scale; the air layer carries the content above
   12 kHz. A final ±full-scale slew guard makes the no-wraparound check exact.
6. **Output** — Q31 (`int32_t`); the OS's voice loop halves every sample afterwards, as for the stock machines.

All arithmetic is integer C99: no float, no 64-bit, no libc, no division in per-sample code. Phases are
`uint32_t`; everything else is clamped. Host and device therefore run the same bits.

## Controls

### Wires (COLOR)

| | 0 | 32 | 64 | 96 | 127 |
|---|---|---|---|---|---|
| body level | 0.70 | 1.00 | 0.90 | 0.75 | 0.30 |
| wire level | 0 | 0.55 | 0.80 | 0.70 | 0.40 |
| wire cutoff | 1.2 kHz | 2.2 kHz | 4.5 kHz | 5.5 kHz | 2.5 kHz |
| wire Q | 0.7 | 0.8 | 0.9 | 1.1 | 1.6 |
| HP share | 0.20 | 0.40 | 0.65 | 0.70 | 0.40 |
| rattle r | 0 | 0.35 | 0.60 | 0.50 | 0.20 |
| wire decay × | 0.60 | 0.90 | 1.00 | 1.10 | 1.30 |
| clap amount | 0 | 0 | 0.25 | 0.70 | 1.00 |
| clap bursts / spacing | – | – | 1 / 9 ms | 3 / 10 ms | 4 / 11 ms |
| air amount | 0 | 0.25 | 0.50 | 0.70 | 0.80 |
| wire delay | 0 | 2.5 ms | 1.5 ms | 0.5 ms | 0 |

0 = head only; 32 = loose vintage wires; 64 = modern wires; 96 = snare + clap; 127 = clap only.

### Body (SHAPE)

| | 0 | 32 | 64 | 96 | 127 |
|---|---|---|---|---|---|
| mode ratios | 1, 2, 3 | 1, 1.83, 2.5 | 1, 1.594, 2.136 | 1, 1.594, 2.917 | 1, 2.756, 5.404 |
| mode levels | 0.70, 0.056, 0 | 1.00, 0.60, 0.15 | 1.00, 0.55, 0.35 | 0.80, 0.60, 0.60 | 0.70, 0.80, 0.70 |
| tension drop | +13.3 st / 45 ms | +7 / 25 | +4 / 18 | +2 / 10 | +1 / 5 |
| PM index | 0 | 0 | 0.15 | 0.40 | 1.20 |
| ring ratio | 2.0 | 2.0 | 2.296 | 2.917 | 3.6 |
| ring level | 0 | 0.05 | 0.18 | 0.35 | 0.50 |
| ring feedback | 0 | 0.40 | 0.60 | 0.75 | 0.88 |
| ring damping | 0.50 | 0.25 | 0.50 | 0.80 | 0.80 |
| body decay × | 1.12 | 1.20 | 1.00 | 0.80 | 1.40 |

0 = thump; 32 = analog; 64 = ideal membrane (1, 1.594 and 2.136 are its (1,1) and (2,1) modes, 2.296 its (0,2)
mode); 96 = piccolo; 127 = free bar / metal. Ratios interpolate in the log domain, so every set stays ascending.

### Snap (SWEEP)

| | 0 | 32 | 64 | 96 | 127 |
|---|---|---|---|---|---|
| attack (body + wires) | 6 ms | 1 ms | 0 | 0 | 0 |
| click level | 0 | 0.30 | 0.70 | 1.00 | 0.80 |
| click decay | 1 ms | 1.5 ms | 2.5 ms | 4 ms | 6 ms |
| click high-pass | 2 kHz | 3 kHz | 5 kHz | 3 kHz | 1 kHz |
| spike | 0 | +5 st / 3 ms | +12 st / 4 ms | +24 st / 8 ms | +48 st / 25 ms |
| transient boost | −3 dB / 6 ms | +2 dB / 4 ms | +4 dB / 5 ms | +7 dB / 6 ms | +5 dB / 8 ms |

0 = brush; 32 = stick; 64 = modern snap; 96 = hard; 127 = zap.

### Heat (CONTOUR)

| | 0 | 32 | 64 | 96 | 127 |
|---|---|---|---|---|---|
| drive | 1.0 | 1.6 | 2.4 | 4.0 | 6.0 |
| clip hardness | 0 | 0 | 0.20 | 0.80 | 1.00 |
| compressor k | 0 | 0.5 | 1.2 | 2.0 | 2.0 |
| tilt t | 0 | −0.5 | +0.6 | +0.9 | 0 |
| body pre-gain | 1.0 | 1.1 | 1.0 | 1.4 | 1.2 |
| wavefold | 0 | 0 | 0 | 0 | 0.60 (from 104) |
| crush hold / bits | 1 / 16 | 1 / 16 | 1 / 16 | 1 / 16 | 4 / 6 (from 104) |
| makeup | 1.585 | 1.450 | 1.000 | 0.680 | 1.000 |

0 = raw; 32 = warm; 64 = smack; 96 = clipped; 127 = destroyed. The makeup column keeps the 150 ms RMS within
±3 dB of the hero.

### PITCH, PUNCH, GATE, velocity, DECAY

- **PITCH** sets f0 (with the −5 semitone transpose); the wire cutoff tracks at +0.5 octave per octave, the ring
  follows f0 exactly.
- **PUNCH** (rim): ring level +0.25, ring feedback +0.1 (max 0.92), ring ratio ×1.25, click level +6 dB, click
  high-pass ×1.5, click decay ×5 (12.5 ms at home), mode 3 level +0.3, spike +5 st, wire decay ×0.75, wire level
  ×0.85, core +2 dB.
- **GATE**: while the trig is held, mode 1 and the wire envelope hold 12 dB below their peak until the note ends,
  then decay normally. Above DECAY 100 the wire/air/clap use the 80s gated shape (attack, a hold that droops about
  3 dB, then a 12 ms fade; hold 60 ms at 101 to 450 ms at 127).
- **Velocity** (0 is read as 100): level `0.1 + 0.9·v²`, click `×v²`, wire cutoff `×(0.6 + 0.4·v)`, spike
  `×(0.5 + 0.5·v)`, rattle `×(0.5 + 0.5·v)`. The level is applied after the compressor, so the compressor cannot
  flatten the soft/loud difference.
- **DECAY** (the stock knob, read as a macro):

| DECAY | 0 | 20 | 40 | 64 | 90 | 100 | 101–127 |
|---|---|---|---|---|---|---|---|
| body T60 | 25 ms | 80 ms | 210 ms | 350 ms | 900 ms | 1.4 s | as 100 |
| wire T60 | 20 ms | 110 ms | 310 ms | 500 ms | 1.2 s | 2.0 s | gated shape |

The home T60 values are 210/310 ms (inside the ±30 % tuning window) so the hero's −40 dB point lands at about
180 ms, inside the 170–380 ms check.

### Round robin

Every trig updates the voice's own LCG: ±0.3 dB level, ±3 % wire cutoff, ±0.25 semitone tension depth and ±10 %
click decay. It is deterministic, so host and emulator still match.

## Recipes

| # | sound | PITCH | COLOR | SHAPE | SWEEP | CONTOUR | DECAY | other |
|---|---|---|---|---|---|---|---|---|
| 1 | Hero snare | 64 | 64 | 64 | 64 | 64 | 40 | |
| 2 | Hero rimshot | 64 | 64 | 64 | 64 | 64 | 40 | PUNCH on |
| 3 | Ghost note | 64 | 64 | 64 | 64 | 64 | 40 | velocity 35 |
| 4 | Trap clap-snare | 70 | 100 | 60 | 70 | 90 | 45 | |
| 5 | 808-style snare | 64 | 40 | 32 | 30 | 20 | 50 | |
| 6 | 909-style snare | 64 | 50 | 36 | 64 | 40 | 44 | |
| 7 | Brush | 64 | 56 | 64 | 0 | 10 | 30 | velocity 60 |
| 8 | Gated 80s snare | 60 | 70 | 48 | 64 | 70 | 115 | |
| 9 | Kick | 40 | 0 | 0 | 64 | 80 | 60 | |
| 10 | Tom | 52 | 8 | 40 | 40 | 40 | 70 | |
| 11 | Clap | 64 | 127 | 64 | 50 | 60 | 50 | |
| 12 | Metallic hat | 100 | 80 | 120 | 80 | 50 | 8 | |
| 13 | Metal hit | 64 | 30 | 127 | 64 | 96 | 60 | |
| 14 | Zap | 64 | 0 | 64 | 127 | 64 | 30 | |
| 15 | Crushed lo-fi snare | 64 | 64 | 64 | 64 | 127 | 40 | |
| 16 | Sustained bass | 40 | 0 | 10 | 20 | 40 | 64 | GATE on, a 4-step note |

The demos are in `build/crack-demos/` after `python3 tests/crack/render_demos.py` (not committed; the folder is
gitignored).

## Tests

- `python3 tests/crack/test_crack_sound.py` — §9.1–9.3 on host renders. 159 checks.
- `python3 tests/crack/test_crack_emu.py --cycles firmware/model-cycles_OS1.13.syx` — §9.4 in the real OS
  voice loop: sample exact, level, CPU.
- `python3 tests/crack/render_demos.py` — the 16 recipes and the macro sweeps into `build/crack-demos/`.

The host build uses `cc -std=c99 -O2 -Wall -Wextra -Werror`. On the owner's current macOS the default SDK link is
broken (`tapi error`); the tests retry with the newest working SDK and print which one they used.

## Size and CPU (measured)

- DSP voice: **640 bytes** exactly, comb buffer included; the bridge input adds 20 bytes (660 per track).
- Tables: **566 bytes** (`CR_SEMI`, `CR_POW2`, `CR_TAN_G`, `CR_HP_A`, `CR_MACRO`, the two DECAY tables).
- `crack.o`: 13,784 bytes; `text` 9,048, `bss` 3,960. Packed image contribution: **16,240 bytes**.
- CPU (emulator, six voices, same run): TONE 5,559 instructions/voice steady. CRACK engine cost **4,733/voice
  steady = 0.851 × TONE**, **5,381/voice worst (trig with all four knobs changing) = 0.968 × TONE**. The hard
  ceiling (≤1.00 × TONE) is met; the 0.90 margin target is not met on the worst block. The raw six-voice block is
  larger (78,295 worst vs 33,356 TONE) because the OS's six-voice loop takes a more expensive idle path for an
  engine that bypasses the stock amp chain; the engine-code comparison is the 0.968 above. Silent engine work:
  192 instructions.

## Credits

Engine, DSP, bridge and tests: 100demons (own work), 2026, MIT. The repository's carried-over code (composer,
registrar, SysEx/aPLib/HMAC) keeps its own credits in the root LICENSE and source headers. The TPT SVF is the
standard Zavalishin/Simper topology; the parabolic sine and the 8.8 knob/phase conventions follow this repository's
`refmachine.c` and `punch.c`.
