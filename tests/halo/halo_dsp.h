/*
 * HALO: the sound core, fixed point, no FPU. A modal resonator after Mutable Instruments Elements (mallet and flow
 * exciters, HALO_MODES two-pole resonators plus a detuned doublet of the fundamental) at 12 kHz, brought to 48 kHz
 * by a halfband and a linear stage; then, after the OS amp chain, a granular delay after Mutable Instruments Clouds
 * (s16 buffer, Hann grains, detune and octave shimmer, diffuser, filtered feedback) at 48 / HALO_CLOUD_DIV kHz.
 * Elements and Clouds: Emilie Gillet, MIT (LICENSE-Mutable-Instruments). The sound is specified by
 * tools/emu/halo_model.py; every table comes from tools/gen_halo.py.
 *
 * This file is compiled twice: for the Cycles (halo.c, -mcpu=54418: the EMAC in signed saturating fractional mode,
 * MACSR = 0xa0 during the voice loop) and for the host (tools/emu/halo_ref.c) with the same arithmetic to the bit.
 *
 * Formats: signals Q28 (1.0 = 2^28) inside, Q31 in out[]; coefficients Q31; pitches semitones << 16; knobs 0..127;
 * cloud buffer s16 (Q15).
 *
 * CPU (HALO_PLAN.md, Gate C): the resonator runs mode by mode over the block's 8 samples with its state in
 * registers; the grains run grain by grain; the 48 kHz stages are a few instructions per sample; a silent strike
 * filter, a silent excitation, a silent dry voice and a silent cloud are skipped.
 */
#ifndef HALO_DSP_H
#define HALO_DSP_H

typedef signed char s8;
typedef short s16;
typedef int s32;
typedef unsigned int u32;

#ifdef __mcoldfire__
/* products summed in ACC0, read and cleared (the OS requires ACC0 empty) */
static inline s32 qmul(s32 a, s32 b)
{
	s32 r;
	__asm__ volatile("mac.l %1,%2,%%acc0\n\tmovclr.l %%acc0,%0" : "=d"(r) : "r"(a), "r"(b));
	return r;
}
static inline s32 qmac2(s32 a, s32 b, s32 c, s32 d)
{
	s32 r;
	__asm__ volatile("mac.l %1,%2,%%acc0\n\tmac.l %3,%4,%%acc0\n\tmovclr.l %%acc0,%0"
			 : "=d"(r) : "r"(a), "r"(b), "r"(c), "r"(d));
	return r;
}
static inline s32 qmac3(s32 a, s32 b, s32 c, s32 d, s32 e, s32 f)
{
	s32 r;
	__asm__ volatile("mac.l %1,%2,%%acc0\n\tmac.l %3,%4,%%acc0\n\tmac.l %5,%6,%%acc0\n\tmovclr.l %%acc0,%0"
			 : "=d"(r) : "r"(a), "r"(b), "r"(c), "r"(d), "r"(e), "r"(f));
	return r;
}
static inline s32 qmac4(s32 a, s32 b, s32 c, s32 d, s32 e, s32 f, s32 g, s32 h)
{
	s32 r;
	__asm__ volatile("mac.l %1,%2,%%acc0\n\tmac.l %3,%4,%%acc0\n\tmac.l %5,%6,%%acc0\n\tmac.l %7,%8,%%acc0\n\t"
			 "movclr.l %%acc0,%0"
			 : "=d"(r) : "r"(a), "r"(b), "r"(c), "r"(d), "r"(e), "r"(f), "r"(g), "r"(h));
	return r;
}
static inline s32 qmac5(s32 a, s32 b, s32 c, s32 d, s32 e, s32 f, s32 g, s32 h, s32 i, s32 j)
{
	s32 r;
	__asm__ volatile("mac.l %1,%2,%%acc0\n\tmac.l %3,%4,%%acc0\n\tmac.l %5,%6,%%acc0\n\t"
			 "mac.l %7,%8,%%acc0\n\tmac.l %9,%10,%%acc0\n\tmovclr.l %%acc0,%0"
			 : "=d"(r) : "r"(a), "r"(b), "r"(c), "r"(d), "r"(e), "r"(f), "r"(g), "r"(h), "r"(i), "r"(j));
	return r;
}
#else
/* the EMAC, exactly (tools/emu/emac.py): each product truncated to 2^-39 (8 bits under the LSB), summed, then
 * shifted by 31 and saturated to 32 bits */
