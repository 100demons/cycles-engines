/*
 * PUNCH: the sound of the machine, an OPL2-style 2-operator FM voice in integer C (no FPU, no libgcc, no 64-bit
 * arithmetic, no division by zero), 32-sample blocks at 48 kHz.
 *
 * Operators work in the chip's log domain, like the OPL2: a quarter-wave log-sine table and a 2^-x power table,
 * rebuilt from their formulas by tools/gen_punch.py (TINY's tables). PUNCH's engines use the sine, FM or additive
 * connection, modulator feedback 0..7, envelopes in attenuation, key scale level and rate, and the chip's LFO
 * (vibrato 6.07 Hz, tremolo 3.7 Hz).
 *
 * The 12 recipes are PUNCH's percussion engines (KIK, TOM, BNG, CNG, TAI, TIM, MAR, DJB, BLK, GNG, BUB, ZAP);
 * WOD and WTR come from Addy. The recipes are in tools/gen_punch.py and tools/machines/punch/punch.c.
 *
 * This file is compiled twice: for the Cycles (punch.c, -mcpu=54418) and for the computer
 * (tools/emu/punch_host.c), with the same integer arithmetic: the proof compares them sample for sample.
 *
 * Units: phases 32 bits (the table index is the top 10 bits); attenuation in log units (256 = one octave of
 * amplitude, 6.02 dB), envelopes in Q16 of those units; notes in semitones << 16 (60 << 16 = C4); output Q15.
 */
#ifndef PUNCH_DSP_H
#define PUNCH_DSP_H

#include <stdint.h>

typedef int32_t s32;
typedef uint32_t u32;
typedef int16_t s16;
typedef uint16_t u16;
typedef int8_t s8;
typedef uint8_t u8;

#include "punch_tables.h"

#define FM_RATIO_COUNT ((int)(sizeof(FM_MULT) / sizeof(FM_MULT[0])))
#define FM_BLOCK 32
#define FM_SILENT 3328u                         /* 13 octaves down: the power table gives 0 */
#define FM_ATT_MAX ((u32)FM_SILENT << 16)
#define DB(x) ((u32)((x) * 42.52 + 0.5))        /* dB -> log units (constants only) */

enum {
	M_KIK, M_TOM, M_BNG, M_CNG, M_TAI, M_TIM, M_MAR, M_DJB, M_BLK, M_GNG, M_BUB, M_ZAP,
	FM_MODELS
};

/* what the bridge gives each block */
typedef struct {
	s32 note;               /* the trig's note plus PITCH, semitones << 16 */
	s32 sweep;              /* SWEEP, 8.8: the knob position; a fraction (FUNC turns) leaves the ratio steps */
	u8 model, color, contour, decay;
	u8 punch, gate, vel;    /* PUNCH and GATE on/off; vel: the trig's velocity, 1..127 */
	u8 trig, end;           /* a trig in this block; the note ended in this block */
} fm_in;

enum { EG_ATT, EG_DEC, EG_SUS, EG_REL, EG_OFF };

typedef struct {
	u32 ph, inc, dinc;      /* phase; increment, and its change per sample (pitch envelopes) */
	u32 att;                /* envelope attenuation, Q16 log units */
	s32 d;                  /* the envelope's change per sample in this block */
	u32 ka;                 /* attack: share of the attenuation removed per block, Q16 (65536: instant) */
	u32 dr, rr;             /* decay and release: attenuation added per sample, Q16 units */
	u32 sl;                 /* sustain level, Q16 units (FM_ATT_MAX: decays to silence) */
	u32 base;               /* attenuation outside the envelope (level, KSL, tremolo, velocity), log units */
	u8 eg, hold, pad, pad2; /* hold 1: stays at the sustain level while GATE holds the note; 2: until a key off */
} fm_op;

typedef struct {
	fm_op op[2];            /* 0: modulator (or first oscillator), 1: carrier */
	s32 fb1, fb2;           /* the modulator's last two outputs, for feedback */
	u32 lfo_v, lfo_a;       /* chip LFO phases */
	u32 pmul, pk;           /* pitch envelope: multiplier (Q16, 65536 = none) and its decay per block (Q16) */
	u32 inc0, r, cents;     /* this block: base increment, modulator ratio (Q8), modulator detune (cents) */
	u32 bm, bc;             /* this block: modulator and carrier attenuation before tremolo (log units) */
	u8 model, alg, fb, active, held, vib, am;
	u8 click;               /* blocks of mallet click left */
} fm_voice;

