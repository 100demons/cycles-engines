/*
 * CRACK: a hybrid-rate drum voice for the Model:Cycles (OS 1.13), integer C99 only.
 *
 * The file is pure: it knows nothing about the OS, the voice loop or addresses.  The bridge (crack.c) fills a
 * cr_in and calls cr_block() once per block of 32 frames at 48 kHz.  The core (body / wires / ring / heat) runs at
 * 24 kHz, 16 samples per block; the click / air layer runs at 48 kHz, added after a 4-point upsampler.  The output
 * is Q31 (the OS's voice loop halves every sample afterwards, as it does for the stock machines).
 *
 * No float, no 64-bit arithmetic, no libc, no division in per-sample code.  Every product is range-checked by
 * construction; phases are uint32_t; envelopes and audio are clamped.
 *
 * Units: audio Q15 internally (32767 = full scale), phases 2^32 = one cycle at 24 kHz, envelopes Q15 except the two
 * pitch envelopes (Q8 semitones).  Times are ms Q8, frequencies Hz Q8, levels Q15, ratios Q12.
 */
#ifndef CRACK_DSP_H
#define CRACK_DSP_H

#include <stdint.h>

typedef int32_t s32;
typedef uint32_t u32;
typedef int16_t s16;
typedef uint16_t u16;
typedef int8_t s8;
typedef uint8_t u8;

#include "crack_tables.h"

#if defined(__GNUC__)
#define CR_AI static inline __attribute__((always_inline))
#else
#define CR_AI static inline
#endif

#define CR_BLOCK        16                  /* core samples per 32-frame block (24 kHz) */
#define CR_COMB         128                 /* ring delay buffer, samples */
#define CR_TRANSPOSE_OFF (5 << 16)          /* f0 = note + PITCH + FINE - 5 semitones (55 -> 196 Hz) */
#define CR_REF_NOTE     (55 << 16)          /* the home f0 in semitones << 16 */
#define CR_MUTE_INC     1789569707u         /* 10 kHz at 24 kHz */
#ifndef CR_MASTER_Q12
#define CR_MASTER_Q12   2900                /* final output trim, Q12 */
#endif
#ifndef CR_BODY_GAIN_Q12
#define CR_BODY_GAIN_Q12 1625                /* internal layer trims (the macro tables stay as specified) */
#endif
#ifndef CR_WIRE_GAIN_Q12
#define CR_WIRE_GAIN_Q12 2300
#endif
#ifndef CR_CLICK_GAIN_Q12
#define CR_CLICK_GAIN_Q12 15000
#endif
#ifndef CR_IN_Q12
#define CR_IN_Q12       2867                /* sum into the heat chain */
#endif
#ifndef CR_AIR_GAIN_Q12
#define CR_AIR_GAIN_Q12 1000                 /* the 48 kHz air is a sheen, not the main noise */
#endif

/* what the bridge gives each block */
typedef struct {
	s32 note;               /* the trig's note plus PITCH - 64 and FINE, semitones << 16 */
	s16 color, shape, sweep, contour, decay;   /* knob() values, 0 .. 32766 */
	u8 punch, gate, vel;    /* PUNCH and GATE on/off; velocity 1 .. 127 (0 is read as 100 upstream) */
	u8 trig, end;           /* a trig in this block; the note ended in this block */
	u8 seed;                /* the track number, for the deterministic round robin */
} cr_in;

typedef struct {
	s16 level;              /* Q15 (pitch envelopes: Q8 semitones) */
	s16 step;               /* change per sample */
	s16 peak;               /* the value at the trig */
	s16 mul;                /* decay multiplier per block, Q15 */
	u16 atk;                /* samples of attack left */
	u8 state;               /* 0 idle, 1 attack, 2 decay, 3 hold, 4 fade */
} cr_env;

typedef struct {
	/* cached macros (16-bit, as the spec requires) */
	s16 m[CR_M_COUNT];

	/* input change detector */
	s32 last_note;
	s16 last_color, last_shape, last_sweep, last_contour, last_decay;
	u8 last_vel, last_flags, mapped;

	/* body: three modes */
	u32 phase[3], inc[3];
	s32 dinc[3];
	cr_env benv[3];
	cr_env tenv, spenv;     /* tension and spike pitch envelopes, Q8 semitones */
	cr_env wenv;            /* wire / clap envelope, Q15 */
	s16 wire_delay;
	u8 reset_inc;

	/* wires */
	u32 rng;                /* noise */
	u32 rr;                 /* round robin */
	s32 ic1, ic2;           /* TPT states */
	s32 g, a1, a2, a3, k;   /* per-block filter coefficients */

	/* clap */
	s16 clap_level, clap_mul, clap_amt;
	u16 clap_next, clap_space;
	u8 clap_idx, clap_bursts, clap_tail;

	/* ring */
	s16 comb[CR_COMB];
	s32 comb_lp;
	s32 ring_delay, ring_fb, ring_damp, ring_lvl;
	u16 widx;
	u8 ring_on;

	/* heat */
	s32 tb_level, tb_mul;
	s32 comp, comp_step;
	s32 tilt_lp;
	s32 dcx, dcy;
	s32 crush_cnt, crush_hold, crush_mask;
	s32 crush_val;
	u8 crush_bits;

	/* click / air */
	u32 rng48;
	s32 lp48;
	u16 click_env;
	s16 click_mul, click_lp1, click_lp2;
	s32 hp_a;

	/* upsampler history */
	s16 m1, m2, last_out;

	/* state */
	u8 active;
	s16 vel_gain;

	/* per-block diagnostics for tests.brk (not used by the sound) */
	s32 d_f0, d_mode1;      /* Hz Q8 */
	s16 d_body, d_wire, d_tension;
	u8 d_hold, d_bits;
} cr_voice;

/* ------------------------------------------------------------------------------------------------------------
 * Small integer helpers
 */
CR_AI s32 cr_clamp(s32 x, s32 lo, s32 hi)
{
	return x < lo ? lo : (x > hi ? hi : x);
}

CR_AI s32 cr_abs(s32 x)
{
	return x < 0 ? -x : x;
}

