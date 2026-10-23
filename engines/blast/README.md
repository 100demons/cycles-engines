# BLAST

**Fourteen tonal OPL2 two-operator FM models, with a stepped modulator ratio on SWEEP.**

Status: `experimental` · Version: `3.0.0` · For: Model:Cycles OS 1.13 · License: MIT

## What it sounds like

One extra machine after Chord with fourteen tonal models built from the OPL2's two-operator FM:
SIN, HLF, ABS, PLS, BAS, PIA, MAR, GLK, BRS, ACC, STR, STL, TMP and ZAP. SHAPE shows the model
name; SWEEP is the modulator ratio in three-character steps, like the Digitone, with FUNC leaving
the steps for fine movement. The machine has its own MACHINES-page splash and its own value
windows for Model and Ratio.

| SHAPE | Model | SHAPE | Model |
|---:|---|---:|---|
| 0 | SIN (sine) | 7 | GLK (glockenspiel) |
| 1 | HLF (half-sine) | 8 | BRS (brass) |
| 2 | ABS (abs-sine) | 9 | ACC (accordion) |
| 3 | PLS (pulse) | 10 | STR (strings) |
| 4 | BAS (bass) | 11 | STL (steel drum) |
| 5 | PIA (electric piano) | 12 | TMP (timpani) |
| 6 | MAR (marimba) | 13 | ZAP (zap) |

## Knobs

| Knob | What it does |
|---|---|
| PITCH | the note, as on every machine; FINE detunes |
| COLOR | Index: the FM modulation index / modulator level, the mallet hardness or the feedback |
| SHAPE | Model, 0–13 (names above) |
| SWEEP | Ratio: the modulator ratio in steps; FUNC leaves the steps |
| CONTOUR | Env: brightness decay, swell, feedback, detune or mallet click |
| DECAY | decay, or release when GATE holds the note; PUNCH is the chip's LFO |

## Weight

About **60 to 84 % of a stock TONE voice** per block; a silent voice costs almost nothing.
Standalone: this tweak adds machine 6 and cannot be combined with another added-machine mod.

## Testing

- Emulator: the source tree's bridge proof (all models, GATE, silence, cost, memory, stack) and
  OS proof (boot, descriptors, names, ratios, voice loop) passed. **Not yet tested on hardware.**
- Real Model:Cycles: **not yet**.

## Install

Easiest: tick **BLAST** in the [model27 flasher](https://100demons.github.io/cycles-flasher/flasher/).
`addon/66-blast.json` is the standalone tweak; apply it with `tools/build.py -t blast` or a flasher
that accepts a tweak. This folder contains no Elektron firmware.

## Credits

- Engine, tables and design: **100demons and Claude**, MIT.

## Changelog

See [CHANGELOG.md](CHANGELOG.md).
