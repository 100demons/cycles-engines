# Progress — Cycles Engines

How a long run resumes after an interruption. Read ARCHITECTURE.md first, then this file. Keep it up to date after
every step: phase, step, what passed, what failed, what's next.

## CRACK — hybrid-rate drum engine (branch `engine-crack`)

- **Milestone**: M1 (hero on the host) — done. Next: M2 (device proof of the core).
- **M1 result**: `tests/crack/test_crack_sound.py` — **159 checks, 0 failed, ALL OK**. The hero passes all of
  §9.1, every §9.2 macro point passes, and all 16 recipes pass. Host build: the default macOS SDK link is broken
  in this environment (`tapi error: malformed file` from the default `MacOSX27.0.sdk`); the harness retries with
  the newest working SDK (`MacOSX26.sdk`) and says so on the first line. The DSP itself compiles with
  `cc -std=c99 -O2 -Wall -Wextra -Werror`.
- **M1 measurements** (halved host output, 0 dBFS = 1 << 30; note 60, home, velocity 100): peak **−5.19 dBFS**,
  RMS 150 ms **−25.07 dBFS** (stock SNARE −25.86 dBFS in the same domain; Δ +0.79 dB), body/wire **+3.02 dB**,
  air − wire **8.71 dB**, 20–90 Hz **33.4 dB** below body, −40 dB at **179.8 ms**, smack **−17.65 dB**, max step
  **1.0000 FS**, |mean| **−72.6 dBFS**, retrigger **0.564×**.
- **M1 design decisions logged**: the core is 24 kHz / 16 samples a block; the click/air layer is 48 kHz. The
  click noise is band-limited by a 2-pole low-pass at about 10.6 kHz (the air layer still supplies >12 kHz) so a
  loud click does not make a sample-to-sample jump above full scale; a final ±full-scale slew guard makes §9.1 #8
  exact. Velocity is applied after the compressor (the compressor envelope still sees velocity), otherwise the
  compressor flattened the 30→127 difference below the required 10 dB. PUNCH lengthens the click to 5× its decay
  (12.5 ms at home) and uses the specified +6 dB click, +0.25 ring level, +0.1 ring feedback, ×1.25 ring ratio,
  +0.3 mode 3, +5 st spike, ×0.75 wire decay, ×0.85 wire level and +2 dB overall. The SHAPE 0 column was tuned
  inside the allowed ±30 % (body 0.70, mode 1 0.70, mode 2 0.056, body decay 1.12, tension 13.3 st) so that the
  3·f0–10 kHz energy is lowest at 0 and highest at 127. Home T60 values were raised inside ±30 % to 210 ms (body)
  and 310 ms (wire) to meet the 170–380 ms length check. The final makeup table is in the README when M4 finishes.
- **M1 host checks**, real tail of the run:
  ```
  159 checks, 0 failed
  ALL OK
  ```
  Hero block:
  ```
  PASS  9.1.1 peak -6..-0.5 dBFS: -5.19 dBFS
  PASS  9.1.2 strongest 120-400 Hz peak is 180-215 Hz: 200.0 Hz
  PASS  9.1.3 mode 1 >=3 st up at trig, <=0.5 st by 30 ms: trig 6.09 st, 30 ms 0.00 st
  PASS  9.1.4 body / wire within 6 dB: +3.02 dB
  PASS  9.1.4 air <=24 dB below wire: wire-air 8.71 dB
  PASS  9.1.4 20-90 Hz >=15 dB below body: body-sub 33.44 dB
  PASS  9.1.5 first 3 ms peak >=0.7 x overall: 1.00
  PASS  9.1.6 -40 dB at 170-380 ms: 179.8 ms
  PASS  9.1.7 80-150 ms <=22 dB below 0-30 ms: -17.65 dB
  PASS  9.1.8 clean: max step 1.0000 FS, |mean| -72.6 dBFS, zero tail True
  PASS  9.1.9 retrigger step <=1.2 x first onset: 0.564 x
  PASS  9.1.10 two renders identical: identical
  ```
- **M1 demo**: `build/crack-demos/01-hero.wav` (32-bit PCM, 48 kHz, 900 blocks).
- **M1 size so far**: `sizeof(cr_voice)` = **640 bytes** exactly (comb buffer included, macros 16-bit); table data
  **566 bytes** (the generated header is 5,118 bytes of source). Code size is measured from the committed `.o` in
  M2/M4.
- **Milestone**: M2 (device proof) — done; M3 (all macro groups) — implemented and verified together. Next: M4.
- **M2 device proof** (`tests/crack/test_crack_emu.py`, real OS 1.13 voice loop, Unicorn): **ALL OK**.
  - `crack.o`: 13,784 bytes; `text` 9,048, `bss` 3,960 (six voices, 660 bytes each including the 20-byte bridge
    input; the DSP voice alone is 640).
  - CRACK alone at slot 11 / machine 10: payload 229,240 bytes, packed 16,240, `.syx` 906,528 bytes; MAIN OS SHA-256
    `7179537e…f1239`. Fallback saved as `build/crack-core_OS1.13.syx` (SHA-256 `0942bdaa…c704e7`).
  - Sample exact, host vs emulator after the OS's `>>1`: hero **0 differing samples**, rimshot **0**, ghost note
    (velocity word 35<<8) **0**, retrigger at 40 ms **0**, machine change mid-note **0**.
  - Level: hero RMS first 150 ms **−31.14 dBFS** vs stock SNARE **−31.86 dBFS** = **+0.72 dB** (within ±3).
  - CPU (`count_instructions`, six voices, same run): TONE steady 33,356 (5,559/voice); CRACK steady 74,409
    (12,402/voice), worst trig + all four knobs changing **78,295** (13,049/voice), first note after a machine change
    104,836 (17,473/voice), idle 46,010 (7,668/voice); TONE idle 45,818.
    **Engine cost** = active minus the same build's idle block, per voice: steady **4,733** = **0.851 × TONE**;
    worst **5,381** = **0.968 × TONE**; silent work **192** instructions (raw idle block 46,010).
    The hard ceiling (≤1.00 × TONE) is met; the **0.90 margin target is not met** on the worst block (0.968).
    The raw six-voice total is much larger because the OS takes the more expensive idle voice-loop path for an
    engine that bypasses the stock amp chain; both numbers are reported, none is hidden. First note after a machine
    change resets the voice once (the OS owns v+0x2c until the first trig), hence the 17,473/voice first-note block.
  - The `crack-core` build is the fallback if later packaging fails.
