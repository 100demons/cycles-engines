# Rhythmicon V5 Cloud — build report

**What this is:** the fifth Rhythmicon (the FIFTH engine) with its fifths removed and HALO New's granular cloud added
after it. The cloud itself is unchanged. The Rhythmicon now has one SHAPE macro and two cloud knobs on SWEEP and
CONTOUR; PUNCH belongs to the cloud.

**Status:** experimental, emulator-tested only. Hardware has not been tested.

---

## 1. What changed

- **No fifths.** FIFTH's fifth group (3, 6, 12 above / 1/3, 1/6, 1/12 below) is gone. The engine only ever strikes
  octaves 1, 2, 4, 8 and 16 × the note. The deleted path is exact: at SHAPE 0 the output is sample-for-sample FIFTH
  with SHAPE 64, its "no fifths" setting.
- **COLOR** keeps FIFTH's octave control, but its startup value is now **63** (FIFTH used 64).
- **SHAPE is a macro.** Turning it up increases the ping rate and shortens the ring; turning it down does the
  opposite. It is smoothed with the same one-pole, 1/16-per-block style as the VX engines (about 10.7 ms), so knob
  turns and p-locks glide instead of clicking.
  - Sweep: `SHAPE × 76 / 127` feeds FIFTH's existing sweep curve, with the fractional part kept in Q16. SHAPE 0 is
    old SWEEP 0, SHAPE 27 is old SWEEP 16, SHAPE 127 is old SWEEP 76; all 128 values are distinct and rising.
  - Ring: `T60 = 325 ms × (20 / 325)^(SHAPE / 127)`, from a new lookup table. This is log-spaced and inverted
    (SHAPE 0 = longest, 127 = shortest). All octave partials share the same pole radius, so the T60 applies to every
    partial; there are no separate mode ratios to preserve.
- **CONTOUR is no longer the Rhythmicon's.** The cloud keeps CONTOUR. FIFTH's old CONTOUR only controlled ring
  length, so once ring time moved to SHAPE there was no other Rhythmicon job left to freeze at 64.
- **SWEEP is no longer the Rhythmicon's.** The cloud keeps SWEEP (TIME). FIFTH's old SWEEP became SHAPE's sweep.
- **PUNCH is the cloud's Freeze.** This is a real conflict: FIFTH used PUNCH as FREE (keep the rhythm running across
  trigs). The cloud wins. The Rhythmicon's FREE is frozen at its startup default, **off**, so every trig resets the
  ping phases exactly as FIFTH did with PUNCH 0.
- **Signal flow:** Rhythmicon → stock amp envelope/VCA (no PUNCH stage) → cloud → out. The cloud is in the same
  place HALO New put it, after the amp chain, so its echoes outlive the note.