static inline s32 emac_sat(long long a)
{
	return a > 0x7fffffffLL ? 0x7fffffff : (a < -0x80000000LL ? (s32)0x80000000 : (s32)a);
}
#define P23(a, b) (((long long)(a) * (b)) >> 23)
static inline s32 qmul(s32 a, s32 b)
{
	return emac_sat(P23(a, b) >> 8);
}
static inline s32 qmac2(s32 a, s32 b, s32 c, s32 d)
{
	return emac_sat((P23(a, b) + P23(c, d)) >> 8);
}
static inline s32 qmac3(s32 a, s32 b, s32 c, s32 d, s32 e, s32 f)
{
	return emac_sat((P23(a, b) + P23(c, d) + P23(e, f)) >> 8);
}
static inline s32 qmac4(s32 a, s32 b, s32 c, s32 d, s32 e, s32 f, s32 g, s32 h)
{
	return emac_sat((P23(a, b) + P23(c, d) + P23(e, f) + P23(g, h)) >> 8);
}
static inline s32 qmac5(s32 a, s32 b, s32 c, s32 d, s32 e, s32 f, s32 g, s32 h, s32 i, s32 j)
{
	return emac_sat((P23(a, b) + P23(c, d) + P23(e, f) + P23(g, h) + P23(i, j)) >> 8);
}
#endif

#include "halo_tables.h"

#define HALO_ONE    0x7fffffff
#define HALO_MASK   (HALO_BUF_LEN - 1)
#define HALO_GTOP   ((HALO_GRID_N - 1) << 12)          /* top of the pitch grid, semitones << 16 */
#define HALO_RTOP   ((HALO_R_N - 1) << 10)
#define HALO_Q28MAX 0x0fffffff                         /* just under 1.0 in Q28: x << 3 stays in Q31 */

typedef struct {
	s32 a1h;           /* r cos w (a1 / 2) */
	s32 na2;           /* -r^2 */
	s32 bh;            /* sin w x comb x norm / 2 (the excitation comes in doubled) */
} halo_coef_t;

typedef struct {
	s32 y1, y2;
} halo_st_t;

typedef struct {
	u32 ph;            /* read position in the buffer, samples << 16 (wraps with the buffer) */
	u32 inc;           /* samples << 16 per cloud sample: 1.0 +- detune, or 2.0 (shimmer) */
	u32 ep;            /* window phase, 0 .. 2^32 */
	u32 einc;          /* window step; 0: grain off */
} halo_grain_t;

typedef struct {
	/* --- dry voice */
	halo_coef_t cf[2][HALO_MODES + 1];               /* two mode banks: the one playing, and the next one */
	s32 ncf[2];        /* modes in each bank (doublet last) */
	s32 cur;           /* the bank playing */
	halo_st_t st[HALO_MODES + 1];                    /* the resonators' state (slot by slot, whatever the bank) */
	s32 prep;          /* the other bank holds the next strike's modes (built on the block before it) */
	s32 pmat, pdec, pnote;
	u32 ppos;
	s32 lp1, lp2, imp, strk;         /* strike filter, pending impulse, filter still sounding */
	s32 fst, flp, fenv;/* flow exciter: state (+-0.5), low-pass, envelope (Q31) */
	s32 spark, spleft; /* attack sparkle: gain, samples left */
	s32 hist[17];      /* upsampler: 9 previous 12 kHz samples, then this block's 8 */
	s32 sprev;         /* last 24 kHz sample, |sprev| < 2^28 */
	u32 blocks;        /* blocks since the last reset: mode retune every 8 */
	s32 kmat, kdec, knote;           /* what the mode bank was built for (kmat < 0: never) */
	s32 kbuilt;        /* DECAY of the last mode bank (the flow level follows its ring time) */
	u32 rng, rngpos, pos;
	/* --- cloud */
	u32 head, filled, quiet, phasor;
	halo_grain_t g[HALO_GRAINS];
	s32 di[4];
	s32 fblp, fbhp, fbx1, clp1, clp2, wprev;
	s32 wsmall;        /* the last block's wet output stayed tiny */
	s32 asleep;
	s32 dl[HALO_DIFF_TOTAL + 1];
	s16 buf[HALO_BUF_LEN + 2];       /* + a copy of buf[0] at buf[HALO_BUF_LEN]: reads never wrap */
} halo_t;

