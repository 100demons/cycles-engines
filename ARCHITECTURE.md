# Cycles Engines — architecture

This file is the source of truth for this repository. Every AI session reads it first and follows it. If something
here turns out to be wrong, say so and propose an edit; don't silently work around it.

## What this is

A web flasher for the Elektron Model:Cycles, **OS 1.13 only**, where **any combination of engines can be flashed
together**. The user ticks engines, drops their own official OS file, the page builds the firmware in the browser and
sends it over USB. Nothing from Elektron is ever committed or uploaded.

The owner (100demons) is not a developer. Report in plain language, say what was tested and what wasn't.

## Where this comes from

The previous flasher (`100demons/cycles-flasher`, a fork of durchgemacht's model27, itself downstream of
18nelli18's Modded-Cycles) is the **reference**, not the base. It stays live and untouched until this one launches.

What was wrong with it, and what this design fixes:

| Old problem | Fix here |
|---|---|
| PUNCH, BLAST and SAM were built as finished one-engine firmware patches, all assuming they are the 7th machine at the same addresses, so any two overwrite each other | Every engine is a **package**; nothing in an engine knows its slot or address (§2) |
| Hard cap of 6 added engines, from the table layout inherited from Modded-Cycles (descriptor room 109 = 76 stock + 5 × 6) | **One capacity constant, `MAX_ENGINES = 16`**; every table is sized from it (§3) |
| A saved sound stores its slot number; slots changed with each selection, so sounds played the wrong engine | **Stable slots** with a silent `----` placeholder for gaps (§4) |
| Picture slot 6 and descriptors 76–80 hard-coded by engines | Slot-relative **patch points** for pictures and knob displays (§2.3) |
| `one_of` / `excludes` hacks on the page to hide conflicts | No hand-written exclusions; the composer refuses a build and says why |
| ~5,500 lines of Modded-Cycles generators behind the composer, a 6,855-line generated `tweaks.js`, hundreds of unrelated mods | Only what this flasher needs |

Pre-building every combination (as Avery's fork does: 1,536 baked recipes in an 8.7 MB file) is **not** used here:
the number of builds doubles with every engine. Builds are composed in the browser from packages.

## 1. Principles

1. **An engine is a package.** One folder, one manifest, one relocatable object. If an engine needs its slot, its
   descriptor numbers or its picture index, it gets them from a patch point that the composer fills.
2. **One composer, two implementations.** `core/compose.py` is the reference; `docs/flasher/composer.js` must give
   the same bytes. A test compares them on every change; they are always changed in the same commit.
3. **Capacity is one constant.** `MAX_ENGINES` lives in `core/registrar/layout.json`. No other file hard-codes a
   machine count.
4. **Reuse proven low-level code, rewrite the rest.** Carried over (MIT, credits kept): the SysEx / ELE3 container /
   aPLib / HMAC code (`builder.js`, `mtlib`), the Web MIDI transfer (`flasher.js`), the relocator, the registrar
   runtime and the composer. Rewritten: repo structure, page logic, catalogue generation, capacity, slots.
   The reverse-engineered OS 1.13 addresses are never re-derived from scratch: they come from the old repo.
5. **The page looks the same as the old one.** Same HTML structure, CSS and assets; new logic underneath.
6. Addresses are virtual addresses of OS 1.13 (`BASE = 0x40000400`). Slot 7 is the first added engine; internally
   the OS numbers it machine 6 (machines 0–5 are the stock six). Always say which numbering you mean.

## 2. The engine contract

### 2.1 Folder

```
engines/<id>/
  manifest.json      cycles-mod/0.1 schema (durchgemacht's Cycles Mod Standard draft) — keeps engines portable
  src/               C / asm sources and generated tables
  <id>.o             committed relocatable object (ld -r), rebuilt by core/build_engine.py --check
  card.json          page text: label, description, status, credit, license file, video (optional)
  README.md, CHANGELOG.md, LICENSE
```

### 2.2 Rules

- Exactly 4 knobs (COLOR, SHAPE, SWEEP, CONTOUR positions) plus a DECAY default; PITCH, DECAY, PUNCH, GATE and FINE
  are the stock ones.
- `update` and `render` symbols; no fixed addresses anywhere in code or tables (only `R_68K_32` relocations).
- No OS hook unless it is declared in the manifest's `uses` and the composer knows how to place it per slot.
- Engine names: 5 letters or fewer.
- Budgets, reported for every engine on every build:
  - **CPU**: voice-loop cycles per voice, measured in the emulator, compared to stock TONE. Above TONE is a warning.
  - **Size**: code + tables + variables. All installed engines share the image budget in §3.

### 2.3 Patch points the composer fills

| kind | value |
|---|---|
| `machine_index` | the engine's OS machine number (slot − 1) |
| `descriptor_base` | its first knob descriptor |
| `picture_index` | its picture index (added in phase 4) |

Display overrides: a `descriptor.state` entry gives an engine one knob-state initializer for **all four knobs at
once** — never a single knob, and never a hard-coded descriptor number. The manifest carries one `uses` entry
`{ "point": "descriptor.state", "init": "<symbol>", "why": "…" }`; `init` is `init(knob) -> state*` where `knob` is
**0 = COLOR, 1 = SHAPE, 2 = SWEEP, 3 = CONTOUR** (the position among the engine's four knobs, *not* a descriptor
number). It returns that knob's interface-object pointer, or **0** to keep the generic SNARE object. The registrar
runtime calls `init(knob)` for any of the engine's four knob descriptors and falls back to the generic SNARE object
when it returns 0; Amp Decay (`first_desc + 4`) is never overridden. There is no per-knob `descriptor` field.

The knob-turn stepping for stepped SWEEP ratios (BLAST, PUNCH) is a separate, also slot-relative hook: a `uses`
entry `{ "point": "turn", "init": "<symbol>", "why": "…" }` whose `init` is `init(cur, delta) -> value` (the new
value, or −1 to keep the OS's own step). The registrar runtime's `turn` entry calls it for the engine's SWEEP knob,
never a hard-coded descriptor.

## 3. Capacity and memory

- Today (inherited): engine tables between `PAY + 0x33000` and `PAY + 0x36000`, `PAY = 0x43000000`. Descriptor
  room = 109 → 6 engines. Next walls: parameter rows/records (9), MACHINES page marks (two rows of 7 = 14 machines
  in all, i.e. 8 added).
- New: one reserved table region sized from `MAX_ENGINES = 16`: runtime + parameter block, data block, descriptors
  (76 + 5 × 16), parameter rows and CC rows (32 bytes × (6 + 16) each), records (0x60 × 16). Exact addresses are
  computed once in phase 2 and written to `layout.json`.
- MACHINES page marks: draw a third row only if measured to fit; otherwise marks past 14 machines are not drawn, and
  never drawn outside their area. The engines themselves still work.
- **Image budget**: the payload is stored after the OS image (`image_end = 0x401aa140`) and must end before
  `end_limit = 0x40200000`: about 344 KB for all engines' code and tables together. The five current engines use
  about 115 KB. The page shows the budget on every build. Raising the limit or compressing is a separate, measured
  decision, not assumed.
- Reference for moving the tables: the old repo's `tools/gen_syntakt_engines.py` (`LAYOUT`, `ROWS7`, `check_room`,
  `detours_asm`) and `tools/gen_registrar.py`; Avery's fork (aveycole/Modded-Cycles, `notes/118` and
  `tools/gen_machine_combinations.py`) moved the same tables to `PAY + 0x84000…0x89400`. Study it as a reference
  only: that repo has no license file, so don't copy its code without Avery's permission.

## 4. Stable slots

- `engines/slots.json`: one permanent slot per engine, from 7 upward. Entries are appended, never reordered, never
  reused. A removed engine keeps its slot as a placeholder forever.
- A build holds slots 7 … (highest ticked). Every unticked slot below that is filled with the shared, silent
  `----` placeholder engine (its code placed once, however many gaps). Measured cost per gap in the old repo:
  344–408 bytes (old repo `notes/62-level-b4-stable-machine-ids.md`, option A).
- Result: a sound or pattern saved with an engine finds the same engine in any later build that includes it.
- The page shows each engine's slot on its card.

## 5. Repository layout

```
ARCHITECTURE.md        this file
AGENTS.md              "Read ARCHITECTURE.md first", plus the working rules (§8)
engines/               packages (§2), slots.json, placeholder/, refmachine/ (test engine, never on the page)
core/
  registrar/           registrar.S, layout.json, frames generated for 1…MAX_ENGINES
  compose.py           reference composer
  relocate.py
  build_engine.py      compiles a package to its committed .o (m68k-elf-gcc or m68k-linux-gnu-gcc)
  os113.py             the stock-OS addresses and helpers the composer needs, extracted from the old generators
  mtlib/               SysEx, ELE3 container, aPLib, HMAC (drumkilla, MIT)
  build.py             command-line build: OS file + engine ids → .syx in build/ (gitignored)
docs/                  GitHub Pages root
  flasher/             index.html, style.css, app.js, builder.js, composer.js, flasher.js, engines.js (generated)
  assets/
tests/
  test_equivalence.*   composer.js == compose.py, byte for byte
  emu/                 Unicorn harness: boot, MACHINES page, per-slot dispatch and sound, save/reload
tools/
  make_catalog.py      engines/*/ → docs/flasher/engines.js
  new_engine.py        scaffold a new engine from the template (phase 6)
```

## 6. The page

- Copy the old `docs/flasher/index.html`, `style.css` and `docs/assets/` so it looks the same; rewrite `app.js`.
- Flow: drop OS file → verify it is official OS 1.13 (hash) → tick engines (up to `MAX_ENGINES`) → compose → build →
  flash (fast USB transfer or classic SysEx, as today).
- Shown on each build: slots used, image budget used, each engine's CPU against TONE.
- No `one_of`, no `excludes`. If a build can't be made, show the composer's reason.

## 7. Tests (all must pass before merging to `main`)

1. **Equivalence**: `composer.js` == `compose.py`, byte for byte: each engine alone, all engines, 20 random subsets.
2. **Emulator**: the build boots; the MACHINES page builds; every installed slot dispatches to its own
   update/render and makes sound; the six stock machines are unchanged; placeholders are silent; a sound saved on a
   slot reloads on the same engine after rebuilding with other engines.
3. **Size and CPU report** for every engine and the full build.
4. **Hardware** (owner only): checklist given at each stop. An automated pass is never reported as hardware-tested.

## 8. Working rules for AI agents

- Read this file at the start of every session.
- One branch per phase; merge to `main` only after §7 tests 1–3 pass. `main` publishes the beta site of this repo;
  the old flasher stays untouched until launch.
- Change `core/compose.py` and `docs/flasher/composer.js` together, never one alone.
- Never commit Elektron firmware or a built `.syx`; builds go to `build/` (gitignored).
- Never change an engine's DSP code while porting it. Prove it: render the same notes through the old build and the
  new one in the emulator and compare the audio.
- If a check can't run (missing tool, file or hardware), say so; never skip it silently.
- If something fails three times, stop and report what failed and what you tried.
- Keep credits and license files for everything carried over: Modded-Cycles (18nelli18 and contributors, MIT),
  model27 (durchgemacht, MIT), mtlib (drumkilla, MIT), the Elektroid protocol reference, and each engine's sources.