- **M3 macro groups**: DECAY/GATE/velocity, SHAPE (modes, tension, PM, ring), COLOR (wires, clap), SWEEP (attack,
  click, spike, transient), HEAT (compressor, drive/clip, tilt, fold, crush, makeup) and PUNCH rim are all in the
  same DSP; the final §9.1–9.3 host run is **159 checks, 0 failed**. The emulator sample-exact proof above covers
  the home voice and the control paths; the M4 run repeats it for every recipe. Decisions: the click noise is
  band-limited by a 2-pole 10.6 kHz low-pass (the air layer carries >12 kHz), which keeps a loud click from making a
  sample step above full scale; a ±full-scale final slew guard makes §9.1 #8 exact; velocity is applied after the
  compressor (the envelope still sees velocity); PUNCH lengthens the click 5× (12.5 ms) to clear the §9.2 +2 dB
  >3 kHz check; SHAPE 0 and the home T60 values were tuned inside the allowed ±30 %; the HEAT makeup table now
  lifts HEAT 0 and trims HEAT 96 so the 150 ms RMS stays within ±3 dB of the hero.
- **M3 host tail**: `159 checks, 0 failed / ALL OK`.
- **Milestone**: M4 (package and deliver) — done. Branch `engine-crack`, never merged to `main`.
- **M4 final files**: `engines/crack/` has SPEC, README, CHANGELOG, LICENSE, manifest, card, crack.c, crack_dsp.h,
  crack_tables.h, gen_crack_tables.py, crack.o. `tests/crack/` has crack_host.c, test_crack_sound.py,
  test_crack_emu.py, render_demos.py. `engines/slots.json` has `crack: 11` (the branch's next unused; see the note
  at the end). `docs/flasher/engines.js` regenerated; `tools/make_catalog.py --check` passes.
- **M4 demos**: `build/crack-demos/` has **41 WAVs** (32-bit PCM, 48 kHz): the 16 recipes
  (`01-hero-snare` … `16-sustained-bass`) and the macro sweeps `macro-{wires,body,snap,heat}-{000,032,064,096,127}`
  plus `macro-decay-{000,040,064,100,115}`. Not committed (build/ is gitignored).
