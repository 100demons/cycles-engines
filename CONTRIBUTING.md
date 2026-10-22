# Contributing

Thanks for wanting to help. Bug reports, test reports and ideas are welcome in the Issues tab.

## Hard rules

- **No Elektron firmware.** Never submit an official OS file, or any `.syx` built from one. Patches only.
- **No secrets** (keys, passwords).
- **Credit and license everything.** If you use someone else's code or ideas, say so in
  `THIRD-PARTY-NOTICES.md` and in the engine's README, with their license. If you are not sure the license
  allows it, ask first.
- **Be honest about status.** `tested` means tested on a real Model:Cycles, with a date in the engine's README.

## Test reports

If you flashed an engine, tell us: engine and version, your OS version, what worked, what didn't, whether you
had to recover the machine. A test report is worth more than a code change.

## Changing an engine

1. Open an issue first for anything bigger than a typo.
2. Change the engine's files and its `CHANGELOG.md`; bump `version` in `engine.json`.
3. Run `python3 scripts/build_index.py` and `python3 scripts/check_safety.py --all`. Both must pass.
4. Open a pull request.

## Adding a new engine

Copy `engines/_template/` to `engines/<new-id>/`, fill in `engine.json` and the README, add your source and
add-on files, then run `python3 scripts/build_index.py`.