/* piecewise-linear over the five knob breakpoints (x is 0 .. 32766, 8.8) */
CR_AI s32 cr_lut5(const s16 *t, s32 x)
{
	u32 s;
	s32 f;

	s = (u32)x >> 13;
	f = x & 8191;
	return (s32)t[s] + (((s32)(t[s + 1] - t[s]) * f) >> 13);
}

/* DECAY has six breakpoints (0, 20, 40, 64, 90, 100); 101..127 use the last value */
static inline s32 cr_decay_lut(const s16 *t, s32 x)
{
	int i;

	if (x < 0)
		x = 0;
	if (x > 32512)
		x = 32512;
	for (i = 0; i < 5; i++) {
		if (x <= (s32)CR_DECAY_BP[i + 1]) {
			s32 lo = (s32)CR_DECAY_BP[i], hi = (s32)CR_DECAY_BP[i + 1];
			return (s32)t[i] + (((s32)(t[i + 1] - t[i]) * (x - lo)) / (hi - lo));
		}
	}
	return t[5];
}

/* 2^l, where l is log2 in Q8; result Q15 (32768 = 1.0) */
CR_AI s32 cr_exp2_q8(s32 l)
{
	s32 i, f, v;

	i = l >> 8;
	f = l & 255;
	v = CR_POW2[f >> 3];
	v += ((CR_POW2[(f >> 3) + 1] - v) * (f & 7)) >> 3;
	if (i >= 0) {
		if (i > 15)
			i = 15;
		return v << i;
	}
	if (i < -15)
		return 0;
	return v >> -i;
}

/* per-block decay multiplier for a T60 time in ms Q8 */
static inline s16 cr_t60_mul(s32 t_q8)
{
	if (t_q8 < 16)
		return 0;
	return (s16)cr_exp2_q8(-435379 / t_q8);
}

/* per-sample decay multiplier for a T60 time in ms Q8 (click at 48 kHz) */
static inline s16 cr_t60_mul_48(s32 t_q8)
{
	if (t_q8 < 8)
		return 0;
	return (s16)cr_exp2_q8(-13607 / t_q8);
}

/* phase increment of a note in semitones << 16, for 24 kHz (refmachine.phase_inc rescaled) */
static inline u32 cr_phase_inc(s32 note)
{
	s32 d, k, oct;
	u32 frac, a, b, r, inc;

	if (note < 0)
		note = 0;
	if (note > (127 << 16))
		note = 127 << 16;
	d = (note >> 16) - 69;
	k = d % 12;
	oct = d / 12;
	if (k < 0) {
		k += 12;
		oct--;
	}
	a = CR_SEMI[k];
	b = CR_SEMI[k + 1];
	frac = (u32)note & 0xffff;
	r = a + ((((b - a) >> 4) * (frac >> 4)) >> 8);
	inc = (78741068u >> 12) * (r >> 12);
	return oct >= 0 ? inc << oct : inc >> -oct;
}

/* parabolic sine, Q15 (same shape as refmachine.c) */
CR_AI s32 cr_sine(u32 ph)
{
	u32 p = ph >> 16;
	s32 x = (s32)(p & 0x7fff);
	s32 s = (x * (32768 - x)) >> 13;

	return (p & 0x8000) ? -s : s;
}

/* triangle fold around +-32768, period 4 * 32768 (mask, no division) */
CR_AI s32 cr_out_slew(cr_voice *v, s32 x)
{
	if (x - (s32)v->last_out > 32767)
		x = (s32)v->last_out + 32767;
	else if ((s32)v->last_out - x > 32767)
		x = (s32)v->last_out - 32767;
	v->last_out = (s16)x;
	return x;
}

CR_AI s32 cr_fold(s32 x)
{
	u32 a = (u32)cr_abs(x) & 0x1ffffu;

	if (a < 32768u)
		return (s32)a;
	if (a < 98304u)
		return 65536 - (s32)a;
	return (s32)a - 131072;
}

/* ------------------------------------------------------------------------------------------------------------
 * Envelopes
 */
static inline void cr_env_trig(cr_env *e, s32 peak, s32 attack_samples)
{
	e->peak = (s16)cr_clamp(peak, 0, 32767);
	if (attack_samples <= 0) {
		e->level = e->peak;
		e->step = 0;
		e->atk = 0;
		e->state = 2;
	} else {
		e->atk = (u16)(attack_samples > 65535 ? 65535 : attack_samples);
		e->state = 1;
		e->step = (s16)((e->peak - e->level) / attack_samples);
	}
}

/* one block: exponential decay, optional GATE hold at -12 dB */
static inline void cr_env_block(cr_env *e, s32 mul, u8 holdable, u8 gate)
{
	s32 target;

	if (e->state == 0)
		return;
	if (e->state == 1) {
		if (e->atk <= CR_BLOCK) {
			e->level = e->peak;
			e->step = 0;
			e->atk = 0;
			e->state = 2;
		} else {
			e->atk = (u16)(e->atk - CR_BLOCK);
		}
		return;
	}
	if (holdable && gate && e->state != 3 && e->level <= (s16)(e->peak >> 2)) {
		e->state = 3;
		e->step = 0;
		return;
	}
	if (e->state == 3) {
		if (gate)
			return;
		e->state = 2;
	}
	target = ((s32)e->level * mul) >> 15;
	if (target < 1 && e->level < 32) {
		e->level = 0;
		e->step = 0;
		e->state = 0;
		return;
	}
	e->step = (s16)((target - e->level) >> 4);
	if (!e->step && target < e->level)
		e->step = -1;
}

CR_AI void cr_env_advance(cr_env *e, s32 n)
{
	s32 l;

	if (e->state == 0)
		return;
	l = (s32)e->level + (s32)e->step * n;
	if (l < 0)
		l = 0;
	if (l > 32767)
		l = 32767;
	if (e->state == 1 && l > e->peak)
		l = e->peak;
	e->level = (s16)l;
}

CR_AI s32 cr_env_sample(cr_env *e)
{
	s32 l = (s32)e->level + e->step;

	if (l < 0)
		l = 0;
	if (l > 32767)
		l = 32767;
	if (e->state == 1 && l > e->peak)
		l = e->peak;
	e->level = (s16)l;
	return l;
}