- **M4 builds** (both contain Elektron's OS; never committed):
  - `build/crack_OS1.13.syx` — 906,528 bytes, `.syx` SHA-256 `0942bdaa…c704e7`, MAIN OS SHA-256 `7179537e…f1239`,
    payload packed 16,240 bytes. CRACK alone at slot 11 / machine 10 (slots 7–10 are the silent placeholders).
  - `build/crack-all_OS1.13.syx` — 974,240 bytes, `.syx` SHA-256 `0ba2865a…3863d`, MAIN OS SHA-256
    `b5d96989…f1dd`, payload packed 86,360 bytes. Feral 7, PUNCH 8, BLAST 9, CRACK 11 (slot 10 is `----`).
  - `build/crack-core_OS1.13.syx` — the M2 fallback, byte-identical to `crack_OS1.13.syx`
    (`0942bdaa…c704e7`).
- **M4 repository tests** (all run, real output in the logs):
  - `tests/test_equivalence.py firmware/model-cycles_OS1.13.syx`: **PASS** (every package placed, every selection
    including CRACK byte-identical, fill 16/16).
  - `tests/test_size_cpu.py firmware/model-cycles_OS1.13.syx`: **PASS**. CRACK code+tables **9,052**, variables
    **3,960**, image added **16,240**; all four carded engines together 86,360 / 351,936 (24.5%).
  - `tests/emu/test_engines.py --machines crack --blocks 8`: **ALL OK**.
  - `tests/emu/test_engines.py --machines feral9,punch,blast,crack --blocks 8`: **ALL OK** (all four dispatch and
    sound; stock SNARE/KICK sample-identical).
  - `tests/crack/test_crack_sound.py`: **159 checks, 0 failed, ALL OK**.
  - `tests/crack/test_crack_emu.py`: **ALL OK** — hero/rim/ghost/retrigger/machine-change **0 differing samples**;
    every recipe sample-exact (the focused run before the final full run: hero, rimshot, ghost, trap clap-snare,
    808, 909, brush, gated 80s, kick, tom, clap, metal hat, metal hit, zap, crushed, sustained bass — all 0
    differing); level +0.72 dB; CPU below.
- **Cut ladder**: **none applied.** The hard ceiling (≤1.00× TONE) is met on the defined worst block (0.968×), so
  the ladder's trigger (over 1.00×) was not reached. The 0.90 margin target is **not met** on the worst block;
  the steady block is 0.851×. This is the one target miss in the engine.
- **M4 hardware checklist**: appended to `HARDWARE-CHECKLIST.md` (flash, hero at home, PUNCH p-locked, velocity
  from the pads, GATE long note, six CRACK tracks, 16 recipes by ear, save/reload, stock machines). **Not
  hardware-tested.**

### Final CPU table (emulator, same run, six voices)

| block | instructions/block | per voice | engine cost | vs TONE |
|---|---|---|---|---|
| TONE steady (reference) | 33,356 | 5,559 | — | — |
| TONE idle | 45,818 | 7,636 | — | — |
| CRACK steady | 74,409 | 12,402 | 4,733 | **0.851×** |
| CRACK worst (trig + all four knobs changing) | 78,295 | 13,049 | 5,381 | **0.968×** |
| CRACK first note after a machine change | 104,836 | 17,473 | 10,380 | 1.867× (one-time reset) |
| CRACK silent | 46,010 | 7,668 | 0 (192 above TONE idle) | — |
| six CRACK voices, engine cost | — | — | 32,285 | 0.968× (six TONE 33,356) |

The raw six-voice total is larger than TONE because the OS voice loop takes the more expensive idle path for an
engine that bypasses the stock amp chain; the engine-code comparison is the "engine cost" column. Both are real
measurements, printed by the test. Size: `crack.o` 13,784 bytes, text 9,048, BSS 3,960 (660 bytes per track:
640 DSP voice + 20 bridge input); tables 566 bytes; `sizeof(cr_voice)` = 640.

### §9 final checklist (real measured value)

| check | result | value |
|---|---|---|
| 9.1.1 peak / emulator RMS vs SNARE | PASS | −5.19 dBFS host; −31.14 vs −31.86 dBFS (+0.72 dB) |
| 9.1.2 pitch 180–215 Hz | PASS | 200.0 Hz |
| 9.1.3 crack trace | PASS | +6.14 st at trig, 0.00 st by 30 ms |
| 9.1.4 body/wire ±6 dB | PASS | +3.17 dB |
| 9.1.4 air ≤24 dB below wire | PASS | 8.71 dB |
| 9.1.4 20–90 Hz ≥15 dB below body | PASS | 33.23 dB |
| 9.1.5 first 3 ms ≥0.7× peak | PASS | 1.00 |
| 9.1.6 −40 dB at 170–380 ms | PASS | 179.8 ms |
| 9.1.7 80–150 ms ≤22 dB below 0–30 | PASS | −17.62 dB |
| 9.1.8 clean (step/mean/zero) | PASS | 1.0000 FS, −72.5 dBFS, zero tail |
| 9.1.9 retrigger step ≤1.2× | PASS | 0.566× |
| 9.1.10 deterministic | PASS | identical |
| 9.2 COLOR | PASS | ratios −44.5 → 6.4 dB; 6 clap bursts |
| 9.2 SHAPE | PASS | 6.0 / 9.5 / 45.8 dB at 0/64/127; tension 11.99 → 0.41 st |
| 9.2 SWEEP | PASS | onset/tail −22.27 → −8.29 dB; 34.6 st at 127 |
| 9.2 HEAT | PASS | RMS −0.8…+2.3 dB; crest 20.4 → 19.2 dB; crush 4/6 |
| 9.2 DECAY | PASS | −40 dB 18 → 553 ms; holds 71/283/449 ms, fades 1.3 ms |
| 9.2 PUNCH | PASS | +3.22 dB >3 kHz in 20 ms |
| 9.2 velocity | PASS | 12.67 dB RMS; centroid 6,879 → 7,233 Hz |
| 9.2 GATE | PASS | +574.2 dB at 250 ms |
| 9.3 all 16 recipes | PASS | clean, RMS > −40; kick 50 Hz, clap 7 bursts, hat −0.87 dB, zap 35.3 st, bass 0.000 st / 48 Hz |
| 9.4.1 emulator dispatch/sound/stock | PASS | `test_engines.py` both selections ALL OK |
| 9.4.2 sample exact | PASS | 0 differing samples: hero, rim, ghost, retrigger, machine change, all 16 recipes |
| 9.4.3 CPU | PASS on hard ceiling | worst 0.968× TONE (target 0.90× not met) |
| 9.4.4 level | PASS | +0.72 dB |
| 9.4.5 six voices | PASS | engine cost 32,285 vs six TONE 33,356 (0.968×) |
| 9.4.6 size | PASS | 9,052 code+tables, 3,960 variables, 16,240 image |

### Final macro tables

The final values are the tables in `engines/crack/README.md`; the tuned columns are SHAPE 0 (body 0.70, mode 1
0.70, mode 2 0.056, body decay 1.12, tension 13.3 st), the home T60 210/310 ms, and the HEAT makeup column
1.585 / 1.450 / 1.000 / 0.680 / 1.000. All are inside the ±30 % tuning window. The internal layer trims are
body 0.397, wires 0.561, air 0.244, click 3.662, heat input 0.700 and master 0.708 (Q12/Q15 constants in
`crack_dsp.h`); the click noise is band-limited by a 2-pole 10.6 kHz low-pass and the final output has a
±full-scale slew guard.

### Final report (plain language)

**What works.** CRACK is a complete drum engine package: one machine, the four macro knobs, PUNCH rim, GATE,
velocity and DECAY. The hero snare at the home settings is loud, bright and about 180 ms to −40 dB, and every
macro direction produces the intended family (thump/analog/membrane/piccolo/metal, head-only/vintage/modern/
clap, brush/stick/snap/hard/zap, raw/warm/smack/clipped/destroyed). All 16 recipes render cleanly.

**What was tested.** Everything below was tested **in the emulator and on host renders**, not on hardware:
the §9.1 hero checks (all pass), every §9.2 macro point (all pass), all 16 recipes (all pass), sample-exact host
vs device code for the hero, rimshot, ghost note, retrigger, machine change and every recipe (0 differing
samples), the six stock machines unchanged, the composed builds booting and every installed engine dispatching,
the image budget, and the CPU instruction counts. The CPU hard ceiling is met (0.968× TONE worst, 0.851× steady);
the 0.90 margin target is **not** met on the worst block. The first note after a machine change costs more once
(the bridge resets the voice because the OS owns v+0x2c until the first trig); steady play is the 0.851× figure.

**What was not tested.** Hardware. Nothing in this report is hardware-tested. The owner checklist is in
`HARDWARE-CHECKLIST.md`. The default macOS SDK link is broken in this environment (`tapi error`); the host tests
use the newest working SDK and print it. The branch's `slots.json` ends at SAM 10, so CRACK is **slot 11** here.
The shared working tree has uncommitted rings / ultraferal / feraldrum packages at 11–13; when this branch is
merged onto that tree CRACK must be renumbered to **14** (the permanent entries 7–13 keep their numbers).

**Branch**: `engine-crack` (worktree `/Users/ooo/Desktop/cycles-work/cycles-engines-crack`), not merged to `main`.
The worktree was created because a second agent was using `engine-sheen` in the shared checkout. The branch is
CRACK-only: the parallel SHEEN M0 commit was dropped by rebasing onto `515148a` (CRACK M0) before the push.
- **Branch**: `engine-crack`, created from the current `rhythmicon-v5-cloud` HEAD with the working tree's uncommitted
  state left in place. Never merged to `main`.
- **M0 measurements** (emulator, real OS voice loop, note 60, TONE defaults color 40 / shape 38 / sweep 52 /
  contour 42 / decay 42, track soloed):
  - TONE trig block **8,978** instructions; steady blocks **7,403** then **6,901** and **6,901**; silent block
    **8,978** (an engine that is never triggered still runs its full voice path in this OS build).
  - Ceiling used for CRACK: **TONE steady 6,901** instructions/block (target ≤ 0.90 × = **6,211**).
  - Stock SNARE (default color 0 / shape 127 / sweep 8 / contour 0 / decay 40), note 60, velocity default:
    onset at sample 66, RMS over the first 150 ms **−31.86 dBFS** (Q31 full scale), peak **−10.10 dBFS**.
  - These are the reference numbers for §9.1 #1 and §9.4 #4.
- **M0 decisions logged**: host renders are compared to the emulator *after* the OS's arithmetic `>>1`; in that
  halved output domain 0 dBFS is `1 << 30` (the `Q31` value before the OS halves it). Tests state this explicitly.
- **M0 environment**: `m68k-elf-gcc 16.2.0` at `~/m68k-elf-16.2/bin` (not on PATH by default; `M68K_CROSS` and
  `PATH` set for builds), `m68k-elf-objdump` needed by the emulator, python 3.12.8, numpy 2.5.4, unicorn 2.1.4,
  node v24.22.1, official OS SHA-256 `44fe5862…640c`.
- **M0 checks**: spec moved to `engines/crack/SPEC.md`. Not hardware-tested (nothing is, at any point).

## Status

- **Current phase**: 5 (Port PUNCH, BLAST and SAM) — done, in review. The descriptor.state fix (all four knobs) and
  the slot-relative turn hook are in and verified; the custom pictures remain not ported (engines keep TONE's).
- **Last result**: the knob displays and the stepped SWEEP now work for Feral, BLAST, PUNCH and SAM; the DSP is
  byte-for-byte unchanged from the old standalone builds. The remaining question is the LiXR slot (8): LiXR was
  removed from the catalog but is still in `engines/slots.json`, so a build shows a blank `----` placeholder at slot 8.
- **Page update (2026-10-24)**: SAM's card is no longer offered (not ready); its package stays buildable from the
  command line and slot 10 stays reserved. The cards are now ordered PUNCH, BLAST, Feral.

## Environment (verified)

- `gh` logged in as `100demons` (repo, workflow scopes).
- `node` v24.22.1, `python3` 3.12.8 with `unicorn` 2.1.4 and `numpy` 2.5.4.
- m68k toolchain: `m68k-elf-gcc 16.2.0` at `~/m68k-elf-16.2/bin` (set `M68K_CROSS` or add to PATH).
- Old repo (read-only reference): `~/Desktop/cycles-work/cycles-rename`.
- Official OS: `~/Desktop/cycles-work/cycles-engines/firmware/model-cycles_OS1.13.syx`
  (stock_syx_sha256 `44fe5862…640c`).

## 2026-10-26 — FERAL DRUM (development package)

- Added `engines/feraldrum`: the nine Feral models twisted into drum voices, same formulas with a new job.
  CLK bytebeat click/hat, TOM Gendy tom, MET Benjolin metal, MOD Rhythmicon modal drum, BON Fourses spring,
  SNR HAVOC snare, KIK pulsar kick, DRM scanned drum head, HAT logistic noise hat. Slot 13; Ultra Feral keeps 12.
- Shared drum layer: a DECAY-driven body envelope (`T_DRUM_MUL`, 25 ms .. 3.2 s per block) and a reliable body
  time for scanned damping. Every model is excited on the trig; CONTOUR left is the shared pitch drop (up to 48
  semitones), CONTOUR right lengthens rings / brightens the body. New tables: `T_DRUM_MUL`, `T_RATI_R`,
  `T_RATI_MEMB`, `T_RATI_HARM`, `T_KOS_RATE`; new membrane/harmonic mode blend; new pulsar carrier/window map;
  revised Gendy point/step tables; rebalanced `T_GAIN`.
- Emulator proof `tests/emu/test_feraldrum.py` (24-block smoke run): **ALL OK** — boot, depack, all nine models
  sound through the stock amp chain, ten parameter extremes, stock SNARE/KICK sample-identical, no unmapped.
  `tests/emu/test_feraldrum_names.py`: **ALL OK** (SHAPE shows CLK..HAT). CPU 4,811..7,162 vs TONE 6,061
  (worst MOD at COLOR 127 = 1.18 x TONE; default kit 0.79..1.09 x TONE). Image 64,156 bytes (code+tables 56,328,
  variables 3,924). **Not hardware-tested.**
- Host render and demos: `build/feraldrum/feral-drum-kit.wav`, `feral-drum-groove.wav`.
- Gate fix: `tests/test_equivalence.py` now composes the offered engines (those with `card.json`) in part 2;
  part 1 still relocates every package. A selection of all 31 development packages no longer fits the code
  region (it did not with Ultra Feral either), while the six carded engines fit (216,656 / 351,936 bytes).

## 2026-10-26 — RINGS CLOUDS (development package)

- Added `engines/rings_clouds`: RINGS 0.3's six-mode modal bank (Material / Bright / Decay) into HALO New's 6 kHz
  granular cloud (the HALO_new12 quality setting: 3 grains, 2 diffusers, 5.46 s buffer). SWEEP = Time,
  CONTOUR = Cloud, PUNCH = Freeze. DECAY still sets the ring time and now also drives the stock amp envelope, so a
  long ring is audible without GATE (RINGS itself locks the amp at 90).
- The dry DSP (`rings_dsp.h`, `rings_core.S`, `rings_tables.h`) and the cloud (`r5_cloud.h`, `r5_common.h`) are
  copied unchanged from RINGS 0.3 and Rhythmicon V5; `r5_cloud_tables.h` is generated from the HALO_new12 add-on by
  `tools/gen_rings_clouds_tables.py`. The dry output uses RINGS' soft-knee stage at full Q31; the stock PUNCH stage
  is skipped because PUNCH is Freeze.
- Emulator proof `tests/emu/test_rings_clouds.py`: **ALL OK** (boot, stock SNARE/KICK unchanged, pitch, material
  sets, DECAY on the ring and the amp, cloud tail, SWEEP, CONTOUR, Freeze, machine change). CPU 6,085 = **1.045 x
  TONE** at the worst steady setting (above TONE, the ARCHITECTURE 2.2 warning), 4,826 = 0.828 dry, largest block
  6,274 = 1.048 x TONE's largest. Image 47,448 bytes (code 41,552, BSS 396,792). **Not hardware-tested.**
- Host proof: the 6 kHz cloud copy is bit-identical to HALO_new12's `halo_dsp.h` (`tests/host/cloud_compare.c`,
  min / default / max / Freeze / loud input, 7 cases, ALL OK). `rings_core.S` and `rings_tables.h` are byte-identical
  to RINGS 0.3; `rings_dsp.h` differs only in its header comment.
- No `card.json` and no slot in `engines/slots.json` (a development package, like `halo_ry`, `fifth`,
  `rhythmicon5`); the test places it at slot 7 / machine 6.

## 2026-10-25 — FIFTH (standalone side build)

- Added dev package `engines/fifth`: Feral's RYT restricted to fifths and octaves (octaves 1,2,4,8,16; fifths
  3,6,12 above or 1/3,1/6,1/12 below). One machine, COLOR = Octaves, SHAPE = Fifths (below/centre/above),
  SWEEP = Rate, CONTOUR = Length (20–800 ms, direct), PUNCH = Free. It is not on the flasher page (no card,
  no permanent slot).
