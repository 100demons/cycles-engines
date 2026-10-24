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