typedef struct {
	s32 note;          /* semitones << 16, 0 .. 127 << 16, PITCH and FINE included */
	s32 mat, exc, tim, cld, dec;     /* knobs 0..127: COLOR, SHAPE, SWEEP, CONTOUR, DECAY */
	s32 freeze;        /* PUNCH */
	s32 trig;          /* a new note starts in this block */
	s32 held;          /* the note is held (bowed swell) */
} halo_in_t;

static inline u32 halo_rand(u32 *s)                       /* xorshift32, as Rng in the model */
{
	u32 x = *s;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	*s = x;
	return x;
}

static inline s32 halo_grid(const s32 *t, s32 pitch)      /* tables on the 1/16-semitone grid, interpolated */
{
	s32 i;
	pitch = pitch < 0 ? 0 : (pitch > HALO_GTOP - 1 ? HALO_GTOP - 1 : pitch);
	i = pitch >> 12;
	return t[i] + qmul(t[i + 1] - t[i], (pitch & 0xfff) << 19);
}

static inline s32 halo_r(s32 lt)                          /* log2(T60 in samples) Q16 -> pole radius Q31 */
{
	s32 x = lt + (-(HALO_R_LO) << 16);
	x = x < 0 ? 0 : (x > HALO_RTOP ? HALO_RTOP : x);
	return R_OF_LT60[(x + 512) >> 10];
}

static inline s32 halo_rsqrt(s32 x)                       /* x Q27 (0..16) -> 1 / sqrt(max(0.25, x)), Q30 */
{
	s32 i = x >> 22;
	if (i > 511)
		return RSQRT[512];
	return RSQRT[i] + qmul(RSQRT[i + 1] - RSQRT[i], (x & 0x3fffff) << 9);
}

static inline s32 halo_soft(s32 x)                        /* x - 4/27 x^3 on x clamped to +-1.5, Q28 */
{
	s32 x2, t;
	x = x > 0x18000000 ? 0x18000000 : (x < -0x18000000 ? -0x18000000 : x);
	x2 = qmul(x << 2, x << 2);                            /* x^2, Q29 */
	t = qmul(x2, HALO_K427) << 2;                         /* 4/27 x^2, Q31 */
	return x - qmul(x, t);
}

static inline void halo_clear(s32 *w, s32 n)             /* n words to zero, n a multiple of 4 or not */
{
	while (n >= 4) {
		w[0] = 0; w[1] = 0; w[2] = 0; w[3] = 0;
		w += 4;
		n -= 4;
	}
	while (n-- > 0)
		*w++ = 0;
}

/* --- reset ------------------------------------------------------------------------------------------------- */
/* the dry voice before a strike that follows silence: the resonators and filters (the strike rebuilds the rest) */
static inline void halo_reset_dry(halo_t *s)
{
	halo_clear((s32 *)s->st, 2 * (HALO_MODES + 1));
	s->lp1 = s->lp2 = s->imp = s->strk = 0;
	s->flp = s->fenv = s->spleft = 0;
	halo_clear(s->hist, 17);
	s->sprev = 0;
	s->blocks = 0;
	s->fst = 1 << 27;
	s->kmat = -1;
}

/* everything (on the first note after a machine change), but the buffer: filled = 0 hides it */
static inline void halo_reset(halo_t *s, s32 t)
{
	halo_clear((s32 *)s, (s32 *)s->buf - (s32 *)s);
	s->fst = 1 << 27;
	s->kmat = -1;
	s->rng = 0x9E3779B9u * (u32)(t + 1);
	s->rngpos = 0x7F4A7C15u * (u32)(t + 1);
	s->pos = HALO_POSINIT;
	s->quiet = HALO_BUF_LEN;
	s->wsmall = 1;
	s->asleep = 1;
}