/* the wire envelope has the 80s gated shape above DECAY 100: attack, a drooping hold, a 12 ms fade */
static inline void cr_wire_block(cr_voice *v, const cr_in *in, s32 mul, s32 gated_hold)
{
	cr_env *e = &v->wenv;

	if (v->wire_delay > 0) {
		v->wire_delay = (s16)(v->wire_delay - CR_BLOCK);
		if (v->wire_delay > 0)
			return;
	}
	if (e->state == 6) {
		e->level = 32767;
		e->step = 0;
		e->state = 2;
	}
	if (gated_hold > 0 && e->state == 2)
		e->state = 5;                          /* DECAY > 100: start the 80s hold */
	if (e->state == 0)
		return;
	if (e->state == 1) {
		if (e->atk <= CR_BLOCK) {
			e->level = e->peak;
			e->step = 0;
			e->atk = 0;
			e->state = gated_hold > 0 ? 5 : 2;
			if (e->state == 5) {
				e->atk = (u16)cr_clamp(gated_hold, 1, 65535);
				e->step = (s16)(((s32)(e->peak * 23170 >> 15) - e->level) / (s32)e->atk);
			}
		} else {
			e->atk = (u16)(e->atk - CR_BLOCK);
		}
		return;
	}
	if (e->state == 3) {                    /* GATE hold: flat until the note ends */
		if (in->gate && !in->end)
			return;
		e->state = 2;
	}
	if (e->state == 5) {                    /* 80s gated shape: droop to -3 dB, then fade */
		s32 target = (s32)(e->peak * 23170 >> 15);

		if (e->atk == 0) {
			e->atk = (u16)cr_clamp(gated_hold, 1, 65535);
			e->step = (s16)((target - e->level) / (s32)e->atk);
		}
		if (e->atk > CR_BLOCK) {
			e->atk = (u16)(e->atk - CR_BLOCK);
			e->step = (s16)((target - e->level) >> 4);
			return;
		}
		e->state = 4;
		e->step = (s16)(-e->level / 18);    /* 12 ms at 24 kHz */
		return;
	}
	if (e->state == 4) {
		if (e->level <= 0) {
			e->level = 0;
			e->step = 0;
			e->state = 0;
		}
		return;
	}
	/* normal decay, with GATE holding at -12 dB until the note ends */
	if (in->gate && !in->end && e->level <= (s16)(e->peak >> 2)) {
		e->state = 3;
		e->atk = 0;
		e->step = 0;
		return;
	}
	{
		s32 target = ((s32)e->level * mul) >> 15;
		if (target < 1 && e->level < 32) {
			e->level = 0;
			e->step = 0;
			e->state = 0;
			return;
		}
		e->step = (s16)((target - e->level) >> 4);
		if (!e->step && target < e->level)
			e->step = -1;
	}
}

/* ------------------------------------------------------------------------------------------------------------
 * Macro mapper (per block, only when an input changed)
 */
static void cr_map(cr_voice *v, const cr_in *in)
{
	s16 *m = v->m;

	m[CR_M_BODY_LVL] = (s16)cr_lut5(CR_MACRO[CR_M_BODY_LVL], in->color);
	m[CR_M_WIRE_LVL] = (s16)cr_lut5(CR_MACRO[CR_M_WIRE_LVL], in->color);
	m[CR_M_WIRE_CUT] = (s16)cr_lut5(CR_MACRO[CR_M_WIRE_CUT], in->color);
	m[CR_M_WIRE_Q] = (s16)cr_lut5(CR_MACRO[CR_M_WIRE_Q], in->color);
	m[CR_M_HP_MIX] = (s16)cr_lut5(CR_MACRO[CR_M_HP_MIX], in->color);
	m[CR_M_RATTLE] = (s16)cr_lut5(CR_MACRO[CR_M_RATTLE], in->color);
	m[CR_M_WIRE_DECAY] = (s16)cr_lut5(CR_MACRO[CR_M_WIRE_DECAY], in->color);
	m[CR_M_CLAP_AMT] = (s16)cr_lut5(CR_MACRO[CR_M_CLAP_AMT], in->color);
	m[CR_M_CLAP_BURSTS] = (s16)cr_lut5(CR_MACRO[CR_M_CLAP_BURSTS], in->color);
	m[CR_M_CLAP_SPACE] = (s16)cr_lut5(CR_MACRO[CR_M_CLAP_SPACE], in->color);
	m[CR_M_AIR_AMT] = (s16)cr_lut5(CR_MACRO[CR_M_AIR_AMT], in->color);
	m[CR_M_WIRE_DELAY] = (s16)cr_lut5(CR_MACRO[CR_M_WIRE_DELAY], in->color);

	m[CR_M_MODE2_SEMI] = (s16)cr_lut5(CR_MACRO[CR_M_MODE2_SEMI], in->shape);
	m[CR_M_MODE3_SEMI] = (s16)cr_lut5(CR_MACRO[CR_M_MODE3_SEMI], in->shape);
	m[CR_M_MODE1_LVL] = (s16)cr_lut5(CR_MACRO[CR_M_MODE1_LVL], in->shape);
	m[CR_M_MODE2_LVL] = (s16)cr_lut5(CR_MACRO[CR_M_MODE2_LVL], in->shape);
	m[CR_M_MODE3_LVL] = (s16)cr_lut5(CR_MACRO[CR_M_MODE3_LVL], in->shape);
	m[CR_M_TENSION_ST] = (s16)cr_lut5(CR_MACRO[CR_M_TENSION_ST], in->shape);
	m[CR_M_TENSION_T] = (s16)cr_lut5(CR_MACRO[CR_M_TENSION_T], in->shape);
	m[CR_M_PM_INDEX] = (s16)cr_lut5(CR_MACRO[CR_M_PM_INDEX], in->shape);
	m[CR_M_RING_RATIO] = (s16)cr_lut5(CR_MACRO[CR_M_RING_RATIO], in->shape);
	m[CR_M_RING_LVL] = (s16)cr_lut5(CR_MACRO[CR_M_RING_LVL], in->shape);
	m[CR_M_RING_FB] = (s16)cr_lut5(CR_MACRO[CR_M_RING_FB], in->shape);
	m[CR_M_RING_DAMP] = (s16)cr_lut5(CR_MACRO[CR_M_RING_DAMP], in->shape);
	m[CR_M_BODY_DECAY] = (s16)cr_lut5(CR_MACRO[CR_M_BODY_DECAY], in->shape);

	m[CR_M_ATTACK_MS] = (s16)cr_lut5(CR_MACRO[CR_M_ATTACK_MS], in->sweep);
	m[CR_M_CLICK_LVL] = (s16)cr_lut5(CR_MACRO[CR_M_CLICK_LVL], in->sweep);
	m[CR_M_CLICK_DECAY] = (s16)cr_lut5(CR_MACRO[CR_M_CLICK_DECAY], in->sweep);
	m[CR_M_CLICK_HP] = (s16)cr_lut5(CR_MACRO[CR_M_CLICK_HP], in->sweep);
	m[CR_M_SPIKE_ST] = (s16)cr_lut5(CR_MACRO[CR_M_SPIKE_ST], in->sweep);
	m[CR_M_SPIKE_T] = (s16)cr_lut5(CR_MACRO[CR_M_SPIKE_T], in->sweep);
	m[CR_M_TRANSIENT_DB] = (s16)cr_lut5(CR_MACRO[CR_M_TRANSIENT_DB], in->sweep);
	m[CR_M_TRANSIENT_T] = (s16)cr_lut5(CR_MACRO[CR_M_TRANSIENT_T], in->sweep);

	m[CR_M_DRIVE] = (s16)cr_lut5(CR_MACRO[CR_M_DRIVE], in->contour);
	m[CR_M_CLIP_HARD] = (s16)cr_lut5(CR_MACRO[CR_M_CLIP_HARD], in->contour);
	m[CR_M_COMP_K] = (s16)cr_lut5(CR_MACRO[CR_M_COMP_K], in->contour);
	m[CR_M_TILT] = (s16)cr_lut5(CR_MACRO[CR_M_TILT], in->contour);
	m[CR_M_BODY_FAT] = (s16)cr_lut5(CR_MACRO[CR_M_BODY_FAT], in->contour);
	m[CR_M_WAVEFOLD] = (s16)cr_lut5(CR_MACRO[CR_M_WAVEFOLD], in->contour);
	m[CR_M_MAKEUP] = (s16)cr_lut5(CR_MACRO[CR_M_MAKEUP], in->contour);

	v->last_note = in->note;
	v->last_color = in->color;
	v->last_shape = in->shape;
	v->last_sweep = in->sweep;
	v->last_contour = in->contour;
	v->last_decay = in->decay;
	v->last_vel = in->vel;
	v->last_flags = (u8)(in->punch | (in->gate << 1));
	v->mapped = 1;
}

