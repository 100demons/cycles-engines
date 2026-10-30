# Cycles Engines

A web flasher for the Elektron **Model:Cycles, OS 1.13 only**, where **any combination of engines can be flashed
together**. You tick engines, drop your own official OS file, the page builds the firmware in the browser and sends
it over USB. **Nothing from Elektron is ever committed or uploaded.**

An *engine* is a package (one folder, one manifest, one relocatable object). The composer places the ticked engines
in the firmware, registering them together after the six stock machines, in the order you tick them.

- **The page**: `docs/flasher/` — published on GitHub Pages. Drop `model-cycles_OS1.13.syx`, tick engines, flash.
- **The command line**: `python3 core/compose.py --cycles model-cycles_OS1.13.syx --machines feral,lixr`
  builds a `.syx` with `python3` alone (no toolchain).
- **The reference**: `ARCHITECTURE.md` is the source of truth. `PROGRESS.md` tracks a long run.

## Credits

Built on the Modded-Cycles toolchain (18nelli18 and contributors, MIT) and model27 (durchgemacht, MIT); the low-level
code (mtlib) is drumkilla's (MIT). Carried-over low-level code: the SysEx / ELE3 container / aPLib / HMAC code
(`core/mtlib`, `docs/flasher/builder.js`), the Web MIDI transfer (`docs/flasher/flasher.js`), the relocator, the
registrar runtime and the composer. See `LICENSE`, `core/mtlib/LICENSE` and each engine's `LICENSE`. No Elektron
firmware is included.

## Making an engine

See `ENGINES.md` (added in phase 6) and `tools/new_engine.py`.