/* ------------------------------------------------------------------------------------------------------------
 * Shared helpers
 */
/* phase increment (2^32 f / 48 kHz) of a note in semitones << 16: TINY's note_inc */
static u32 note_inc(s32 note)
{
	u32 n, frac, oct, semi, a, b;

	if (note < 0)
		note = 0;
	if (note > (127 << 16))
		note = 127 << 16;
	n = (u32)note >> 16;
	frac = ((u32)note >> 8) & 0xff;
	oct = n / 12;
	semi = n - oct * 12;
	a = FM_SEMI_INC[semi];
	b = FM_SEMI_INC[semi + 1];
	return (a + (((b - a) * frac) >> 8)) << oct;
}

/* an increment times a ratio in Q8; it wraps like the chip's phase (a frequency above 24 kHz folds back) */
static inline u32 ratio_inc(u32 inc, u32 r_q8)
{
	return (inc >> 8) * r_q8;
}

/* an increment raised or lowered by c cents (0..63) */
static inline u32 cents_up(u32 inc, u32 c)
{
	return inc + (inc >> 16) * FM_CENTS[c > 63 ? 63 : c];
}

static inline u32 cents_down(u32 inc, u32 c)
{
	return inc - (inc >> 16) * FM_CENTS[c > 63 ? 63 : c];
}

/* one operator: log-sine plus attenuation, then the power table; PUNCH's engines all use the sine */
static inline s32 fm_op_out(u32 p, u32 att)
{
	u32 s, tot;

	p &= 0x3ff;
	s = FM_LOGSIN[(p & 0x100) ? (~p & 0xff) : (p & 0xff)];
	tot = s + att;
	if (tot >= FM_SILENT)
		return 0;
	s = FM_POW[tot & 0xff] >> (tot >> 8);
	return (p & 0x200) ? -(s32)s : (s32)s;
}

/* SWEEP as a ratio, Q8. A whole knob position snaps: the travel 0..127 is split into the ratio list (the OPL2's
 * multipliers plus the percussion ratios, including 4:1), a few knob steps each, like the Digitone's ratio list.
 * A value with a fraction (what a FUNC turn leaves when the OS's fine adjustment writes one) is not snapped: the
 * ratio glides between the steps. */
static u32 snap_ratio(s32 sweep)
{
	u32 s = sweep < 0 ? 0 : (u32)sweep, pos, i, f;

	if (s > 0x7f00)
		s = 0x7f00;
	pos = (s * FM_RATIO_COUNT) >> 7;                         /* position in steps, 8.8 */
	i = pos >> 8;
	if (i >= (u32)FM_RATIO_COUNT - 1)
		return FM_MULT[FM_RATIO_COUNT - 1];
	if (!(s & 0xff))
		return FM_MULT[i];
	f = pos & 0xff;
	return FM_MULT[i] + (((FM_MULT[i + 1] - FM_MULT[i]) * f) >> 8);
}


/* 2^(q / 256), q = -2048..1535 (1/256 to ~64), Q16 */
static u32 exp2_q8(s32 q)
{
	u32 e;

	if (q < -2048)
		q = -2048;
	if (q > 1535)
		q = 1535;
	e = (u32)(q + 2048);
	return (FM_EXP2[e & 0xff] << (e >> 8)) >> 8;
}

/* key scale rate: the envelope rates times 2^(k (note - 60) / 48), k quarter-octaves per octave (OPL KSR: 2) */
static u32 ksr_mul(s32 note, s32 k)
{
	return exp2_q8((((note >> 16) - 60) * k * 256) / 48);
}

static inline u32 scale_rate(u32 r, u32 mul)
{
	return ((r >> 6) * (mul >> 6)) >> 4;
}

/* key scale level: attenuation above C3 by 0, 1.5, 3 or 6 dB per octave (k = 0..3) */
static u32 ksl_att(s32 note, u32 k)
{
	static const u8 per_oct[4] = { 0, 64, 128, 255 };
	s32 n = (note >> 16) - 48;

	return n > 0 ? ((u32)n * per_oct[k & 3]) / 12 : 0;
}

/* the chip's LFO: a triangle, -32768..32767 */
static inline s32 tri(u32 ph)
{
	s32 x = (s32)(ph >> 15);

	return x < 65536 ? x - 32768 : 98303 - x;
}

