# Progress — Cycles Engines

How a long run resumes after an interruption. Read ARCHITECTURE.md first, then this file. Keep it up to date after
every step: phase, step, what passed, what failed, what's next.

## Status

- **Current phase**: 5 (Port PUNCH, BLAST and SAM) — done, in review. The descriptor.state fix (all four knobs) and
  the slot-relative turn hook are in and verified; the custom pictures remain not ported (engines keep TONE's).
- **Last result**: the knob displays and the stepped SWEEP now work for Feral, BLAST, PUNCH and SAM; the DSP is
  byte-for-byte unchanged from the old standalone builds. The remaining question is the LiXR slot (8): LiXR was
  removed from the catalog but is still in `engines/slots.json`, so a build shows a blank `----` placeholder at slot 8.

## Environment (verified)

- `gh` logged in as `100demons` (repo, workflow scopes).
- `node` v24.22.1, `python3` 3.12.8 with `unicorn` 2.1.4 and `numpy` 2.5.4.
- m68k toolchain: `m68k-elf-gcc 16.2.0` at `~/m68k-elf-16.2/bin` (set `M68K_CROSS` or add to PATH).
- Old repo (read-only reference): `~/Desktop/cycles-work/cycles-rename`.
- Official OS: `~/Desktop/cycles-work/cycles-engines/firmware/model-cycles_OS1.13.syx`
  (stock_syx_sha256 `44fe5862…640c`).

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
