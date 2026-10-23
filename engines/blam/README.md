# BLAM

**Twelve OPL2 FM percussion models and two SAM speech banks on one extra machine.**

Status: `experimental` · Version: `1.0.0` · For: Model:Cycles OS 1.13 · License: MIT

## What it sounds like

One extra machine after Chord: twelve FM percussion models (KIK, TOM, BNG, CNG, TAI, TIM, MAR, DJB,
BLK, GNG, BUB, ZAP) and two SAM speech banks. The FM models use the stepped Ratio list, with 4:1
as the default; SAM1 and SAM2 use COLOR for the word, SWEEP for the scrub position and CONTOUR for
the speaking speed. The MACHINES-page card reads `CLASS: PERC`, `STYLE:OPL2`.

## Knobs

| Knob | What it does |
|---|---|
| PITCH | the note, as on every machine; FINE detunes |
| COLOR | the FM model's colour; on SAM1/SAM2, the word |
| SHAPE | Model, 0–13 (names above) |
| SWEEP | the stepped Ratio on FM models; the scrub position on SAM1/SAM2 |
| CONTOUR | the envelope contour; the speaking speed on SAM1/SAM2 |
| DECAY | decay, or release when GATE holds the note; PUNCH and GATE as on Tone |

## Weight

A playing voice costs **2,700 to 4,400 instructions per block**, against 5,291 for TONE
(source-tree measurement). Standalone: this tweak adds machine 6 and cannot be combined with another
added-machine mod.

## Testing

- Emulator: the source tree's bridge and OS proofs passed. **Not yet tested on hardware.**
- Real Model:Cycles: **not yet**.

## Install

Easiest: tick **BLAM** in the [model27 flasher](https://100demons.github.io/cycles-flasher/flasher/).
`addon/67-blam.json` is the standalone tweak; apply it with `tools/build.py -t blam` or a flasher
that accepts a tweak. This folder contains no Elektron firmware.

## Credits

- Engine, tables, SAM words and design: **100demons and Claude**, MIT.

## Changelog

See [CHANGELOG.md](CHANGELOG.md).