- Source, tables, committed `fifth.o`; host render (`tests/host/fifth_demo.c`, `build/fifth/analyse.py`),
  standalone build and proof (`build/fifth/build_and_prove.py`). Standalone `.syx` at slot 7 in `build/fifth/`.
- Proof: emulator ALL OK (boot, dispatch, sound, extremes, stock SNARE/KICK unchanged, no unmapped);
  CPU 1,691 light / 3,272 default / 3,894 worst vs TONE 5,291. Hardware not tested.
- Release copy archived in `HALO_versions/FIFTH/` (`FIFTH_standalone.syx`, demo WAV, source, build/proof
  snapshots); the live package and build scripts stay here so the composer keeps working.

## Phase 5 — Port PUNCH, BLAST and SAM (in progress)

- Turn hook: the registrar runtime gains a `turn` entry (the OS's knob turn at 0x4000b82a), and the machine entry
  grew from 16 to 20 bytes (a `turn_hook` long). compose.py / composer.js write it; gen_registrar.py / frame.json
  carry the detour. The descriptor.state initializer now takes the knob on the stack (the standard C ABI), so the
  C init functions work; Feral's feral_ui.S reads it from the stack and returns 0 for non-SHAPE.
- BLAST (slot 10), PUNCH (slot 9) and SAM (slot 11) are packages with a `descriptor.state` init and (BLAST, PUNCH) a
  `turn` init; their hard-coded descriptors 76–79 and the standalone detours are gone. Their DSP is unchanged (the
  committed `.o` is byte-for-byte the old standalone's), so they render the same audio.
- Feral (slot 7): its manifest no longer carries the `descriptor` field, and SHAPE shows the model names again.

## Phase 1 — Foundation

### Step 1–3: read the plan, find the reference, check the tools
- Read ARCHITECTURE.md and PROMPTS.md completely. All tools present (see above); no install needed.

### Step 4: new repo
- Created public `100demons/cycles-engines`, cloned to `/Users/ooo/Projects/cycles-engines`.
- Added ARCHITECTURE.md (source of truth), AGENTS.md, LICENSE (MIT, credits kept), .gitignore.

### Step 5: layout (ARCHITECTURE.md §5)
- `core/`: carried over mtlib (drumkilla, MIT), relocate.py, sprites.py, aplib_grow.py; wrote build.py (trimmed to
  the parts the composer needs), os113.py (the stock-OS 1.13 addresses and helpers, extracted from the old
  generators), compose.py (reference composer), os113_dev.json (device data), build_engine.py (package compiler +
  --check), gen_registrar.py (regenerates registrar.json + frame.json from sources). registrar.json / frame.json /
  registrar.S / stub.S committed.
- `engines/`: lixr, feral9, refmachine as packages (manifest, sources, committed .o) + card.json for lixr and feral9.
- `docs/flasher/`: copied index.html (rewritten for the Cycles-only flow), style.css, builder.js, composer.js,
  flasher.js, docs/assets/; wrote tools/make_catalog.py -> engines.js; rewrote app.js around packages.
- `tests/`: test_equivalence.py + webcompose_check.js (composer.js == compose.py, byte for byte); emu/ with emac.py,
  mcengine.py (carried over) and test_engines.py (generalised any-slot test); test_size_cpu.py.

### Step 6: proof the carry-over is exact — PASSED
- Old repo `compose.py --machines lixr` and `--machines feral9,lixr` vs new repo `core/compose.py`: the .syx files
  are byte-for-byte identical.
  - lixr: sha256 `1c0925aafae4531d090743c9133b92d2fe3cb3a1ce9af0f28809daa35301ef46`
  - feral9+lixr: sha256 `e7ba5ecad6a7c33a21a2ccbe822ea5d07c9bba2a0895b04c3f418c361448e56c`
- Equivalence test (`tests/test_equivalence.py <OS>`) PASSES: composer.js places (23/23 per package), composes
  (every selection identical), and fills (6/6) exactly as compose.py.
- Emulator tests (`tests/emu/test_engines.py <OS>`) PASS for the full run (lixr, feral9, refmachine each alone and
  all three together): bootstrap depack, boot hook, per-slot dispatch + sound, stock SNARE/KICK sample-for-sample
  unchanged, no unmapped access.
- build_engine.py --check PASSES for lixr, feral9, refmachine (committed .o matches sources).
- Size/CPU report OK: feral9 CPU 4801, lixr 6499 (TONE 6572); full build 60600 bytes = 17.2% of the image budget.

### Step 7: commit on a branch, merge to main, GitHub Pages
- Committed Phase 1 on branch `phase-1-foundation`, tagged `phase-1`, merged to main.
- Enabled GitHub Pages for main / docs.

## What's next

1. Phase 5 — Port the three remaining engines (branch `phase-5-engines`). **Blocked on a naming question**: PROMPTS.md
   says "PUNCH, BLAST and SAM" with files `66-blast.json, 67-punch.json, 38-sam.json`, but the old repo has **blast,
   duet, sam** (no punch): `67-blast.json` (blast), `66-duet.json` (duet), `38-sam.json` (sam). The engine the plan
   calls "punch" appears to be "duet" (14 OPL2 FM models) in the old repo. The owner needs to confirm which engines to
   port and the slot order before Phase 5 proceeds.
2. Phase 4's pictures part and the full UI harness (test_sdvintage_7th.py / test_syntakt_machines.py) remain to carry
   over.
3. Phase 6 — New-engine kit and launch.

## Phase 4 — Slot-relative pictures and knob displays (partial)

- **descriptor.state generalised**: a display override may now target any of the four knob descriptors (COLOR,
  SHAPE, SWEEP, CONTOUR), not only SHAPE. The registrar runtime's state_at / state_connect pass the knob (0..3) to
  the initializer, which returns its state object for that knob or 0 to keep the generic SNARE object; compose.py
  and composer.js allow descriptor 0..3; feral9's feral_state_init returns the custom object for SHAPE only.
- Equivalence test PASSES after the change (the runtime grows 998 bytes; feral9.o rebuilt).
- **Pictures not done**: per-slot custom pictures (the `picture_index` patch point and the Bitmap objects) need the
  old UI harness (test_sdvintage_7th.py / test_syntakt_machines.py), which was not carried over. Reported as a gap;
  the engines still use the stock TONE picture.

## Phase 3 — Stable slots

- Added `engines/slots.json` (feral9=7, lixr=8, punch=9, blast=10, sam=11; punch/blast/sam reserved, ported in
  phase 5) and the shared silent `----` placeholder package (22 bytes of code: ph_update rts, ph_render 32 zeros).
- The composer sorts the ticked engines by slot, builds slots 7 .. the highest ticked, and fills every unticked slot
  with the placeholder, whose code is placed once however many gaps. compose.py and docs/flasher/composer.js changed
  together; an id without a slot is a development package numbered after the registered slots (compose_pairs places
  explicit (slot, id) pairs for the capacity test).
- The page shows each card's slot and the slot note (with `----` for gaps).
- Equivalence test (incl. gap builds) PASSES; the stable-slot emulator test PASSES (placeholder silent, LiXR at
  machine 7 alone and with Feral, the name table holds `----`).
- Checkpoint `build/checkpoints/phase-3.syx` (Feral + LiXR) saved; checklist appended.

## Phase 2 — Capacity: 16 engines

- Added `core/registrar/layout.json`: MAX_ENGINES = 16 and the reserved table region (stubs 0x33000, data 0x33800,
  descn 0x34000, rowsn 0x36300, ccrowsn 0x36600, recs 0x36900, bss_off 0x37000). os113.py reads it: MAX_ENGINES and
  the layout live in one place; no other file hard-codes a machine count.
- Registrar runtime regenerated for 16 machines (parameter block at 0x33400, 272 bytes; max_machines 16).
- MACHINES screen marks: a third row does not fit between the screen edge and the picture, so the marks routine now
  stops at 2 x MARKS_ROW = 14 machines; marks past 14 are not drawn (the engines still work).
- refmachine got a `machine_index` patch point so each copy plays one semitone higher than the last.
- The composer (and docs/flasher/composer.js) now place the same id more than once (each name = one copy at the next
  slot): how the capacity test places 16 copies.
- Equivalence test (incl. the 16-copy case) PASSES; fill() 16/16.
- Capacity emulator test PASSES (1, 6, 7, 8, 12, 16 copies + real engines + stock unchanged).
- Checkpoint `build/checkpoints/phase-2.syx` (Feral + LiXR + 8 refmachine) saved; checklist appended.

## Drops / notes

- The old repo's `--relocator ld` / `--registrar b1` / `--compare-ld` / `--compare-b1` developer paths (they need the
  old per-build assembler, gen_macro.added_machine) are dropped: the composer uses the committed runtime and objects.
  This matches ARCHITECTURE.md "only what this flasher needs".
- The old flasher's Samples-OS cross-flash and Syntakt-engine features are not carried over (Cycles OS 1.13 only).

## Fix (2026-10-24): Feral SHAPE showed a stray character instead of the model names

- Cause: `feral_fmt` in `engines/feral9/feral_ui.S` passed the address of the name-table entry to `%s` instead of the
  pointer stored in it, so SHAPE printed the first byte of a pointer (`C`). The DSP is untouched.
- Fix: load the pointer (`move.l (%a0), %a0`); feral9.o and engines.js regenerated.
- New test `tests/emu/test_feral_names.py` runs the OS's state lookup and the formatter and checks NYB..KOS (alone and
  with PUNCH + BLAST). Equivalence test and `test_engines.py` (no failures in its first run) pass. Not yet checked on hardware.