/* --- the mode bank (Elements' ComputeFilters as two-pole resonators): on a strike, or a retune ------------------- */
static void halo_modes(halo_t *s, s32 bank, s32 mat, s32 dec, s32 note, u32 pos)
{
	const s32 *semi = MAT_SEMI[mat], *lt = MAT_LT60[mat];
	s32 d = DEC_LT60[dec], sum = 0, n = 0, i, p, r, r0 = 0, a, a0 = 0, c, sn, f, norm;
	halo_coef_t *m = s->cf[bank];

	for (i = 0; i < HALO_MODES; i++) {
		p = note + semi[i];                               /* semi >= 0: p >= 0 */
		if (p >= HALO_PITCH_MAX)
			break;
		f = (p & 0xfff) << 19;
		sn = SINW[(p + 0x800) >> 12];                     /* the input gain: no need to interpolate */
		p >>= 12;
		c = COSW[p] + qmul(COSW[p + 1] - COSW[p], f);     /* the frequency: interpolated */
		r = halo_r(d + lt[i]);
		a = COS1K[(pos * (u32)(i + 1)) >> 22];            /* strike position, as a comb on the modes */
		if (!i) {
			r0 = r;
			a0 = a;
		}
		m[n].a1h = qmul(r, c);
		m[n].na2 = -qmul(r, r);
		m[n].bh = qmul(sn, a);                            /* times norm below */
		sum += qmul(a, a) >> 4;                           /* Q27 */
		n++;
	}
	if (n && note + HALO_DOUBLET_SEMI < HALO_PITCH_MAX) {       /* the doublet: slow beating with the fundamental */
		p = note + HALO_DOUBLET_SEMI;
		f = (p & 0xfff) << 19;
		sn = SINW[(p + 0x800) >> 12];
		p >>= 12;
		a = a0 >> 1;
		m[n].a1h = qmul(r0, COSW[p] + qmul(COSW[p + 1] - COSW[p], f));
		m[n].na2 = -qmul(r0, r0);
		m[n].bh = qmul(sn, a);
		sum += qmul(a, a) >> 4;
		n++;
	}
	norm = halo_rsqrt(sum);                               /* Q30 */
	for (i = 0; i < n; i++)
		m[i].bh = qmul(m[i].bh, norm);                    /* Q31 x Q30: b / 2 */
	s->ncf[bank] = n;
}

/* a new bank for the playing note (strike without preparation, retune): built in the other bank, then swapped */
static void halo_retune(halo_t *s, const halo_in_t *in)
{
	halo_modes(s, s->cur ^ 1, in->mat, in->dec, in->note, s->pos);
	s->cur ^= 1;
	s->kmat = in->mat;
	s->kdec = in->dec;
	s->knote = in->note;
	s->kbuilt = in->dec;
}

static inline u32 halo_draw_pos(halo_t *s)               /* strike position, 0.06 .. 0.10 of the string */
{
	return HALO_POS0 + ((u32)qmul((s32)(halo_rand(&s->rngpos) >> 1), HALO_POSSPAN) << 1);
}

/* On the block before a strike (the OS already holds the new note and its step's locks): the next strike's modes,
 * built in the other bank, so the strike's block only swaps them in. */
static void halo_prepare(halo_t *s, const halo_in_t *nx)
{
	s->ppos = halo_draw_pos(s);
	halo_modes(s, s->cur ^ 1, nx->mat, nx->dec, nx->note, s->ppos);
	s->prep = 1;
	s->pmat = nx->mat;
	s->pdec = nx->dec;
	s->pnote = nx->note;
}

/* --- the dry voice: 8 samples at 12 kHz, then 32 at 48 kHz in out[] (Q31, before the OS amp chain) ---------------- */
#define HALO_STEP_E(k) \
	y = qmac4(a, y1, a, y1, b, y2, c, e[k]); h[k] += y; y2 = y1; y1 = y;
#define HALO_STEP_0(k) \
	y = qmac3(a, y1, a, y1, b, y2); h[k] += y; y2 = y1; y1 = y;

