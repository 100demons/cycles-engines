# Feral

**Nine noisy, unpredictable synthesis models on one extra machine, light on the processor.**

Status: `tested` · Version: `9.0.0` · For: Model:Cycles OS 1.13 · License: MIT

## What it sounds like

One extra machine after Chord with nine models on SHAPE, from bytebeat and Gendy to a
Benjolin, a HAVOC rungler, a pulsar and scanned synthesis. COLOR and SWEEP are the
model's two macros; CONTOUR is the tone envelope (or a third control on BNJ and RNG).
PUNCH is FREE: the engine keeps running between trigs. It is meant for noise, percussion
and unstable textures.

| SHAPE | Model | What it is |
|---:|---|---|
| 0 | NYB | bytebeat |
| 1 | GND | Gendy |
| 2 | BNJ | Benjolin |
| 3 | RYT | Rhythmicon resonators |
| 4 | FRS | Fourses |
| 5 | RNG | HAVOC rungler |
| 6 | PLS | pulsar |
| 7 | SCN | scanned synthesis |
| 8 | KOS | logistic map |

## Knobs

| Knob | What it does |
|---|---|
| PITCH | the note, as on every machine; FINE detunes |
| COLOR | the model's first macro; the value window names the real control for that model |
| SHAPE | Model: 0 NYB, 1 GND, 2 BNJ, 3 RYT, 4 FRS, 5 RNG, 6 PLS, 7 SCN, 8 KOS |
| SWEEP | the model's second macro |
| CONTOUR | the tone envelope; a third control on BNJ and RNG |
| DECAY | the stock amp envelope; GATE and PUNCH work as on Tone |

## Weight

CPU cost per voice: the worst model is **4,801 instructions per 32-sample block**, against
5,290 for TONE (one playing voice, measured in the source tree). All nine models stay below
the stock TONE voice. There is no load governor of its own.

Works together with: LiXR and other model27 machine packages through the composer. This folder
is a model27 package (manifest, sources and the committed object).

Does not work with: the old prebuilt single-machine tweaks (MACRO, ACID, the Syntakt engines,
Model-TG) unless they are composed as packages.

## Testing

- Emulator proof: **2026-10-22**, model27 `tools/emu/test_feral9.py`: sound through the stock amp
  chain, no unmapped access, stock KICK unchanged. **ALL OK**.
- Real Model:Cycles: **2026-10-22**, standalone; the composed build with LiXR is new.

## Install

Easiest: tick **Feral** in the [model27 flasher](https://100demons.github.io/cycles-flasher/flasher/)
and bring your own official `model-cycles_OS1.13.syx`. Tick LiXR too and the page composes both.

This folder **is** a model27 package: `manifest.json` points at `src/` and `addon/`; `addon/64-feral9.json`
is the composed single-machine build, `addon/feral9.o` the committed object. SHAPE currently shows the
model number (0–8) and the MACHINES picture is TONE's; those custom parts are not ported yet.

This folder contains no Elektron firmware. Every write in the tweak is checked against the
bytes of *your own* official OS file when it is applied.

## Credits

- Engine, tables and design: **100demons and Claude**, MIT.

## Changelog

See [CHANGELOG.md](CHANGELOG.md).
