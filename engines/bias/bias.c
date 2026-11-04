/*
 * BIAS : a clean-room Model:Cycles machine modelled on the documented Basimilus Iteritas Alia voice
 * (Noise Engineering). No Noise Engineering code, table or sample is used; the architecture is taken from
 * their public manuals (Stephen McCaul's design notes) as summarised in "BIA for Model:Cycles: Technical Spec and
 * Correction Brief". Every tuning value (the spec's GUESS values) is in bias_config.h.
 *
 * The voice, in the spec's order:
 *
 *   six oscillators, one exponential envelope each  ->  sum, normalised by the sum of their levels
 *   ->  threshold-reflection folder (unlimited stages)  ->  decaying pulses at the folded peaks (top quarter)
 *   ->  post-fold envelope  ->  soft limit  ->  48 kHz output (interpolated)
 *
 * and the oscillators, the folder and the pulses run on a pitch-locked internal clock: N x f0, N a power of two
 * chosen per block so the internal rate sits in [W/2, W), W = BIAS_CFG_WINDOW_KHZ (36 kHz: the spec's 48 does not
 * fit one TONE voice's CPU). Oscillator 1 advances exactly 1/N of a cycle
 * per internal sample, so folding, square edges and integer ratios alias onto harmonics of f0, as on the module.
 * The internal samples are interpolated linearly to the 48 kHz output.
 *
 * Controls (the spec's mapping, with Attack, Metal and the octave switch removed):
 *   COLOR    Harmonic  fully left one tone; a second fades in by a quarter turn; then the other four appear at
 *                      a low level with short decays that stretch, then their levels rise
 *   SHAPE    Morph     every oscillator through sine, triangle, saw and square, two shapes blended at a time
 *   SWEEP    Spread    the five upper ratios from the harmonic series to the prime series, inharmonic between
 *   CONTOUR  Fold      first 3/4: the folder's threshold; top 1/4 (96 and up): decaying pulses at the peaks
 *   DECAY    Decay     one decay for the six envelopes, 10 ms to 4 s to -60 dB; 127 drones
 *   PUNCH    Liquid    on = Skin plus a pitch envelope on the clock, so all six oscillators drop together;
 *                      off = Skin; p-lockable
 *   GATE     sustain   optional hold: the envelopes freeze at half level while the note is held
 *
 * Removed on request: Attack (the onset is fixed at the spec's noon setting: instant, every phase restarted at a
 * quarter cycle, the pop), and with it the noise oscillator; Metal (the FM voice); the octave switch (a fixed
 * range, PITCH covers it).
 *
 * Voice-loop contract (notes/14): update(pmod, v, p) then render(out, v), once per block of 32 frames at
 * 48 kHz. pmod = the trig's note (semitones << 16); v = the Cycles voice (0x31c bytes); p = the track's
 * parameters (big-endian 8.8 words: +0x12 MACHINE, +0x14 PITCH, +0x16 COLOR, +0x18 SHAPE, +0x1a SWEEP,
 * +0x1c CONTOUR, +0x1e PUNCH, +0x20 GATE, +0x22 FINE, +0x24 DECAY); v+0x34 / v+0x38: a trig in this block /
 * in the previous one; v+0x3c: the note ended.
 */
typedef signed char s8;
typedef short s16;
typedef int s32;
typedef unsigned int u32;
typedef unsigned long long u64;

#define BIAS_ONE28   0x10000000
#define BIAS_ONE24   0x01000000

#ifdef __mcoldfire__
/* a x b (Q31) through the EMAC; ACC0 is left empty, as the OS wants it */
static inline s32 qmul(s32 a, s32 b)
{
	s32 r;
	__asm__ volatile("mac.l %1,%2,%%acc0\n\tmovclr.l %%acc0,%0" : "=d"(r) : "r"(a), "r"(b));
	return r;
}
#else
/* the EMAC, identically (tools/emu/emac.py): the product truncated at 2^-39, saturated to 32 bits */
static inline s32 emac_sat(long long a)
{
	return a > 0x7fffffffLL ? 0x7fffffff : (a < -0x80000000LL ? (s32)0x80000000 : (s32)a);
}
static inline s32 qmul(s32 a, s32 b)
{
	return emac_sat((((long long)a * b) >> 23) >> 8);
}
#endif

