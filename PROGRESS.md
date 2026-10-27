# Progress — Cycles Engines

How a long run resumes after an interruption. Read ARCHITECTURE.md first, then this file. Keep it up to date after
every step: phase, step, what passed, what failed, what's next.

## SHEEN — hybrid-rate hi-hat / metal percussion engine (branch `engine-sheen`)

- **Milestones**: M0–M4 done, with two checks failing and one capacity finding. Next: owner decision on CPU,
  the roll step-ratio check, and the shared code window.
- **Branch**: `engine-sheen`, created from the `engine-crack` working tree. CRACK's uncommitted files are left
  untouched; only SHEEN's files, this section and the shared files named in the spec are committed. Never merged.
- **Environment**: m68k-elf-gcc 16.2.0 at `~/m68k-elf-16.2/bin`; python 3.12.8, numpy 2.5.4, unicorn 2.1.4;
  node v24.22.1; official OS SHA-256 `44fe5862…640c`.

### M0 — baseline (passed)

- TONE trig **8,982**, steady **7,403 / 6,901 / 6,901 …**; ceiling **TONE steady 6,901**, 0.90 × = **6,211**.
- Stock METAL peak (all knobs 64, DECAY 40, note 60): **522,540,549** = **−6.26 dBFS** (FS `1 << 30`, the OS's
  halved output), RMS 0–100 ms **−19.01 dBFS**.
- Host/emulator compare in the halved domain (`>>1`, 0 dBFS = `1 << 30`). Slot decision: 11 = rings, 12 =
  ultraferal, 13 = feraldrum; **SHEEN = slot 14 / machine 13** (slot 11 stays reserved for CRACK).

### M1 — hero pair on the host (passed except the roll step check)

Files: `engines/sheen/sheen_dsp.h`, `sheen_tables.h` (generated), `gen_sheen_tables.py`; `tests/sheen/sheen_host.c`,
`test_sheen_sound.py`. Host build: `cc -std=c99 -O2 -Wall -Wextra -Werror`; the default macOS SDK link is broken
(`tapi error: malformed file`), so the harness retries with `MacOSX26.sdk` and says so.

Real hero output:

```
PASS  9.1.1 peak -6..-0.5 dBFS: -4.54 dBFS
PASS  9.1.2 >=70% energy above 5 kHz: 92.1 %
PASS  9.1.2 100-1000 Hz >=24 dB below 6-11 kHz: 25.48 dB
PASS  9.1.2 2-4.5 kHz >=4 dB below 6-11 kHz: 10.41 dB
PASS  9.1.3 air 3..15 dB below 6-11 kHz: 5.08 dB
PASS  9.1.4 peak within 2 ms: 0.02 ms
PASS  9.1.5 -40 dB time 40..120 ms: 46.4 ms
PASS  9.1.6 round-robin correlation <0.95: 0.888
PASS  9.1.6 round-robin RMS within 1 dB: 0.01 dB
PASS  9.1.7 open -40 dB time 400..1100 ms: 504.8 ms
PASS  9.1.8 bloom 15-40 ms within 6 dB of 0-10 ms: -1.92 dB
PASS  9.1.9 centroid falls >=5% by 300-400 ms: 10851 -> 10197 Hz
PASS  9.1.10 open >=65% energy above 5 kHz: 81.6 %
PASS  9.1.10 open 100-1000 Hz >=20 dB below 6-11 kHz: 20.69 dB
PASS  9.1.11 choke >=25 dB: 28.57 dB
PASS  9.1.12 roll peaks rise (one dip <=1.5 dB): 0 dips, max 0.00 dB
FAIL  9.1.12 roll step <=1.2 x first onset: 10.278 x
PASS  9.1.13/14 clean and deterministic
```

The roll's **step-ratio check is a real FAIL**, not hidden. The check compares the largest sample-to-sample step
around a high-velocity retrig with the largest at the first onset, which is at velocity 40. A high-frequency hat's
natural slope scales with amplitude, so the high-velocity hits are about 10× larger. A limiter that would pass it
flattens the peaks and breaks the "each hit higher" part or the velocity/timbre checks; I did not fake it. This needs
an owner decision (see §11-style note at the bottom of this section).

### M2 — device proof (passed)

