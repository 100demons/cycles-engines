/*
 * The granular cloud of HALO New, copied into the Rhythmicon V5 Cloud engine.
 *
 * The arithmetic, constants, curves, defaults, buffer sizes, grain logic, diffuser, feedback and Freeze behaviour are
 * HALO New's (Mutable Instruments Clouds after Emilie Gillet, MIT; HALO New by 100demons and Claude, MIT). The code
 * below from halo_decim to halo_cloud_busy is the HALO New source verbatim; only the two state structs are cut down
 * to the cloud half (the resonator fields are not used here). tests/host/cloud_compare.c renders this copy and the
 * original HALO header side by side for the bit-exact proof.
 *
 * Formats: signals Q28 (1.0 = 2^28) inside, Q31 in out[]; the cloud buffer is s16 (Q15).
 */
#ifndef R5_CLOUD_H
#define R5_CLOUD_H

#include "r5_common.h"
#include "r5_cloud_tables.h"

#define HALO_ONE    0x7fffffff
#define HALO_MASK   (HALO_BUF_LEN - 1)
#define HALO_Q28MAX 0x0fffffff                         /* just under 1.0 in Q28: x << 3 stays in Q31 */

typedef struct {
	u32 ph;            /* read position in the buffer, samples << 16 (wraps with the buffer) */
	u32 inc;           /* samples << 16 per cloud sample: 1.0 +- detune, or 2.0 (shimmer) */
	u32 ep;            /* window phase, 0 .. 2^32 */
	u32 einc;          /* window step; 0: grain off */
} halo_grain_t;

typedef struct {
	/* --- cloud */
	u32 head, filled, quiet, phasor;
	u32 rng, pos;
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

/* the cloud half of halo_reset: buffer empty, the same seeds and sleep flags as HALO New */
static inline void r5_cloud_reset(halo_t *s, s32 t)
{
	/* as HALO: clear the state, not the 64 KB buffer; filled = 0 hides whatever it held */
	halo_clear((s32 *)s, (s32 *)s->buf - (s32 *)s);
	s->rng = 0x9E3779B9u * (u32)(t + 1);
	s->pos = HALO_POSINIT;
	s->quiet = HALO_BUF_LEN;
	s->wsmall = 1;
	s->asleep = 1;
}

/* --- the cloud, copied verbatim from HALO New --------------------------------------------------------------- */
static inline s32 halo_decim(const s32 *o)                /* box average of HALO_CLOUD_DIV samples, Q31 -> Q28 */
{
#if HALO_CLOUD_DIV == 4
	return qmac4(o[0], 1 << 26, o[1], 1 << 26, o[2], 1 << 26, o[3], 1 << 26);
#else
	return qmac4(o[0], 1 << 25, o[1], 1 << 25, o[2], 1 << 25, o[3], 1 << 25)
	     + qmac4(o[4], 1 << 25, o[5], 1 << 25, o[6], 1 << 25, o[7], 1 << 25);
#endif
}

static inline s32 halo_soft(s32 x)                        /* x - 4/27 x^3 on x clamped to +-1.5, Q28 */
{
	s32 x2, t;
	x = x > 0x18000000 ? 0x18000000 : (x < -0x18000000 ? -0x18000000 : x);
	x2 = qmul(x << 2, x << 2);                            /* x^2, Q29 */
	t = qmul(x2, HALO_K427) << 2;                         /* 4/27 x^2, Q31 */
	return x - qmul(x, t);
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
