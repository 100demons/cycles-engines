# Feral

**Nine noisy, unpredictable synthesis models on one extra machine, light on the processor.**

Status: `experimental` · Version: `9.0.0` · For: Model:Cycles OS 1.13 · License: MIT

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

Works together with: nothing else that adds a machine. This is a standalone tweak, not a
composer package.

Does not work with: LiXR, SAMPLE, MACRO, ACID, the Syntakt engines or Model-TG; they take
the same machine slot or the same payload window.

## Testing

- Emulator proof: **not run in model27**. The source tree measured the CPU cost and reported
  the model set; the sound has not been checked against a reference here.
- Real Model:Cycles: **not yet**.

## Install

Easiest: tick **Feral** in the [model27 flasher](https://100demons.github.io/cycles-flasher/flasher/)
and bring your own official `model-cycles_OS1.13.syx`.

`addon/63-feral9.json` is the ready-made tweak. It is a standalone added-machine tweak, not a
model27 package: apply it with `tools/build.py -t feral9` or a flasher that accepts a tweak.

This folder contains no Elektron firmware. Every write in the tweak is checked against the
bytes of *your own* official OS file when it is applied.

## Credits

- Engine, tables and design: **100demons and Claude**, MIT.

## Changelog

See [CHANGELOG.md](CHANGELOG.md).
