# SHEEN — a modern hi-hat and metal percussion engine for the Model:Cycles

SHEEN is one added machine for the Elektron Model:Cycles, OS 1.13. It is built hat-first: at the home position
(PITCH 64, COLOR 64, SHAPE 64, SWEEP 64, CONTOUR 64, DECAY 40, velocity 100, note 60) it is a crisp, dense, modern
closed hi-hat; with **PUNCH on** it is the matching open hat, and the next closed step chokes it like a real pedal.
The four knobs are macros: each one leaves the home pair in two directions, so the same machine also plays 808 and
909 hats, pedal chicks, shakers, tambourines, rides, crashes, cowbells, reversed cymbals, gongs, laser chirps and
crushed lo-fi hats.

SHEEN is a package: `engines/sheen/` (sources, committed `sheen.o`, manifest, card) plus the host and emulator tests
in `tests/sheen/`. It is **not hardware-tested**.

## The sound

- **Home, PUNCH off:** a sub-millisecond tick, a bright metallic "tss" around 7–10 kHz, a short 3 kHz chick, real
  content above 12 kHz, gone in about 60–80 ms, and every hit a little different.
- **Home, PUNCH on:** the same family with the open DECAY row, less chick, the sizzle blooming over the first ~15 ms,
  a slow wash and a gentle darkening, gone in about 0.6–0.8 s.
- **P-lock trick:** put PUNCH on the open steps and leave the following closed step at PUNCH off. The closed trig
  restarts every envelope with the closed times, so it chokes the open tail. GATE on makes the note's length do the
  same: the hat closes when the note ends.

## How it works

Two rates in one voice, 32 frames per block at 48 kHz:

- **48 kHz:** six TR-808 metal square oscillators (SUM or XOR RING), a 32-bit LCG noise, the source mix, a one-pole
  highpass and a per-sample tick envelope, a second highpass and the air envelope. The tick and air are the layer
  that gives a hat its top end.
- **24 kHz:** a one-pole pre-highpass, a TPT state-variable filter (BP and HP), the split body/sizzle envelopes, a
  two-pole chik resonator, a 128-sample damped shimmer comb, and the Heat chain (glue, drive, soft/hard clip, tilt,
  crush, DC blocker). The core is upsampled back to 48 kHz with a 4-point midpoint interpolator, one core-sample
  latency; the tick and air are delayed by the same two 48 kHz samples.

**The hand-off has no decimation filter, on purpose.** The metal and noise sources are already broadband and
inharmonic. Taking every other sample folds 12–24 kHz down into the 0–12 kHz core as more of the same metal and
noise; an anti-alias filter here would cost a filter per voice for no audible gain, and would smear the very tick
the engine exists for.

## Controls

PITCH, FINE and the note follow the stock machines. DECAY is read by SHEEN itself (the stock amp chain is not
called): the body, sizzle and chik have their own times, and above DECAY 100 the envelope rises as a reverse cymbal,
then cuts in 4 ms and fires the tick.

**COLOR = Metal** (0 pure noise, 64 hero blend, 127 808 cowbell pair):

| target | 0 | 32 | 64 | 96 | 127 |
|---|---|---|---|---|---|
| metal share | 0 | 0.30 | 0.75 | 0.80 | 1.00 |
| RING share | – | 0.60 | 0.50 | 0.10 | 0 |
| oscillators 1–4 | 1 | 1 | 1 | 1 | 0 |
| voice cutoff × | 1.10 | 1.05 | 1.00 | 0.90 | 0.35 |
| voice Q | 0.70 | 0.80 | 1.00 | 1.30 | 3.50 |
| body share | 0.35 | 0.40 | 0.50 | 0.60 | 0.90 |
| air level | 1.00 | 0.95 | 0.85 | 0.60 | 0.15 |
| chik level | 0.45 | 0.50 | 0.55 | 0.45 | 0.15 |
| shimmer send | 0 | 0 | 0.08 | 0.15 | 0.30 |

**SHAPE = Size** (0 tight, 64 hero, 127 gong):