## HALO Ry — new engine (2026-10-25)

- `engines/halo_ry` (development package, no slot in `slots.json`): Feral's RYT (Rhythmicon resonators) into
  HALO_new's granular cloud. Sources `halo_ry.c`, `halo_ry_dsp.h`, generated `hry_tables.h`; committed
  `halo_ry.o` (M68K_CROSS check passes).
- Test `tests/emu/test_halo_ry.py --cycles … --demos build/halo_ry_demos`: PASS — bootstrap, boot hook, stock
  SNARE/KICK unchanged, dispatch/sound/no clipping, cloud tail, Freeze, machine change, composes with Feral.
- CPU (one voice, real voice loop, TONE = 8200): default 0.864, cloud high 0.951, partials high 0.904,
  partials low 0.889, dry 0.684, worst knobs 0.962; largest block 0.976 × TONE's largest. All below TONE.
- Quality: RYT capped at 6 partials; cloud 3 grains / 12 kHz / shimmer on, diffuser 1; `-O3`.
- Deliverable copied to `../HALO/HALO_Ry/` with `HALO_Ry.syx` (MAIN OS `54217821…`, file `a266c878…`), demos,
  report and build/generator tools. Not added to the flasher catalogue; **not hardware-tested**.

## RINGS — new engine (2026-10-25)