/* Accumulate a x b (Q31) into ACC0, then read and clear it: the six oscillators share one movclr. */
#ifdef __mcoldfire__
#define MAC0(a, b) __asm__ volatile("mac.l %0,%1,%%acc0" : : "r"(a), "r"(b))
static inline s32 movclr0(void)
{
	s32 r;
	__asm__ volatile("movclr.l %%acc0,%0" : "=d"(r));
	return r;
}
#else
static long long bias_acc0;                            /* each product truncated at 2^-39, as the EMAC keeps it */
#define MAC0(a, b) (bias_acc0 += ((long long)(a) * (b)) >> 23)
static inline s32 movclr0(void)
{
	s32 r = emac_sat(bias_acc0 >> 8);

	bias_acc0 = 0;
	return r;
}
#endif

#include "bias_config.h"
#include "bias_tables.h"

/* bias_config.h's linear values in fixed point, folded at compile time (no float at run time) */
#define Q28F(x)      ((s32)((x) * 268435456.0 + 0.5))
#define Q15F(x)      ((s32)((x) * 32768.0 + 0.5))
#define BIAS_A_START Q28F(BIAS_CFG_HARM_A_START)
#define BIAS_EDGE    Q15F(BIAS_CFG_HARM_EDGE)
#define BIAS_A_FADE  Q15F(BIAS_CFG_HARM_A_FADE)
#define BIAS_PULSE   Q28F(BIAS_CFG_PULSE_MAX)
#define BIAS_SUSTAIN Q28F(BIAS_CFG_SUSTAIN)
#define BIAS_POP     ((u32)((BIAS_CFG_POP_PHASE) * 4294967296.0))
#define BIAS_LIQ     ((s32)(BIAS_CFG_LIQUID_SEMIS))
#define BIAS_OFFSET  ((s32)(BIAS_CFG_PITCH_OFFSET) * 65536)
#define BIAS_WTOP    ((u32)((BIAS_CFG_WINDOW_KHZ) / 48.0 * 4294967295.0))   /* the window's top as rho, Q32 */

static inline s32 bias_mul28(s32 a, s32 b)
{
	return qmul(a, b) << 3;
}

/* 2^-L, L >= 0 in Q24 -> Q28 */
static s32 bias_exp2n(s32 L)
{
	s32 n, i, a;

	if (L <= 0)
		return BIAS_ONE28;
	n = L >> 24;
	if (n >= 29)
		return 0;
	i = (L >> 16) & 0xff;
	a = BIAS_EXP2[i];
	a += qmul(BIAS_EXP2[i + 1] - a, (L & 0xffff) << 15);
	return a >> (n + 2);
}

/* 2^e, 0 <= e < 6 in Q24 -> Q24 */
static s32 bias_exp2p(s32 e)
{
	s32 n = e >> 24, f = e & 0xffffff;

	if (!f)
		return BIAS_ONE24 << n;
	return (bias_exp2n(BIAS_ONE24 - f) >> 3) << n;          /* 2^f = 2 x 2^-(1-f) */
}

/* semitones << 16 -> phase increment per 48 kHz sample, Q32 */
static inline u32 bias_inc(s32 pitch)
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
	return BIAS_INC[i] + (u32)qmul((s32)(BIAS_INC[i + 1] - BIAS_INC[i]), f << 15);
}

#ifndef CYC_VOICE0
#define CYC_VOICE0  0x42308828
#endif
#define CYC_VSTRIDE 0x31c
#define MARK_OFF    0x2c
#define MARK        0x42494131                       /* 'BIAS1' */

#define V32(o)      (*(s32 *)(v + (o)))
#define P16(o)      (*(const s16 *)(p + (o)))

