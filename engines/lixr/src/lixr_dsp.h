/*
 * LiXR : a fixed-point drum voice for the Model:Cycles.
 *
 * Clean-room implementation of the signal path described by the Sonic Potions
 * LXR drum voice (oscillator, FM, noise/transient layer, pitch envelope,
 * sample-rate decimator, soft saturation). No LXR code, table or sample is
 * copied; the structure and parameter behaviour come from the manual and from
 * reading the open-source LXR 0.37 port. The LXR code is float/Cortex-M; this
 * file is integer-only for the ColdFire.

 * To stay at the CPU cost of a stock TONE voice, this version uses a single
 * tableless triangle/saw/rectangle oscillator with feedback FM, instead of the
 * second oscillator and four wavetables of the first LiXR version.
 *
 * The filter of the original voice is replaced by a sample-rate decimator
 * (sample-and-hold), controlled by CONTOUR. This is the version retained after
 * listening: LiXR is the former LIX2 engine.
 *
 * Formats: signal 1.0 = 2^28 (LIX_ONE28); pitch in semitones << 16; knob
 * values 0..32767 (8.8 clamped). The EMAC is used in fractional signed
 * saturated mode by the OS voice loop (MACSR = 0xa0); qmul() clears ACC0.
 */
#ifndef LIXR_DSP_H
#define LIXR_DSP_H

typedef signed char s8;
typedef short s16;
typedef int s32;
typedef unsigned int u32;

#define LIX_ONE28   0x10000000
#define LIX_ONE31   0x7fffffff

#ifdef __mcoldfire__
static inline s32 qmul(s32 a, s32 b)
{
	s32 r;
	__asm__ volatile("mac.l %1,%2,%%acc0\n\tmovclr.l %%acc0,%0"
			 : "=d"(r) : "r"(a), "r"(b));
	return r;
}
#else
/* The same EMAC arithmetic as tools/emu/emac.py, for the host reference. */
static inline s32 emac_sat(long long a)
{
	return a > 0x7fffffffLL ? 0x7fffffff : (a < -0x80000000LL ? (s32)0x80000000 : (s32)a);
}
static inline s32 qmul(s32 a, s32 b)
{
	return emac_sat((((long long)a * b) >> 23) >> 8);
}
#endif

#include "lixr_tables.h"

static inline s32 lix_mul28(s32 a, s32 b)
{
	return qmul(a, b) << 3;
}

static inline s32 lix_sat(s32 x, s32 lim)
{
	return x > lim ? lim : (x < -lim ? -lim : x);
}

/* tableless main waveform: triangle -> saw -> rectangle. hi selects the
 * second half, m is Q13 (0..8192). Q28 out (+-0.5). */
static inline s32 lix_wave(u32 ph, s32 hi, s32 m)
{
	s32 x = ph >> 16;                              /* 0..65535 */
	if (hi) {
		s32 saw = (x << 1) - 65536;
		s32 rect = (x < 32768) ? 65536 : -65536;
		return (saw + (((rect - saw) * m) >> 13)) << 11;
	} else {
		s32 tri = (x < 32768) ? (x << 2) - 65536 : 196608 - (x << 2);
		s32 saw = (x << 1) - 65536;
		return (tri + (((saw - tri) * m) >> 13)) << 11;
	}
}

/* semitones << 16 -> phase increment, 48 kHz */
static inline u32 lix_inc(s32 pitch)
{
	s32 i = pitch >> 16;
	s32 f = pitch & 0xffff;
	if (i < 0) {
		i = 0;
		f = 0;
	}
	if (i > 127) {
		i = 127;
		f = 0;
	}
	return LIX_INC[i] + (u32)qmul((s32)(LIX_INC[i + 1] - LIX_INC[i]), f << 15);
}