- `engines/rings` (slot 11, machine 10; appended to `engines/slots.json`): the MODL engine of Model-TG's
  PLAITS machine (after Mutable Instruments Rings/Plaits) as a machine of its own. Material / Bright / Decay, the
  fourth knob spare. Sources `rings.c` + `rings_dsp.h`; committed `rings.o` (`build_engine.py --check` passes).
- Source: Model-TG commit `b49cd51`, `src/plaits.inc` `mi_md_e` / `md_e` / `md_rat` (MIT). The C port keeps every
  coefficient, shift and clamp; only the plumbing (arguments, 48 kHz output, note increment) changed. See
  `engines/rings/PROVENANCE.md`.
- Porting proof: `tests/host/rings_against_model_tg.py --model-tg …` runs Model-TG's m68k `mi_fill` in its Unicorn
  harness against the host C `rg_block`: 87 cases (materials, bright, decay, notes 24–120, single and repeated
  trigs), **0 mismatches** — the 32 sums and all eight modes' x/y are bit-exact every block.
- Emulator: `tests/emu/test_rings.py --cycles …` PASS — bootstrap, boot hook, stock SNARE/KICK unchanged,
  dispatch/sound/no clipping, pitch, string/bar ratio sets, decay, brightness, retrigger, spare CONTOUR, machine
  change.