#ifdef BIAS_HOST
extern char *bias_voice0;
#define BIAS_VOICE_INDEX(v) (((char *)(v) - bias_voice0) / CYC_VSTRIDE)
#else
#define BIAS_VOICE_INDEX(v) (((u32)(v) - CYC_VOICE0) / CYC_VSTRIDE)
#endif

#define BIAS_OSCS    6
#define BIAS_QUIET_BLOCKS 32
#define BIAS_FULL    32766                           /* a knob at 127, as knob() gives it */
#define BIAS_KNEE    (3 * 32768 / 4)                 /* Fold: threshold below, pulses above */
#define BIAS_SILENT  134543                          /* -66 dB in Q28: the governor's threshold; quieter oscillators
                                                      * are skipped */

/* one oscillator's hot state, in the order the inner loop reads it. The gain (level x envelope) is split between
 * the two Morph shapes: ga = gain x (1 - crossfade), gb = gain x crossfade, so the EMAC does the crossfade. */
struct bias_osc {
	u32 ph, inc;                                 /* phase and its step per internal sample, Q32 */
	s32 ga, dga, gb, dgb;                        /* the two shares of the gain and their steps per sample, Q28 */
};

struct bias_track {
	s32 note;                                    /* trig note + PITCH + FINE - 64, semitones << 16 */
	s32 harm, morph, spread, fold;               /* smoothed knobs, 0..32766 */
	s32 decay;                                   /* DECAY knob, 0..127 */
	s32 liquid;                                  /* PUNCH on: the pitch envelope */
	s32 trig, held, gate, sustaining, idle, quiet, fresh;
	s32 last_decay, last_harm, last_spread;

	struct bias_osc os[BIAS_OSCS];
	s32 nact;                                    /* oscillators 1..nact are above -66 dB this block */
	s32 rat[BIAS_OSCS];                          /* frequency ratio, Q16 */
	s32 amp[BIAS_OSCS];                          /* level from Harmonic, Q28 */
	s32 dec[BIAS_OSCS];                          /* envelope factor per block, Q28 */
	s32 env[BIAS_OSCS];                          /* envelope, Q28 */
	s32 g[BIAS_OSCS];                            /* gain (level x envelope) at the start of the block, Q28 */
	s32 ge[BIAS_OSCS];                           /* the gains at the end of this block */
	s32 sum_gain;                                /* 1 / sum of levels, Q28 */

	s32 penv;                                    /* Liquid's pitch envelope, Q28 */
	s32 wi, wf;                                  /* Morph: the first shape of the pair, the Q15 crossfade */
	s32 fold_s;                                  /* 1 / (T x sum of levels), Q26; 0 = folder off */
	s32 fold_amt;                                /* the post-fold envelope's share, Q28 */
	s32 pe, dpe, pe_end;                         /* post-fold envelope gain, its step, its end of block */
	s32 pulse_mix, pulse_c, pulse, prev_y, last_dir;

	u32 acc, rho;                                /* the clock: position between internal samples and its step
	                                              * per output sample, Q31 */
	s32 prev, cur;                               /* the last two internal samples, at the output's level (Q30) */
};

static struct bias_track bias_trk[6];

static inline s32 knob(s32 x)                        /* 8.8, 0..127 -> 0..32766 */
{
	x = x < 0 ? 0 : (x > 0x7f00 ? 0x7f00 : x);
	return x + (x >> 7);
}

/* one block's step toward the knob, landing exactly on it */
static inline s32 smooth(s32 cur, s32 want)
{
	s32 d = want - cur;

	if (d > -16 && d < 16)
		return want;
	return cur + (d >> 3);
}

static inline s32 unit(s32 k)                        /* 0..32766 -> 0..32768, so 127 is exactly 1 */
{
	return k >= BIAS_FULL ? 32768 : k;
}

static inline s32 clamp_k(s32 x)
{
	return x < 0 ? 0 : (x > 127 ? 127 : x);
}