/* knob 0..127 to the modulator's attenuation: 127 is 0 dB, 0 is -36 dB */
static inline u32 depth_att(u32 k)
{
	return (127 - k) * 12;
}

/* decay or release to -60 dB in 5 ms .. 10 s; attack 0.5 ms .. 2 s */
static inline u32 rate(u32 k)
{
	return FM_RATE[k > 127 ? 127 : k];
}


/* ------------------------------------------------------------------------------------------------------------
 * Envelopes: the chip's stages, in attenuation. The per-sample work is one add: each block sets the change per
 * sample d of each operator, and stages change at block edges.
 */
static void eg_start(fm_op *o)
{
	if (o->ka >= 65536) {                                   /* instant attack */
		o->att = 0;
		o->eg = EG_DEC;
	} else
		o->eg = EG_ATT;                                     /* from where it is, as the chip does */
}

static void eg_block(fm_op *o, int held)
{
	u32 a = o->att, e;

	switch (o->eg) {
	case EG_ATT:
		e = a - (((a >> 12) * o->ka) >> 4);                 /* att x (1 - ka) */
		if (e < (24u << 16)) {                              /* within 0.6 dB: the attack is over */
			o->d = -(s32)(a >> 5);
			o->eg = EG_DEC;
		} else
			o->d = -(s32)((a - e) >> 5);
		return;
	case EG_DEC:
		e = a + (o->dr << 5);
		if (e >= o->sl || e < a) {
			o->d = (s32)((o->sl > a ? o->sl - a : 0) >> 5);
			if (o->sl >= FM_ATT_MAX)
				o->eg = EG_OFF;
			else
				o->eg = ((held && o->hold) || o->hold == 2) ? EG_SUS : EG_REL;
		} else
			o->d = (s32)o->dr;
		return;
	case EG_SUS:
		o->d = 0;
		if (!((held && o->hold) || o->hold == 2))
			o->eg = EG_REL;
		return;
	case EG_REL:
		e = a + (o->rr << 5);
		if (e >= FM_ATT_MAX || e < a) {
			o->d = (s32)((FM_ATT_MAX - a) >> 5);
			o->eg = EG_OFF;
		} else
			o->d = (s32)o->rr;
		return;
	default:
		o->att = FM_ATT_MAX;
		o->d = 0;
		return;
	}
}

/* ------------------------------------------------------------------------------------------------------------
 * The PUNCH models: params() turns the knobs into the operators' settings each block (so knob moves act at
 * once), start() keys a note on, tick() runs the LFO, the increments and the envelopes, then a render.
 *
 * vib (vibrato): bit 0 modulator, bit 1 carrier, bit 2 deep (14 cents, else 7). am (tremolo): bit 0 modulator,
 * bit 1 carrier, bit 2 shallow (1 dB, else 4.8 dB).
 */
