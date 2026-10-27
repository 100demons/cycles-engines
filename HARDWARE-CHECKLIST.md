# Hardware checklist

These are checks for the owner to run on a real Model:Cycles. **Nothing here is hardware-tested** — these runs only
do automated checks (the emulator, the equivalence test). Each phase appends its checklist under a heading that names
its checkpoint `.syx`; flash the checkpoints in order if the final build misbehaves.

---

## phase-1.syx (Feral + LiXR)

Built from the beta page, or from `core/compose.py --machines feral9,lixr`. Flash Feral + LiXR (two added engines,
slots 7 and 8 after the stock six).

- [ ] Both Feral and LiXR appear on the MACHINES page, after the six stock machines.
- [ ] Feral makes sound on one track; LiXR makes sound on another track.
- [ ] The six stock machines (Kick, Snare, Metal, Perc, Tone, Chord) still work as before.
- [ ] A pattern saved with these engines reloads correctly after a power cycle.

---

## phase-2.syx (Feral + LiXR + 8 refmachine copies — 10 engines, the capacity build)

Built from `core/compose.py --machines feral9,lixr,refmachine x8`. This is a capacity test build: the eight
refmachine copies are not offered on the page, they just prove the 16-engine room.

- [ ] The device boots and the MACHINES page shows the engines after the stock six.
- [ ] Feral and LiXR make sound as before.
- [ ] The eight REF copies each play a pitch one semitone higher than the last (their slot number).
- [ ] The six stock machines still work as before.

---

## phase-3.syx (Feral + LiXR at their stable slots)

Built from `core/compose.py --machines feral9,lixr`. Feral is slot 7 (machine 6), LiXR is slot 8 (machine 7). The
point of this phase is that LiXR is always slot 8 — a sound saved with it finds it again in any later build.

- [ ] Flash LiXR alone (slot 8, slot 7 is a silent `----` placeholder). The MACHINES page shows `----` at 7 and
      LiXR at 8. `----` is silent; LiXR makes sound.
- [ ] Save a sound that uses LiXR, and a pattern with a machine lock on LiXR.
- [ ] Flash Feral + LiXR. The saved sound and the machine lock still play LiXR (not Feral, not `----`).
- [ ] The six stock machines still work as before.

---

## phase-5.syx (Feral + BLAST + PUNCH + SAM — the four engines, with knob displays)

Built from `core/compose.py --machines feral9,blast,punch,sam` (the page no longer offers SAM). The four engines
sit at slots 7..10 with no gap. This phase fixes the value-window displays and the stepped SWEEP.

- [ ] The MACHINES page shows the four engines after the six stock ones, with no blank `----` gap.
- [ ] Each engine makes sound on its own track.
- [ ] Feral: SHAPE shows the model names (NYB..KOS); COLOR, SWEEP and CONTOUR show their normal numbers.
- [ ] BLAST: SHAPE shows the model names (SIN..ZAP) and SWEEP shows the ratios (1:2..15X), stepping one detent
      at a time; COLOR and CONTOUR show their normal numbers.
- [ ] PUNCH: COLOR shows the FM index, SHAPE shows the model names (KIK..ZAP) and SWEEP shows the ratios
      (1:4..15X), stepping one detent at a time.
- [ ] SAM: COLOR shows the rate, SHAPE the word, SWEEP the scrub and CONTOUR the character (DEEP/DARK/SAM/BRIT/THIN).
- [ ] The six stock machines still work as before.

---

## CRACK 0.1 beta (`build/crack_OS1.13.syx`, `build/crack-all_OS1.13.syx`)

Built from `core/compose.py --machines crack` and `--machines feral9,punch,blast,crack`. On the `engine-crack`
branch CRACK is slot 11 (machine 10); slots 7–10 are the silent `----` placeholders unless Feral/PUNCH/BLAST are
also in the build. **Not hardware-tested.** The emulator checks are listed in `PROGRESS.md`.

- [ ] Flash `build/crack_OS1.13.syx`; the device boots and the MACHINES page shows CRACK after the stock six
      (and `----` placeholders at the unused slots).
- [ ] Put CRACK on a track; the default trig at the home settings plays the hero snare (PITCH 64, all four knobs
      64, DECAY 40). It is loud and bright, with a tight ~196 Hz body, a fast crack, wires, air and a short tail.
- [ ] P-lock PUNCH on alternate steps: the odd steps are a rimshot (more ring, a 12.5 ms click, a brighter
      attack), the even steps stay the hero. No click or dropout at the p-lock.
- [ ] Play the pads at low and high velocity: soft hits are quieter and darker, accents crack. The ghost note
      (velocity ~35) is clearly below the accent.
- [ ] Hold a long note with GATE on (DECAY 20): the body and wires hold instead of dying, then release when the
      note ends. Try DECAY 115 for the 80s gated shape.
- [ ] Six tracks of CRACK together at the worst settings (COLOR 96, SHAPE 127, SWEEP 127, CONTOUR 127, PUNCH on,
      GATE on, velocity 127, DECAY 115): no glitches, dropouts or stuck voices. This is the CPU stress case.
- [ ] Play each of the 16 recipes by ear (hero, rimshot, ghost, trap clap-snare, 808, 909, brush, gated 80s,
      kick, tom, clap, metallic hat, metal hit, zap, crushed lo-fi, sustained bass). The WAVs are in
      `build/crack-demos/` after `python3 tests/crack/render_demos.py`.
- [ ] Save a sound/pattern that uses CRACK, rebuild with other engines, and confirm it still finds CRACK at the
      same slot.
- [ ] The six stock machines still work as before, sample for sample.
- [ ] Report the CPU feel: if six tracks of CRACK strain the device more than six TONE tracks, say so. The
      emulator engine-cost ratio is **0.606× TONE steady** and **0.713× TONE on the worst trig block**; the 0.90×
      margin target is met on that measure. **The raw six-voice block is 2.09× TONE** because of the OS loop path,
      so this is the most important thing to judge by ear/feel on hardware. The first note after a machine change
      is heavier once (1.464×); steady play is the 0.606× figure.
