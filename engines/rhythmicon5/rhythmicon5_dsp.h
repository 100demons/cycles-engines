/*
 * Rhythmicon V5 Cloud: FIFTH's Rhythmicon (Feral's RYT reduced to octaves, no more fifths) followed by HALO New's
 * granular cloud.
 *
 * The dry half keeps FIFTH's arithmetic: damped-sine pings at octaves 1, 2, 4, 8, 16 of the played note, each
 * retriggered at its own rate, at 24 kHz (two internal steps per 48 kHz frame), then the 10 Hz DC blocker and the
 * soft clipper. COLOR chooses the octave count exactly as FIFTH did.
 *
 * SHAPE is the new macro. It is slewed one-pole in Q16 (1/16 per block, the same ~10.7 ms as the VX engines), then:
 *   - sweep: SHAPE 0..127 maps linearly onto FIFTH's old SWEEP 0..76, keeping the fractional part in Q16
 *     (sweep = SHAPE x 76 / 127), then feeds FIFTH's T_RATE curve unchanged;
 *   - ring:  T60 = 325 ms x (20 / 325)^(SHAPE / 127), from T_R5_RING; the two-pole resonator updates at 96 kHz, so
 *     the pole radius per update is 10^(-3 / (T60 x 96000)).
 * The old fifths group is gone (old SHAPE 64 added no fifths, so removing the path is bit-identical at 64).
 *
 * PUNCH is the cloud's Freeze. The Rhythmicon's old PUNCH function (FREE: keep the rhythm running across trigs) is
 * frozen at its startup default, FREE off: every trig resets the ping phases, exactly as FIFTH did with PUNCH 0.
 * The old CONTOUR (ring length) is replaced by SHAPE's decay, and its no-other-job centre value is not used.
 *
 * The cloud is HALO New's, copied unchanged in r5_cloud.h. The dry path is Q30; the cloud expects Q31, so the glue
 * converts with a +6 dB saturating scale before it and -6 dB after it. At CLOUD 0 the cloud leaves the buffer alone,
 * so the dry output is exactly FIFTH's (apart from that lossless round trip).
 *
 * Fixed point: dry signals Q30 after the soft clipper; coefficients Q31; phases u32. Compiled for the ColdFire and
 * for the computer with the same arithmetic.
 */
#ifndef RHYTHMICON5_DSP_H
#define RHYTHMICON5_DSP_H

#include "r5_common.h"
#include "r5_tables.h"
#include "r5_cloud.h"

#define ONE        0x7fffffff
#define KMAX       0x7f00
#define R5_OCT     5                       /* octave partials: 1, 2, 4, 8, 16 */
#define INC9K      805306368u              /* 9 kHz in phase increment */
#define RDC        2144672598              /* DC blocker at 10 Hz, Q31 */
#define R5_SLEW    4                       /* one-pole on SHAPE: 1/16 per block, ~10.7 ms */
#ifndef R5_GAIN
#define R5_GAIN    483183821               /* FIFTH's 0.9, Q29 */
#endif

static inline s32 imin(s32 a, s32 b) { return a < b ? a : b; }
static inline s32 iclamp(s32 x, s32 lo, s32 hi) { return x < lo ? lo : (x > hi ? hi : x); }
static inline s32 kcl(s32 k) { return iclamp(k, 0, KMAX); }
static inline u32 mag(s32 x) { return x < 0 ? 0u - (u32)x : (u32)x; }

static inline s32 sadd(s32 a, s32 b)
{
#ifdef __mcoldfire__
	__asm__("add.l %1,%0\n\tsats.l %0" : "+d"(a) : "r"(b) : "cc");
	return a;
#else
	s32 r = (s32)((u32)a + (u32)b);
	if (((a ^ r) & (b ^ r)) < 0)
		r = a < 0 ? (s32)0x80000000 : 0x7fffffff;
	return r;
#endif
}

static inline void zero32(void *p, u32 n)
{
	u32 *q = (u32 *)p;
	while (n--)
		*q++ = 0;
}

/* sine of phase ph, Q31 */
static inline s32 sin31(u32 ph)
{
	u32 i = ph >> 22;
	s32 a = T_SIN[i], b = T_SIN[i + 1];
	return a + qmul(b - a, (s32)((ph & 0x3fffff) << 9));
}

static inline s32 cos31(u32 ph) { return sin31(ph + 0x40000000u); }

/* phase increment of note key (semitones << 16), clamped to 0..127 */
static inline u32 inc_of(s32 key)
{
	s32 i;
	key = iclamp(key, 0, 127 << 16);
	i = key >> 16;
	return T_INC[i] + (u32)qmul((s32)(T_INC[i + 1] - T_INC[i]), (key & 0xffff) << 15);
}