static void params(fm_voice *v, const fm_in *in)
{
	fm_op *m = &v->op[0], *c = &v->op[1];
	s32 n = in->note;
	u32 col = in->color, con = in->contour, dec = in->decay;
	u32 vel_att = (127u - in->vel) * 6;                     /* velocity brightness: 1/7 dB per step under 127 */

	v->alg = 0;
	v->fb = 0;
	v->vib = 0;
	v->am = 0;
	v->cents = 0;
	v->bm = 0;
	v->bc = DB(6);
	m->hold = 2;                                            /* a modulator holds its level until a key off */
	c->hold = 1;                                            /* a carrier holds only while GATE holds the note */
	m->ka = c->ka = 65536;
	m->sl = c->sl = 0;
	m->dr = m->rr = c->dr = c->rr = rate(dec);
	v->inc0 = note_inc(n);
	v->r = snap_ratio(in->sweep);

	switch (v->model) {
	case M_KIK:                                             /* the classic FM kick: a fast pitch drop, a sine body */
		v->inc0 = note_inc(n - (24 << 16));
		v->fb = 1;
		v->bm = depth_att(col) + DB(3) + ksl_att(n, 2);
		m->sl = FM_ATT_MAX;
		m->dr = m->rr = FM_RATE[30];
		c->sl = FM_ATT_MAX;
		c->dr = c->rr = scale_rate(rate(dec), ksr_mul(n, 2));
		break;
	case M_TOM:                                             /* a tuned membrane, one octave of pitch drop */
		v->inc0 = note_inc(n - (12 << 16));
		v->bm = depth_att(col);
		v->fb = 2;
		m->sl = DB(18) << 16;
		m->dr = m->rr = FM_RATE[26];
		c->sl = FM_ATT_MAX;
		c->dr = c->rr = scale_rate(rate(dec), ksr_mul(n, 2));
		break;
	case M_BNG:                                             /* high, short bongo */
		v->inc0 = note_inc(n - (12 << 16));
		v->bm = depth_att(col);
		v->fb = 2;
		m->sl = FM_ATT_MAX;
		m->dr = m->rr = FM_RATE[20];
		c->sl = FM_ATT_MAX;
		c->dr = c->rr = scale_rate(rate(dec), ksr_mul(n, 4));
		break;
	case M_CNG:                                             /* lower, rounder conga */
		v->inc0 = note_inc(n - (12 << 16));
		v->bm = depth_att(col) + DB(3);
		v->fb = 2;
		m->sl = DB(18) << 16;
		m->dr = m->rr = FM_RATE[24];
		c->sl = FM_ATT_MAX;
		c->dr = c->rr = scale_rate(rate(dec), ksr_mul(n, 2));
		break;
	case M_DJB:                                             /* hand drum: pitch drop, FM body only */
		v->inc0 = note_inc(n - (12 << 16));
		v->bm = depth_att(col) + DB(3);
		v->fb = 3;
		m->sl = FM_ATT_MAX;
		m->dr = m->rr = FM_RATE[22];
		c->sl = FM_ATT_MAX;
		c->dr = c->rr = scale_rate(rate(dec), ksr_mul(n, 2));
		break;
	case M_TAI:                                             /* big low drum, deep pitch drop, FM body only */
		v->inc0 = note_inc(n - (24 << 16));
		v->bm = depth_att(col) + DB(3);
		v->fb = 4;
		m->sl = FM_ATT_MAX;
		m->dr = m->rr = FM_RATE[22];
		c->sl = FM_ATT_MAX;
		c->dr = c->rr = scale_rate(rate(dec), ksr_mul(n, 1));
		break;
	case M_TIM:                                             /* timpani: a felt thud and a long membrane */
		v->inc0 = note_inc(n - (12 << 16));
		v->bm = depth_att(col);
		v->fb = 7;
		m->sl = FM_ATT_MAX;
		m->dr = FM_RATE[24];
		c->sl = FM_ATT_MAX;
		c->dr = scale_rate(rate(dec), ksr_mul(n, 1));
		break;
	case M_MAR:                                             /* marimba bars: fundamental and tuned overtone, added */
		v->alg = 1;
		v->bm = depth_att(col) + vel_att + DB(4);
		v->bc = DB(4);
		m->sl = FM_ATT_MAX;
		c->sl = FM_ATT_MAX;
		c->rr = m->rr = FM_RATE[56];
		v->fb = v->click ? 6 : 0;
		m->dr = scale_rate(rate(14 + con / 2), ksr_mul(n, 4));
		c->dr = scale_rate(rate(dec), ksr_mul(n, 4));
		v->vib = in->punch ? 3 : 0;
		break;
	case M_BLK:                                             /* woodblock: a short, hard pitched click */
		v->fb = 3;
		v->bm = depth_att(col) + DB(4);
		m->sl = c->sl = FM_ATT_MAX;
		m->dr = m->rr = FM_RATE[16];
		c->dr = c->rr = rate(12 + con / 2);
		break;
	case M_GNG:                                             /* gong: low, inharmonic, long FM decay */
		v->inc0 = note_inc(n - (12 << 16));
		v->cents = 4 + ((con * 60) >> 7) + ((col * 40) >> 7);
		v->fb = (col * 8) >> 7;                             /* INDEX: clean carrier to feedback metal */
		v->bm = (127 - col) * 14 + DB(6);                   /* INDEX: nearly no FM to full metal */
		v->bc = DB(6);
		m->sl = FM_ATT_MAX;
		m->dr = m->rr = rate(8 + con * 119 / 127);           /* ENV: fast tick to almost static metal */
		c->sl = FM_ATT_MAX;
		c->dr = c->rr = scale_rate(rate(dec), ksr_mul(n, 1));
		v->am = in->punch ? 3 : 0;
		break;
	case M_ZAP:                                             /* laser: a fast four-octave fall */
		v->bm = (127 - col) * 9 + DB(3);
		v->bc = DB(12);                                     /* ZAP sits below the drum engines */
		v->fb = (u8)(col >> 4);
		m->sl = c->sl = FM_ATT_MAX;
		break;
	case M_BUB:                                             /* bubble: the pitch rises into the note */
		v->bm = depth_att(col) + DB(4);
		v->fb = 1;
		m->sl = FM_ATT_MAX;
		m->dr = m->rr = FM_RATE[26];
		c->sl = FM_ATT_MAX;
		c->dr = c->rr = rate(dec);
		break;
	}
}