static inline u8 cr_changed(const cr_voice *v, const cr_in *in)
{
	return !v->mapped || v->last_note != in->note || v->last_color != in->color
	       || v->last_shape != in->shape || v->last_sweep != in->sweep
	       || v->last_contour != in->contour || v->last_decay != in->decay
	       || v->last_vel != in->vel
	       || v->last_flags != (u8)(in->punch | (in->gate << 1));
}

/* ------------------------------------------------------------------------------------------------------------
 * Reset (machine change / first note)
 */
static void cr_reset(cr_voice *v, u8 seed)
{
	u32 i;
	u8 *p = (u8 *)v;

	for (i = 0; i < sizeof(*v); i++)
		p[i] = 0;
	v->rng = 0x12345678u + seed * 0x9e37u;
	v->rng48 = 0x2468ace0u + seed * 0x1357u;
	v->rr = 0x9e3779b9u + seed * 0x10001u;
	v->hp_a = CR_HP_A[40];
}

/* ------------------------------------------------------------------------------------------------------------
 * Trig
 */
static void cr_trig(cr_voice *v, const cr_in *in)
{
	const s16 *m = v->m;
	s32 vel = in->vel ? in->vel : 100;
	s32 vq, v2, lv, attack, t;
	u32 r;

	if (vel > 127)
		vel = 127;
	vq = vel * 258;                          /* Q15 */
	v2 = (vq * vq) >> 15;
	lv = 3277 + ((29491 * v2) >> 15);        /* 0.1 + 0.9 v^2 */

	v->vel_gain = (s16)lv;

	/* round robin: deterministic per voice and trig */
	v->rr = v->rr * 1664525u + 1013904223u;
	r = v->rr;
	lv = (lv * (32768 + (((s32)(r & 255) - 128) * 9))) >> 15;
	v->rr = v->rr * 1664525u + 1013904223u;
	r = v->rr;

	if (!v->active) {                        /* from silence: phases start at zero, buffer clean */
		v->phase[0] = v->phase[1] = v->phase[2] = 0;
		v->reset_inc = 1;
		v->comb_lp = 0;
		v->widx = 0;
		v->ic1 = v->ic2 = 0;
		v->lp48 = 0;
		v->tilt_lp = 0;
		v->dcx = v->dcy = 0;
		v->crush_cnt = 1;
		v->crush_val = 0;
	}
	v->active = 1;

	attack = ((s32)m[CR_M_ATTACK_MS] * 24) >> 8;    /* ms Q8 -> samples at 24 kHz */
	if (attack < 0)
		attack = 0;

	/* body modes */
	t = (s32)m[CR_M_TENSION_ST];                    /* Q8 semitones, not velocity-scaled */
	t += (in->punch ? (5 << 8) : 0);
	cr_env_trig(&v->tenv, t, 0);
	{
		s32 sp = (s32)m[CR_M_SPIKE_ST];
		sp = (sp * (16384 + ((16384 * v2) >> 15))) >> 15;
		sp += (in->punch ? (5 << 8) : 0);
		cr_env_trig(&v->spenv, sp, 0);
	}
	/* mode 1 peak: body level * mode level * velocity * fat; modes 2/3 their own level */
	{
		s32 bl = (s32)m[CR_M_BODY_LVL];
		s32 fat = (s32)m[CR_M_BODY_FAT];

		bl = (bl * fat) >> 15;
		cr_env_trig(&v->benv[0], (bl * m[CR_M_MODE1_LVL]) >> 15, attack);
		cr_env_trig(&v->benv[1], (bl * m[CR_M_MODE2_LVL]) >> 15, attack);
		{
			s32 m3 = (s32)m[CR_M_MODE3_LVL] + (in->punch ? 9830 : 0);

			cr_env_trig(&v->benv[2], (bl * cr_clamp(m3, 0, 32767)) >> 15, attack);
		}
	}
	/* wires */
	{
		s32 delay = (s32)m[CR_M_WIRE_DELAY] * 24 >> 8;

		v->wenv.step = 0;
		v->wenv.peak = 32767;
		v->wenv.mul = 0;
		v->wenv.atk = (u16)(attack > 0 ? attack : 0);
		v->wire_delay = (s16)(delay < 0 ? 0 : (delay > 65535 ? 65535 : delay));
		if (v->wire_delay > 0) {
			v->wenv.level = 0;
			v->wenv.state = attack > 0 ? 1 : 6;   /* 6: wait, then jump to the peak */
			v->wenv.step = attack > 0 ? (s16)(32767 / attack) : 0;
		} else if (attack > 0) {
			v->wenv.state = 1;
			v->wenv.step = (s16)((32767 - v->wenv.level) / attack);
		} else {
			v->wenv.state = 2;
			v->wenv.level = 32767;
			v->wenv.step = 0;
		}
	}
	/* clap */
	v->clap_amt = (s16)cr_clamp((s32)m[CR_M_CLAP_AMT], 0, 32767);
	v->clap_bursts = (u8)cr_clamp((s32)m[CR_M_CLAP_BURSTS], 0, 4);
	v->clap_space = (u16)(((s32)m[CR_M_CLAP_SPACE] * 24) >> 8);
	v->clap_idx = 0;
	v->clap_tail = 0;
	v->clap_level = v->clap_amt;
	if (v->clap_space < 1)
		v->clap_space = 1;
	v->clap_next = v->clap_space;
	v->clap_mul = cr_t60_mul(6 << 8);

	/* click: instant attack, scaled by v^2 */
	v->click_env = (u16)cr_clamp((((s32)m[CR_M_CLICK_LVL] * v2) >> 15) * CR_CLICK_GAIN_Q12 >> 12, 0, 65535);
	if (in->punch)
		v->click_env = (u16)cr_clamp((s32)v->click_env * 2, 0, 65535);
	v->click_mul = cr_t60_mul_48(in->punch ? (((s32)m[CR_M_CLICK_DECAY] * 10) >> 1) : (s32)m[CR_M_CLICK_DECAY]);
	v->tb_level = 32767;
	v->tb_mul = cr_t60_mul(cr_exp2_q8((s32)m[CR_M_TRANSIENT_T]) >> 7);

	/* compressor starts at its target (no ramp on the first block) */
	{
		s32 e = cr_clamp((s32)v->benv[0].level, 0, 32767);
		s32 k = (s32)m[CR_M_COMP_K];
		s32 den = 32768 + ((k * e) >> 12);

		v->comp = (32768 * 32768) / den;
		v->comp_step = 0;
	}

}

