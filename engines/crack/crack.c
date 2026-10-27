/*
 * CRACK: the bridge between the Model:Cycles OS 1.13 voice loop and crack_dsp.h.
 *
 * Voice-loop contract (refmachine.c): crack_update(pmod, v, p) then crack_render(out, v), once per block of 32
 * frames at 48 kHz.  pmod is the trig's note (semitones << 16), v the track's voice (0x31c bytes at
 * 0x42308828 + track * 0x31c), p its parameters (big-endian 8.8 words: +0x14 PITCH, +0x16 COLOR, +0x18 SHAPE,
 * +0x1a SWEEP, +0x1c CONTOUR, +0x1e PUNCH, +0x20 GATE, +0x22 FINE, +0x24 DECAY).  v+0x38 is a trig in this
 * block, v+0x3c the note ended; v+0x00..0x3c is cleared when the track changes machine, so our mark at v+0x2c
 * tells the first block on this machine.
 *
 * The voice makes its own envelopes; the stock amp chain (AMP_ENV / VCA / PUNCH) is not called.  The output is
 * Q31; the OS's voice loop halves each sample afterwards, exactly as for the stock machines.
 */
#include "crack_dsp.h"

#ifndef CYC_VOICE0
#define CYC_VOICE0  0x42308828u
#define CYC_VEL     ((const u8 *)0x40fe4cdc)   /* per track, a 16-bit word: the last trig's velocity << 8 */
#endif
#define CYC_VSTRIDE 0x31c
#define MARK_OFF    0x2c
#define MARK        0x4352434b                 /* 'CRCK' */

#define TRACK_OF(v) ((u32)(((uintptr_t)(v) - (uintptr_t)CYC_VOICE0) / CYC_VSTRIDE))
#define V32(o)      (*(s32 *)(v + (o)))
#define P16(o)      ((s16)((u8)p[o] << 8 | (u8)p[(o) + 1]))

struct track {
	cr_voice voice;
	cr_in in;
};

static struct track trk[6];

/* 8.8 word, 0..0x7f00 -> 0..32766, fractional part kept (refmachine.knob) */
static inline s32 knob(s32 x)
{
	x = x < 0 ? 0 : (x > 0x7f00 ? 0x7f00 : x);
	return x + (x >> 7);
}

void crack_update(s32 pmod, char *v, const char *p)
{
	u32 t = TRACK_OF(v);
	struct track *tr = &trk[t];
	s32 tune;

	if (V32(MARK_OFF) != MARK) {                   /* first note on this machine: a clean voice */
		cr_reset(&tr->voice, (u8)t);
		tr->in.vel = 0;
		V32(MARK_OFF) = MARK;
	}
	/* the note: the trig's, plus PITCH - 64 and FINE (+-2 semitones), as the stock machines */
	tune = ((s32)P16(0x14) << 8) + (((s32)P16(0x22) - 0x4000) << 3) - (64 << 16);
	tr->in.note = (pmod < 0 ? 0 : (pmod > (127 << 16) ? 127 << 16 : pmod)) + tune;
	tr->in.color = (s16)knob(P16(0x16));
	tr->in.shape = (s16)knob(P16(0x18));
	tr->in.sweep = (s16)knob(P16(0x1a));
	tr->in.contour = (s16)knob(P16(0x1c));
	tr->in.decay = (s16)knob(P16(0x24));
	tr->in.punch = (u8)(P16(0x1e) != 0);
	tr->in.gate = (u8)(P16(0x20) != 0);
	tr->in.trig = (u8)(V32(0x38) != 0);
	tr->in.end = (u8)(V32(0x3c) != 0);
	tr->in.seed = (u8)t;
	if (tr->in.trig) {
		u8 vel = CYC_VEL[2 * t];

		tr->in.vel = vel ? (vel > 127 ? 127 : vel) : 100;
	} else if (!tr->in.vel) {
		tr->in.vel = 100;
	}
}

void crack_render(s32 *out, char *v)
{
	struct track *tr = &trk[TRACK_OF(v)];

	(void)cr_block(&tr->voice, &tr->in, out);       /* a silent voice writes zeros and returns 0 */
}