static void halo_dry(halo_t *s, const halo_in_t *in, s32 *out)
{
	s32 *e = out + 24, *h = s->hist + 9;                  /* out[24..31]: this block's excitation (x 2) */
	s32 k, i, flow, x, excited;

	if (in->trig) {
		if (s->prep && s->pnote == in->note) {
			s->pos = s->ppos;                             /* prepared on the block before: swap it in; a knob */
			s->cur ^= 1;                                  /* still turning is caught up by the next retune */
			s->kmat = s->pmat;
			s->kdec = s->pdec;
			s->knote = s->pnote;
			s->kbuilt = s->pdec;
		} else {
			s->pos = s->prep ? s->ppos : halo_draw_pos(s);
			halo_retune(s, in);
		}
		s->prep = 0;
		for (i = 0; i <= HALO_MODES; i++) {              /* the mallet stops part of the old ring */
			s->st[i].y1 = qmul(s->st[i].y1, HALO_CHOKE);
			s->st[i].y2 = qmul(s->st[i].y2, HALO_CHOKE);
		}
		s->imp = EXC_IMP[in->exc];
		s->strk = s->imp != 0;
		s->spark = EXC_SPARK[in->exc];
		s->spleft = s->spark ? HALO_SPARK_LEN : 0;
		s->fst = 1 << 27;
	} else if (!(s->blocks & 7) && !s->prep && (in->mat != s->kmat || in->dec != s->kdec || in->note != s->knote))
		halo_retune(s, in);                               /* MATERIAL, DECAY or pitch moved during the note */
	s->blocks++;

	/* --- exciter: strike (two one-poles whose cutoff follows the note) and flow (Elements' ProcessFlow) */
	flow = EXC_FLOW[in->exc];
	excited = s->strk || flow;
	if (s->strk) {
		s32 as = halo_grid(STRIKE_A, in->note + EXC_HARD[in->exc]), l1 = s->lp1, l2 = s->lp2;
		for (k = 0; k < 8; k++) {
			l1 += qmul(as, s->imp - l1);
			s->imp = 0;
			l2 += qmul(as, l1 - l2);
			e[k] = l2;
		}
		if (l1 < 64 && l1 > -64 && l2 < 64 && l2 > -64) {  /* the strike has died out */
			l1 = l2 = 0;
			s->strk = 0;
		}
		s->lp1 = l1;
		s->lp2 = l2;
	} else if (flow)
		for (k = 0; k < 8; k++)
			e[k] = 0;
	if (flow) {
		u32 thr = EXC_FTHR[in->exc];
		s32 scale = EXC_FSCALE[in->exc], fnorm, key, ni = in->note >> 16;
		s32 fs = s->fst, fl = s->flp, fe = s->fenv, held = in->held;
		key = KEY[ni] + qmul(KEY[ni + 1] - KEY[ni], (in->note & 0xffff) << 15);
		fnorm = qmul(qmul(FLOW_DEC[s->kbuilt], flow), key) << 2;
		for (k = 0; k < 8; k++) {
			u32 u = halo_rand(&s->rng);
			if (u < thr)
				fs = -fs;
			x = fs + qmul(((s32)(u >> 4) - (1 << 27)) - fs, scale);
			fl += qmul(HALO_A_F, x - fl);
			if (!held)
				fe = qmul(fe, HALO_REL);
			else if (fe < HALO_ONE - 1024)            /* settled: stays put */
				fe += qmul(HALO_ATT, HALO_ONE - fe);
			e[k] += qmul(qmul(fl, fe), fnorm);
		}
		s->fst = fs;
		s->flp = fl;
		s->fenv = fe;
	}

	/* --- the modes, mode by mode over the 8 samples, summed into the upsampler's input */
	for (k = 0; k < 8; k++)
		h[k] = 0;
	if (excited) {
		for (k = 0; k < 8; k++)
			e[k] <<= 1;
		const halo_coef_t *m = s->cf[s->cur];
		halo_st_t *q = s->st;
		for (i = s->ncf[s->cur]; i > 0; i--, m++, q++) {
			s32 a = m->a1h, b = m->na2, c = m->bh, y1 = q->y1, y2 = q->y2, y;
			HALO_STEP_E(0) HALO_STEP_E(1) HALO_STEP_E(2) HALO_STEP_E(3)
			HALO_STEP_E(4) HALO_STEP_E(5) HALO_STEP_E(6) HALO_STEP_E(7)
			q->y1 = y1;
			q->y2 = y2;
		}
	} else {
		const halo_coef_t *m = s->cf[s->cur];
		halo_st_t *q = s->st;
		for (i = s->ncf[s->cur]; i > 0; i--, m++, q++) {
			s32 a = m->a1h, b = m->na2, y1 = q->y1, y2 = q->y2, y;
			HALO_STEP_0(0) HALO_STEP_0(1) HALO_STEP_0(2) HALO_STEP_0(3)
			HALO_STEP_0(4) HALO_STEP_0(5) HALO_STEP_0(6) HALO_STEP_0(7)
			q->y1 = y1;
			q->y2 = y2;
		}
	}

	/* --- 12 -> 24 kHz (halfband), 24 -> 48 kHz (linear), straight to Q31 (inputs clamped just under 1.0) */
	{
		s32 sp = s->sprev, *o = out;
		for (k = 0; k < 8; k++, o += 4) {
			const s32 *xx = s->hist + k;              /* xx[0] oldest .. xx[9] newest */
			s32 a = xx[4], mid;
			mid = qmac5(HALO_HB0, xx[5] + xx[4], HALO_HB1, xx[6] + xx[3], HALO_HB2, xx[7] + xx[2],
				    HALO_HB3, xx[8] + xx[1], HALO_HB4, xx[9] + xx[0]);
			if ((u32)(a + 0x10000000) >= 0x20000000u)
				a = a < 0 ? -0x10000000 : HALO_Q28MAX;
			if ((u32)(mid + 0x10000000) >= 0x20000000u)
				mid = mid < 0 ? -0x10000000 : HALO_Q28MAX;
			o[0] = (sp + a) << 2;
			o[1] = a << 3;
			o[2] = (a + mid) << 2;
			o[3] = mid << 3;
			sp = mid;
		}
		s->sprev = sp;
		for (k = 0; k < 9; k++)
			s->hist[k] = s->hist[k + 8];
	}

	/* --- attack sparkle at 48 kHz: what 12 kHz cannot carry; the same 256-sample burst after each strike */
	if (s->spleft > 0) {
		const s32 *t = SPARK_TAB + (HALO_SPARK_LEN - s->spleft);
		s32 g = s->spark;
		for (k = 0; k < 32; k++)
			out[k] = qmac2(out[k], HALO_ONE, t[k], g);
		s->spleft -= 32;
	}
}