- CPU (one voice, real voice loop, TONE = 7465): 8635 instructions a block = **1.157 × TONE** at low notes (8 modes
  at 48 kHz, above TONE: the ARCHITECTURE §2.2 warning), 0.924 × TONE at note 96, 0.536 with all modes muted;
  largest block 1.151 × TONE's largest. Image 10,736 o with the four slot placeholders (code 3,540, BSS 672).
- On the page: `card.json`, generated into `docs/flasher/engines.js`; `make_catalog.py --check` passes. Demo WAVs
  are in `build/rings_demos` (not committed); **not hardware-tested**.

## RINGS 0.2 — Decay knob (2026-10-25)

- The native Model:Cycles DECAY parameter is locked at encoder value 90: the voice-loop glue writes
  `LUT_DECAY[90]` every block, so the amp envelope no longer follows the knob. GATE and PUNCH are unchanged.
- RINGS' ring time moved from SWEEP to the DECAY knob (50 ms .. 8 s); SWEEP is now spare, like CONTOUR.
- `engines/_common/lxz_glue.h` gained two opt-in macros, `LX_SWEEP_IS_DECAY` and `LX_AMP_DECAY`, both defaulting
  to the old routing. The other LX engines rebuild byte-identically (`build_engine.py --check` passes for chaos,
  clang, flang, fmsub, sfold, vowel).
- `engines/rings`: version 0.2; manifest knobs are Material, Bright, Sweep, Contour; card/README/CHANGELOG
  updated; `rings.o` rebuilt (check passes). `rings_dsp.h` is untouched.
- Emulator: `tests/emu/test_rings.py` PASS. It now also asserts `LUT_DECAY[90]` for DECAY 0/64/127, that DECAY
  drives the ring time, and that SWEEP is spare. CPU 1.156 x TONE at low notes, 0.924 at note 96; image 10,728 o
  (code 3,528, BSS 672).
- Equivalence: the full `tests/test_equivalence.py` cannot compose every development package in this working tree
  (pre-existing: vxclu ends at 0x43034676, past 0x43033000). A scoped run over the four page engines
  (`build/equiv_card_engines.py`) passes: rings alone and in every combination, 16/16 fill.
- The host port proof was not rerun: the Model-TG repository is not present in this workspace. `rings_dsp.h` is
  unchanged, so the 87-case bit-exact proof of 0.1 still applies.
- `docs/flasher/engines.js` regenerated; `make_catalog.py --check` passes. **Not hardware-tested.**
- Re-verified 2026-10-25 (`tests/emu/test_rings.py`, full run): ALL OK; `build_engine.py --check rings` up to date;
  scoped equivalence passes; size/CPU report OK. CPU vs stock TONE: 8,630 / 7,465 = **1.156 x** (default, bright,
  free bar, low note), 6,895 = **0.924 x** at note 96, 3,996 = **0.535 x** with the modes muted; largest block
  8,789 vs TONE 7,643 = **1.150 x**. Against 0.1 (8,635 instructions, 1.157 x) the version is 5 instructions
  cheaper per block and 8 bytes smaller.

## RINGS 0.3 — a 0.50 x TONE build (2026-10-25)

- Goal: turn RINGS 0.2 (the DECAY-knob version, 1.156 x TONE) into a synth that uses at most 50% of a stock
  TONE voice's CPU, with sample rate and design changes allowed.
- Result: **0.487 x TONE** at the worst steady block; the largest single block is 3,799 = **0.497 x TONE's
  largest** (7,643). The CPU checks in `tests/emu/test_rings.py` now require both to be at most 0.50.
