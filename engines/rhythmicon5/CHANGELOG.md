# Changelog

## 1.0 — 2026-10-25

- FIFTH's Rhythmicon with the fifths group removed; only octaves 1, 2, 4, 8, 16 remain.
- SHAPE is one macro: sweep 0..76 (old SWEEP), ring T60 325..20 ms (old CONTOUR, now log-spaced and driven by the
  real 24 kHz update rate). Slewed one-pole so knob turns and p-locks do not click.
- COLOR default 63, SHAPE default 27 (old SWEEP 16), DECAY default 80.
- HALO New's granular cloud copied after the amp chain, unchanged: SWEEP = TIME (default 70), CONTOUR = CLOUD (40),
  PUNCH = FREEZE.
- PUNCH conflict resolved in the cloud's favour; the Rhythmicon's old FREE is fixed at OFF.
- Q30 dry path converted to the cloud's Q31 interface and back; no extra gain trim.
- Standalone build at slot 7 / OS machine 6; cloud bit-exact proof, host/emulator bit-exact proof, T60 and CPU
  measurements, four demo WAVs. Hardware not tested.