static void bias_reset(struct bias_track *tr)
{
	s32 i;
	char *z = (char *)tr;

	for (i = 0; i < (s32)sizeof(*tr); i++)
		z[i] = 0;
	tr->note = 60 << 16;
	tr->decay = 60;
	tr->idle = 1;
	tr->last_decay = tr->last_harm = tr->last_spread = -1;
	tr->sum_gain = BIAS_ONE28;
	tr->pe = BIAS_ONE28;
	tr->pulse_c = BIAS_PULSE_C[0];
	for (i = 0; i < BIAS_OSCS; i++) {
		tr->dec[i] = BIAS_ONE28;
		tr->rat[i] = (i + 1) << 16;
	}
}

/* A trig restarts every envelope at full level and every phase at the pop phase: the spec's noon onset. */
static void bias_start(struct bias_track *tr)
{
	s32 i;

	for (i = 0; i < BIAS_OSCS; i++) {
		tr->os[i].ph = BIAS_POP;
		tr->env[i] = BIAS_ONE28;
	}
	tr->fresh = 1;
	tr->sustaining = 0;
	tr->penv = tr->liquid ? BIAS_ONE28 : 0;
	tr->pulse = tr->prev_y = tr->last_dir = 0;
	tr->quiet = 0;
	tr->idle = 0;
}

/* Harmonic and Decay: each oscillator's level and its envelope factor per block. Runs when either moved. */
static void bias_harmonic(struct bias_track *tr)
{
	s32 h = unit(tr->harm);
	s32 L0 = BIAS_DECAY_L[tr->decay];
	s32 a_up, stretch, i, sum = 0;

	/* oscillators 3..6: absent below 0.25, a quick fade to the start level, the start level up to the edge,
	 * then a rise to full. Their decays stretch from HARM_D_MIN to HARM_D_MAX between 0.25 and the edge. */
	if (h < 8192)
		a_up = 0;
	else if (h < 8192 + BIAS_A_FADE)
		a_up = (BIAS_A_START >> 15) * (((h - 8192) << 15) / BIAS_A_FADE);
	else if (h < BIAS_EDGE)
		a_up = BIAS_A_START;
	else
		a_up = BIAS_A_START + ((BIAS_ONE28 - BIAS_A_START) >> 15) * (((h - BIAS_EDGE) << 15) /
		                                                            (32768 - BIAS_EDGE));
	stretch = h <= 8192 ? 0 : (h >= BIAS_EDGE ? 32768 : ((h - 8192) << 15) / (BIAS_EDGE - 8192));

	for (i = 0; i < BIAS_OSCS; i++) {
		s32 a, ld, L;

		if (i == 0)
			a = BIAS_ONE28;
		else if (i == 1)
			a = h >= 8192 ? BIAS_ONE28 : h << 15;    /* 0 -> full over the first quarter */
		else
			a = a_up;
		/* log2 of 1 / (decay fraction): 0 for oscillators 1 and 2, HARM_D_MIN..HARM_D_MAX for 3..6 (geometric),
		 * and each higher oscillator a little shorter (HARM_TILT) */
		ld = i < 2 ? 0 : BIAS_LOG2_INV_DMIN - (BIAS_LOG2_DSPAN >> 15) * stretch;
		ld += i * BIAS_LOG2_INV_TILT;
		L = ld ? qmul(L0, bias_exp2p(ld)) << 7 : L0;     /* the oscillator's attenuation per block, Q24 */
		tr->amp[i] = a;
		tr->dec[i] = bias_exp2n(L);
		sum += a;
	}
	tr->sum_gain = (BIAS_ONE28 / (sum >> 12)) << 16;
	tr->last_decay = tr->decay;
	tr->last_harm = tr->harm;
}