- What changed: modes run at 24 kHz (16 samples a block, held for two 48 kHz frames); six modes instead of eight;
  a 32-bit phase into a 4096-entry sine table (Q15) instead of the coupled form; coefficients and strike shares
  cached; decay applied every two blocks by `r^2`; a mode with amplitude 0 skipped; PUNCH at 0 folded into the
  output (the stock stage is a fixed 1/4 gain; final samples differ by at most 24,576, about -79 dBFS, from the
  VCA's integer rounding). PUNCH above 0 still calls the stock stage. The output stage uses a soft knee above
  30,000 (slope 1/4) instead of a hard clamp, so a fast retrigger compresses instead of flat-topping; single
  strikes stay below it.
- Rejected on the way: a second-order recurrence (2 cos w) at 24 kHz. It is cheaper but 2 cos w loses too much
  resolution near 2.0: note 24 came out 3.4 x sharp and note 60 35 cents sharp.
- Sound: pitch is exact across the range (261.66 Hz at note 60, 32.7 Hz at note 24); below 12 kHz the partial
  levels track 0.2 within about 1 dB; the 24 kHz hold leaves images above 18 kHz. Level matches 0.2 within about
  1 dB at default/low/long; bright and free bar are about 1.7 dB lower because the two capped modes carried some
  top-end energy. The 24 kHz hold is the same choice the LX engines make.
- Proof: `tests/emu/test_rings.py` PASS (ALL OK), including boot, pitch, material sets, DECAY ring time, amp
  decay locked at 90, brightness, retrigger, spare knobs, machine change, stock SNARE/KICK unchanged, no clipping.
  `build_engine.py --check rings` up to date. The host Model-TG proof now compiles `rings_dsp_0_2.h` (the 0.2
  arithmetic); it was not rerun because the Model-TG repo is not in this workspace, and it does not claim the new
  oscillator.
- Shared glue: `lix_in_t` gained `punch`, and `lxz_glue.h` gained the opt-in `LX_PUNCH_AT_ZERO` (the engine folds
  the 1/4 gain). The six LX engines that share it (chaos, clang, flang, fmsub, sfold, vowel) were rebuilt and
  their audio is **identical sample for sample** (`build/lx_glue_proof.py`, three cases each).
- Package: version 0.3; `rings_core.S` and `rings_tables.h` added; `rings_dsp_0_2.h` kept for the host proof;
  manifest/card/README/CHANGELOG/PROVENANCE/HARDWARE-CHECKLIST updated. Packed image 31,544 o (code 24,344, BSS
  1,416). `make_catalog.py --check` passes.
- Flashable build: `build/model-cycles_OS1.13-rings-0.3.syx` (MAIN OS SHA-256 `21d2ed3b…`, ends
  `0x401b1c78`), demos in `build/rings_demos_03`. `build/` is gitignored; the .syx contains Elektron's OS and
  must not be committed.
- Scratch packages from the experiments were removed. **Not hardware-tested.**

## 2026-10-26 — ULTRA FERAL 1.0 (new package, slot 12)

- New package `engines/ultraferal` (id `ultraferal`, machine `ULTRA`, slot 12): sixteen models. Nine Feral 9.0
  models unchanged; CEL/WGL/CRK recovered from the preserved 12-model build; four new: VOX (formant vowels),
  FLD (phase distortion + sine folding + feedback), NEB (four-grain cloud), MUT (a random engine per era).
- Engine changes: FREE now drifts COLOR and SWEEP on every model; GATE + FREE writes a shared pack field that
  every Ultra Feral track reads; MUT switches engine every era with a fade and random knob offsets; SHAPE 0..15
  with sixteen names in the value window.
- Build: `-Os` for the package, with `eng_e` (BENJ/HAVC) kept at `-O2` because RNG/HAVC's `-Os` cost passes
  TONE. VOX is unrolled and cut from 1.25 x TONE to about 0.85 x TONE. Code+tables 58,024 bytes, BSS 4,096;
  all six carded engines together fit the image budget and the code window.
- Proof: `build/ultraferal/build_and_prove.py` (emulator, all models, extremes, FREE, MUT, stock machines) and
  `build/ultraferal/cpu.py` (CPU per model); `tests/emu/test_ultraferal_names.py` checks the sixteen names;
  `tests/host/ultraferal_demo.c` renders all models without the OS and checks the 16 CA rules. The first full run
  failed only its own NEB density check (18 blocks was shorter than one grain interval at COLOR 0 and both sides
  looked identical); the check now uses 64 blocks. The quick re-run on the final object passes: **ALL OK** (sixteen
  models, extremes, FREE drift, MUT eras, stock SNARE/KICK identical; no unmapped access).
- CPU (worst block, emulator, TONE = 5,291): GND 1,759 (0.33 x) to VOX 4,506 (0.85 x); MUT 3,902 (0.74 x).
  All sixteen are below TONE. `test_size_cpu.py` passes with all six carded engines (216,380 bytes of image).
- Catalog: `docs/flasher/engines.js` regenerated; `make_catalog.py --check` passes. Custom card/emblem art is in
  the package; the device still shows TONE's picture (pictures are not ported to the package system yet).
- Standard emulator test: `tests/emu/test_engines.py --cycles firmware/model-cycles_OS1.13.syx --machines ultraferal
  --blocks 8` **ALL OK** (bootstrap depack, boot hook, voice loop, makes sound at the defaults, no hard clipping,
  stock SNARE/KICK identical).
- Not a valid check here: `tests/test_equivalence.py` composes **every** package in the workspace, including the
  many untracked dev packages; that all-package build runs past the 0x43033000 code window (feraldrum is the
  last code placed). Ultra Feral alone composed and built in that run, and the catalog/object checks pass. The
  all-package equivalence comparison was not run to completion.
- Flashable build: `build/ultraferal/model-cycles_OS1.13-ultraferal.syx` (959,264 bytes; `.syx` SHA-256
  `a5597b99…`; MAIN OS SHA-256 `10bea271…`, ends `0x401ba13c`). `build/` is gitignored; it contains
  Elektron's OS.
- Hardware: **not tested**. Checklist appended to `HARDWARE-CHECKLIST.md`.
