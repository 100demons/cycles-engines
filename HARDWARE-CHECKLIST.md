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

## RINGS 0.2 (rings alone at slot 11)

Built from `core/compose.py --machines rings`; RINGS is slot 11 (machine 10), with the silent `----` placeholders
at slots 7–10. Back up first.

- [ ] The device boots; the MACHINES page shows `----` at 7–10 and RINGS at 11.
- [ ] A trig on a RINGS track rings; MATERIAL goes string → bell → drum → free bar; BRIGHT changes the strike and
      the tail; DECAY changes the ring time; SWEEP and CONTOUR change nothing.
- [ ] The DECAY knob changes only the ring, not the amp envelope: with GATE/PUNCH fixed, the overall loudness
      envelope should stay the same while the tail length changes (the amp decay is locked at 90).
- [ ] A second trig adds to what is still ringing; a new note changes the pitch; PITCH/FINE work.
- [ ] Six RINGS tracks do not drop out (this engine costs about 1.16 × a TONE voice; try low notes with BRIGHT
      and DECAY high). If they do, that is the CPU warning, not a bug.
- [ ] A sound saved on RINGS and a pattern with a machine lock reload correctly after a power cycle.
- [ ] The six stock machines still work; a track that is not RINGS is unchanged.

---

## RINGS 0.3 (rings alone at slot 11, the 0.50 x TONE build)

Built from `core/compose.py --machines rings`; RINGS 0.3 is slot 11 (machine 10), with the silent `----`
placeholders at slots 7–10. Back up first. This is the lean build: six modes at 24 kHz, about 0.49 x a TONE voice.

- [ ] The device boots; the MACHINES page shows `----` at 7–10 and RINGS at 11.
- [ ] A trig on a RINGS track rings; MATERIAL goes string → bell → drum → free bar; BRIGHT changes the strike and
      the tail; DECAY changes the ring time; SWEEP and CONTOUR change nothing.
- [ ] The DECAY knob changes only the ring, not the amp envelope: with GATE/PUNCH fixed, the overall loudness
      envelope should stay the same while the tail length changes (the amp decay is locked at 90).
- [ ] A second trig adds to what is still ringing; a new note changes the pitch; PITCH/FINE work. Check the low
      end (around note 24) and the top (around note 96): the 24 kHz bank should tune exactly, with no octave or
      semitone error.
- [ ] A fast roll (repeated trigs) should compress rather than crackle. A single hit should be clean; the soft
      knee above 30,000 only engages when strikes pile up.
- [ ] On a very high note, the 24 kHz hold can add a faint metallic edge around 14–17 kHz. Check that it is
      acceptable; it is the CPU trade-off, not a fault.
- [ ] Six RINGS tracks do not drop out, including low notes with BRIGHT and DECAY high. The engine is about
      0.49 x a TONE voice, so this is no longer expected to be the CPU bottleneck.
- [ ] The top end is slightly less bright than 0.2 at extreme BRIGHT/free-bar settings: 0.3 caps the bank at six
      modes. Check that this is acceptable, not a fault.
- [ ] A sound saved on RINGS and a pattern with a machine lock reload correctly after a power cycle.
- [ ] The six stock machines still work; a track that is not RINGS is unchanged.


---

## ULTRA FERAL 1.0 (slot 12)

Built from `core/compose.py --machines ultraferal` (the page offers it as 🐺 ULTRA FERAL). Slot 12 (machine 11),
with the silent `----` placeholders at 7–11. Back up first.

- [ ] The device boots; the MACHINES page shows `----` at 7–11 and ULTRA at 12, with TONE's picture.
- [ ] A trig on an ULTRA track makes sound. SHAPE steps through the sixteen names NYB, GND, BNJ, RYT, FRS, RNG,
      PLS, SCN, KOS, WGL, CEL, CRK, VOX, FLD, NEB, MUT in the value window.
- [ ] The nine Feral names sound as they do on Feral 9.0 (keep a Feral pattern and compare).
- [ ] WGL is a wandering triangle/fifth, CEL is a stepped digital ring, CRK is dust in a resonator, VOX is a
      vowel, FLD screams and folds, NEB is a grain cloud, MUT changes engine every fraction of a second.
- [ ] FREE (PUNCH) on a held note makes COLOR and SWEEP drift. GATE + FREE on one track and a held note on
      another ULTRA track: the second track's sound moves too (the pack).
- [ ] The knob value window keeps the model names after switching tracks and after a power cycle.
- [ ] Six ULTRA tracks do not drop out (try RYT, CEL and NEB at their heaviest; if they do, that is the CPU
      warning, not a bug).
- [ ] A sound saved on ULTRA and a pattern with a machine lock reload on the same model after a rebuild with
      other engines.
- [ ] The six stock machines still work; a track that is not ULTRA is unchanged.

---

## RINGS CLOUDS 0.1 (development package, slot 7 / machine 6)

