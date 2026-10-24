/* REF: model27's reference machine for the Model:Cycles OS 1.13 (notes/59 §2a).

   The smallest added machine that follows the machine contract of the Cycles Mod Standard (§2):
   two functions in the voice loop, four knobs plus Amp Decay, the stock amp chain set up the way
   TONE sets it, nothing while the voice is idle, no hook into the OS, no memory outside its own
   variables. It exists to prove the composer (two machines side by side), to be copied by a new
   author, and to measure what the registrar itself costs. It is never offered to users.

   Contract of the voice loop (One-Shot TECHNICAL §5, Modded-Cycles notes/14 and notes/43):
   ref_update(pmod, v, p), then ref_render(out, v), once per block of 32 frames at 48 kHz. pmod is
   the trig's note in semitones << 16, v the track's voice (0x31c bytes at 0x42308828 + t * 0x31c),
   p its parameters as 8.8 words (+0x14 PITCH, +0x16 COLOR, +0x18 SHAPE, +0x1a SWEEP, +0x1c CONTOUR,
   +0x22 FINE, +0x24 DECAY). v+0x34 is set in the block of a trig, v+0x38 in the block after it,
   v+0x3c when the note ends; v+0x00..0x3c is cleared when the track changes machine (0x400a7ab8),
   so a marker at v+0x2c tells the first block on this machine.

   Sound: a parabolic sine (no table, about -40 dB of harmonics) mixed with white noise, through a
   one-pole lowpass. COLOR = Tone (the lowpass, 127 = open), SHAPE = Noise (0 = sine only, 127 =
   noise only), SWEEP = Detune (-1 .. +1 semitone, 64 = none), CONTOUR = Level. PITCH, FINE and the
   trig's note set the pitch as on every machine: PITCH - 64 + note + FINE (+-2 semitones). DECAY,
   GATE and PUNCH are the stock amp chain. Plain C99, no libc, no float, no 64-bit arithmetic. */
#include <stdint.h>

typedef int32_t s32;
typedef uint32_t u32;
typedef int16_t s16;

#define CYC_VOICE0  0x42308828u
#define CYC_VSTRIDE 0x31c
#define MARK_OFF    0x2c
#define MARK        0x52454631                      /* 'REF1' */
#define IDLE_LEVEL  (1 << 14)                       /* amp envelope below -102 dB */

#define AMP_ENV(v)  ((void (*)(char *))0x400a9252)(v)
#define VCA(o, v)   ((void (*)(s32 *, char *))0x400a9430)(o, v)
#define PUNCH(o, v) ((void (*)(s32 *, char *))0x400a967a)(o, v)
#define LUT_DECAY   ((const s32 *)0x4011d84c)       /* DECAY -> factor per block (TONE's) */

#define V32(o)      (*(s32 *)(v + (o)))
#define P16(o)      (*(const s16 *)(p + (o)))

struct track {
	u32 phase, inc;                                 /* oscillator: phase and increment per frame, 2^32 = one cycle */
	u32 noise;                                      /* linear congruential noise */
	s32 lp;                                         /* the lowpass's state */
	s32 tone, mix, level;                           /* knobs, 0..32767 */
	s32 pending;                                    /* a trig in this block: the phase restarts next block */
};

static struct track trk[6];

/* 2^(k / 12) for k = 0 .. 12, Q24 */
static const u32 semi[13] = {
	16777216u, 17774841u, 18831786u, 19951585u, 21137968u, 22394871u, 23726566u, 25137542u,
	26632384u, 28215914u, 29893106u, 31669879u, 33554432u,
};

static inline s32 knob(s32 x)                       /* 8.8, 0 .. 0x7f00 -> 0 .. 32766 */
{
	x = x < 0 ? 0 : (x > 0x7f00 ? 0x7f00 : x);
	return x + (x >> 7);
}

static inline u32 mag(s32 x)
{
	return x < 0 ? 0u - (u32)x : (u32)x;
}