/* --- the cloud ---------------------------------------------------------------------------------------------- */
static inline s32 halo_decim(const s32 *o)                /* box average of HALO_CLOUD_DIV samples, Q31 -> Q28 */
{
#if HALO_CLOUD_DIV == 4
	return qmac4(o[0], 1 << 26, o[1], 1 << 26, o[2], 1 << 26, o[3], 1 << 26);
#else
	return qmac4(o[0], 1 << 25, o[1], 1 << 25, o[2], 1 << 25, o[3], 1 << 25)
	     + qmac4(o[4], 1 << 25, o[5], 1 << 25, o[6], 1 << 25, o[7], 1 << 25);
#endif
}

/* one grain from sample k to the end of the block (or of its window): returns the first sample it no longer plays */
static inline s32 halo_grain_run(const s16 *buf, halo_grain_t *g, s32 *acc, s32 k)
{
	u32 ph = g->ph, inc = g->inc, ep = g->ep, einc = g->einc, e2;
	for (; k < HALO_SUBC; k++) {
		const s16 *b = buf + ((ph >> 16) & HALO_MASK);
		s32 s0 = b[0];
		s32 smp = s0 + (((b[1] - s0) * (s32)((ph & 0xffff) >> 1)) >> 15);
		acc[k] += (smp * HANN1K[ep >> 22]) >> 2;
		ph += inc;
		e2 = ep + einc;
		if (e2 < ep) {
			g->einc = 0;
			return k + 1;
		}
		ep = e2;
	}
	g->ph = ph;
	g->ep = ep;
	return HALO_SUBC;
}

static inline void halo_grain_start(halo_t *s, halo_grain_t *g, u32 head, s32 filled, s32 pos, s32 size,
				    u32 einc, u32 shim, s32 det, s32 jit)
{
	u32 u = halo_rand(&s->rng), inc;
	s32 dist, j;
	if (u < shim)
		inc = 0x20000;                                    /* octave-up shimmer grain */
	else
		inc = 0x10000 + qmul((s32)(halo_rand(&s->rng) ^ 0x80000000u), det);
	j = qmul((s32)(halo_rand(&s->rng) >> 1) - 0x40000000, jit);
	dist = pos + qmul(pos, j) + (inc == 0x20000 ? size << 1 : qmul(size, (s32)(inc << 14)) << 1);
	if (dist > (filled - 2) << 16)
		return;                                           /* not enough history yet */
	g->ph = (head << 16) - (u32)dist;
	g->inc = inc;
	g->ep = 0;
	g->einc = einc;
}

