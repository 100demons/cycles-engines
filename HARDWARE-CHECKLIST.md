# Hardware checklist

These are checks for the owner to run on a real Model:Cycles. **Nothing here is hardware-tested** — these runs only
do automated checks (the emulator, the equivalence test). Each phase appends its checklist under a heading that names
its checkpoint `.syx`; flash the checkpoints in order if the final build misbehaves.

---

## phase-1.syx (Feral + LiXR)

Built from the beta page, or from `core/compose.py --machines feral,lixr`. Flash Feral + LiXR (two added engines,
slots 7 and 8 after the stock six).

- [ ] Both Feral and LiXR appear on the MACHINES page, after the six stock machines.
- [ ] Feral makes sound on one track; LiXR makes sound on another track.
- [ ] The six stock machines (Kick, Snare, Metal, Perc, Tone, Chord) still work as before.
- [ ] A pattern saved with these engines reloads correctly after a power cycle.

---

## phase-2.syx (Feral + LiXR + 8 refmachine copies — 10 engines, the capacity build)

Built from `core/compose.py --machines feral,lixr,refmachine x8`. This is a capacity test build: the eight
refmachine copies are not offered on the page, they just prove the 16-engine room.

- [ ] The device boots and the MACHINES page shows the engines after the stock six.
- [ ] Feral and LiXR make sound as before.
- [ ] The eight REF copies each play a pitch one semitone higher than the last (their slot number).
- [ ] The six stock machines still work as before.

---

## phase-3.syx (Feral + LiXR at their stable slots)

Built from `core/compose.py --machines feral,lixr`. Feral is slot 7 (machine 6), LiXR is slot 8 (machine 7). The
point of this phase is that LiXR is always slot 8 — a sound saved with it finds it again in any later build.

- [ ] Flash LiXR alone (slot 8, slot 7 is a silent `----` placeholder). The MACHINES page shows `----` at 7 and
      LiXR at 8. `----` is silent; LiXR makes sound.
- [ ] Save a sound that uses LiXR, and a pattern with a machine lock on LiXR.
- [ ] Flash Feral + LiXR. The saved sound and the machine lock still play LiXR (not Feral, not `----`).
- [ ] The six stock machines still work as before.

---

## phase-5.syx (Feral + BLAST + PUNCH + SAM — the four engines, with knob displays)

Built from `core/compose.py --machines feral,blast,punch,sam` (the page no longer offers SAM). The four engines
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