| target | 0 | 32 | 64 | 96 | 127 |
|---|---|---|---|---|---|
| metal base × | 1.80 | 1.35 | 1.00 | 0.75 | 0.50 |
| voice cutoff × | 1.30 | 1.12 | 1.00 | 0.85 | 0.60 |
| chik centre | 4.6 kHz | 3.9 kHz | 3.2 kHz | 2.6 kHz | 1.7 kHz |
| body/sizzle decay × | 0.55 | 0.80 | 1.00 | 1.80 | 3.50 |
| bloom | 0 | 0 | 0.10 | 0.45 | 0.75 |
| wash detune | 0 | 3 ct | 8 ct | 16 ct | 28 ct |
| darkening | 0 | 0.10 | 0.38 | 0.50 | 0.80 oct |
| shimmer level / feedback | 0 / 0 | 0.04 / 0.30 | 0.08 / 0.50 | 0.25 / 0.75 | 0.45 / 0.90 |
| shimmer ratio | 6.0 | 6.0 | 4.7 | 3.9 | 2.6 |
| shimmer damping | bright | bright | medium | medium | dark |

**SWEEP = Tick** (0 shaker grains, 64 crisp, 127 laser):

| target | 0 | 32 | 64 | 96 | 127 |
|---|---|---|---|---|---|
| attack | 8 ms | 2 ms | 0.3 ms | 0 | 0 |
| bursts | 3 / 7 ms | 1 | 1 | 1 | 1 |
| tick level | 0 | 0.40 | 0.75 | 1.00 | 0.80 |
| tick decay | 0.4 ms | 0.7 ms | 1.2 ms | 2.0 ms | 4.0 ms |
| tick highpass | 6 kHz | 5 kHz | 4 kHz | 2.5 kHz | 1 kHz |
| chik decay × | 1.6 | 1.2 | 1.0 | 0.8 | 0.6 |
| metal chirp | 0 | 0 | +2 st / 2 ms | +12 st / 5 ms | +36 st / 20 ms |
| cutoff spike | 0 | +0.2 oct / 4 ms | +0.4 oct / 6 ms | +0.9 oct / 8 ms | +2 oct / 30 ms |

**CONTOUR = Heat** (0 raw, 64 polished, 127 crushed):

| target | 0 | 32 | 64 | 96 | 127 |
|---|---|---|---|---|---|
| drive | 1.0 | 1.3 | 1.8 | 6.0 | 5.0 |
| clip hardness | 0 | 0 | 0.15 | 1.0 | 1.0 |
| glue k | 0 | 0.3 | 0.8 | 0 | 1.5 |
| tilt | 0 | +0.3 | +0.6 | +0.4 | −0.3 |
| air × | 0.85 | 0.95 | 1.0 | 0.9 | 0.2 |
| crush hold / bits | 1 / 16 | 1 / 16 | 1 / 16 | 1 / 16 | 3 / 7 |
| makeup | 1.35 | 1.0 | 1.0 | 0.31 | 1.0 |

**DECAY** (the stock knob, read by SHEEN):

| DECAY | 0 | 20 | 40 (home) | 64 | 90 | 100 | 101–127 |
|---|---|---|---|---|---|---|---|
| closed body T60 | 6 ms | 18 ms | 35 ms | 80 ms | 220 ms | 400 ms | reverse |
| closed sizzle T60 | 10 ms | 30 ms | 60 ms | 140 ms | 380 ms | 650 ms | reverse |
| open body T60 | 90 ms | 200 ms | 350 ms | 700 ms | 1.6 s | 2.5 s | reverse |
| open sizzle T60 | 150 ms | 380 ms | 700 ms | 1.3 s | 3.0 s | 4.5 s | reverse |
| chik T60 | 4 ms | 8 ms | 14 ms | 20 ms | 30 ms | 40 ms | 14 ms |

Above DECAY 100 the swell starts at about −24 dB and rises to the peak (80 ms at 101 to 900 ms at 127, ×1.6 with
PUNCH), then cuts in 4 ms and fires the tick.

**PUNCH = Open** adds the open decay row, less chick (×0.35), more bloom, +6 cents wash, a slightly lower/wider band
(cutoff ×0.9, Q ×0.85), less tick (×0.8), more darkening and a little shimmer. It is latched at the trig.

**GATE = Choke** uses the note length: at the note end the body, sizzle and wash fall with a 12 ms T60, the shimmer
feedback drops to 0 and the chik fires once at 30 %. A new trig also restarts every envelope from its current value
with the new hit's times, which is what makes the closed-after-open choke work.