`engines/sheen/sheen.c` is the bridge: PUNCH's trig (`v+0x38`), note end (`v+0x3c`), machine marker (`v+0x2c`),
velocity word (`0x40fe4cdc + 2*track`), PITCH/FINE and the four knobs. `sheen_update`/`sheen_render`;
`manifest.json`; `card.json`; committed `sheen.o` (`python3 core/build_engine.py sheen` passes).
`build/sheen-core_OS1.13.syx` is the M2 fallback (also copied as `build/sheen_OS1.13.syx`).

`tests/emu/test_engines.py --machines sheen` — **ALL OK** (boot, dispatch, sound, no hard clipping, stock
machines sample-identical). The same test with `feral9,punch,blast,sheen` — **ALL OK** (payload 90,696 bytes,
`0x401c0388 < 0x40200000`).

### M3 — macros, one group at a time (passed)

Every group was added and the host checks run after it: DECAY/reverse/GATE/velocity; SIZE/bloom/wash/darkening/
shimmer; COLOR/metal share/RING/cowbell; TICK/attack/bursts/chirp/spike; HEAT/glue/drive/clip/tilt/crush/air/makeup.
Real tail of the full macro run:

```
131 checks, 0 failed
ALL OK
```

Notable measured values: COLOR flatness 0=0.499, 64=0.500→after the final table 127=0.070 (the check is
127 < 64 < 0); SIZE centroid 0=12690 → 127=10929 Hz, length 0=23 ms → 127=192 ms; TICK onset/tail 0=−34.20 →
96=−13.05 dB, 3 bursts at 0, 127 metal base 1609.5 Hz; HEAT RMS within ±3 dB at every point, crest 0=21.7 →
96=20.7 dB, hold 3 / bits 7 at 127; DECAY closed 14 ms → 547 ms, open 117 ms → 341 ms, reverse peaks at 77/522/863
ms and cuts in 0.2 ms; PUNCH 505/46 ms; velocity 30↔127 19.31 dB, centroid 9804→11379 Hz; GATE 571 dB.

### M4 — recipes, package, checks

**Recipes:** `47 checks, 0 failed, ALL OK` (all 17 render cleanly, RMS > −40 dBFS; note-length GATE 43.86 dB; trap
22.0 ms; shaker 2 bursts; tambourine 8 bursts / 91.9 % >4 kHz; ride 1.2 s+; crash 1.5 s+ / 91.6 %; cowbell peaks
795 and 540 Hz; reverse peak 990 ms; crushed hold 3/7; laser 1541–1609 Hz; gong 1.4–1.6 s −7.5 dB).

**Emulator sample-exact:** host vs device after the OS's `>>1`: **22 cases, ALL OK** in the final run
(`build/sheen_exact.log`): hero_closed, hero_open, choke_pair, soft_hit, roll, all 17 recipes and a machine change,
all sample-identical. The test feeds the OS trig one block before the host script because the OS sets `v+0x38` in
the following block, exactly as PUNCH reads it.

```
ok    hero_closed: 9600 samples identical
ok    hero_open: 28800 samples identical
ok    choke_pair: 22400 samples identical
ok    roll: 16000 samples identical
ok    recipe04_trap_closed: 19200 samples identical
ok    recipe17_gong: 32000 samples identical
ok    recipe14_reverse_cymbal: 64000 samples identical
...
ALL OK
```

**CPU (real voice loop, same run): FAIL**

```
TONE trig 8982, steady 6901 instructions/block
SHEEN worst 24804 = 3.594 x TONE steady
hero trig 9547, steady 17008, silent 9571 (TONE silent 9006, marginal 565)
six SHEEN 137385, six TONE 45818 (2.998 x)
```

The engine is over the 0.90 × TONE target by a wide margin. The first ladder cuts (crush, tilt, shimmer 128→64,
RING two pairs, shimmer removed, metal at 24 kHz) cannot bridge a 3.6× gap and would remove protected features or
the 48 kHz identity; I stopped and report it rather than cut the sound. **No cut-ladder step was applied.**

**Size:** `--dump-size` = **640 bytes per voice** (including the 128-sample shimmer buffer) — at the limit. The
composed package is **12,436 bytes of code and tables**, **3,960 bytes of variables for six tracks**, and **20,576
bytes added to the image**. The all-carded-engine `test_size_cpu.py` run fails on the **shared code window**:
`!! sheen: code and tables end at 0x43036004, past 0x43033000`. The six carded engines already used 208,728 of the
208,896-byte region; SHEEN cannot be added without a shared capacity decision. The required four-engine build fits.

