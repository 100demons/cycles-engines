# Build checkpoints

Test builds saved at each phase gate. The `.syx` files here are **gitignored** (never committed, like all builds);
this README is committed and records which engines each checkpoint contains.

- `phase-1.syx` — Feral + LiXR (two added engines, slots 7 and 8). Byte-for-byte identical to the old repo's
  `compose.py --machines feral9,lixr` output (sha256 `e7ba5ecad6a7c33a21a2ccbe822ea5d07c9bba2a0895b04c3f418c361448e56c`).