**Velocity** (0 is read as 100): level `0.08 + 0.92·v²`, tick `×v³`, air `×v²`, cutoff
`×(0.75 + 0.25·v)`, body/sizzle `×(0.85 + 0.15·v)`. Soft hits are quieter, darker, shorter and less ticky.

## Recipes

| # | sound | PITCH | Metal | Size | Tick | Heat | DECAY | other |
|---|---|---|---|---|---|---|---|---|
| 1 | Hero closed | 64 | 64 | 64 | 64 | 64 | 40 | |
| 2 | Hero open | 64 | 64 | 64 | 64 | 64 | 40 | PUNCH |
| 3 | Note-length open | 64 | 64 | 64 | 64 | 64 | 64 | PUNCH, GATE, 250 ms |
| 4 | Trap closed | 66 | 50 | 40 | 80 | 72 | 24 | |
| 5 | 808 closed | 64 | 96 | 64 | 40 | 30 | 30 | |
| 6 | 808 open | 64 | 96 | 64 | 40 | 30 | 40 | PUNCH |
| 7 | 909-style | 64 | 24 | 52 | 64 | 48 | 32 | |
| 8 | Pedal chick | 64 | 64 | 70 | 20 | 40 | 10 | velocity 60 |
| 9 | Shaker | 64 | 0 | 30 | 0 | 40 | 22 | |
| 10 | Tambourine | 64 | 90 | 16 | 8 | 60 | 20 | PUNCH |
| 11 | Ride | 64 | 100 | 96 | 92 | 50 | 78 | PUNCH |
| 12 | Crash | 64 | 48 | 112 | 70 | 64 | 80 | PUNCH |
| 13 | 808 cowbell | 64 | 127 | 64 | 72 | 70 | 30 | PUNCH |
| 14 | Reverse cymbal | 64 | 50 | 100 | 64 | 64 | 120 | PUNCH |
| 15 | Crushed lo-fi | 64 | 64 | 64 | 64 | 127 | 40 | |
| 16 | Laser chirp | 64 | 96 | 40 | 127 | 64 | 20 | |
| 17 | Metal gong | 40 | 120 | 127 | 30 | 50 | 100 | PUNCH |

All seventeen render in `build/sheen-demos/` (gitignored), with `00-groove.wav` first: 2 bars at 120 BPM of 1/16
notes, open on the off-beat 8ths, velocities 110/70, plus a 1/32 roll on the last beat ramping 40 → 120.

## Tests and measurements

- Host: `python3 tests/sheen/test_sheen_sound.py` (§9.1–9.3). All hero checks pass except the roll's step-ratio
  check (the first onset is at velocity 40; later hits at 120 have ordinary waveform slopes 10× larger — see
  `PROGRESS.md`). All macro and recipe checks pass.
- Emulator: `python3 tests/sheen/test_sheen_emu.py --cycles <OS> --only exact` compares host and device sample for
  sample after the OS's `>>1`; all 22 cases pass. `--only cpu,level,six` measures CPU and level.
- **CPU is over budget**: in the real voice loop the worst SHEEN block costs 24,804 instructions against 6,901
  for TONE steady (3.59×), and six active SHEEN tracks cost 137,385 against 45,818 for six TONE (3.00×). This is
  reported as a FAIL, not hidden.
- Variables: 640 bytes per voice (including the 128-sample shimmer buffer), measured with `--dump-size`.
  Code + tables: 12,436 bytes placed. The all-carded-engine build exceeds the shared code window (see PROGRESS).

## Build

```
python3 core/build_engine.py sheen          # needs M68K_CROSS
python3 core/compose.py --cycles <OS> --machines sheen
```

The flashable builds are in `build/`: `sheen_OS1.13.syx`, `sheen-core_OS1.13.syx` (the M2 fallback) and
`sheen-all_OS1.13.syx` (feral9, punch, blast, sheen). `build/` is gitignored because it contains Elektron's OS.

## Credits

The six metal oscillator frequencies are the TR-808's cymbal/hat oscillators (205.3, 304.4, 369.6, 522.7, 540 and
800 Hz), a published piece of the TR-808's design; the square/RING metal idea follows that machine. The TPT
state-variable filter is the Zavalishin/Simper topology. The bridge follows the owner's PUNCH engine
(`engines/punch/punch.c`) and `refmachine.c`. Everything here is MIT, © 2026 100demons; see `LICENSE`.