**Equivalence:** `tests/test_equivalence.py` places every package and composes each offered engine alone; it fails
only on the **all-seven-carded** composition at the same code-window limit:
`!! sheen: code and tables end at 0x43036004, past 0x43033000`. The required selections and `sheen` alone pass the
per-engine placement and the composer's own build. This is the same repository capacity issue, reported.

**Page/catalog:** SHEEN appended at slot 14; `tools/make_catalog.py` regenerated `docs/flasher/engines.js`
(`--check` passes).

**Builds (gitignored, contain Elektron's OS):**
`build/sheen_OS1.13.syx` (909,856 bytes, SHA-256 `47dd73e94be23dc5…`),
`build/sheen-core_OS1.13.syx` (same bytes), and `build/sheen-all_OS1.13.syx` (feral9, punch, blast, sheen;
977,568 bytes, SHA-256 `00a5c849f311ea14…`). Demos in `build/sheen-demos/`: `00-groove.wav` plus all 17 recipes
and the four macro sweeps (54 files). Hardware checklist appended to `HARDWARE-CHECKLIST.md`.

**Decisions / deviations logged**

- The five-point macro interpolation uses the real 0/32/64/96/127 breakpoints (the last segment is 96..127, not
  96..128), so the 127 column is reached exactly.
- Envelope times use a Q16 log2 exponent and a split multiply for ms × Q12; without it, long T60s silently
  overflowed or rounded to zero (ride/crash/gong lost their tails).
- Round robin: the start phases are random **per voice** (fixed across its trigs) and the core noise is reseeded at
  each trig, so a roll's peak follows velocity; hits vary by detune (±3 cents), cutoff (±0.5 %), decay (±1 %), tick
  decay (±3 %) and level (±0.1 dB). The round-robin correlation is 0.888 < 0.95. This is a deliberate change from
  "random phases on every trig"; with per-trig phases the roll's peak varied by up to 8 dB.
- Velocity carve: level `0.08 + 0.92·v²`, tick `×v³`, air `×v²` (the starting text said tick ×v² and air
  0.4+0.6v; the harder tick/air carve is what makes the loud hit's centroid higher).
- The trim values after tuning are in `engines/sheen/README.md`; the final macro tables are there too.

**Still not passed / owner decisions**

1. **CPU**: 3.59× TONE worst, 3.00× six voices. This is a stop-and-report item. A real fix needs a different
   implementation (fewer instructions per sample), not a ladder cut.
2. **Roll step ratio**: 10.28× vs the 1.2× check, a consequence of comparing a velocity-40 first onset with
   velocity-120 natural slopes. A limiter would flatten the peaks/velocity; I did not fake it.
3. **Shared code window**: all seven carded engines no longer fit `0x43033000`. The owner must drop an engine,
   raise the window, or keep SHEEN off the all-engines page selection.

### §10 report at a glance

- **What works:** the hero closed/open pair, the choke, all four macros, all 17 recipes, the 1/32 roll (peaks) and
  every sample-exact device case. The closed-after-open and GATE chokes work. The page/catalog and four-engine build
  work. Nothing is hardware-tested.
- **CPU (instructions/block, same emulator run):** SHEEN worst **24,804** (3.594 × TONE steady 6,901), hero steady
  **17,008**, silent marginal **565** over TONE silent; six active SHEEN **137,385** vs six TONE **45,818**
  (2.998 ×). **FAIL**; no ladder cut applied.
- **Size:** code + tables **12,436 bytes**; variables **640 bytes/voice** (3,960 for six tracks); image added
  **20,576 bytes**. The all-seven-carded build exceeds the shared code window by 12,292 bytes; the required
  four-engine build fits.
- **Checks:** the combined host run is **196 checks, 1 failed**: §9.1 17/18 (roll step ratio FAIL 10.278×,
  everything else PASS), §9.2 **131/131 PASS**, §9.3 **47/47 PASS**. §9.4 exact **22/22 PASS**; level
  +1.71 dB PASS; CPU/six voices FAIL; size/code window FAIL; `test_engines.py` alone and with
  `feral9,punch,blast,sheen` PASS.
- **Where:** branch `engine-sheen`, slot **14** (machine 13). `build/sheen_OS1.13.syx`,
  `build/sheen-core_OS1.13.syx`, `build/sheen-all_OS1.13.syx`; `build/sheen-demos/00-groove.wav` then the 17
  recipes and macro sweeps. Hardware checklist appended.
- Nothing is hardware-tested. Nothing is merged to `main`.

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

## 2026-10-27 — FMSUB 0.2: SUB fixed at 127, INDEX on CONTOUR (test build)

- Requested change: the sub-oscillator is fixed at 127 (full level, no longer a knob), INDEX moves from the
  SHAPE encoder to CONTOUR, and SHAPE is left unused.
- `engines/fmsub` is version `0.2-test`: `fmsub.c` hardcodes the sub gain at knob 127 (`SUB_127`) and reads the
  FM index from `in->contour`; the manifest knobs are Ratio (COLOR), Shape (spare), Sweep, Index (CONTOUR);
  `fmsub.o` rebuilt (4,284 bytes of code and tables, 480 BSS).
- Standalone machine 6 build and proof: `build/newdrums/build_and_prove.py` **ALL OK** — bootstrap depack, boot
  hook, sound/no clipping/no unmapped access, SHAPE 0 == SHAPE 127 sample for sample, CONTOUR 0/64/127 differ,
  CONTOUR 0 still sounds, stock SNARE/KICK identical, and the new build is sample-identical to the old 0.1
  object at the equivalent settings (old SHAPE = new CONTOUR, old CONTOUR = 127) for INDEX 0, 64 and 127.
- Deliverable: `/Users/ooo/Desktop/NEWDRUMS/01-FMSUB-sub127-index-contour.syx` (901,792 bytes; SHA-256
  `a33c301d94a0b81241a908d7aa81b1526db3974259663c0b4bb77ea59e9053dc`; MAIN OS ends `0x401ac904`). The model27
  `tools/flash.py --verify` container check passes. `build/` is gitignored; it contains Elektron's OS.
- **Not hardware-tested.**

## 2026-10-27 — MET extracted from Feral Drum (NEWDRUMS)

- New development package `engines/met` (id `met`, machine `MET`): the Benjolin metal drum voice of Feral Drum,
  one model only. The SHAPE encoder is not read, so it has no effect and is free.
- `met_tables.h` is the subset of `feraldrum_tables.h` that MET uses, copied verbatim. `met_dsp.h` is the
  Benjolin engine plus Feral Drum's shared drum envelope, DC blocker and soft clipper, with the model dispatch
  removed. `met.c` is the voice-loop glue with the SHAPE read and descriptor-name rewriting removed.
- Object: `core/build_engine.py met --check` up to date (13,976 o; code+tables 11,356, BSS 1,776).
- CPU: 5,993 instructions per 32-sample block = **0.989 x TONE** (6,061), emulator, 2026-10-27.
- Proof: `build/newdrums/build_and_prove_met.py` **ALL OK** — boot, depack, sound, no clipping, no unmapped;
  SHAPE 0/64/127 render identically; MET and Feral Drum's MET are sample-for-sample identical at two settings;
  COLOR and CONTOUR change the sound. `relocate.place` equivalence includes met (23/23 placements).
- Standalone build: `build/newdrums/model-cycles_OS1.13-MET.syx` (909,984 bytes; SHA-256 `057c9576…`, MAIN OS
  ends `0x401ae4a0`), MET at machine 6 / slot 7. `tools/flash.py --verify` passes (Elektron, product 0x11,
  checksums OK). The package is also copied to `build/newdrums/met/` (the NEWDRUMS folder).
- Not on the flasher page (no card.json, not in slots.json). **Not hardware-tested.**

## 2026-10-27 — MET 1.1: COLOR spans 80..127

- MET's COLOR knob is remapped at the input: knob 0 = internal COLOR 80, knob 127 = internal 127, linear in
  8.8. The DSP and tables are untouched; `met.c` has the mapping (`met_color`).
- CPU: 5,876 instructions/block at COLOR 64 (0.969 x TONE); worst 6,871 at COLOR 127 (1.134 x TONE, above
  TONE: the §2.2 warning). Code+tables 11,476 bytes.
- Proof rerun **ALL OK**: SHAPE 0/64/127 identical; new COLOR 0/64/127 sample-for-sample identical to Feral
  Drum MET at internal COLOR 80/103.684/127; COLOR and CONTOUR still change the sound.
- Standalone build rebuilt: `build/newdrums/model-cycles_OS1.13-MET.syx` (909,984 bytes; SHA-256 `f34facd5…`,
  MAIN OS ends `0x401ae518`). `build/newdrums/met/` resynced. Not hardware-tested.