#define HALO_WRITE(w) {                                                                  \
	s32 y_ = halo_soft(w) >> 13;                          /* Q28 -> Q15 */            \
	y_ = y_ > 32767 ? 32767 : y_;                                                    \
	buf[head] = (s16)y_;                                                             \
	if (!head)                                                                       \
		buf[HALO_BUF_LEN] = (s16)y_;                                                 \
	head = (head + 1) & HALO_MASK;                                                   \
	if ((u32)(y_ + 1) <= 2) {                                                        \
		if (quiet < HALO_BUF_LEN)                                                    \
			quiet++;                                                                 \
	} else                                                                           \
		quiet = 0;                                                                   \
}

#define HALO_DSTAGE(n, base, len) {                                                      \
	s32 *ln_ = s->dl + (base), tail_ = ln_[i##n], wv_;                                \
	wv_ = d + qmul(HALO_DIFF_K, tail_);                                              \
	d = tail_ - qmul(HALO_DIFF_K, wv_);                                              \
	ln_[i##n] = wv_;                                                                 \
	if (++i##n == (len))                                                             \
		i##n = 0;                                                                    \
}

static void halo_wet(halo_t *s, const halo_in_t *in, s32 *out)
{
	s32 acc[HALO_SUBC], k, i, x;
	s32 c = in->cld, t = in->tim, frz = in->freeze;
	s16 *buf = s->buf;
	u32 head = s->head, quiet = s->quiet;

	if (!c && !s->filled)
		return;
	if (!c) {                                             /* CLOUD at 0: keep recording, nothing out */
		for (i = 0; i < HALO_GRAINS; i++)
			s->g[i].einc = 0;
		for (k = 0; k < HALO_SUBC; k++) {
			x = halo_decim(out + HALO_CLOUD_DIV * k);
			s->clp1 += qmul(HALO_A_C, x - s->clp1);
			s->clp2 += qmul(HALO_A_C, s->clp1 - s->clp2);
			if (!frz)
				HALO_WRITE(s->clp2)
		}
		if (!frz)
			s->filled = s->filled + HALO_SUBC > HALO_BUF_LEN ? HALO_BUF_LEN : s->filled + HALO_SUBC;
		s->head = head;
		s->quiet = quiet;
		s->wprev = 0;
		s->wsmall = 1;
		return;
	}
	{
		s32 pos = TIM_POS[t], size = TIM_SIZE[t], ov = (CLD_DENS16[c] * TIM_SIZE_S[t]) >> 16;
		u32 einc = TIM_EINC[t], dens = CLD_DENS[c], shim = CLD_SHIM[c];
		s32 det = CLD_DET[c], jit = CLD_JIT[c], wet = CLD_WET[c], fb = CLD_FB[c], dif = CLD_DIFF[c];
		s32 norm = RSQRT_OV[ov > 16 * HALO_GRAINS ? 16 * HALO_GRAINS : ov], small = 1;
		s32 free_at[HALO_GRAINS];

		/* --- the grains already playing, then the new ones (in time order, as the model) */
		for (k = 0; k < HALO_SUBC; k++)
			acc[k] = 0;
		for (i = 0; i < HALO_GRAINS; i++)
			free_at[i] = s->g[i].einc ? halo_grain_run(buf, &s->g[i], acc, 0) : 0;
		for (k = 0; k < HALO_SUBC; k++) {
			u32 ph0 = s->phasor;
			s->phasor += dens;
			if (s->phasor >= ph0)
				continue;
			for (i = 0; i < HALO_GRAINS; i++)
				if (free_at[i] <= k)
					break;
			if (i == HALO_GRAINS)
				continue;
			{
				s32 f = frz ? (s32)s->filled : (s32)s->filled + k;
				halo_grain_start(s, &s->g[i], frz ? head : head + k, f > HALO_BUF_LEN ? HALO_BUF_LEN : f,
						 pos, size, einc, shim, det, jit);
			}
			if (s->g[i].einc && s->g[i].ep == 0)
				free_at[i] = halo_grain_run(buf, &s->g[i], acc, k);
		}

		/* --- pass 1: grain level and Clouds' diffuser (allpasses in series), sample by sample */
		{
			s32 i0 = s->di[0], i1 = s->di[1], i2 = s->di[2], i3 = s->di[3];
			(void)i1; (void)i2; (void)i3;
			for (k = 0; k < HALO_SUBC; k++) {
				s32 v = qmul(acc[k], norm), d = v;
				HALO_DSTAGE(0, 0, HALO_DL0)
#if HALO_DIFF >= 2
				HALO_DSTAGE(1, HALO_DL0, HALO_DL1)
#endif
#if HALO_DIFF >= 3
				HALO_DSTAGE(2, HALO_DL0 + HALO_DL1, HALO_DL2)
#endif
#if HALO_DIFF >= 4
				HALO_DSTAGE(3, HALO_DL0 + HALO_DL1 + HALO_DL2, HALO_DL3)
#endif
				acc[k] = v + qmul(dif, d - v);
			}
			s->di[0] = i0;
			s->di[1] = i1;
			s->di[2] = i2;
			s->di[3] = i3;
		}
		/* --- pass 2: feedback (darker and DC-free each time round), recording, wet level */
		{
			s32 fblp = s->fblp, fbhp = s->fbhp, fbx1 = s->fbx1, clp1 = s->clp1, clp2 = s->clp2;
			const s32 *o = out;
			for (k = 0; k < HALO_SUBC; k++, o += HALO_CLOUD_DIV) {
				s32 v = acc[k], lw;
				fblp += qmul(HALO_A_LP, v - fblp);
				fbhp = qmul(HALO_A_HP, fbhp + fblp - fbx1);
				fbx1 = fblp;
				x = halo_decim(o);
				clp1 += qmul(HALO_A_C, x - clp1);         /* dark write path */
				clp2 += qmul(HALO_A_C, clp1 - clp2);
				if (!frz)
					HALO_WRITE(clp2 + qmul(fb, fbhp))
				lw = qmul(v, wet);                    /* wet is Q29: v x wet / 4 */
				lw = lw > (HALO_Q28MAX >> 2) ? HALO_Q28MAX >> 2
				   : (lw < -(HALO_Q28MAX >> 2) ? -(HALO_Q28MAX >> 2) : lw);
				lw <<= 2;
				if (lw >= (1 << 12) || lw <= -(1 << 12))
					small = 0;
				acc[k] = lw;
			}
			s->fblp = fblp;
			s->fbhp = fbhp;
			s->fbx1 = fbx1;
			s->clp1 = clp1;
			s->clp2 = clp2;
		}
		if (!frz)
			s->filled = s->filled + HALO_SUBC > HALO_BUF_LEN ? HALO_BUF_LEN : s->filled + HALO_SUBC;
		s->head = head;
		s->quiet = quiet;
		s->wsmall = small;
	}
	{                                                     /* linear up to 48 kHz, dry cut, added with saturation */
		s32 *o = out, y = s->wprev << 3, dd, j, dry = CLD_DRY[c];
		for (k = 0; k < HALO_SUBC; k++) {
			dd = ((acc[k] - s->wprev) >> HALO_CD_SHIFT) << 3;
			for (j = 0; j < HALO_CLOUD_DIV; j++, o++) {
				y += dd;
				*o = qmac2(*o, dry, y, HALO_ONE);
			}
			s->wprev = acc[k];
			y = acc[k] << 3;
		}
	}
}

/* Should the cloud run this block? Asleep when the dry voice is silent, the whole buffer is silent, no grain plays,
 * the last wet output was tiny and nothing is frozen; going to sleep clears the diffuser and the filters. */
static inline s32 halo_cloud_busy(halo_t *s, const halo_in_t *in, s32 dry)
{
	s32 i;
	if (dry || (in->freeze && in->cld && s->filled) || s->quiet < HALO_BUF_LEN || !s->wsmall)
		goto busy;
	for (i = 0; i < HALO_GRAINS; i++)
		if (s->g[i].einc)
			goto busy;
	if (!s->asleep) {
		halo_clear(s->dl, HALO_DIFF_TOTAL);
		s->fblp = s->fbhp = s->fbx1 = s->clp1 = s->clp2 = s->wprev = 0;
		s->asleep = 1;
	}
	return 0;
busy:
	s->asleep = 0;
	return 1;
}

#endif