/* Phase increment for a note in semitones << 16: f = 440 Hz * 2^((note - 69) / 12),
   inc = f * 2^32 / 48000. A4 gives 39370534. No 64-bit multiply: the Q24 ratio and the base
   increment are shortened to 13 and 14 bits before the product. */
static u32 phase_inc(s32 note)
{
	s32 d = (note >> 16) - 69, k = d % 12, oct = d / 12;
	u32 frac = (u32)note & 0xffff, a, b, r, inc;

	if (k < 0) {
		k += 12;
		oct--;
	}
	a = semi[k];
	b = semi[k + 1];
	r = a + ((((b - a) >> 4) * (frac >> 4)) >> 8);  /* linear between the two semitones */
	inc = (39370534u >> 12) * (r >> 12);            /* = 39370534 * r / 2^24 */
	return oct >= 0 ? inc << oct : inc >> -oct;
}

void ref_update(s32 pmod, char *v, const char *p)
{
	struct track *tr = &trk[((u32)v - CYC_VOICE0) / CYC_VSTRIDE];
	s32 dec, note, tune;

	if (V32(MARK_OFF) != MARK) {                    /* first block since the track changed machine */
		tr->phase = 0;
		tr->lp = 0;
		tr->noise = 0x12345678u + ((u32)v & 0xfff);
		tr->pending = 0;
		V32(MARK_OFF) = MARK;
	}
	/* the stock amp chain, set up the way TONE sets it (0x400aa7b8), as MACRO, ACID and SAMPLE do */
	dec = (int8_t)p[0x24];
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
		tr->phase = 0;                              /* every note starts at the zero crossing */
	}
	/* pitch: the trig's note, PITCH - 64, FINE (+-2 semitones), Detune (+-1 semitone) */
	note = pmod < 0 ? 0 : (pmod > (127 << 16) ? 127 << 16 : pmod);
	tune = ((s32)P16(0x14) << 8) + (((s32)P16(0x22) - 0x4000) << 3) - (64 << 16)
	       + (((s32)P16(0x1a) - 0x4000) << 2);
	note += tune;
	note = note < 0 ? 0 : (note > (127 << 16) ? 127 << 16 : note);
	tr->inc = phase_inc(note);
	tr->tone = knob(P16(0x16));
	tr->mix = knob(P16(0x18));
	tr->level = knob(P16(0x1c));
}

void ref_render(s32 *out, char *v)
{
	struct track *tr = &trk[((u32)v - CYC_VOICE0) / CYC_VSTRIDE];
	s32 coef, i;

	/* nothing to play: no trig pending and the amp envelope closed */
	if (!V32(0x34) && !V32(0x38) && mag(V32(0x230)) < IDLE_LEVEL && mag(V32(0x234)) < IDLE_LEVEL) {
		for (i = 0; i < 32; i++)
			out[i] = 0;
		return;
	}
	coef = (tr->tone >> 7) + 1;                     /* 1 .. 256: the lowpass's coefficient, Q8 */
	coef = coef * coef >> 1;                        /* 0 .. 32768, steeper towards the dark end */
	coef = coef < 2 ? 2 : (coef > 32767 ? 32767 : coef);
	for (i = 0; i < 32; i++) {
		u32 ph = tr->phase >> 16;                   /* 0 .. 65535, one cycle */
		s32 x = (s32)(ph & 0x7fff), s, n, y;

		s = (x * (32768 - x)) >> 13;                /* a parabola per half cycle: 0 .. 32768 */
		if (ph & 0x8000)
			s = -s;
		tr->phase += tr->inc;
		tr->noise = tr->noise * 1664525u + 1013904223u;
		n = (s32)tr->noise >> 16;                   /* -32768 .. 32767 */
		y = (s * (32767 - tr->mix) + n * tr->mix) >> 15;
		tr->lp += ((y - tr->lp) * coef) >> 15;
		out[i] = (tr->lp * tr->level) << 1;         /* Q15 * Q15 -> Q31 */
	}
	AMP_ENV(v);
	VCA(out, v);
	PUNCH(out, v);
}