/* --- state ---------------------------------------------------------------- */
typedef struct {
	u32 ph;                      /* oscillator phase */
	u32 rng;                     /* noise LFSR */
	s32 last;                    /* previous output, bounded feedback FM */

	s32 penv, tenv;              /* pitch and transient envelopes */
	s32 tpos;                    /* transient position in samples */
	s32 fade;                    /* note-start fade, Q28 */

	s32 dacc, dstep, dheld;      /* decimator */

	s32 dcx, dcy;                /* DC blocker */
} lix_t;

/* Per-block controls and the played note. */
typedef struct {
	s32 note;                    /* pmod, semitones << 16 */
	s32 tune;                    /* PITCH/FINE offset, semitones << 16 */
	s32 color, shape, sweep, contour;  /* 0..32767, smoothed */
	s32 trig;                    /* 0/1 */
} lix_in_t;

static inline void lix_reset(lix_t *s)
{
	s->ph = 0;
	s->rng = 0x9e3779b9u;
	s->last = 0;
	s->penv = s->tenv = 0;
	s->tpos = 0;
	s->fade = 0;
	s->dacc = s->dheld = 0;
	s->dcx = s->dcy = 0;
}

/* one block of 32 samples, Q31 out (before the OS amp chain) */
static inline void lix_block(lix_t *s, const lix_in_t *in, s32 *out)
{
	s32 c28 = in->color << 13;
	s32 w28 = in->sweep << 13;
	s32 ci = in->color >> 9;
	s32 wi = in->sweep >> 9;
	s32 ki = in->contour >> 9;
	s32 mix_gain, trans_gain, clap;
	s32 fm_idx, pdepth, pdec32, tdec, dstep, shape28;
	s32 wave_hi, wave_m, wave_neg;
	s32 penv, inc, dinc;
	s32 n;

	if (ci > 63) ci = 63;
	if (wi > 63) wi = 63;
	if (ki > 63) ki = 63;

	shape28 = in->shape << 13;
	fm_idx = ((in->shape * in->shape) >> 15) << 14;   /* square law, max 0.25 cycle */
	pdepth = (lix_mul28(w28, w28) >> 10) * 12;             /* 0 .. 48 semitones, Q16 */
	pdec32 = LIX_PDEC32[wi];
	tdec = LIX_TDEC[ci];
	dstep = LIX_DSTEP[ki];

	/* COLOR is bipolar around 64: left = triangle->sine, no noise;
	 * centre = clean triangle; right = triangle->saw->rectangle, and noise
	 * only after 0.65, max 0.31, reduced further by SHAPE. */
	{
		s32 b = c28 - 0x0a666666;                       /* 0.65 */
		s32 pos = b > 0 ? b : 0;                        /* 0..0.35 */
		s32 neg = c28 < 0x08000000 ? (0x08000000 - c28) : 0;
		s32 u = (c28 - 0x08000000) << 1;                /* 0.5..1 -> 0..1 */
		s32 noise_scale = LIX_ONE28 - (shape28 >> 1);   /* 1.0 .. 0.5 */
		mix_gain = pos - (pos >> 3);                    /* 0 .. 0.306 */
		mix_gain = lix_mul28(mix_gain, noise_scale);
		trans_gain = (LIX_ONE28 / 10) + pos;            /* 0.1 .. 0.45 */
		trans_gain = lix_mul28(trans_gain, noise_scale);
		clap = 0;
		if (c28 > 0x0d99999a) {                          /* > ~0.85 */
			clap = (c28 - 0x0d99999a) << 3;
			if (clap > LIX_ONE28)
				clap = LIX_ONE28;
		}
		wave_hi = u >= 0x08000000;
		wave_m = wave_hi ? ((u - 0x08000000) >> 15) : (u >> 15);
		wave_neg = neg >> 14;                           /* 0 .. 8192, Q13 */
	}

	/* keep the hot state in locals: fewer loads/stores per sample */
	{
	s32 ph = s->ph, last = s->last, rng = s->rng;
	s32 tenv = s->tenv, tpos = s->tpos;
	s32 dacc = s->dacc, dheld = s->dheld;
	s32 fade = s->fade;
	s32 dcx = s->dcx, dcy = s->dcy;

	penv = s->penv;
	if (in->trig) {
		penv = LIX_ONE31;
		tenv = trans_gain;
		tpos = 0;
		fade = 0;
	}

	/* --- 32 samples: pitch/inc updated once per block, interpolated -------- */
	inc = dinc = 0;
	for (n = 0; n < 32; n++) {
		s32 main, x, y;
		s32 noise, trans;
		u32 off;

		if (n == 0) {
			s32 p0, p1, pn = qmul(penv, pdec32);
			u32 i0, i1;
			p0 = lix_sat(in->note + in->tune - (12 << 16) + qmul(penv, pdepth), 127 << 16);
			p1 = lix_sat(in->note + in->tune - (12 << 16) + qmul(pn, pdepth), 127 << 16);
			i0 = lix_inc(p0);
			i1 = lix_inc(p1);
			inc = (s32)i0;
			dinc = ((s32)i1 - (s32)i0) >> 5;
			penv = pn;
		}

		off = (u32)qmul(fm_idx, last) << 5;
		if (wave_neg) {
			s32 tri = lix_wave(ph + off, 0, 0);
			s32 sine = ((s32)LIX_SINE[(ph + off) >> 24]) << 12;
			main = tri + (((sine - tri) * wave_neg) >> 13);
		} else {
			main = lix_wave(ph + off, wave_hi, wave_m);
		}
		last = main;
		ph += (u32)inc;
		inc += dinc;

		rng = rng * 1664525u + 1013904223u;
		noise = ((s32)(s16)(rng >> 16)) << 12;

		trans = 0;
		if (tpos < 1440) {                           /* 30 ms window */
			if (tpos == 288) {                   /* 6 ms */
				if (clap)
					tenv = lix_mul28(trans_gain, clap);
			} else if (tpos == 576) {                 /* 12 ms */
				if (clap)
					tenv = lix_mul28(trans_gain, clap - (clap >> 2));
			} else if (tpos == 864) {                 /* 18 ms */
				if (clap)
					tenv = lix_mul28(trans_gain, clap >> 1);
			}
			trans = lix_mul28(tenv, noise);
			tenv = qmul(tenv, tdec);
			tpos++;
		}

		x = main + (mix_gain ? lix_mul28(noise - (main >> 1), mix_gain) : 0) + trans;

		/* sample-rate decimator (sample-and-hold), after the voice */
		dacc += dstep;
		if (dacc >= 0x10000) {
			dacc -= 0x10000;
			dheld = x;
		}
		y = dheld;

		/* soft saturator: nearest point of a 129-entry table;
		 * f(a) = a - 0.213333 a^3, a = |y| (max 0.7867, no hard clip) */
		{
			s32 a = y < 0 ? -y : y;
			s32 v;
			if (a > LIX_ONE28)
				a = LIX_ONE28;
			v = LIX_SOFT[a >> 21];
			y = y < 0 ? -v : v;
		}

		/* DC blocker, then the note-start fade and output */
		{
			s32 dc = y - dcx + dcy - (dcy >> 10);
			dcx = y;
			dcy = dc;
			y = dc;
		}

		if (fade < LIX_ONE28) {
			fade += LIX_ONE28 >> 5;
			if (fade > LIX_ONE28)
				fade = LIX_ONE28;
			y = lix_mul28(y, fade);
		}
		y += (y >> 2) + (y >> 3);                     /* 1.375x output trim */

		if (y > 0x0e000000)
			out[n] = 0x70000000;
		else if (y < -0x0e000000)
			out[n] = -0x70000000;
		else
			out[n] = y << 3;
	}
	s->ph = ph; s->last = last; s->rng = rng;
	s->tenv = tenv; s->tpos = tpos;
	s->dacc = dacc; s->dheld = dheld;
	s->fade = fade; s->dcx = dcx; s->dcy = dcy;
	s->penv = penv;
	}
}

#endif