/* Spread: ratio h * (p/h)^s (log, the spec's default) or h + (p - h) s, both ends exact integers. */
static void bias_spread(struct bias_track *tr)
{
	s32 s = unit(tr->spread), i;

	for (i = 0; i < BIAS_OSCS; i++) {
		s32 h = i + 1, p = BIAS_PRIME[i];

		if (s == 0)
			tr->rat[i] = h << 16;
		else if (s == 32768)
			tr->rat[i] = p << 16;
#if BIAS_CFG_SPREAD_LOG
		else
			tr->rat[i] = h * (bias_exp2p(((BIAS_SPREAD_LR[i] >> 9) * s) >> 6) >> 8);
#else
		else
			tr->rat[i] = (h << 16) + (((p - h) * s) << 1);
#endif
	}
	tr->last_spread = tr->spread;
}

/* Block-rate setup: envelopes, the pitch-locked clock, Morph, the folder and the gain steps for this block. */
static void bias_setup(struct bias_track *tr)
{
	s32 i, k, n, pitch, shift, sum_ge = 0;
	s32 *ge = tr->ge;
	u32 x;

	if (tr->decay != tr->last_decay || tr->harm != tr->last_harm)
		bias_harmonic(tr);
	if (tr->spread != tr->last_spread)
		bias_spread(tr);
	if (tr->fresh) {                               /* the onset: every oscillator at its full level at once */
		for (i = 0; i < BIAS_OSCS; i++)
			tr->g[i] = tr->amp[i];
		tr->pe = BIAS_ONE28;
	}

	/* GATE hold: the envelopes freeze once the fundamental's falls to the sustain level */
	tr->sustaining = (tr->gate && tr->env[0] <= BIAS_SUSTAIN) ? 1 : 0;

	/* The clock. f_now includes Liquid's envelope, so the envelope moves the clock. N = 2^k is the largest
	 * power of two with N x f_now below the window's top W (WINDOW_KHZ): the internal rate N x f_now is in
	 * [W/2, W), and rho = N x f_now / 48 kHz is the internal samples per output sample. */
	pitch = tr->note + BIAS_OFFSET + (tr->penv >> 12) * BIAS_LIQ;
	if (pitch < 0)
		pitch = 0;
	else if (pitch > (127 << 16))
		pitch = 127 << 16;
	x = bias_inc(pitch);
	if (!x)
		x = 1;
	k = __builtin_clz(x);                          /* one ff1 on the ColdFire */
	x <<= k;
	if (x >= BIAS_WTOP && k > 1) {                 /* N x f_now at or above the window's top: one octave fewer */
		x >>= 1;
		k--;
	}
	tr->rho = x >> 1;                              /* Q31 */
	shift = 16 - k;                                /* oscillator i advances rat_i / N cycles per tick */
	for (i = 0; i < BIAS_OSCS; i++)
		tr->os[i].inc = shift >= 0 ? (u32)tr->rat[i] << shift : (u32)tr->rat[i] >> -shift;
	if (tr->fresh) {                               /* the inner loop steps before it reads: start one step back */
		for (i = 0; i < BIAS_OSCS; i++)
			tr->os[i].ph -= tr->os[i].inc;
		tr->fresh = 0;
	}
	n = (s32)((((u64)tr->acc) + ((u64)(x >> 1) << 5)) >> 31);  /* internal samples in this block */

	/* Morph: position 0..3 over the four shapes, two neighbours crossfaded */
	{
		s32 pos = (unit(tr->morph) << 1) * 3;

		tr->wi = pos >> 16;
		tr->wf = (pos & 0xffff) >> 1;
		if (tr->wi > 2) {                      /* square: all of the pair's second shape */
			tr->wi = 2;
			tr->wf = 32768;
		}
	}

	/* Envelopes at block rate, the gains stepped per internal sample so nothing zippers. The levels fall with the
	 * oscillator's number and so do the decays, so the oscillators above -66 dB are the first nact. */
	{
		s32 fa = tr->wf >= 32768 ? 0 : 0x7fffffff - (tr->wf << 16);   /* shape a's share, Q31 */

		tr->nact = 0;
		for (i = 0; i < BIAS_OSCS; i++) {
			s32 e = tr->sustaining ? tr->env[i] : bias_mul28(tr->env[i], tr->dec[i]);
			s32 g0 = tr->g[i], g1, a0, b0;
			struct bias_osc *q = &tr->os[i];

			tr->env[i] = e;
			g1 = ge[i] = bias_mul28(tr->amp[i], e);
			sum_ge += g1;
			/* the two shares of the gain and of its step; ga + gb is the gain */
			a0 = qmul(g0, fa);
			b0 = qmul(g1 - g0, BIAS_RECIP[n]);    /* the gain's step per internal sample */
			q->ga = a0;
			q->gb = g0 - a0;
			q->dga = qmul(b0, fa);
			q->dgb = b0 - q->dga;
			if (g0 > BIAS_SILENT || g1 > BIAS_SILENT)
				tr->nact = i + 1;
		}
	}

	/* Fold: the first 3/4 sets the threshold T from 1 down to FOLD_T_MIN (geometric), the top 1/4 mixes in the
	 * pulses. 1 / (T x sum of levels) is one multiply per internal sample. */
	{
		s32 f = unit(tr->fold);

		if (f) {
			s32 u = f < BIAS_KNEE ? (f * 4) / 3 : 32768;
			s32 inv_t = bias_exp2p((BIAS_LOG2_INV_TMIN >> 15) * u);
			s32 pe;

			tr->fold_s = qmul(inv_t, tr->sum_gain) << 5;
			tr->fold_amt = u << 13;
			tr->pulse_mix = f > BIAS_KNEE ? (BIAS_PULSE >> 15) * ((f - BIAS_KNEE) * 4) : 0;
			tr->pulse_c = BIAS_PULSE_C[x >> 26];      /* x: rho in Q32 */
			/* the post-fold envelope: the six envelopes' level-weighted average, scaled in with Fold */
			pe = BIAS_ONE28 - tr->fold_amt + bias_mul28(tr->fold_amt, bias_mul28(sum_ge, tr->sum_gain));
			tr->pe_end = pe;
			tr->dpe = qmul(pe - tr->pe, BIAS_RECIP[n]);
		} else {
			tr->fold_s = 0;
			tr->fold_amt = 0;
			tr->pulse_mix = 0;
			tr->pe = tr->pe_end = BIAS_ONE28;
			tr->dpe = 0;
		}
	}

	/* Liquid's envelope falls once per block */
	if (tr->penv) {
		tr->penv = bias_mul28(tr->penv, BIAS_PENV_DEC);
		if (tr->penv < 1024)
			tr->penv = 0;
	}

}