/* a x b / 65536 (a, b unsigned, b Q16), floor, saturating */
static inline u32 mulq16(u32 a, u32 b)
{
	u32 bh = b >> 16, bl = b & 0xffff, ah = a >> 16, al = a & 0xffff, r, t;
	if (bh && a > 0xffffffffu / bh)
		return 0xffffffffu;
	r = a * bh;
	t = ah * bl + ((al * bl) >> 16);
	r += t;
	return r < t ? 0xffffffffu : r;
}

/* table of 129 points on a knob, read at Q16 (Q16 value, linear interpolation) */
static inline s32 lutk_q16(const s32 *t, s32 k)
{
	s32 i = k >> 16;
	return t[i] + qmul(t[i + 1] - t[i], (k & 0xffff) << 15);
}

static inline u32 lutku_q16(const u32 *t, s32 k)
{
	s32 i = k >> 16;
	return t[i] + (u32)qmul((s32)(t[i + 1] - t[i]), (k & 0xffff) << 15);
}

/* soft clipper, Q28 in -> Q30 out: transparent below 0.8, reaches 1.0 (flat) at 1.2 */
static inline s32 sclip(s32 y)
{
	u32 a, v, w, f;
	if ((u32)(y + 0x0ccccccd) < 0x1999999au)
		return y << 2;
	a = y < 0 ? 0u - (u32)y : (u32)y;
	if (a >= 0x13333333u) {
		a = 0x10000000u;
	} else {
		v = ((a - 0x0ccccccdu) * 5u) >> 1;
		w = 0x10000000u - v;
		f = 0x10000000u - (u32)(qmul((s32)w, (s32)w) << 3);
		a = 0x0ccccccdu + (u32)qmul((s32)f, 429496730);
	}
	return y < 0 ? -(s32)(a << 2) : (s32)(a << 2);
}

/* --------------------------------------------------------------------------------------------- dry voice */
struct st_r5 {
	u32 pp[R5_OCT];
	s32 y1[R5_OCT], y2[R5_OCT];
	s32 last;
};

typedef struct {
	s32 note;          /* trig note, semitones << 16 */
	s32 pitch;         /* PITCH, 8.8 (64 = the note) */
	s32 fine;          /* FINE, 8.8 (64 = just) */
	s32 color;         /* octave partials, 1..5 (8.8 knob) */
	s32 shape;         /* the macro, 8.8 (0..127) */
	s32 tim;           /* cloud TIME, 0..127 */
	s32 cld;           /* cloud CLOUD, 0..127 */
	s32 freeze;        /* PUNCH: cloud Freeze */
	s32 trig;
} r5_in_t;

typedef struct {
	s32 started;
	s32 x1, y1;        /* DC blocker, Q28 */
	s32 shape_q16;     /* slewed SHAPE, Q16; -1: snap to the target on the next block */
	struct st_r5 e;
	halo_t c;          /* the cloud */
} r5_t;

static void r5_init_osc(struct st_r5 *s)
{
	s32 i;
	zero32(s, sizeof *s / 4);
	for (i = 0; i < R5_OCT; i++)
		s->pp[i] = 0xfffffff0u;
}

static inline void r5_reset(r5_t *s, s32 t)            /* first note after choosing the machine */
{
	r5_init_osc(&s->e);
	s->started = 0;
	s->x1 = s->y1 = 0;
	s->shape_q16 = -1;
	r5_cloud_reset(&s->c, t);
}

static inline void r5_reset_dry(r5_t *s)               /* first note after silence: keep the cloud tail */
{
	r5_init_osc(&s->e);
	s->started = 0;
	s->x1 = s->y1 = 0;
	s->shape_q16 = -1;
}

/* One partial: two-pole resonator at half rate, struck when its phase wraps. */
#define R5_PING(IDX, FP, W) do { \
		u32 p_ = e->pp[IDX]; \
		s32 y1_ = e->y1[IDX], y2_ = e->y2[IDX]; \
		s32 a1_ = qmul(r2 << 15, cos31((FP) << 1)); \
		s32 imp_ = qmul(sin31((FP) << 1) >> 1, (W)); \
		u32 rate2_ = mulq16((FP), ts) << 1; \
		for (k = 0; k < 16; k += 2) { \
			s32 y_, t_; \
			p_ += rate2_; \
			t_ = qmac2(a1_, y1_, nr22h, y2_); \
			y_ = sadd(t_, t_); \
			if (p_ < rate2_) { y_ = imp_; y2_ = 0; } else { y2_ = y1_; } \
			y1_ = y_; \
			acc[k] = sadd(acc[k], y_); \
			p_ += rate2_; \
			t_ = qmac2(a1_, y1_, nr22h, y2_); \
			y_ = sadd(t_, t_); \
			if (p_ < rate2_) { y_ = imp_; y2_ = 0; } else { y2_ = y1_; } \
			y1_ = y_; \
			acc[k + 1] = sadd(acc[k + 1], y_); \
		} \
		e->pp[IDX] = p_; \
		e->y1[IDX] = y1_; \
		e->y2[IDX] = y2_; \
	} while (0)