/* a note on: phases and feedback to zero, envelopes from where they are (the chip's key on), pitch envelopes */
static void start(fm_voice *v, const fm_in *in)
{
	fm_op *m = &v->op[0], *c = &v->op[1];
	u32 con = in->contour;

	v->held = 1;
	v->fb1 = v->fb2 = 0;
	v->pmul = 65536;
	v->pk = 65536;
	v->click = 0;
	m->ph = c->ph = 0;
	switch (v->model) {
	case M_KIK:                                             /* three octaves down, in 25..150 ms */
		v->pmul = 65536 * 8;
		v->pk = FM_PDECAY[30 + (con * 70) / 127];
		break;
	case M_TOM:                                             /* one octave down, in 40..220 ms */
		v->pmul = 65536 * 2;
		v->pk = FM_PDECAY[40 + (con * 60) / 127];
		break;
	case M_BNG:                                             /* a short drop, a high drum */
		v->pmul = 65536 * 2;
		v->pk = FM_PDECAY[24 + (con * 30) / 127];
		break;
	case M_CNG:
		v->pmul = 65536 + ((con * 18000) >> 7);
		v->pk = FM_PDECAY[40 + (con * 45) / 127];
		break;
	case M_DJB:
		v->pmul = 65536 * 3;
		v->pk = FM_PDECAY[34 + (con * 50) / 127];
		break;
	case M_TAI:                                             /* deep and slow */
		v->pmul = 65536 * 4;
		v->pk = FM_PDECAY[28 + (con * 40) / 127];
		break;
	case M_TIM:
		v->pmul = 65536 + ((con * 7800) >> 7);              /* up to +2 semitones, sags in ~150 ms */
		v->pk = FM_PDECAY[92];
		break;
	case M_MAR:
		v->click = 2;
		break;
	case M_BLK:
		v->pmul = 65536 + 9000;
		v->pk = FM_PDECAY[20];
		break;
	case M_GNG:                                             /* a shallow sag, speed and depth from CONTOUR */
		v->pmul = 65536 + ((con * 16000) >> 7);
		v->pk = FM_PDECAY[40 + (con * 60) / 127];
		break;
	case M_ZAP:                                             /* 4 octaves down, in 5 ms .. 0.4 s */
		v->pmul = 65536 * 16;
		v->pk = FM_PDECAY[30 + (con * 80) / 127];
		break;
	case M_BUB:                                             /* from a quarter of the note, rising */
		v->pmul = 65536 / 4;
		v->pk = FM_PDECAY[20 + (con * 60) / 127];
		break;
	}
	eg_start(m);
	eg_start(c);
}

static void tick(fm_voice *v, const fm_in *in)
{
	fm_op *m = &v->op[0], *c = &v->op[1];
	u32 im, ic, bm = v->bm, bc = v->bc;
	int held = in->gate && v->held;

	v->lfo_v += FM_LFO_VIB;
	v->lfo_a += FM_LFO_AM;
	ic = v->inc0;
	im = v->r ? ratio_inc(ic, v->r) : 0;
	if (v->cents)
		im = cents_up(im, v->cents);
	if (v->vib) {
		s32 t = tri(v->lfo_v);
		u32 cents = ((u32)(t < 0 ? -t : t) * ((v->vib & 4) ? 14u : 7u)) >> 15;

		if (t >= 0) {
			if (v->vib & 1) im = cents_up(im, cents);
			if (v->vib & 2) ic = cents_up(ic, cents);
		} else {
			if (v->vib & 1) im = cents_down(im, cents);
			if (v->vib & 2) ic = cents_down(ic, cents);
		}
	}
	if (v->pmul != 65536) {                                 /* pitch envelope, ramped across the block */
		s32 pd = (s32)v->pmul - 65536;
		s32 next = 65536 + (pd >> 8) * (s32)(v->pk >> 8);
		u32 m0, m1, c0, c1;

		if (next < 256)
			next = 256;
		if ((pd > 0 && next <= 65536) || (pd < 0 && next >= 65536))
			next = 65536;
		m0 = (im >> 8) * (v->pmul >> 8);
		m1 = (im >> 8) * ((u32)next >> 8);
		c0 = (ic >> 8) * (v->pmul >> 8);
		c1 = (ic >> 8) * ((u32)next >> 8);
		m->inc = m0;
		m->dinc = (u32)((s32)(m1 - m0) >> 5);
		c->inc = c0;
		c->dinc = (u32)((s32)(c1 - c0) >> 5);
		v->pmul = (u32)next;
	} else {
		m->inc = im;
		c->inc = ic;
		m->dinc = c->dinc = 0;
	}
	if (v->am) {
		u32 a = ((u32)(tri(v->lfo_a) + 32768) * ((v->am & 4) ? DB(1) : DB(4.8))) >> 16;

		if (v->am & 1)
			bm += a;
		if (v->am & 2)
			bc += a;
	}
	m->base = bm;
	c->base = bc;
	if (v->click)
		v->click--;
	eg_block(m, held);
	eg_block(c, held);
}