void bias_update(s32 pmod, char *v, const char *p)
{
	s32 t = BIAS_VOICE_INDEX(v);
	struct bias_track *tr;
	s32 h, m, s, f, n, first = 0;

	if (t < 0 || t >= 6)                          /* only the six track voices */
		return;
	tr = &bias_trk[t];

	if (V32(MARK_OFF) != MARK) {                  /* first block on this machine */
		bias_reset(tr);
		first = 1;
		V32(MARK_OFF) = MARK;
	}

	h = knob(P16(0x16));                          /* COLOR: Harmonic */
	m = knob(P16(0x18));                          /* SHAPE: Morph */
	s = knob(P16(0x1a));                          /* SWEEP: Spread */
	f = knob(P16(0x1c));                          /* CONTOUR: Fold */
	if (first) {
		tr->harm = h;
		tr->morph = m;
		tr->spread = s;
		tr->fold = f;
	} else {
		tr->harm = smooth(tr->harm, h);
		tr->morph = smooth(tr->morph, m);
		tr->spread = smooth(tr->spread, s);
		tr->fold = smooth(tr->fold, f);
	}

	tr->trig = V32(0x38) ? 1 : 0;
	/* A new note is down. v+0x34 is the trig in this block and v+0x38 the one in the previous block;
	 * the first note after a machine change arrives only as v+0x38, so accept either. */
	if (V32(0x34) || V32(0x38))
		tr->held = 1;
	if (V32(0x3c))                                /* the note ended: GATE releases the hold */
		tr->held = 0;
	tr->gate = (P16(0x20) != 0) && tr->held;
	if (!V32(0x34)) {                             /* the trig's note, one block after the trig */
		n = pmod < 0 ? 0 : (pmod > (127 << 16) ? 127 << 16 : pmod);
		n += ((s32)P16(0x14) << 8) + (((s32)P16(0x22) - 0x4000) << 3) - (64 << 16);
		tr->note = n;
	}

	tr->decay = clamp_k((s8)p[0x24]);

	/* PUNCH on = Liquid: the pitch envelope fires on the next trig; off = Skin, and a running envelope stops */
	tr->liquid = P16(0x1e) != 0;
	if (!tr->liquid)
		tr->penv = 0;

	if (tr->trig)
		bias_start(tr);
}