static void r5_dry(r5_t *s, const r5_in_t *in, s32 *out)
{
	struct st_r5 *e = &s->e;
	s32 acc[16];
	s32 key, oct, n, i, k, x1, y1, x, y, g, shp, d;
	s32 r22h, nr22h, wn;
	u32 inc, ts, r2;

	if (!in->trig && !s->started) {
		for (i = 0; i < 32; i++)
			out[i] = 0;
		return;
	}
	if (in->trig) {
		r5_init_osc(&s->e);
		s->started = 1;
	}

	/* SHAPE, slewed one-pole in Q16; the first block snaps to the knob */
	shp = kcl(in->shape) << 8;
	if (s->shape_q16 < 0) {
		s->shape_q16 = shp;
	} else {
		d = shp - s->shape_q16;
		if (d >= -(1 << R5_SLEW) && d <= (1 << R5_SLEW))
			s->shape_q16 = shp;
		else
			s->shape_q16 += d >> R5_SLEW;
	}

	/* pitch: trig note + PITCH - 64 + FINE */
	key = in->note + ((in->pitch << 8) - (64 << 16));
	key += (kcl(in->fine) - 0x4000) << 3;
	key = iclamp(key, 0, 127 << 16);
	inc = inc_of(key);

	/* octaves: 1..5 (COLOR 0 -> 1, COLOR 127 -> 5) */
	oct = 1 + (kcl(in->color) * 4) / KMAX;
	if (oct > R5_OCT)
		oct = R5_OCT;
	n = oct;

	/* SHAPE 0..127 -> old SWEEP 0..76, Q16, then FIFTH's sweep curve */
	ts = lutku_q16(T_RATE, (s->shape_q16 * 76) / 127);
	/* SHAPE -> T60 325..20 ms, pole radius per 96 kHz update */
	r2 = (u32)lutk_q16(T_R5_RING, s->shape_q16);
	r22h = qmul((s32)(r2 << 15), (s32)(r2 << 15)) >> 1;
	nr22h = -r22h;
	wn = qmul(T_RATI_NORM[n], 1932735283);       /* 0.9 / n^0.8 */

	zero32(acc, 16);

	/* octave stack: 1, 2, 4, 8, 16 (the fifths group is gone) */
	for (i = 0; i < oct; i++) {
		u32 fp;
		if (inc > (INC9K >> i))
			break;
		fp = inc << i;
		R5_PING(i, fp, wn);
	}

	for (k = 0; k < 16; k++) {
		s32 o = acc[k];
		out[2 * k] = (e->last >> 1) + (o >> 1);
		out[2 * k + 1] = o;
		e->last = o;
	}

	/* model gain (Q29), 10 Hz DC blocker (Q28), soft clipper to Q30 */
	g = R5_GAIN;
	x1 = s->x1;
	y1 = s->y1;
	for (i = 0; i < 32; i++) {
		x = qmul(out[i], g);
		y = sadd(x - x1, qmul(y1, RDC));
		x1 = x;
		y1 = y;
		out[i] = sclip(y);
	}
	s->x1 = x1;
	s->y1 = y1;
}

/* Q30 -> Q31 for the cloud's input, saturating instead of wrapping at +1.0 */
static inline s32 to31(s32 x)
{
	if (x > 0x3fffffff)
		return 0x7fffffff;
	if (x < -0x40000000)
		return (s32)0x80000000;
	return x << 1;
}

/* The cloud after the OS amp chain: same place as in HALO New. Returns without touching out when it sleeps. */
static void r5_wet(r5_t *s, const r5_in_t *in, s32 *out, s32 dry)
{
	halo_in_t hi;
	s32 q31[32];
	s32 i;

	hi.tim = in->tim;
	hi.cld = in->cld;
	hi.freeze = in->freeze;
	if (!halo_cloud_busy(&s->c, &hi, dry))
		return;
	for (i = 0; i < 32; i++)
		q31[i] = to31(out[i]);
	halo_wet(&s->c, &hi, q31);
	for (i = 0; i < 32; i++)
		out[i] = q31[i] >> 1;
}

#endif
