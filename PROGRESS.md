# Progress — Cycles Engines

How a long run resumes after an interruption. Read ARCHITECTURE.md first, then this file. Keep it up to date after
every step: phase, step, what passed, what failed, what's next.

## Status

- **Current phase**: 2 (Capacity — 16 engines) — done, committing/merging.
- **Last result**: Phase 2 capacity test passes (1, 6, 7, 8, 12, 16 copies: boot, dispatch, distinct pitch, stock
  unchanged; real engines next to refmachine copies). Equivalence (incl. 16 copies) and size/CPU report pass.

## Environment (verified)

- `gh` logged in as `100demons` (repo, workflow scopes).
- `node` v24.22.1, `python3` 3.12.8 with `unicorn` 2.1.4 and `numpy` 2.5.4.
- m68k toolchain: `m68k-elf-gcc 16.2.0` at `/Users/ooo/Projects/m68k-elf-16.2/bin` (set `M68K_CROSS` or add to PATH).
- Old repo (read-only reference): `/Users/ooo/Projects/cycles-flasher`.
- Official OS: `/Users/ooo/Projects/model-cycles/firmware/model-cycles_OS1.13.syx`
  (section_sha256 `cc99d4f0…ee98`, stock_syx_sha256 `44fe5862…640c`).

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

1. Commit Phase 2, tag `phase-2`, merge to main.
2. Phase 3 — Stable slots (branch `phase-3-slots`).

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