/* The soft limit, f(y) = y - K y^3 on |y| <= 1 (K = SOFT_K), and the output. */
#define BIAS_SOFTK   Q28F(BIAS_CFG_SOFT_K)

/* one oscillator of the inner loop: its phase, the two Morph shapes at that phase into the EMAC, each with its
 * share of the gain, and the gains' steps. The phase is stepped first and read after, so a trig sets it one step
 * back (bias_setup). On the ColdFire it is written out (13 instructions, a multiply-with-load among them; the
 * compiler's took 21), with the same arithmetic as the C below, which the host build uses. */
#ifdef __mcoldfire__
#define BIAS_OSC(i) __asm__ volatile(                                            \
	"move.l  %c[o]+0(%[q]),%%d0\n\t"                                             \
	"add.l   %c[o]+4(%[q]),%%d0\n\t"          /* ph += inc */                   \
	"move.l  %%d0,%c[o]+0(%[q])\n\t"                                             \
	"lsr.l   %[s],%%d0\n\t"                   /* the table index */             \
	"move.l  (%[wa],%%d0.l*4),%%d1\n\t"       /* shape a */                     \
	"move.l  (%[wb],%%d0.l*4),%%d3\n\t"       /* shape b */                     \
	"move.l  %c[o]+8(%[q]),%%d2\n\t"          /* ga */                          \
	"mac.l   %%d1,%%d2,%c[o]+16(%[q]),%%d4,%%acc0\n\t"   /* a x ga; loads gb */  \
	"add.l   %c[o]+12(%[q]),%%d2\n\t"         /* ga + dga */                    \
	"move.l  %%d2,%c[o]+8(%[q])\n\t"                                             \
	"mac.l   %%d3,%%d4,%%acc0\n\t"             /* b x gb */                      \
	"add.l   %c[o]+20(%[q]),%%d4\n\t"         /* gb + dgb */                    \
	"move.l  %%d4,%c[o]+16(%[q])"                                                \
	: : [q] "a"(os), [wa] "a"(wa), [wb] "a"(wb), [s] "d"(s22), [o] "i"((i) * (s32)sizeof(struct bias_osc)) \
	: "d0", "d1", "d2", "d3", "d4", "cc", "memory")
#else
#define BIAS_OSC(i) do {                                                         \
	u32 ph_ = os[i].ph + os[i].inc;                                              \
	os[i].ph = ph_;                                                              \
	MAC0(wa[ph_ >> 22], os[i].ga);                                               \
	MAC0(wb[ph_ >> 22], os[i].gb);                                               \
	os[i].ga += os[i].dga;                                                       \
	os[i].gb += os[i].dgb;                                                       \
} while (0)
#endif

