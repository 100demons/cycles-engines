/*
 * Rhythmicon V5 Cloud: an added machine of the Model:Cycles (OS 1.13). FIFTH's Rhythmicon goes through HALO New's
 * granular cloud. The sound is in rhythmicon5_dsp.h / r5_cloud.h; this file plugs it into the voice loop like
 * fifth.c and halo.c.
 *
 * Voice-loop contract: update(pmod, v, p) then render(out, v), once per block of 32 frames at 48 kHz.
 * pmod = the trig's note (semitones << 16); v = the Cycles voice (0x31c bytes); p = the track's parameters
 * (8.8 words: +0x12 MACHINE, +0x14 PITCH, +0x16 COLOR, +0x18 SHAPE, +0x1a SWEEP, +0x1c CONTOUR, +0x1e PUNCH,
 * +0x20 GATE, +0x22 FINE, +0x24 DECAY); v+0x34 / v+0x38: trig in this block / in the previous one (the new note
 * starts), v+0x3c: the note ended; out: 32 x int32.
 *
 * Knobs: COLOR = octaves (FIFTH), SHAPE = the macro (sweep up, ring down), SWEEP = cloud TIME, CONTOUR = cloud CLOUD,
 * PUNCH = cloud FREEZE, DECAY = the stock amp envelope, PITCH/FINE/GATE as on TONE. The Rhythmicon's old SWEEP is now
 * SHAPE; its old CONTOUR is gone; its old PUNCH (FREE) is frozen at the startup default, OFF.
 *
 * Order in render: the dry Rhythmicon, the OS amp chain (AMP_ENV, VCA, no PUNCH), then the cloud, so its echoes
 * outlive the note. A silent dry voice is not computed; the cloud sleeps when its buffer and grains are silent.
 *
 * State: one voice per track, here (the payload's BSS, zeroed at boot). After a machine change (mark at v+0x2c,
 * cleared by the OS voice reset 0x400a7ab8), the voice is only marked pending: it is fully reset when its first note
 * starts, and nothing runs before. The dry half is reset on the first trig after the voice went silent.
 */
#include "rhythmicon5_dsp.h"

#define CYC_VOICE0  0x42308828
#define CYC_VSTRIDE 0x31c
#define MARK_OFF    0x2c
#define MARK        0x52354331                       /* 'R5C1' */

#define AMP_ENV(v)  ((void (*)(char *))0x400a9252)(v)
#define VCA(o, v)   ((void (*)(s32 *, char *))0x400a9430)(o, v)
#define LUT_DECAY   ((const s32 *)0x4011d84c)        /* DECAY -> multiplier per block (TONE's) */

#define V32(o)      (*(s32 *)(v + (o)))
#define P16(o)      (*(const s16 *)(p + (o)))

#define IDLE_LEVEL  (1 << 14)                        /* amp envelope under -102 dB: silent voice */

struct track {
	r5_t s;
	r5_in_t in;
	s32 idle;                                        /* the dry voice went silent: reset at the next note */
	s32 pend;                                        /* machine just chosen: full reset at the first note */
	s32 note;                                        /* the trig's note, semitones << 16 */
};

static struct track trk[6];

static inline s32 knob(s32 x)                        /* 8.8 -> 0..127 */
{
	x = (x + 0x80) >> 8;
	return x < 0 ? 0 : (x > 127 ? 127 : x);
}

void rhythmicon5_update(s32 pmod, char *v, const char *p)
{
	s32 t = ((u32)v - CYC_VOICE0) / CYC_VSTRIDE, dec, n, tune;
	struct track *tr = &trk[t];

	if (V32(MARK_OFF) != MARK) {                     /* machine just chosen (or the OS reset the voice) */
		tr->pend = 1;
		tr->idle = 1;
		V32(MARK_OFF) = MARK;
	}
	/* the stock amp chain, set as TONE (0x400aa7b8), without PUNCH (it is Freeze here) */
	dec = (s8)p[0x24];
	dec = dec < 0 ? 0 : (dec > 127 ? 127 : dec);
	V32(0x250) = LUT_DECAY[dec];
	if (V32(0x38)) {
		V32(0x274) = V32(0x278) = 0x012c0f26;
		V32(0x280) = V32(0x284) = 0x001f7c5c;
		V32(0x290) = V32(0x294) = 0x7fffffff;
		V32(0x27c) = 0x82581e4b;
		V32(0x288) = 0x803ef8b8;
		V32(0x29c) = 0x7fffffff; V32(0x2a0) = 0x20000000; V32(0x2a4) = 0x20000000;
		V32(0x48) = 0;
	}

	tune = ((s32)P16(0x14) << 8) + (((s32)P16(0x22) - 0x4000) << 3) - (64 << 16);   /* PITCH - 64, FINE +-2 */
	if (!V32(0x34))
		tr->note = pmod < 0 ? 0 : (pmod > (127 << 16) ? 127 << 16 : pmod);
	n = tr->note + tune;
	tr->in.note = n < 0 ? 0 : (n > (127 << 16) ? 127 << 16 : n);
	tr->in.pitch = P16(0x14);
	tr->in.fine = P16(0x22);
	tr->in.color = P16(0x16);
	tr->in.shape = P16(0x18);
	tr->in.tim = knob(P16(0x1a));                /* cloud TIME */
	tr->in.cld = knob(P16(0x1c));                /* cloud CLOUD */
	tr->in.freeze = P16(0x1e) != 0;              /* cloud Freeze */
	tr->in.trig = V32(0x38) != 0;
}

/* the cloud call, kept as its own function so the proof can read its input (as halo_wet_entry) */
__attribute__((noinline)) void rhythmicon5_wet_entry(s32 *out, struct track *tr, s32 dry)
{
	r5_wet(&tr->s, &tr->in, out, dry);
}

void rhythmicon5_render(s32 *out, char *v)
{
	s32 t = ((u32)v - CYC_VOICE0) / CYC_VSTRIDE, i, dry = 0;
	struct track *tr = &trk[t];

	if (!V32(0x34) && !V32(0x38) && mag(V32(0x230)) < IDLE_LEVEL && mag(V32(0x234)) < IDLE_LEVEL) {
		for (i = 0; i < 32; i++)                     /* silent dry voice: not computed */
			out[i] = 0;
		tr->idle = 1;
	} else {
		if (tr->idle && !tr->in.trig) {              /* silent until the strike of the next block */
			for (i = 0; i < 32; i++)
				out[i] = 0;
		} else {
			if (tr->pend) {                          /* first note on this machine: everything clean */
				r5_reset(&tr->s, t);
				tr->pend = 0;
			} else if (tr->idle)                     /* first note after silence: a clean resonator */
				r5_reset_dry(&tr->s);
			tr->idle = 0;
			r5_dry(&tr->s, &tr->in, out);
			dry = 1;
		}
		AMP_ENV(v);
		VCA(out, v);
	}
	if (!tr->pend)
		rhythmicon5_wet_entry(out, tr, dry);
}
