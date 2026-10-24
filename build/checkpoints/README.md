# Build checkpoints

Test builds saved at each phase gate. The `.syx` files here are **gitignored** (never committed, like all builds);
this README is committed and records which engines each checkpoint contains.

- `phase-1.syx` — Feral + LiXR (two added engines, slots 7 and 8). Byte-for-byte identical to the old repo's
  `compose.py --machines feral9,lixr` output (sha256 `e7ba5ecad6a7c33a21a2ccbe822ea5d07c9bba2a0895b04c3f418c361448e56c`).
- `phase-2.syx` — Feral + LiXR + 8 refmachine copies (10 added engines, the capacity test build). MAIN OS sha256
  `d025403873079579108a884528648bf272091c33044500c439cbebc7dfe4b8cf`.
- `phase-3.syx` — Feral + LiXR at their stable slots (Feral 7, LiXR 8). MAIN OS sha256
  `971c48202a7e269795772c9f3b3118f9046f01ef0155cd8cc9d85685ba7394ee`.