void bias_render(s32 *out, char *v)
{
	s32 t = BIAS_VOICE_INDEX(v);
	struct bias_track *tr;
	s32 o, i, prev, cur;
	s32 fold_s, pmix, pc, pulse, py, pdir, pe, dpe, sg;
	u32 acc, rho;
	const s32 *wa, *wb;
	struct bias_osc *os;
	s32 nact;
#ifdef __mcoldfire__
	s32 s22 = 22;                                 /* the table index's shift, kept in a register */
#endif

	if (t < 0 || t >= 6) {
		for (o = 0; o < 32; o++)
			out[o] = 0;
		return;
	}
	tr = &bias_trk[t];

	if (tr->idle && !tr->trig && !V32(0x34)) {
		for (o = 0; o < 32; o++)
			out[o] = 0;
		return;
	}

	bias_setup(tr);
	wa = BIAS_WAVE[tr->wi];
	wb = wa + 1024;                               /* the next shape */
	os = tr->os;
	nact = tr->nact;
	acc = tr->acc;
	rho = tr->rho;
	prev = tr->prev;
	cur = tr->cur;
	fold_s = tr->fold_s;
	pmix = tr->pulse_mix;
	pc = tr->pulse_c;
	pulse = tr->pulse;
	py = tr->prev_y;
	pdir = tr->last_dir;
	pe = tr->pe;
	dpe = tr->dpe;
	sg = tr->sum_gain;

	for (o = 0; o < 32; o++) {
		u32 a2 = acc + rho;                   /* both below 2^31: bit 31 is the carry */

		if (a2 & 0x80000000u) {
			a2 &= 0x7fffffff;
			/* One internal sample: the oscillators (the EMAC sums them and does the Morph crossfade), the
			 * normalisation, the folder, the pulses, the post-fold envelope, the soft limit. Q28. */
			s32 y;

			/* the oscillators that sound, unrolled */
			switch (nact & 7) {                   /* every value covered: no range test */
			case 7:
			case 6:
				BIAS_OSC(5);
				/* fall through */
			case 5:
				BIAS_OSC(4);
				/* fall through */
			case 4:
				BIAS_OSC(3);
				/* fall through */
			case 3:
				BIAS_OSC(2);
				/* fall through */
			case 2:
				BIAS_OSC(1);
				/* fall through */
			case 1:
			case 0:
				BIAS_OSC(0);
			}
			y = movclr0();
			if (fold_s) {
				/* u = x / T, x the normalised sum; a triangle of period 4 reflects at +-1 as often as
				 * needed, which is the unlimited folder; dividing by T was the amplitude compensation.
				 * Q24, the wrap is a mask. */
				s32 m = (((qmul(y, fold_s) << 1) - BIAS_ONE24) & 0x03ffffff) - 2 * BIAS_ONE24;

				y = ((m < 0 ? -m : m) - BIAS_ONE24) << 4;
				if (pmix) {
					/* top quarter: a +-1 pulse with the extremum's sign at every peak and trough */
					s32 dy = y - py;

					if (dy) {
						s32 dir = dy >> 31;

						if (dir != pdir)
							pulse = py >= 0 ? pmix : -pmix;   /* the +-1 pulse times its mix */
						pdir = dir;
					}
					py = y;
					y += pulse;
					pulse = bias_mul28(pulse, pc);
				}
				y = bias_mul28(y, pe);
				pe += dpe;
			} else {
				y = bias_mul28(y, sg);
			}
			if (y > BIAS_ONE28)
				y = BIAS_ONE28;
			else if (y < -BIAS_ONE28)
				y = -BIAS_ONE28;
			prev = cur;
			/* Q28 -> Q30: Q31 / 2, the output's level (that of SLEI and TINY) */
			cur = (y - bias_mul28(BIAS_SOFTK, bias_mul28(bias_mul28(y, y), y))) << 2;
		}
		acc = a2;
		out[o] = prev + qmul(cur - prev, (s32)acc);   /* linear interpolation to 48 kHz */
	}

	tr->acc = acc;
	tr->prev = prev;
	tr->cur = cur;
	tr->pulse = pulse;
	tr->prev_y = py;
	tr->last_dir = pdir;
	for (i = 0; i < BIAS_OSCS; i++)               /* land the steps exactly on their targets */
		tr->g[i] = tr->ge[i];
	tr->pe = tr->pe_end;

	if (!tr->nact) {                              /* every oscillator below -66 dB: the voice is silent */
		if (++tr->quiet >= BIAS_QUIET_BLOCKS)
			tr->idle = 1;
	} else {
		tr->quiet = 0;
	}
}