/* 2 operators, FM or additive, with feedback */
static void render_2op(fm_voice *v, s32 *out)
{
	fm_op *m = &v->op[0], *c = &v->op[1];
	u32 pm = m->ph, pc = c->ph, im = m->inc, ic = c->inc, dm = m->dinc, dc = c->dinc;
	u32 am = m->att, ac = c->att, bm = m->base, bc = c->base;
	s32 sm = m->d, sc = c->d, f1 = v->fb1, f2 = v->fb2;
	u32 fbs = v->fb ? 10u - v->fb : 0;
	int i;

	if (v->alg) {
		for (i = 0; i < FM_BLOCK; i++) {
			s32 x, fbm = fbs ? (f1 + f2) >> fbs : 0;

			pm += im; pc += ic; im += dm; ic += dc;
			am += (u32)sm; ac += (u32)sc;
			x = fm_op_out((pm >> 22) + (u32)fbm, bm + (am >> 16));
			f2 = f1; f1 = x;
			out[i] = (x + fm_op_out(pc >> 22, bc + (ac >> 16))) * 2;
		}
	} else {
		for (i = 0; i < FM_BLOCK; i++) {
			s32 x, fbm = fbs ? (f1 + f2) >> fbs : 0;

			pm += im; pc += ic; im += dm; ic += dc;
			am += (u32)sm; ac += (u32)sc;
			x = fm_op_out((pm >> 22) + (u32)fbm, bm + (am >> 16));
			f2 = f1; f1 = x;
			out[i] = fm_op_out((pc >> 22) + (u32)(x >> 2), bc + (ac >> 16)) * 4;
		}
	}
	m->ph = pm; c->ph = pc; m->inc = im; c->inc = ic;
	m->att = am; c->att = ac;
	v->fb1 = f1; v->fb2 = f2;
}

/* ------------------------------------------------------------------------------------------------------------
 * One block of one voice. Returns 0 when the voice is silent (out is then all zeros).
 */
static int fm_block(fm_voice *v, const fm_in *in, s32 *out)
{
	int i;

	if (in->trig) {
		if (in->model != v->model) {                        /* a new model: a clean voice */
			u8 *b = (u8 *)v;
			unsigned long k;

			for (k = 0; k < sizeof(*v); k++)
				b[k] = 0;
			v->op[0].att = v->op[1].att = FM_ATT_MAX;
			v->op[0].eg = v->op[1].eg = EG_OFF;
		}
		v->model = in->model < FM_MODELS ? in->model : FM_MODELS - 1;
		v->active = 1;
	}
	if (!v->active)
		goto silent;
	if (in->end)
		v->held = 0;
	params(v, in);
	if (in->trig)
		start(v, in);
	else if (in->end && in->gate) {                         /* key off: every stage goes to its release */
		if (v->op[0].eg < EG_REL)
			v->op[0].eg = EG_REL;
		if (v->op[1].eg < EG_REL)
			v->op[1].eg = EG_REL;
	}
	tick(v, in);
	render_2op(v, out);
	/* a carrier that has finished ends the note (an FM voice also needs its modulator to be off) */
	if (v->op[1].eg == EG_OFF && (v->op[0].eg == EG_OFF || !v->alg))
		v->active = 0;
	return 1;
silent:
	for (i = 0; i < FM_BLOCK; i++)
		out[i] = 0;
	return 0;
}
#endif
