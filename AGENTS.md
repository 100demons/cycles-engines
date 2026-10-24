# Agents: read ARCHITECTURE.md first

`ARCHITECTURE.md` at the root is the source of truth for this repository. Read it completely at the
start of every session and follow it. If something in it turns out to be wrong, say so and propose an
edit; don't silently work around it.

The working rules, from ARCHITECTURE.md §8:

- One branch per phase; merge to `main` only after the tests in §7 (1–3) pass. `main` publishes the
  beta site of this repo; the old flasher stays untouched until launch.
- Change `core/compose.py` and `docs/flasher/composer.js` together, never one alone.
- Never commit Elektron firmware or a built `.syx`; builds go to `build/` (gitignored).
- Never change an engine's DSP code while porting it. Prove it: render the same notes through the old
  build and the new one in the emulator and compare the audio.
- If a check can't run (missing tool, file or hardware), say so; never skip it silently.
- If something fails three times, stop and report what failed and what you tried.
- Keep credits and license files for everything carried over.

Also see `PROGRESS.md` at the root: it is how a long run resumes after an interruption. Read it after
ARCHITECTURE.md and continue from where it says, and keep it up to date after every step.

## Environment

- m68k toolchain: set `M68K_CROSS` to the prefix of an `m68k-elf-` or `m68k-linux-gnu-` toolchain (for
  example `export M68K_CROSS=/path/to/bin/m68k-elf-`). Only `core/build_engine.py --check` and engine
  porting need it; the composer and the build use the committed `.o` objects and run with python3 alone.
- Python 3 with `unicorn` and `numpy` for the emulator tests; `node` for the web-composer equivalence
  check; `gh` logged in to the owner's account for GitHub.
