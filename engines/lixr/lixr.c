/*
 * LiXR : one added machine for the Model:Cycles (OS 1.13).
 *
 * A clean-room fixed-point drum voice in the spirit of the Sonic Potions LXR
 * drum voices: one tableless oscillator with feedback FM, a noise/transient
 * layer, a pitch envelope, a sample-rate decimator (sample-and-hold) driven by
 * CONTOUR, and a soft saturator. The decimator replaces the filter of the
 * original voice. To reach the CPU cost of a stock TONE voice, the second
 * oscillator and the four wavetables of the first version were replaced by
 * feedback FM and a tableless triangle/saw/rectangle morph.
 *
 * The LXR itself is float code for a Cortex-M with an FPU; this is a new
 * integer implementation. See notes/75 for the design and the parameter
 * mapping. The LXR source is credited in PROVENANCE.md; no LXR code, table
 * or sample is copied.
 *
 * Voice-loop contract (notes/14): update(pmod, v, p) then render(out, v), once
 * per 32-frame block at 48 kHz. pmod = note of the trig (semitones << 16) ;
 * v = voice (0x31c bytes) ; p = track parameters (8.8 words: +0x12 MACHINE,
 * +0x14 PITCH, +0x16 COLOR, +0x18 SHAPE, +0x1a SWEEP, +0x1c CONTOUR,
 * +0x1e PUNCH, +0x20 GATE, +0x22 FINE, +0x24 DECAY). v+0x34 / v+0x38 :
 * trig in this block / the previous block ; v+0x3c : note end. out : 32 x
 * int32, before the OS amp chain.
 */
#include "lixr_dsp.h"

#define CYC_VOICE0  0x42308828
#define CYC_VSTRIDE 0x31c
#define MARK_OFF    0x2c
#define MARK        0x4c585231                       /* 'LXR1' */

#define AMP_ENV(v)  ((void (*)(char *))0x400a9252)(v)
#define VCA(o, v)   ((void (*)(s32 *, char *))0x400a9430)(o, v)
#define PUNCH(o, v) ((void (*)(s32 *, char *))0x400a967a)(o, v)
#define LUT_DECAY   ((const s32 *)0x4011d84c)

#define V32(o)      (*(s32 *)(v + (o)))
#define P16(o)      (*(const s16 *)(p + (o)))

#define IDLE_LEVEL  (1 << 14)

struct track {
	lix_t s;
	lix_in_t in;
	s32 idle;                                        /* silent: skip the DSP */
	s32 scolor, sshape, ssweep, scontour;            /* smoothed knobs, 0..32767 */
};

static struct track trk[6];

static inline s32 knob(s32 x)                        /* 8.8, 0..127 -> 0..32767 */
{
	x = x < 0 ? 0 : (x > 0x7f00 ? 0x7f00 : x);
	return x + (x >> 7);
}

static inline u32 mag(s32 x)
{
	return x < 0 ? 0u - (u32)x : (u32)x;
}

static void lix_update(s32 pmod, char *v, const char *p)
{
	s32 t = ((u32)v - CYC_VOICE0) / CYC_VSTRIDE;
	struct track *tr = &trk[t];
	s32 color, shape, sweep, contour, dec;

	if (V32(MARK_OFF) != MARK) {                     /* first note on this machine */
		lix_reset(&tr->s);
		tr->in.note = 0;
		tr->in.tune = 0;
		tr->idle = 1;
		color = knob(P16(0x16));
		shape = knob(P16(0x18));
		sweep = knob(P16(0x1a));
		contour = knob(P16(0x1c));
		tr->scolor = color;
		tr->sshape = shape;
		tr->ssweep = sweep;
		tr->scontour = contour;
		V32(MARK_OFF) = MARK;
	}

	color = knob(P16(0x16));
	shape = knob(P16(0x18));
	sweep = knob(P16(0x1a));
	contour = knob(P16(0x1c));
	tr->scolor += (color - tr->scolor) >> 3;
	tr->sshape += (shape - tr->sshape) >> 3;
	tr->ssweep += (sweep - tr->ssweep) >> 3;
	tr->scontour += (contour - tr->scontour) >> 3;

	tr->in.color = tr->scolor;
	tr->in.shape = tr->sshape;
	tr->in.sweep = tr->ssweep;
	tr->in.contour = tr->scontour;
	tr->in.trig = V32(0x38) ? 1 : 0;

	if (!V32(0x34))
		tr->in.note = pmod < 0 ? 0 : (pmod > (127 << 16) ? 127 << 16 : pmod);
	tr->in.tune = ((s32)P16(0x14) << 8) + (((s32)P16(0x22) - 0x4000) << 3) - (64 << 16);

	/* the OS amp chain, set as TONE does; DECAY and GATE stay the OS's */
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
}

void lixr_update(s32 pmod, char *v, const char *p)
{
	lix_update(pmod, v, p);
}

static void lix_render(s32 *out, char *v)
{
	struct track *tr = &trk[((u32)v - CYC_VOICE0) / CYC_VSTRIDE];
	int i;

	if (!V32(0x34) && !V32(0x38)
	    && mag(V32(0x230)) < IDLE_LEVEL && mag(V32(0x234)) < IDLE_LEVEL) {
		for (i = 0; i < 32; i++)
			out[i] = 0;
		tr->idle = 1;
		return;
	}
	if (tr->idle && tr->in.trig) {
		lix_reset(&tr->s);
		tr->idle = 0;
	}
	lix_block(&tr->s, &tr->in, out);
	AMP_ENV(v);
	VCA(out, v);
	PUNCH(out, v);
}

void lixr_render(s32 *out, char *v)
{
	lix_render(out, v);
}
