/*
 * SHEEN: the bridge from the Model:Cycles OS 1.13 voice loop to sheen_dsp.h.
 *
 * The DSP is pure; this file only reads the OS's per-track parameters (8.8 words at p + 0x14 ...), the trig and
 * note-end flags (v + 0x38 / v + 0x3c, as PUNCH reads them), the per-track velocity word (0x40fe4cdc, velocity << 8)
 * and the machine-change marker at v + 0x2c, and writes 32 Q31 samples.  The voice-loop contract is the one in
 * engines/refmachine/refmachine.c.
 */
#include "sheen_dsp.h"

#ifndef CYC_VOICE0
#define CYC_VOICE0  0x42308828
#define CYC_VEL     ((const u8 *)0x40fe4cdc)        /* per track, a 16-bit word: the last trig's velocity << 8 */
#endif
#define CYC_VSTRIDE 0x31c
#define TRACK_OF(v) ((u32)(((uintptr_t)(v) - (uintptr_t)(CYC_VOICE0)) / CYC_VSTRIDE))
#define MARK_OFF    0x2c
#define MARK        0x53484545                       /* 'SHEE' */

#define V32(o)      (*(s32 *)(v + (o)))
#define P16(o)      ((s16)((u8)p[o] << 8 | (u8)p[(o) + 1]))      /* a big-endian 8.8 word */

struct track {
	sh_voice voice;
	sh_in in;
};

static struct track trk[6];

static inline u8 knob(s32 x)                         /* 8.8, 0..127 -> 0..127 */
{
	return (u8)(x < 0 ? 0 : (x > 0x7f00 ? 127 : x >> 8));
}

void sheen_update(s32 pmod, char *v, const char *p)
{
	u32 t = TRACK_OF(v);
	struct track *tr = &trk[t];
	sh_in *in = &tr->in;
	s32 tune;

	if (V32(MARK_OFF) != MARK) {                     /* first note on this machine: a clean voice */
		u8 *b = (u8 *)&tr->voice;
		u32 k;

		for (k = 0; k < sizeof(tr->voice); k++)
			b[k] = 0;
		sh_reset(&tr->voice, (u8)t);
		tr->voice.mapped = 0;
		in->vel = 0;
		V32(MARK_OFF) = MARK;
	}
	/* the note: the trig's, plus PITCH - 64 and FINE (+-2 semitones), as the stock machines */
	tune = ((s32)P16(0x14) << 8) + (((s32)P16(0x22) - 0x4000) << 3) - (64 << 16);
	in->note = (pmod < 0 ? 0 : (pmod > (127 << 16) ? 127 << 16 : pmod)) + tune;
	in->color = (s16)(knob(P16(0x16)) * 256);
	in->shape = (s16)(knob(P16(0x18)) * 256);
	in->sweep = (s16)(knob(P16(0x1a)) * 256);
	in->contour = (s16)(knob(P16(0x1c)) * 256);
	in->decay = (s16)(knob(P16(0x24)) * 256);
	in->punch = P16(0x1e) != 0;
	in->gate = P16(0x20) != 0;
	in->trig = V32(0x38) != 0;
	in->end = V32(0x3c) != 0;
	in->seed = (u8)t;
	if (in->trig) {
		u8 vel = CYC_VEL[2 * t];

		in->vel = vel ? (vel > 127 ? 127 : vel) : 100;
	} else if (!in->vel) {
		in->vel = 100;
	}
}

void sheen_render(s32 *out, char *v)
{
	struct track *tr = &trk[TRACK_OF(v)];

	sh_block(&tr->voice, &tr->in, out);              /* a silent voice writes zeros and returns 0 */
}