- **Defaults:** COLOR 63, SHAPE 27, cloud SWEEP 70 and cloud CONTOUR 40 (HALO New's own defaults), DECAY 80
  (FIFTH's startup value), PUNCH off.

## 2. Final control table

| Control | Job | Default |
|---|---|---|
| **COLOR** | Rhythmicon octaves: 1, 2, 4, 8, 16 partials (0 → 1, 64 → 3, 127 → 5) | **63** |
| **SHAPE** | Rhythmicon macro: sweep up + ring down | **27** |
| **SWEEP** | Cloud TIME, exactly HALO New | **70** |
| **CONTOUR** | Cloud amount, exactly HALO New | **40** |
| **PUNCH** | Cloud Freeze, exactly HALO New | off |
| **DECAY** | Stock amp envelope | **80** |
| **PITCH / FINE / GATE** | As FIFTH / TONE | stock |

## 3. Measured T60

Measured from host renders of the real fixed-point code (note C5, COLOR 0, one octave, band-passed envelope fit):

| SHAPE | Table T60 | Measured | Error |
|---|---|---|---|
| 0 | 325.0 ms | 324.3 ms | −0.2 % |
| 27 | 179.7 ms | 179.3 ms | −0.2 % |
| 64 | 79.7 ms | 80.0 ms | +0.4 % |
| 96 | 39.5 ms | 39.2 ms | −0.6 % |
| 127 | 20.0 ms | 19.9 ms | −0.5 % |

All are inside the required ±10 %.

## 4. CPU

Instructions per 32-frame block, one voice, measured in the real OS voice loop against TONE in the same run.
TONE = 5,291.

| Case | Median | Worst steady block | × TONE (med / worst) |
|---|---|---|---|
| Default | 3,899 | 3,996 | 0.74 / 0.76 |
| Heavy (SHAPE 0, cloud max) | 4,665 | 4,759 | 0.88 / 0.90 |
| Heavy + Freeze | 4,351 | 4,436 | 0.82 / 0.84 |
| Dry (CONTOUR 0) | 3,147 | 3,147 | 0.59 / 0.59 |

The sustained load is under one TONE voice. The **first note after selecting the machine** costs 7,374 instructions
for one block (1.39 × TONE). That is a one-time initialization block, not a sustained block; HALO New had the same
pattern (6,033, 1.14 ×) and FIFTH reported its first strike separately. No sound or cloud code was changed to chase
that one block.

## 5. Tests — all passed

| Test | Result |
|---|---|
| Official OS hash | PASS: `44fe586269631a0ca7da25a3383fc6733c314809505fc3cc52f1e0ed9800640c` |
| Cloud unchanged | PASS: copied cloud vs HALO New `halo_dsp.h`, **bit-identical** at min, default, max, Freeze on/off and loud input (13,400 blocks) |
| Sweep mapping | PASS: SHAPE 0 = old SWEEP 0, SHAPE 27 = old SWEEP 16, SHAPE 127 = old SWEEP 76; 128 distinct rising values; within 1 LSB of `SHAPE × 76 / 127` |
| Decay | PASS: five T60 measurements inside ±10 % (table above) |
| No fifths | PASS: the fifth group is absent from the source; SHAPE 0 is bit-identical to FIFTH's no-fifths build |
| Defaults | PASS: COLOR 63, SHAPE 27, cloud SWEEP 70, cloud CONTOUR 40, DECAY 80 |
| Worst case / clipping | PASS: SHAPE 0, cloud max, Freeze after the buffer filled, notes 30/36/48/84; worst peak 0.27 Q30, no clipping or wraparound |
| Emulator | PASS: boot, depack, dispatch, sound at defaults and extremes, stock SNARE/KICK unchanged, no unmapped access |
| Host vs emulator | PASS: dry core, cloud output and final output bit-exact over 614 blocks in three cases (default, moving SHAPE/cloud/Freeze, heavy) |
| Usual proof suite | PASS: `tests/emu/test_engines.py` — ALL OK |

Full log: `build/rhythmicon5/prove.log`.

## 6. Deliverables

| File | What |
|---|---|
| `model-cycles_OS1.13-rhythmicon5.syx` | 922,912 bytes, starts `F0`, ends `F7`; single engine at slot 7 (OS machine 6). SHA-256 `09da4fe0a23c5da9323368a45869976cfbf046ee4f64a8714f3106d671b7843d`. Contains Elektron's OS — keep private. |
| Patched MAIN OS | 1,776,052 bytes, ends `0x401b1db4` (limit `0x40200000`), SHA-256 `870d13a77217a054f96ebfb120f8c2e04c63c3a1d34ca7058c0e2943ab56fc69` |
| Add-on zip | `rhythmicon5-addon.zip` — source, tables, object, manifest, tests, build scripts, this report, demo WAVs. No firmware. |
| Demo WAVs | default patch; slow SHAPE 0→127 with cloud dry; the same sweep with cloud wet; Freeze in action. |

## 7. Decisions I had to make

1. **Which engine is "the fifth Rhythmicon".** I used `engines/fifth` (FIFTH 0.1), the standalone octave/fifth
   Rhythmicon. It is the only engine in the workspace whose SHAPE adds fifths. I left FIFTH itself untouched and made
   a new package, `rhythmicon5`.
2. **The 24 kHz discovery.** FIFTH's `T_RING` table was generated as if the ping loop ran at 96 kHz, but the loop
   takes 16 internal steps per 32-frame block, so it really updates at 24 kHz. Its real rings were about four times
   the documented values. The new table is computed at the real 24 kHz rate so the requested T60 table is actually
   met. This is a deliberate correction, not a cloud change.
3. **CONTOUR's "other job".** The brief says old CONTOUR was bipolar. In FIFTH it was unipolar ring length and had no
   other job, so there was nothing else to freeze at 64. I followed the source and the actual engine.
4. **PUNCH conflict.** The cloud keeps Freeze; the Rhythmicon's FREE is fixed at OFF (startup default).
5. **SHAPE smoothing.** FIFTH had no smoother, so I used the VX engines' one-pole: Q16, 1/16 per block (~10.7 ms),
   snapping when within 16 Q16 units so steady values are exact. This keeps the sweep endpoints exact.
6. **Level into the cloud.** The cloud expects Q31 and FIFTH's dry path is Q30. I used a saturating one-bit shift
   (+6.0 dB) before the cloud and the matching shift back after it. At CONTOUR 0 this is transparent. I did **not**
   add a further trim: FIFTH's peak (~0.75) is only about 2.4 dB below HALO's loudest resonator (~0.99), which is not
   "far". The worst case was measured for clipping.
7. **Cloud reset.** Like HALO New, the machine reset clears the cloud state but not its 64 KB buffer; `filled = 0`
   hides the old contents. This keeps the reset bit-identical to HALO and avoids a large one-off CPU block.
8. **Slot.** The standalone `.syx` is at slot 7 / OS machine 6, the same convention as FIFTH. I did not add the
   package to `engines/slots.json` or the web flasher, because no permanent slot was requested. `test_engines.py`
   places it at machine 10 as a development package.
9. **Demos.** Rendered by the real fixed-point host code. A simple gate envelope stands in for the OS amp chain and
   is applied *before* the cloud, exactly where the OS amp chain sits, so cloud tails outlive the note.
10. **CPU first strike.** Reported separately from sustained blocks, as HALO and FIFTH did. No cloud or Rhythmicon
    sound was changed to reduce it.

## 8. Cloud provenance

The cloud code and tables were copied from HALO New's add-on (25 October 2026):

- `halo_dsp.h` SHA-256 `3406437e4b0b043a5f3d6f26015a51a4ccfc94e705e610e19968121e568ba6d8`
- `halo_tables.h` SHA-256 `c9fa973f46ad453005ac9fe521db5ad300fa6b76d4e582dcda234f63a2f89232`

`r5_cloud.h` contains the cloud functions verbatim (only the state structs are cut down to the cloud half);
`r5_cloud_tables.h` contains only the values the cloud reads, copied verbatim. The side-by-side proof compiles the originals and the copy and compares every sample. An unmodified reference copy of
`halo_dsp.h` and `halo_tables.h` is included in `tests/halo/` so the proof can be rerun without the HALO package.
