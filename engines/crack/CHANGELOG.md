# Changelog

## 0.1 (2026-10-26)

- First CRACK package: one machine, four macro knobs (Wires, Body, Snap, Heat), PUNCH rim, GATE, velocity and the
  stock DECAY knob as the engine's own body/wire time.
- Hybrid-rate voice: 24 kHz body / wires / ring / heat, 48 kHz click and air.
- Hero snare at the home settings; all 16 recipes from the spec render cleanly.
- Host checks §9.1–9.3: 159/159 pass. Emulator §9.4: sample exact (hero, rimshot, ghost, retrigger, machine
  change), level +0.72 dB vs stock SNARE, engine CPU 0.606× TONE steady / 0.713× TONE worst.
- Not hardware-tested. The 0.90 CPU margin target is met (hard ceiling ≤1.00× TONE met). Code+tables are 11,564 bytes, above the 6 KB target.