Built from `core/compose.py --machines rings_clouds` or `compose_pairs(stock, [(7, "rings_clouds")])`; RINGS CLOUDS
is slot 7 (machine 6), a standalone side build like FIFTH. Back up first. It is **not** on the flasher page.

- [ ] The device boots; the MACHINES page shows RGCLD at slot 7 with TONE's picture.
- [ ] A trig rings the modal bank; MATERIAL goes string → bell → drum → free bar; BRIGHT changes the strike and
      the tail.
- [ ] DECAY changes the ring **and** the amp envelope (unlike RINGS): a long DECAY holds a long note without GATE.
- [ ] SWEEP changes where and how long the cloud grains read; CONTOUR brings in the cloud, and at 127 the dry is
      gone. At CONTOUR 0 the output is the RINGS dry voice.
- [ ] A released note leaves a granular tail; PUNCH (Freeze) holds the cloud and a later trig is not recorded while
      it is frozen.
- [ ] A second trig adds to what is still ringing; PITCH/FINE work; a machine change starts cleanly.
- [ ] Check the CPU: one voice is about 1.045 × a TONE voice at the default/heavy settings. One to three tracks
      should be safe; six tracks may drop out. That is the CPU warning, not a bug.
- [ ] A sound saved on RGCLD and a pattern with a machine lock reload correctly after a power cycle.
- [ ] The six stock machines still work; a track that is not RINGS CLOUDS is unchanged.

---

## feraldrum (Feral Drum — nine drum voices, slot 13)

Built from `core/compose.py --machines feraldrum` (slots 7..12 are `----` placeholders; the engine is machine 12),
or tick Feral Drum on the page next to your other engines.

- [ ] The MACHINES page shows `FDRUM` after the stock six (and after any other ticked engines).
- [ ] SHAPE shows CLK, TOM, MET, MOD, BON, SNR, KIK, DRM, HAT (not 0..8).
- [ ] COLOR, SWEEP and CONTOUR show their per-model names; the value window changes when SHAPE changes.
- [ ] Every model makes a drum-like hit: CLK click/hat, TOM tom, MET metal, MOD modal drum, BON spring, SNR snare,
      KIK kick, DRM drum head, HAT noise hat.
- [ ] CONTOUR fully left gives the hardest pitch drop; fully right gives the longest ring / brightest body.
- [ ] DECAY shortens the hit (body and stock amp); long DECAY still rings as a drum, not as a drone.
- [ ] PUNCH = FREE: off, each trig restarts; on, the model keeps running between trigs.
- [ ] GATE holds the amp for the note length.
- [ ] The six stock machines still work as before.
- [ ] A pattern saved with Feral Drum reloads on the same model after a power cycle.

---

## SHEEN 0.1 (hi-hat / metal percussion, slot 14 / machine 13)

Builds: `build/sheen_OS1.13.syx` (SHEEN alone), `build/sheen-core_OS1.13.syx` (the M2 fallback), and
`build/sheen-all_OS1.13.syx` (feral9, punch, blast, sheen). **Not hardware-tested** by the agent. Back up first.

- [ ] The device flashes and boots; the MACHINES page shows SHEEN at slot 14 (after the stock six and any other
      ticked engines) with METAL's picture.
- [ ] At the home settings (all four knobs 64, DECAY 40, velocity 100, note 60) one trig is the hero closed hat:
      crisp tick, bright metallic body, gone in about 60–80 ms.
- [ ] PUNCH p-locked on the off-beat 8ths gives the open hat; the next closed step chokes the open tail. Try a
      two-bar 1/16 groove at 120 BPM.
- [ ] PUNCH on, GATE on, a 250 ms note: the hat rings for the note and closes at the end (a pedal close).
- [ ] Velocity from the pads: soft hits are quieter, darker, shorter and less ticky; hard hits open the top end.
- [ ] A fast retrig roll (16 notes at 25 ms, velocity 40 → 120) does not click or drop out. The peak rises with
      velocity; the engine's own report notes that the strict step-ratio check does not pass (see PROGRESS).
- [ ] COLOR at 0 is pure noise, at 64 the hero blend, at 127 the two-oscillator 808 cowbell; SHAPE at 0 is tight,
      at 127 a gong; SWEEP at 0 is a shaker, at 127 a laser; CONTOUR at 127 is crushed lo-fi.
- [ ] Each of the 17 recipes in `build/sheen-demos/` sounds like its name; the groove is the first listen.
- [ ] Six tracks of SHEEN play together without glitches or dropouts. **Expected warning:** the measured CPU is
      about 3.0 × a TONE voice in emulation, so six voices may be too heavy on hardware; try two to four tracks
      first and report what happens.
- [ ] A sound saved on SHEEN and a pattern with a machine lock reload correctly after a power cycle.
- [ ] The six stock machines still work; a track that is not SHEEN is unchanged.