/* ------------------------------------------------------------------------------------------------------------
 * The block
 */
static int cr_block(cr_voice *v, const cr_in *in, s32 *out)
{
	s16 *m = v->m;
	s32 note_base, f0_q8, vel, vq, v2, lv;
	s32 t, i;
	s32 wire_lvl, wire_cut, hp_mix, rattle, air_amt, click_hp;
	s32 fat, ring_lvl, ring_fb, tune_q12;
	s32 drive, hard, tilt, fold_amt, makeup, comp_k;
	s32 tilt_coef = 10650;                   /* 1.5 kHz at 24 kHz, Q15 */
	s32 tb_boost;
	s32 ck;
	u32 ph1, ph2, ph3;
	s32 s1, s2, s3, pm;

	if (cr_changed(v, in))
		cr_map(v, in);
	if (in->trig)
		cr_trig(v, in);
	if (!v->active) {
		for (i = 0; i < 32; i++)
			out[i] = 0;
		return 0;
	}

	/* pitch: note plus PITCH / FINE, then the fixed -5 semitone transpose */
	note_base = in->note - CR_TRANSPOSE_OFF;
	if (note_base < 0)
		note_base = 0;
	if (note_base > (127 << 16))
		note_base = 127 << 16;
	f0_q8 = cr_exp2_q8(2248 + ((note_base - (69 << 16)) / 3072)) >> 7;
	v->d_f0 = f0_q8;

	/* velocity */
	vel = in->vel ? in->vel : 100;
	if (vel > 127)
		vel = 127;
	vq = vel * 258;
	v2 = (vq * vq) >> 15;
	lv = 3277 + ((29491 * v2) >> 15);

	/* body decay: DECAY * SHAPE factor, then the per-mode time factors (0.6 / 0.4) */
	t = cr_exp2_q8(cr_decay_lut(CR_DECAY_BODY, in->decay)) >> 7;
	t = (t * (s32)m[CR_M_BODY_DECAY]) >> 12;
	cr_env_block(&v->benv[0], cr_t60_mul(t), 1, (u8)(in->gate && !in->end));
	cr_env_block(&v->benv[1], cr_t60_mul((t * 2458) >> 12), 0, 0);
	cr_env_block(&v->benv[2], cr_t60_mul((t * 1638) >> 12), 0, 0);
	cr_env_block(&v->tenv, cr_t60_mul(cr_exp2_q8((s32)m[CR_M_TENSION_T]) >> 7), 0, 0);
	cr_env_block(&v->spenv, cr_t60_mul(cr_exp2_q8((s32)m[CR_M_SPIKE_T]) >> 7), 0, 0);
	cr_env_advance(&v->tenv, CR_BLOCK);
	cr_env_advance(&v->spenv, CR_BLOCK);

	/* wire envelope: normal decay, gated shape above DECAY 100 */
	t = cr_exp2_q8(cr_decay_lut(CR_DECAY_WIRE, in->decay)) >> 7;
	t = (t * (s32)m[CR_M_WIRE_DECAY]) >> 12;
	if (in->punch)
		t = (t * 3) >> 2;
	{
		s32 hold = 0;

		if (in->decay > (100 << 8)) {
			s32 hq8 = (60 << 8) + (((in->decay - (101 << 8)) * (390 << 8)) / (26 << 8));

			if (hq8 < (60 << 8))
				hq8 = 60 << 8;
			if (hq8 > (450 << 8))
				hq8 = 450 << 8;
			hold = (hq8 * 24) >> 8;
		}
		cr_wire_block(v, in, cr_t60_mul(t), hold);
	}

	/* body pitch increments for this block */
	{
		s32 ten = v->tenv.level + v->spenv.level;     /* Q8 semitones */
		s32 semi;

		/* mode 1 */
		semi = 0;
		{
			s32 nm = note_base + ((ten + semi) << 8);
			u32 inc = cr_phase_inc(nm);
			v->d_mode1 = cr_exp2_q8(2248 + ((nm - (69 << 16)) / 3072)) >> 7;
			if (inc >= CR_MUTE_INC)
				inc = 0;
			if (v->reset_inc) {
				v->inc[0] = inc;
				v->dinc[0] = 0;
			} else {
				v->dinc[0] = (s32)(inc - v->inc[0]) >> 4;
				v->inc[0] = inc;
			}
		}
		/* mode 2 */
		{
			s32 nm = note_base + (((s32)m[CR_M_MODE2_SEMI]) << 8) + ((ten) << 8);
			u32 inc = cr_phase_inc(nm);
			if (inc >= CR_MUTE_INC)
				inc = 0;
			if (v->reset_inc) {
				v->inc[1] = inc;
				v->dinc[1] = 0;
			} else {
				v->dinc[1] = (s32)(inc - v->inc[1]) >> 4;
				v->inc[1] = inc;
			}
		}
		/* mode 3 */
		{
			s32 nm = note_base + (((s32)m[CR_M_MODE3_SEMI]) << 8) + ((ten) << 8);
			u32 inc = cr_phase_inc(nm);
			if (inc >= CR_MUTE_INC)
				inc = 0;
			if (v->reset_inc) {
				v->inc[2] = inc;
				v->dinc[2] = 0;
			} else {
				v->dinc[2] = (s32)(inc - v->inc[2]) >> 4;
				v->inc[2] = inc;
			}
		}
		v->reset_inc = 0;
	}

	/* wires: level, cutoff (velocity + half-octave pitch tracking), TPT coefficients */
	wire_lvl = (s32)m[CR_M_WIRE_LVL];
	if (in->punch)
		wire_lvl = (wire_lvl * 27853) >> 15;
	wire_cut = cr_exp2_q8((s32)m[CR_M_WIRE_CUT]) >> 7;      /* Hz Q8 */
	wire_cut = (wire_cut * (19661 + ((13107 * vq) >> 15))) >> 15;
	{
		s32 pf = cr_exp2_q8((note_base - CR_REF_NOTE) / 6144);   /* 2^(0.5 * octaves) */

		wire_cut = (wire_cut * pf) >> 15;
	}
	wire_cut = cr_clamp(wire_cut, 100 << 8, 10000 << 8);
	{
		s32 hz = wire_cut >> 8;
		s32 idx = hz / 100;
		s32 fr = hz - idx * 100;

		if (idx < 0)
			idx = 0;
		if (idx > 99)
			idx = 99;
		v->g = (s32)CR_TAN_G[idx] + (((s32)(CR_TAN_G[idx + 1] - CR_TAN_G[idx]) * fr) / 100);
	}
	{
		s32 q = (s32)m[CR_M_WIRE_Q];
		s32 den;

		if (q < 256)
			q = 256;
		v->k = (4096 * 4096) / q;                            /* 1/Q, Q12 */
		den = 4096 + ((v->g * v->g) >> 12) + ((v->g * v->k) >> 12);
		if (den < 1)
			den = 1;
		v->a1 = (4096 * 4096) / den;
		v->a2 = (v->g * v->a1) >> 12;
		v->a3 = (v->g * v->a2) >> 12;
	}
	hp_mix = (s32)m[CR_M_HP_MIX];
	rattle = (s32)m[CR_M_RATTLE];
	rattle = (rattle * (16384 + ((16384 * v2) >> 15))) >> 15;
	air_amt = (s32)m[CR_M_AIR_AMT];

	/* click highpass coefficient */
	click_hp = cr_exp2_q8((s32)m[CR_M_CLICK_HP]) >> 7;
	if (in->punch)
		click_hp = (click_hp * 3) >> 1;
	click_hp = cr_clamp(click_hp, 1000 << 8, 8000 << 8);
	{
		s32 hz = click_hp >> 8;
		s32 idx = (hz - 1000) / 100;
		s32 fr = hz - 1000 - idx * 100;

		if (idx < 0)
			idx = 0;
		if (idx > 69)
			idx = 69;
		v->hp_a = (s32)CR_HP_A[idx] + (((s32)(CR_HP_A[idx + 1] - CR_HP_A[idx]) * fr) / 100);
	}

	/* ring */
	ring_lvl = (s32)m[CR_M_RING_LVL];
	if (in->punch)
		ring_lvl += (s32)(32768 * 25 / 100);
	ring_lvl = cr_clamp(ring_lvl, 0, 32767);
	ring_fb = (s32)m[CR_M_RING_FB];
	if (in->punch)
		ring_fb += (s32)(32768 * 10 / 100);
	ring_fb = cr_clamp(ring_fb, 0, 30146);                 /* 0.92 */
	tune_q12 = (s32)m[CR_M_RING_RATIO];
	if (in->punch)
		tune_q12 = (tune_q12 * 5) >> 2;
	v->ring_lvl = ring_lvl;
	v->ring_fb = ring_fb;
	v->ring_damp = (s32)m[CR_M_RING_DAMP];
	if (ring_lvl > 0) {
		s32 fr = (s32)((s32)f0_q8 * tune_q12) >> 12;       /* Hz Q8 */
		s32 delay = 2;

		if (fr > 0)
			delay = (24000 << 8) / fr;
		while (delay > CR_COMB)
			delay >>= 1;
		if (delay < 2)
			delay = 2;
		v->ring_delay = delay;
		v->ring_on = 1;
	} else {
		if (v->ring_on) {
			for (i = 0; i < CR_COMB; i++)
				v->comb[i] = 0;
			v->comb_lp = 0;
			v->ring_on = 0;
		}
		v->ring_delay = 0;
	}

	/* heat */
	drive = (s32)m[CR_M_DRIVE];
	hard = (s32)m[CR_M_CLIP_HARD];
	tilt = (s32)m[CR_M_TILT];
	fold_amt = (s32)m[CR_M_WAVEFOLD];
	comp_k = (s32)m[CR_M_COMP_K];
	makeup = (s32)m[CR_M_MAKEUP];
	if (fold_amt > 0) {
		s32 heatx = in->contour;
		if (heatx <= (104 << 8))
			fold_amt = 0;
		else
			fold_amt = (fold_amt * (heatx - (104 << 8))) / (32512 - (104 << 8));
	}
	if (in->contour > (104 << 8)) {
		s32 x = in->contour - (104 << 8);
		s32 span = 32512 - (104 << 8);
		s32 tt = (x * 4096) / span;

		if (tt > 4096)
			tt = 4096;
		v->crush_hold = 1 + ((tt * 3) >> 12);
		v->crush_bits = (u8)(16 - ((tt * 10) >> 12));
		if (v->crush_bits < 6)
			v->crush_bits = 6;
		v->crush_mask = (u16)(0xffffu << (16 - v->crush_bits));
	} else {
		v->crush_hold = 1;
		v->crush_bits = 16;
		v->crush_mask = 0xffff;
	}
	tb_boost = (s32)m[CR_M_TRANSIENT_DB];                  /* dB Q8 */
	tb_boost = cr_exp2_q8((tb_boost * 43) >> 8) >> 3;      /* 10^(dB/20), Q12 */
	/* compressor: per-block target from the summed envelope level (no detector) */
	{
		s32 e = (s32)v->benv[0].level;
		s32 ew = (s32)v->wenv.level + (s32)v->clap_level;

		if (ew > e)
			e = ew;
		e = cr_clamp(e, 0, 32767);
		e = (e * lv) >> 15;
		{
			s32 den = 32768 + ((comp_k * e) >> 12);
			s32 target = (32768 * 32768) / den;

			v->comp_step = (target - v->comp) >> 4;
		}
	}

	/* body pre-gain used by the per-sample path */
	fat = (s32)m[CR_M_BODY_FAT];

	ph1 = v->phase[0];
	ph2 = v->phase[1];
	ph3 = v->phase[2];
	pm = (s32)m[CR_M_PM_INDEX];

	/* the output writes two 48 kHz samples per core sample */
	i = 0;

	for (ck = 0; ck < CR_BLOCK; ck++) {
		s32 body, wires, mix, ring, x, y;
		s32 env1, env2, env3;
		s32 n, hp, gain, clk, up;

		/* --- body: mode 2 phase-modulates mode 1 --- */
		s2 = cr_sine(ph2);
		{
			s32 off = ((s32)(s2 >> 4) * pm) << 12;

			s1 = cr_sine(ph1 + (u32)off);
		}
		s3 = cr_sine(ph3);
		ph1 += v->inc[0];
		ph2 += v->inc[1];
		ph3 += v->inc[2];
		v->inc[0] = (u32)((s32)v->inc[0] + v->dinc[0]);
		v->inc[1] = (u32)((s32)v->inc[1] + v->dinc[1]);
		v->inc[2] = (u32)((s32)v->inc[2] + v->dinc[2]);

		env1 = cr_env_sample(&v->benv[0]);
		env2 = cr_env_sample(&v->benv[1]);
		env3 = cr_env_sample(&v->benv[2]);
		body = (s1 * env1) >> 15;
		body += (s2 * env2) >> 15;
		body += (s3 * env3) >> 15;
		body = (body * fat) >> 15;
		body = (body * CR_BODY_GAIN_Q12) >> 12;

		/* --- wires: noise -> TPT SVF -> BP/HP mix -> rattle -> envelope --- */
		v->rng = v->rng * 1664525u + 1013904223u;
		n = (s32)v->rng >> 16;
		{
			s32 v3 = n - v->ic2;
			s32 v1 = (v->a1 * v->ic1 + v->a2 * v3) >> 12;
			s32 v2s = v->ic2 + ((v->a2 * v->ic1 + v->a3 * v3) >> 12);
			s32 bp, hpx;

			v->ic1 = 2 * v1 - v->ic1;
			v->ic2 = 2 * v2s - v->ic2;
			v->ic1 = cr_clamp(v->ic1, -131072, 131072);
			v->ic2 = cr_clamp(v->ic2, -131072, 131072);
			bp = v1;
			hpx = n - ((v->k * v1) >> 12) - v2s;
			wires = (bp * (32768 - hp_mix) + hpx * hp_mix) >> 15;
		}
		{
			s32 wenv = v->wire_delay > 0 ? 0 : cr_env_sample(&v->wenv);
			s32 rg;

			if (v->wire_delay > 0)
				wenv = 0;
			rg = 32768 - rattle + ((rattle * cr_abs(s1)) >> 15);
			wires = (wires * rg) >> 15;
			if (v->clap_idx <= v->clap_bursts && v->clap_bursts > 0) {
				if (v->clap_next <= CR_BLOCK) {
					v->clap_level = v->clap_amt;
					v->clap_next = (u16)(v->clap_next + v->clap_space);
					v->clap_idx++;
					if (v->clap_idx >= v->clap_bursts)
						v->clap_tail = 1;
				} else {
					v->clap_next = (u16)(v->clap_next - CR_BLOCK);
				}
			}
			if (v->clap_tail)
				v->clap_mul = cr_t60_mul(t);
			v->clap_level = (s16)(((s32)v->clap_level * v->clap_mul) >> 15);
			if (v->clap_level < 1)
				v->clap_level = 0;
			wenv += v->clap_level;
			if (wenv > 32767)
				wenv = 32767;
			wires = (wires * wenv) >> 15;
			wires = (wires * wire_lvl) >> 15;
			wires = (wires * CR_WIRE_GAIN_Q12) >> 12;
			/* air follows the wire + clap envelope */
			air_amt = (s32)m[CR_M_AIR_AMT];
			gain = (wenv * air_amt) >> 15;
			gain = (gain * CR_AIR_GAIN_Q12) >> 12;
			gain = (gain * v->vel_gain) >> 15;
		}
		wires = cr_clamp(wires, -32768, 32767);
		body = cr_clamp(body, -32768, 32767);
		mix = body + wires;
		if (mix > 32767)
			mix = 32767;
		if (mix < -32768)
			mix = -32768;

		/* --- ring --- */
		ring = 0;
		if (v->ring_delay > 0) {
			s32 ridx = ((s32)v->widx + CR_COMB - v->ring_delay) & (CR_COMB - 1);
			s32 dly = v->comb[ridx];
			s32 inp;

			v->comb_lp += ((dly - v->comb_lp) * v->ring_damp) >> 15;
			inp = mix + ((v->comb_lp * v->ring_fb) >> 15);
			inp = cr_clamp(inp, -32768, 32767);
			v->comb[v->widx] = (s16)inp;
			v->widx = (u16)((v->widx + 1) & (CR_COMB - 1));
			ring = (dly * v->ring_lvl) >> 15;
		}
		x = mix + ring;
		x = (x * CR_IN_Q12) >> 12;
		if (in->punch)
			x = (x * 41278) >> 15;          /* +2 dB */

		/* --- heat --- */
		{
			s32 tg = 32768 + (((tb_boost << 3) - 32768) * v->tb_level >> 15);

			x = (x * tg) >> 15;
			x = (x * v->comp) >> 15;
			v->tb_level = (v->tb_level * v->tb_mul) >> 15;
			v->comp += v->comp_step;
		}
		x = (x * drive) >> 8;
		if (x > 262144)
			x = 262144;
		if (x < -262144)
			x = -262144;
		if (fold_amt > 0) {
			s32 f = cr_fold(x);

			x = (x * (32768 - fold_amt) + f * fold_amt) >> 15;
		}
		{
			s32 z = cr_clamp(x, -32768, 32767);
			s32 z2 = (z * z) >> 15;
			s32 soft = z - ((z * z2) >> 15) / 3;
			s32 hardx;

			hardx = z;
			y = (soft * (32768 - hard) + hardx * hard) >> 15;
		}
		v->tilt_lp += ((y - v->tilt_lp) * tilt_coef) >> 15;
		y = y + ((tilt * (y - v->tilt_lp)) >> 12);
		if (v->crush_hold > 1) {
			if (--v->crush_cnt <= 0) {
				v->crush_cnt = v->crush_hold;
				v->crush_val = y;
			}
			y = v->crush_val;
		}
		if (v->crush_bits < 16)
			y = (s16)((u16)y & (u16)v->crush_mask);
		{
			s32 dc = y - v->dcx + ((32000 * v->dcy) >> 15);

			v->dcx = y;
			v->dcy = dc;
			y = dc;
		}
		y = (y * makeup) >> 12;
		y = (y * v->vel_gain) >> 15;
		y = cr_clamp(y, -32768, 32767);

		/* --- upsampler: one-sample-latency 4-point midpoint, then click / air at 48 kHz --- */
		up = v->m1;
		{
			s32 odd = (9 * (v->m1 + y) - (v->m2 + y)) >> 4;   /* x[n] stands in for x[n+1] */

			v->m2 = v->m1;
			v->m1 = (s16)y;
			/* even */
			v->rng48 = v->rng48 * 1664525u + 1013904223u;
			n = (s32)v->rng48 >> 16;
			hp = n - v->lp48;
			v->lp48 += (hp * v->hp_a) >> 15;
			v->click_lp1 = (s16)cr_clamp((s32)v->click_lp1 + (((hp - v->click_lp1) * 24576) >> 15), -32768, 32767);
			v->click_lp2 = (s16)cr_clamp((s32)v->click_lp2 + (((v->click_lp1 - v->click_lp2) * 24576) >> 15), -32768, 32767);
			clk = (v->click_lp2 * (cr_clamp((s32)v->click_env + gain, 0, 65535))) >> 15;
			x = up + clk;
			x = cr_clamp(x, -32768, 32767);
			x = (x * CR_MASTER_Q12) >> 12;
			x = cr_out_slew(v, x);
			out[i++] = (s32)((u32)(s16)x << 16);
			v->click_env = (u16)(((s32)v->click_env * v->click_mul) >> 15);
			if (v->click_env < 1)
				v->click_env = 0;
			/* odd */
			v->rng48 = v->rng48 * 1664525u + 1013904223u;
			n = (s32)v->rng48 >> 16;
			hp = n - v->lp48;
			v->lp48 += (hp * v->hp_a) >> 15;
			v->click_lp1 = (s16)cr_clamp((s32)v->click_lp1 + (((hp - v->click_lp1) * 24576) >> 15), -32768, 32767);
			v->click_lp2 = (s16)cr_clamp((s32)v->click_lp2 + (((v->click_lp1 - v->click_lp2) * 24576) >> 15), -32768, 32767);
			clk = (v->click_lp2 * (cr_clamp((s32)v->click_env + gain, 0, 65535))) >> 15;
			x = odd + clk;
			x = cr_clamp(x, -32768, 32767);
			x = (x * CR_MASTER_Q12) >> 12;
			x = cr_out_slew(v, x);
			out[i++] = (s32)((u32)(s16)x << 16);
			v->click_env = (u16)(((s32)v->click_env * v->click_mul) >> 15);
			if (v->click_env < 1)
				v->click_env = 0;
		}
	}
	v->phase[0] = ph1;
	v->phase[1] = ph2;
	v->phase[2] = ph3;

	/* diagnostics */
	v->d_body = v->benv[0].level;
	v->d_wire = v->wenv.level;
	v->d_tension = v->tenv.level;
	v->d_hold = (u8)(v->crush_hold > 255 ? 255 : v->crush_hold);
	v->d_bits = v->crush_bits;

	/* silence: every audible envelope is gone */
	if (!in->trig && v->benv[0].level == 0 && v->benv[1].level == 0
	    && v->benv[2].level == 0 && v->wenv.level == 0 && v->clap_level == 0
	    && v->click_env == 0) {
		v->active = 0;
		for (i = 0; i < CR_COMB; i++)
			v->comb[i] = 0;
		v->comb_lp = 0;
		v->widx = 0;
	}
	return 1;
}

#endif
