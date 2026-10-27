/*
 * SHEEN: a hybrid-rate metal / hi-hat voice for the Model:Cycles (OS 1.13), integer C99 only.
 *
 * The file is pure: it knows nothing about the OS, the voice loop or addresses.  The bridge (sheen.c) fills a sh_in
 * and calls sh_block() once per block of 32 frames at 48 kHz.  The metal oscillators, noise, tick and air run at
 * 48 kHz; the voice filter, chik, shimmer and heat run at 24 kHz (16 samples per block).  The output is Q31 (the OS
 * halves every sample afterwards, as it does for the stock machines).
 *
 * No float, no 64-bit arithmetic, no libc, no division in per-sample code.  Phases are uint32_t; envelopes and audio
 * are clamped; every product is range-checked by construction.
 *
 * Units: audio Q15 internally (32767 = full scale); phases 2^32 = one cycle; envelopes Q15 except the pitch
 * envelopes (Q8 semitones / octaves).  Times are ms Q8, frequencies Hz Q8, ratios/tilt Q12, levels Q15.
 */
#ifndef SHEEN_DSP_H
#define SHEEN_DSP_H

#include <stdint.h>
#ifdef SH_DEBUG
#include <stdio.h>
#endif

typedef int32_t s32;
typedef uint32_t u32;
typedef int16_t s16;
typedef uint16_t u16;
typedef int8_t s8;
typedef uint8_t u8;

#include "sheen_tables.h"

#define SH_BLOCK        16                  /* core samples per 32-frame block (24 kHz) */
#define SH_COMB         128                 /* shimmer delay buffer, samples */

/* internal layer trims (tuned on the host; kept constant for the device) */
#ifndef SH_BODY_GAIN
#define SH_BODY_GAIN    4096                /* Q12 */
#endif
#ifndef SH_SIZZ_GAIN
#define SH_SIZZ_GAIN    4096                /* Q12 */
#endif
#ifndef SH_CHIK_GAIN
#define SH_CHIK_GAIN    3000                /* Q12 */
#endif
#ifndef SH_TICK_GAIN
#define SH_TICK_GAIN    3000                /* Q12, 48 kHz tick */
#endif
#ifndef SH_AIR_GAIN
#define SH_AIR_GAIN     1500                /* Q12, 48 kHz air */
#endif
#ifndef SH_IN_GAIN
#define SH_IN_GAIN      3000                /* Q12, sum into the heat chain */
#endif
#ifndef SH_MASTER_Q12
#define SH_MASTER_Q12   7500                /* final output trim */
#endif

/* envelope states */
#define SH_ENV_IDLE     0
#define SH_ENV_ATTACK   1
#define SH_ENV_DECAY    2
#define SH_ENV_REVERSE  3
#define SH_ENV_CUT      4

/* what the bridge gives each block */
typedef struct {
	s32 note;               /* the trig's note plus PITCH - 64 and FINE, semitones << 16 */
	s16 color, shape, sweep, contour, decay;   /* knob() values, 0 .. 32766 */
	u8 punch, gate, vel;    /* PUNCH and GATE on/off; velocity 1 .. 127 (0 is read as 100 upstream) */
	u8 trig, end;           /* a trig in this block; the note ended in this block */
	u8 seed;                /* the track number, for the deterministic round robin */
} sh_in;

typedef struct {
	s32 pos;                /* Q4 level: pos >> 4 is the Q15 envelope value */
	s16 step;               /* Q4 per sample */
	s16 peak;               /* Q15 */
	s16 mul;                /* decay multiplier per block, Q15 */
	u16 atk;                /* samples of attack left (or the reverse swell count) */
	u8 state;
} sh_env;

typedef struct {
	/* cached macros (16-bit, as the spec requires) */
	s16 m[SH_M_COUNT];

	/* input change detector */
	u32 last_hash;
	u8 mapped;

	/* metal */
	u32 phase[6], inc[6];
	s16 det[6];             /* wash + round-robin detune, Q8 semitones */

	/* 48 kHz layer */
	u32 rng;
	s16 hp1_lp, hp2_lp;
	s16 tick_env, tick_mul, tick_peak, rev_tick;
	s16 tick_d0, tick_d1, air_d0, air_d1;
	u16 burst_timer, burst_space;
	u8 burst_left;
	s16 burst_gain;

	/* core filter */
	s16 pre_lp;
	s32 sizz_lp;
	s32 dark_a;
	s32 ic1, ic2;
	s32 g, a1, a2, a3, k;
	s32 gs, a1s, a2s, a3s, ks;

	/* envelopes */
	sh_env body, sfast, swash, chik;

	/* chik resonator */
	s32 chik_y1, chik_y2;
	s16 chik_a;

	/* shimmer */
	s16 comb[SH_COMB];
	s16 comb_lp;
	u16 widx;

	/* heat */
	s16 glue, glue_step, tilt_lp;
	s32 dcx, dcy;
	s16 crush_cnt, crush_hold, crush_val;
	u16 crush_mask;
	u8 crush_bits;
	s16 spike_lvl, spike_mul, chirp_lvl, chirp_mul;

	/* upsampler / output */
	s16 m1, m2;
	s16 last_out;
	s16 vel_gain;

	/* round robin */
	u32 rr;
	s16 rr_gain, rr_cut, rr_body, rr_sizz, rr_tick;

	/* latched at the trig */
	s16 lat_decay;
	u16 rev_total;
	u8 lat_punch, reverse, choke, active;

	/* per-block diagnostics for the host tests (not used by the sound) */
	s32 d_base_hz, d_cut_hz;
} sh_voice;

/* ------------------------------------------------------------------------------------------------------------
 * Helpers
 */
static inline __attribute__((always_inline)) s32 sh_clamp(s32 x, s32 lo, s32 hi)
{
	return x < lo ? lo : (x > hi ? hi : x);
}

static inline __attribute__((always_inline)) s32 sh_abs(s32 x)
{
	return x < 0 ? -x : x;
}

/* piecewise linear over the five knob breakpoints (x is 0 .. 32766, 8.8) */
static inline s32 sh_lut5(const s16 *t, s32 x)
{
	u32 s;
	s32 f;

	if (x < 0)
		x = 0;
	if (x > 32512)
		x = 32512;
	if (x >= 24576) {                    /* 96 .. 127: the last interval is 7936 wide, not 8192 */
		f = x - 24576;
		f += f >> 5;
		if (f > 8191)
			f = 8191;
		return (s32)t[3] + (((s32)(t[4] - t[3]) * f) >> 13);
	}
	s = (u32)x >> 13;
	f = x & 8191;
	return (s32)t[s] + (((s32)(t[s + 1] - t[s]) * f) >> 13);
}

/* DECAY has six breakpoints; 101..127 use the 100 value (the reverse swell is separate) */
static inline s32 sh_decay_lut(const s16 *t, s32 x)
{
	int i;

	if (x < 0)
		x = 0;
	if (x > 32512)
		x = 32512;
	for (i = 0; i < 5; i++) {
		if (x <= (s32)SH_DECAY_BP[i + 1]) {
			s32 lo = (s32)SH_DECAY_BP[i], hi = (s32)SH_DECAY_BP[i + 1];

			return (s32)t[i] + (((s32)(t[i + 1] - t[i]) * (x - lo)) / (hi - lo));
		}
	}
	return t[5];
}

/* 2^l, where l is log2 in Q8; result Q15 (32768 = 1.0) */
static inline s32 sh_exp2_q8(s32 l)
{
	s32 i, f, v;

	i = l >> 8;
	f = l & 255;
	v = SH_POW2[f >> 3];
	v += ((SH_POW2[(f >> 3) + 1] - v) * (f & 7)) >> 3;
	if (i >= 0) {
		if (i > 15)
			i = 15;
		return v << i;
	}
	if (i < -15)
		return 0;
	return v >> -i;
}

/* 2^l with a Q16 log2, for very long envelope times where a Q8 log would round to a factor of 1. */
static inline s32 sh_exp2_q16(s32 l)
{
	s32 i = l >> 16;
	s32 f = l & 65535;
	s32 v = SH_POW2[f >> 11];

	v += ((SH_POW2[(f >> 11) + 1] - v) * (f & 2047)) >> 11;
	if (i >= 0) {
		if (i > 15)
			i = 15;
		return v << i;
	}
	if (i < -15)
		return 0;
	return v >> -i;
}

/* ms Q8 from a log2-ms Q8 value */
static inline s32 sh_ms_q8(s32 l)
{
	return sh_exp2_q8(l) >> 7;
}

/* multiply a time in ms Q8 by a Q12 factor without a 32-bit overflow */
static inline __attribute__((always_inline)) s32 sh_mul_q12(s32 x, s32 f)
{
	if (x < 65536)
		return (x * f) >> 12;
	return ((x >> 6) * f) >> 6;
}

/* per-block decay multiplier for a T60 time in ms Q8 */
static inline s16 sh_t60_mul(s32 t_q8)
{
	if (t_q8 < 16)
		return 0;
	return (s16)sh_exp2_q16(-111463680 / t_q8);
}

/* per-sample decay multiplier for a T60 time in ms Q8 (48 kHz tick) */
static inline s16 sh_t60_mul48(s32 t_q8)
{
	if (t_q8 < 8)
		return 0;
	return (s16)sh_exp2_q16(-3483136 / t_q8);
}

/* phase increment of a note in semitones << 16, at 48 kHz (refmachine.c's phase_inc) */
static inline u32 sh_phase_inc(s32 note)
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
	a = SH_SEMI[k];
	b = SH_SEMI[k + 1];
	frac = (u32)note & 0xffff;
	r = a + ((((b - a) >> 4) * (frac >> 4)) >> 8);
	inc = (39370534u >> 12) * (r >> 12);
	return oct >= 0 ? inc << oct : inc >> -oct;
}

/* one-pole highpass coefficient for a cutoff in Hz Q8, at 48 kHz */
static inline s32 sh_hp_a(s32 f_q8)
{
	s32 hz, idx, fr;

	if (f_q8 < 0)
		f_q8 = 0;
	if (f_q8 > (16000 << 8))
		f_q8 = 16000 << 8;
	hz = f_q8 >> 8;
	idx = hz / 100;
	fr = hz - idx * 100;
	if (idx > 159) {
		idx = 159;
		fr = 100;
	}
	return (s32)SH_HP_A[idx] + (((s32)(SH_HP_A[idx + 1] - SH_HP_A[idx]) * fr) / 100);
}

/* tan(pi f / 24000) in Q12 for a cutoff in Hz Q8 */
static inline s32 sh_tan_g(s32 f_q8)
{
	s32 hz, idx, fr;

	if (f_q8 < 0)
		f_q8 = 0;
	if (f_q8 > (10500 << 8))
		f_q8 = 10500 << 8;
	hz = f_q8 >> 8;
	idx = hz / 100;
	fr = hz - idx * 100;
	if (idx > 104) {
		idx = 104;
		fr = 100;
	}
	return (s32)SH_TAN_G[idx] + (((s32)(SH_TAN_G[idx + 1] - SH_TAN_G[idx]) * fr) / 100);
}

/* frequency Hz Q8 of an absolute note (semitones << 16), 1 octave per octave */
static inline s32 sh_note_hz(s32 note)
{
	s32 l = SH_HOME_HZ_LOG + ((note - (60 << 16)) / 3072);

	if (l < -32768)
		l = -32768;
	if (l > 32767)
		l = 32767;
	return sh_exp2_q8(l) >> 7;
}

/* ------------------------------------------------------------------------------------------------------------
 * Envelopes.  The level is Q15; pos keeps four fractional bits so a long tail never stalls on a zero step.
 */
static inline __attribute__((always_inline)) s32 sh_env_level(const sh_env *e)
{
	return e->pos >> 4;
}

static inline __attribute__((always_inline)) void sh_env_block(sh_env *e, s32 rev_total)
{
	s32 level, target, delta;

	if (e->state == SH_ENV_IDLE || e->state == SH_ENV_CUT)
		return;
	level = e->pos >> 4;
	if (e->state == SH_ENV_REVERSE) {
		if (e->atk <= SH_BLOCK) {
			e->pos = (s32)32767 << 4;
			e->state = SH_ENV_CUT;
			e->step = (s16)(-e->pos / 96);        /* 4 ms at 24 kHz */
			e->atk = 0;
			return;
		}
		e->atk = (u16)(e->atk - SH_BLOCK);
		{
			s32 done = rev_total - (s32)e->atk;
			s32 l = -1024 + (1024 * done) / rev_total;   /* -24 dB .. 0 dB, log2 Q8 */

			target = sh_exp2_q8(l);
		}
		if (target > e->peak)
			target = e->peak;
		delta = target - level;
		if (delta > 32767)
			delta = 32767;
		if (delta < -32767)
			delta = -32767;
		e->step = (s16)delta;
		if (!e->step && target < level)
			e->step = -1;
		return;
	}
	if (e->state == SH_ENV_ATTACK) {
		if (e->atk <= SH_BLOCK) {
			e->pos = (s32)e->peak << 4;
			e->step = 0;
			e->atk = 0;
			e->state = SH_ENV_DECAY;
		} else {
			e->atk = (u16)(e->atk - SH_BLOCK);
		}
		return;
	}
	target = ((s32)level * e->mul) >> 15;
	if (target < 1 && level < 2) {
		e->pos = 0;
		e->step = 0;
		e->state = SH_ENV_IDLE;
		return;
	}
	delta = target - level;
	if (delta > 32767)
		delta = 32767;
	if (delta < -32767)
		delta = -32767;
	e->step = (s16)delta;
	if (!e->step && target < level)
		e->step = -1;
}

static inline __attribute__((always_inline)) s32 sh_env_sample(sh_env *e)
{
	s32 l;

	if (e->state == SH_ENV_IDLE)
		return 0;
	e->pos += (s32)e->step;
	if (e->pos <= 0) {
		e->pos = 0;
		e->step = 0;
		e->state = SH_ENV_IDLE;
		return 0;
	}
	if ((e->state == SH_ENV_ATTACK || e->state == SH_ENV_REVERSE) && e->pos > ((s32)e->peak << 4))
		e->pos = (s32)e->peak << 4;
	if (e->pos > ((s32)32767 << 4))
		e->pos = (s32)32767 << 4;
	l = e->pos >> 4;
	return l;
}

static inline void sh_env_trig(sh_env *e, s32 peak, s32 attack_samples, s32 mul)
{
	s32 level = e->pos >> 4;
	s32 delta;

	peak = sh_clamp(peak, 0, 32767);
	e->peak = (s16)peak;
	e->mul = (s16)sh_clamp(mul, 0, 32767);
	e->atk = 0;
	if (attack_samples > 0) {
		e->atk = (u16)(attack_samples > 65535 ? 65535 : attack_samples);
		e->state = SH_ENV_ATTACK;
		delta = ((peak - level) << 4) / attack_samples;
		e->step = (s16)sh_clamp(delta, -32767, 32767);
	} else if (level > peak) {
		/* a choke restarts from the current value; settle to the new peak in one core block */
		e->atk = SH_BLOCK;
		e->state = SH_ENV_ATTACK;
		e->step = (s16)(peak - level);
	} else {
		e->pos = (s32)peak << 4;
		e->step = 0;
		e->state = SH_ENV_DECAY;
	}
}

static inline void sh_env_reverse(sh_env *e, s32 blocks)
{
	if (blocks < 2)
		blocks = 2;
	if (blocks > 60000)
		blocks = 60000;
	e->pos = (s32)(32767 >> 9) << 4;
	e->step = 0;
	e->peak = 32767;
	e->mul = 0;
	e->atk = (u16)blocks;
	e->state = SH_ENV_REVERSE;
}

/* a hash of every input the macro mapper reads: the map runs only when this changes */
static inline u32 sh_in_hash(const sh_in *in)
{
	u32 h = 2166136261u;

	h = (h ^ (u32)in->note) * 16777619u;
	h = (h ^ (u32)(u16)in->color) * 16777619u;
	h = (h ^ (u32)(u16)in->shape) * 16777619u;
	h = (h ^ (u32)(u16)in->sweep) * 16777619u;
	h = (h ^ (u32)(u16)in->contour) * 16777619u;
	h = (h ^ (u32)(u16)in->decay) * 16777619u;
	h = (h ^ (u32)in->vel) * 16777619u;
	h = (h ^ (u32)(in->punch | (in->gate << 1))) * 16777619u;
	return h;
}

/* ------------------------------------------------------------------------------------------------------------
 * Macro mapper (per block, only when an input changed)
 */
static void sh_map(sh_voice *v, const sh_in *in)
{
	s16 *m = v->m;

	m[SH_M_METAL] = (s16)sh_lut5(SH_MACRO[SH_M_METAL], in->color);
	m[SH_M_RING] = (s16)sh_lut5(SH_MACRO[SH_M_RING], in->color);
	m[SH_M_OSC14] = (s16)sh_lut5(SH_MACRO[SH_M_OSC14], in->color);
	m[SH_M_CUT] = (s16)sh_lut5(SH_MACRO[SH_M_CUT], in->color);
	m[SH_M_Q] = (s16)sh_lut5(SH_MACRO[SH_M_Q], in->color);
	m[SH_M_BODY_SHARE] = (s16)sh_lut5(SH_MACRO[SH_M_BODY_SHARE], in->color);
	m[SH_M_AIR] = (s16)sh_lut5(SH_MACRO[SH_M_AIR], in->color);
	m[SH_M_CHIK] = (s16)sh_lut5(SH_MACRO[SH_M_CHIK], in->color);
	m[SH_M_SEND] = (s16)sh_lut5(SH_MACRO[SH_M_SEND], in->color);

	m[SH_M_SIZE_SEMI] = (s16)sh_lut5(SH_MACRO[SH_M_SIZE_SEMI], in->shape);
	m[SH_M_SIZE_CUT] = (s16)sh_lut5(SH_MACRO[SH_M_SIZE_CUT], in->shape);
	m[SH_M_CHIK_HZ] = (s16)sh_lut5(SH_MACRO[SH_M_CHIK_HZ], in->shape);
	m[SH_M_SIZE_DECAY] = (s16)sh_lut5(SH_MACRO[SH_M_SIZE_DECAY], in->shape);
	m[SH_M_BLOOM] = (s16)sh_lut5(SH_MACRO[SH_M_BLOOM], in->shape);
	m[SH_M_SPREAD] = (s16)sh_lut5(SH_MACRO[SH_M_SPREAD], in->shape);
	m[SH_M_DARKEN] = (s16)sh_lut5(SH_MACRO[SH_M_DARKEN], in->shape);
	m[SH_M_SHIM_LVL] = (s16)sh_lut5(SH_MACRO[SH_M_SHIM_LVL], in->shape);
	m[SH_M_SHIM_FB] = (s16)sh_lut5(SH_MACRO[SH_M_SHIM_FB], in->shape);
	m[SH_M_SHIM_RATIO] = (s16)sh_lut5(SH_MACRO[SH_M_SHIM_RATIO], in->shape);
	m[SH_M_SHIM_DAMP] = (s16)sh_lut5(SH_MACRO[SH_M_SHIM_DAMP], in->shape);

	m[SH_M_ATTACK] = (s16)sh_lut5(SH_MACRO[SH_M_ATTACK], in->sweep);
	m[SH_M_TICK_LVL] = (s16)sh_lut5(SH_MACRO[SH_M_TICK_LVL], in->sweep);
	m[SH_M_TICK_DECAY] = (s16)sh_lut5(SH_MACRO[SH_M_TICK_DECAY], in->sweep);
	m[SH_M_TICK_HP] = (s16)sh_lut5(SH_MACRO[SH_M_TICK_HP], in->sweep);
	m[SH_M_CHIK_DECAY] = (s16)sh_lut5(SH_MACRO[SH_M_CHIK_DECAY], in->sweep);
	m[SH_M_CHIRP_SEMI] = (s16)sh_lut5(SH_MACRO[SH_M_CHIRP_SEMI], in->sweep);
	m[SH_M_CHIRP_MS] = (s16)sh_lut5(SH_MACRO[SH_M_CHIRP_MS], in->sweep);
	m[SH_M_SPIKE_OCT] = (s16)sh_lut5(SH_MACRO[SH_M_SPIKE_OCT], in->sweep);
	m[SH_M_SPIKE_MS] = (s16)sh_lut5(SH_MACRO[SH_M_SPIKE_MS], in->sweep);

	m[SH_M_DRIVE] = (s16)sh_lut5(SH_MACRO[SH_M_DRIVE], in->contour);
	m[SH_M_HARD] = (s16)sh_lut5(SH_MACRO[SH_M_HARD], in->contour);
	m[SH_M_GLUE] = (s16)sh_lut5(SH_MACRO[SH_M_GLUE], in->contour);
	m[SH_M_TILT] = (s16)sh_lut5(SH_MACRO[SH_M_TILT], in->contour);
	m[SH_M_AIR_MULT] = (s16)sh_lut5(SH_MACRO[SH_M_AIR_MULT], in->contour);
	m[SH_M_MAKEUP] = (s16)sh_lut5(SH_MACRO[SH_M_MAKEUP], in->contour);
	if (in->color >= (127 << 8))
		m[SH_M_OSC14] = 0;

	v->last_hash = sh_in_hash(in);
	v->mapped = 1;
}

static inline u8 sh_changed(const sh_voice *v, const sh_in *in)
{
	return !v->mapped || v->last_hash != sh_in_hash(in);
}

/* ------------------------------------------------------------------------------------------------------------
 * Reset (machine change / first note)
 */
static void sh_reset(sh_voice *v, u8 seed)
{
	u32 i;
	u8 *p = (u8 *)v;

	for (i = 0; i < sizeof(*v); i++)
		p[i] = 0;
	v->rng = 0x12345678u + seed * 0x9e37u;
	v->rr = 0x9e3779b9u + seed * 0x10001u;
	v->crush_cnt = 1;
	v->crush_hold = 1;
	v->crush_bits = 16;
	v->crush_mask = 0xffff;
	v->vel_gain = 32767;
	v->glue = 32767;
}

/* ------------------------------------------------------------------------------------------------------------
 * Trig: immediate state (phases, round robin, latches).  Envelope peaks/times are set in sh_block.
 */
static void sh_trig(sh_voice *v, const sh_in *in)
{
	s32 vel = in->vel ? in->vel : 100;
	s32 vq, v2, lv;
	u32 r;
	int k;

	if (vel > 127)
		vel = 127;
	vq = vel * 258;
	v2 = (vq * vq) >> 15;
	lv = 2621 + ((30147 * v2) >> 15);
	v->vel_gain = (s16)lv;

	v->rr = v->rr * 1664525u + 1013904223u;
	r = v->rr;
	v->rr_gain = (s16)sh_clamp(16384 + (((s32)(r & 255) - 128) / 4), 16300, 16470);
	v->rr = v->rr * 1664525u + 1013904223u;
	r = v->rr;
	v->rr_cut = (s16)((s32)(r & 15) - 8);
	v->rr = v->rr * 1664525u + 1013904223u;
	r = v->rr;
	v->rr_body = (s16)(4096 + ((s32)(r & 255) - 128) * 2);
	v->rr = v->rr * 1664525u + 1013904223u;
	r = v->rr;
	v->rr_sizz = (s16)(4096 + ((s32)(r & 255) - 128) / 3);
	v->rr = v->rr * 1664525u + 1013904223u;
	r = v->rr;
	v->rr_tick = (s16)(4096 + ((s32)(r & 255) - 128) * 3);

	v->rr = v->rr * 1664525u + 1013904223u;
	v->rng = 0x12345678u + (u32)in->seed * 0x9e37u;
	{
		u32 ps = 0x517cc1b7u + (u32)in->seed * 0x1357u;

		for (k = 0; k < 6; k++) {
			/* random start phases, fixed for this voice: every hit has the same relative phases, so the
			 * metal sum's peak is consistent and a roll follows velocity; hits vary by detune, cutoff,
			 * decay and level below */
			ps = ps * 1664525u + 1013904223u;
			v->phase[k] = ps;
			v->rr = v->rr * 1664525u + 1013904223u;
			v->det[k] = (s16)(((((s32)(v->rr & 31) - 16) * 8) >> 4));  /* +-3 cents, Q8 semitones */
		}
	}

	if (!v->active) {
		s32 i;

		for (i = 0; i < SH_COMB; i++)
			v->comb[i] = 0;
		v->comb_lp = 0;
		v->widx = 0;
		v->ic1 = v->ic2 = 0;
		v->pre_lp = 0;
		v->hp1_lp = v->hp2_lp = 0;
		v->tilt_lp = 0;
		v->dcx = v->dcy = 0;
		v->m1 = v->m2 = 0;
		v->last_out = 0;
		v->chik_y1 = v->chik_y2 = 0;
		v->crush_cnt = 1;
		v->crush_val = 0;
	}
	else {
		v->ic1 = v->ic2 = 0;
		v->pre_lp = v->sizz_lp = 0;
		v->chik_y1 = v->chik_y2 = 0;
		v->comb_lp = 0;
		v->dcx = v->dcy = 0;
		v->tilt_lp = 0;
		v->hp1_lp = v->hp2_lp = 0;
		v->m1 = v->m2 = 0;
		v->tick_d0 = v->tick_d1 = v->air_d0 = v->air_d1 = 0;
		v->last_out = 0;
		v->widx = 0;
		{
			int z;
			for (z = 0; z < SH_COMB; z++) v->comb[z] = 0;
		}
	}
	v->active = 1;
	v->hp1_lp = 0;
	v->hp2_lp = 0;
	v->tick_d0 = v->tick_d1 = 0;
	v->air_d0 = v->air_d1 = 0;
	v->lat_decay = in->decay;
	v->lat_punch = in->punch;
	v->reverse = (u8)(in->decay > (100 << 8));
	v->choke = 0;
}

/* ------------------------------------------------------------------------------------------------------------
 * The block
 */
static int sh_block(sh_voice *v, const sh_in *in, s32 *out)
{
	s16 *m = v->m;
	s32 i, j;
	s32 vel, vq, v2, v_air;
	s32 punch, decay, reverse;
	s32 size_semi, size_cut, size_decay, bloom, spread, darken;
	s32 cut_log, cut_f_q8, air_f_q8, pre_a, tick_a, hp2_a;
	s32 tick_hp_hz, tick_ms, tick_peak_q15;
	s32 body_ms, sizz_ms, chik_ms, body_mul, sizz_mul, chik_mul, wash_mul;
	s32 attack_samples, rise_samples, chik_hz;
	s32 body_share, air_amt, chik_lvl;
	s32 ring_share, metal_share, sum_gain_q15, ring_gain_q15, total_lvl;
	s32 q12, glue_den, glue_t, glue_step;
	s32 drive, hard, tilt, makeup, crush_hold, crush_bits, crush_mask;
	s32 in_gain, master, sizz_now, dark_amt;
	s32 chirp_scale, note_base, base_hz;
	u32 ph0, ph1, ph2, ph3, ph4, ph5;
	s32 l0, l1, l2, l3, l4, l5;
	s32 comb_delay = 0, comb_fb = 0, comb_lvl = 0, comb_damp = 0;
	s32 out_i = 0;
	s32 fire_rev = 0;

	if (sh_changed(v, in))
		sh_map(v, in);
	if (in->trig)
		sh_trig(v, in);

	if (!v->active) {
		for (i = 0; i < 32; i++)
			out[i] = 0;
		return 0;
	}

	punch = v->lat_punch;
	decay = v->lat_decay;
	reverse = v->reverse;

	vel = in->vel ? in->vel : 100;
	if (vel > 127)
		vel = 127;
	vq = vel * 258;
	v2 = (vq * vq) >> 15;
	v_air = v2;                                  /* air follows velocity squared */

	size_semi = (s32)m[SH_M_SIZE_SEMI];
	size_cut = (s32)m[SH_M_SIZE_CUT];
	size_decay = (s32)m[SH_M_SIZE_DECAY];
	bloom = (s32)m[SH_M_BLOOM];
	if (punch) {
		bloom += 32768 / 4;
		if (bloom > 29491)
			bloom = 29491;
	}
	spread = (s32)m[SH_M_SPREAD] + (punch ? 15 : 0);   /* +6 cents, Q8 semitones */
	darken = (s32)m[SH_M_DARKEN] + (punch ? 38 : 0);   /* +0.15 octave, Q8 */

	/* ---- decay times and attack ---- */
	if (reverse) {
		s32 swell_q8 = (80 << 8) + (((decay - (101 << 8)) * (820 << 8)) / (26 << 8));

		if (swell_q8 < (80 << 8))
			swell_q8 = 80 << 8;
		if (swell_q8 > (900 << 8))
			swell_q8 = 900 << 8;
		if (punch)
			swell_q8 = (swell_q8 * 6554) >> 12;       /* x 1.6 */
		body_ms = sizz_ms = 0;
		body_mul = sizz_mul = wash_mul = 0;
		chik_ms = sh_ms_q8(sh_decay_lut(SH_DECAY_CHIK, 100 << 8));
		chik_ms = sh_mul_q12(chik_ms, (s32)m[SH_M_CHIK_DECAY]);
		chik_mul = sh_t60_mul(chik_ms);
		attack_samples = 0;
		rise_samples = 0;
		if (in->trig) {
			s32 rb = (swell_q8 * 24) >> 8;

			rb = sh_clamp(rb, 2, 60000);
			v->rev_total = (u16)rb;
			sh_env_reverse(&v->body, rb);
		}
	} else {
		s32 veldec = 3482 + ((vq * 614) >> 15);      /* 0.85 + 0.15 v, Q12 */

		body_ms = sh_ms_q8(sh_decay_lut(punch ? SH_DECAY_OPEN_BODY : SH_DECAY_CLOSED_BODY, decay));
		sizz_ms = sh_ms_q8(sh_decay_lut(punch ? SH_DECAY_OPEN_SIZZ : SH_DECAY_CLOSED_SIZZ, decay));
		body_ms = sh_mul_q12(body_ms, size_decay);
		body_ms = sh_mul_q12(body_ms, veldec);
		body_ms = sh_mul_q12(body_ms, v->rr_body);
		sizz_ms = sh_mul_q12(sizz_ms, size_decay);
		sizz_ms = sh_mul_q12(sizz_ms, veldec);
		sizz_ms = sh_mul_q12(sizz_ms, v->rr_sizz);
		body_mul = sh_t60_mul(body_ms);
		sizz_mul = sh_t60_mul(sizz_ms);
		wash_mul = sh_t60_mul(sh_mul_q12(sizz_ms, 4710));   /* 1.15 x the sizzle */
		chik_ms = sh_ms_q8(sh_decay_lut(SH_DECAY_CHIK, decay));
		chik_ms = sh_mul_q12(chik_ms, (s32)m[SH_M_CHIK_DECAY]);
		chik_mul = sh_t60_mul(chik_ms);
		attack_samples = (sh_ms_q8((s32)m[SH_M_ATTACK]) * 48) >> 8;
		rise_samples = 0;
		if (bloom > 0) {
			s32 rise_ms = 8 << 8;

			if (in->shape > (64 << 8))
				rise_ms += ((in->shape - (64 << 8)) * (17 << 8)) / (63 << 8);
			rise_samples = (rise_ms * 24) >> 8;
		}
	}

	/* ---- chirp / spike, reset at the trig ---- */
	if (in->trig) {
		v->chirp_lvl = (s32)m[SH_M_CHIRP_SEMI];
		v->spike_lvl = (s32)m[SH_M_SPIKE_OCT];
	}
	v->chirp_mul = sh_t60_mul(sh_ms_q8((s32)m[SH_M_CHIRP_MS]));
	v->spike_mul = sh_t60_mul(sh_ms_q8((s32)m[SH_M_SPIKE_MS]));
	chirp_scale = v->chirp_lvl;

	/* ---- cutoff: log domain ---- */
	if (reverse)
		sizz_now = (sh_env_level(&v->body));
	else if (in->trig)
		sizz_now = 32767;
	else
		sizz_now = (s32)(sh_env_level(&v->sfast)) + ((((s32)(sh_env_level(&v->swash)) - (sh_env_level(&v->sfast))) * bloom) >> 15);
	sizz_now = sh_clamp(sizz_now, 0, 32767);
	dark_amt = (darken * (32768 - sizz_now)) >> 15;       /* Q8 octaves */

	cut_log = (punch ? SH_CUT_OPEN_LOG : SH_CUT_CLOSED_LOG)
	          + (s32)m[SH_M_CUT] + size_cut + (punch ? -39 : 0);
	cut_log += -106 + ((vq * 106) >> 15);                 /* velocity: 0.75 .. 1.0 */
	cut_log += (in->note - (60 << 16)) / 4096;            /* 0.75 octave per octave, Q8 */
	cut_log += (s32)v->rr_cut;
	cut_log -= dark_amt;
	cut_log += v->spike_lvl;
	cut_log = sh_clamp(cut_log, 2551, 3438);              /* 1 .. 10.5 kHz */
	cut_f_q8 = sh_exp2_q8(cut_log) >> 7;
	{
		s32 dark_log = 3545 + (((sizz_now - 32768) * 323) >> 15);

		dark_log = sh_clamp(dark_log, 2800, 3600);
		v->dark_a = sh_hp_a((sh_exp2_q8(dark_log) >> 7) * 2);
	}
	air_f_q8 = sh_exp2_q8(sh_clamp(cut_log + 97, 3069, 3560)) >> 7;
	pre_a = sh_hp_a((cut_f_q8 >> 2) * 2);                 /* 24 kHz one-pole at 2 * f */
	tick_hp_hz = sh_clamp((s32)sh_lut5(SH_MACRO[SH_M_TICK_HP], in->sweep), 1000 << 8, 6000 << 8);
	tick_a = sh_hp_a(tick_hp_hz);
	hp2_a = sh_hp_a(air_f_q8);

	/* ---- tick / air levels and decay ---- */
	tick_peak_q15 = (((s32)m[SH_M_TICK_LVL] * v2) >> 15) * vq >> 15;
	if (punch)
		tick_peak_q15 = (tick_peak_q15 * 26214) >> 15;    /* x 0.8 */
	tick_peak_q15 = sh_clamp(tick_peak_q15, 0, 32767);
	if (reverse)
		tick_peak_q15 = 0;
	tick_ms = sh_ms_q8((s32)m[SH_M_TICK_DECAY]);
	tick_ms = sh_mul_q12(tick_ms, v->rr_tick);
	v->tick_mul = sh_t60_mul48(tick_ms);
	air_amt = ((s32)m[SH_M_AIR] * (s32)m[SH_M_AIR_MULT]) >> 15;
	air_amt = (air_amt * v_air) >> 15;
	air_amt = sh_clamp(air_amt, 0, 32767);

	/* ---- voice Q and the TPT coefficients ---- */
	q12 = (s32)m[SH_M_Q];
	if (punch)
		q12 = (q12 * 27853) >> 15;
	if (q12 < 256)
		q12 = 256;
	{
		s32 g, kt, a1t, a2t, a3t, den;

		kt = (4096 * 4096) / q12;
		g = sh_tan_g(cut_f_q8);
		den = 4096 + ((g * g) >> 12) + ((g * kt) >> 12);
		if (den < 1)
			den = 1;
		a1t = (4096 * 4096) / den;
		a2t = (g * a1t) >> 12;
		a3t = (g * a2t) >> 12;
		v->gs = (g - v->g) >> 4;
		v->a1s = (a1t - v->a1) >> 4;
		v->a2s = (a2t - v->a2) >> 4;
		v->a3s = (a3t - v->a3) >> 4;
		v->ks = (kt - v->k) >> 4;
	}

	/* ---- chik resonator ---- */
	chik_hz = sh_exp2_q8((s32)m[SH_M_CHIK_HZ] + ((in->note - (60 << 16)) / 4096)) >> 7;
	chik_hz = sh_clamp(chik_hz, 1500 << 8, 5000 << 8);
	{
		s32 hz = chik_hz >> 8;
		s32 idx = (hz - 1500) / 50;
		s32 fr = hz - 1500 - idx * 50;

		if (idx < 0) {
			idx = 0;
			fr = 0;
		}
		if (idx > 69) {
			idx = 69;
			fr = 50;
		}
		v->chik_a = (s16)((s32)SH_CHIK_A[idx] + (((s32)(SH_CHIK_A[idx + 1] - SH_CHIK_A[idx]) * fr) / 50));
	}

	/* ---- shimmer ---- */
	{
		s32 ratio = (s32)m[SH_M_SHIM_RATIO];
		s32 send = (s32)m[SH_M_SEND];
		s32 lvl = (s32)m[SH_M_SHIM_LVL] + (punch ? (4 << 8) : 0);
		s32 base_note = in->note + (size_semi << 8);
		s32 f_q8 = sh_note_hz(base_note);
		s32 den;

		comb_delay = 0;
		if (lvl > 0 && send > 0) {
			den = (f_q8 * ratio) >> 12;
			if (den > 0)
				comb_delay = (24000 << 8) / den;
			while (comb_delay > SH_COMB)
				comb_delay >>= 1;
			if (comb_delay < 2)
				comb_delay = 2;
		}
		comb_fb = sh_clamp((s32)m[SH_M_SHIM_FB], 0, 29491);
		comb_lvl = sh_clamp(lvl, 0, 32767);
		comb_damp = sh_clamp((s32)m[SH_M_SHIM_DAMP], 0, 32767);
	}
	(void)base_hz;

	/* ---- heat settings ---- */
	drive = (s32)m[SH_M_DRIVE];
	hard = (s32)m[SH_M_HARD];
	tilt = (s32)m[SH_M_TILT];
	makeup = (s32)m[SH_M_MAKEUP];
	{
		s32 x = in->contour - (104 << 8);
		s32 span = 32512 - (104 << 8);

		if (x > 0) {
			s32 tt = (x * 4096) / span;

			if (tt > 4096)
				tt = 4096;
			crush_hold = 1 + ((tt * 2) >> 12);
			crush_bits = 16 - ((tt * 9) >> 12);
			if (crush_bits < 7)
				crush_bits = 7;
			crush_mask = (s32)(0xffffu << (16 - crush_bits));
		} else {
			crush_hold = 1;
			crush_bits = 16;
			crush_mask = 0xffff;
		}
	}
	v->crush_hold = crush_hold;
	v->crush_bits = (u8)crush_bits;
	v->crush_mask = (u16)crush_mask;

	{
		s32 e = (s32)(sh_env_level(&v->body)) + (s32)(sh_env_level(&v->sfast));

		e = sh_clamp(e, 0, 32767);
		glue_den = 4096 + (((s32)m[SH_M_GLUE] * e) >> 15);
		glue_t = (4096 << 15) / glue_den;
		glue_step = (glue_t - v->glue) >> 4;
	}
	v->glue_step = glue_step;

	/* ---- envelope triggers ---- */
	if (in->trig) {
		if (!reverse) {
			sh_env_trig(&v->body, 32767, attack_samples, body_mul);
			sh_env_trig(&v->sfast, 32767, attack_samples, sizz_mul);
			if (bloom > 0)
				sh_env_trig(&v->swash, 32767, rise_samples, wash_mul);
			else
				sh_env_trig(&v->swash, 0, 0, 0);
		}
		sh_env_trig(&v->chik, 32767, 0, chik_mul);
		v->tick_env = (s16)tick_peak_q15;
		v->tick_peak = (s16)tick_peak_q15;
		v->burst_left = 0;
		v->burst_gain = 24576;
		if (reverse) {
			v->rev_tick = (s16)sh_clamp(((s32)sh_lut5(SH_MACRO[SH_M_TICK_LVL], in->sweep) * v2) >> 15, 0, 32767);
			v->burst_space = 0;
		} else if (in->sweep < (12 << 8)) {
			v->burst_left = 2;
			v->burst_space = (u16)(7 * 48);
		} else if (in->sweep < (24 << 8)) {
			v->burst_left = 1;
			v->burst_space = (u16)(5 * 48);
		} else {
			v->burst_space = 0;
		}
		v->burst_timer = v->burst_space;
		if (!reverse && in->trig)
			v->glue = (s16)sh_clamp(glue_t, 4096, 32767), v->glue_step = 0;
	}

	/* ---- GATE choke at the note end ---- */
	if (in->end && in->gate && !v->choke) {
		s32 cm = sh_t60_mul(12 << 8);

		v->choke = 1;
		v->body.mul = (s16)cm;
		v->sfast.mul = (s16)cm;
		v->swash.mul = (s16)cm;
		comb_fb = 0;
		v->chik.pos = (s32)sh_clamp((s32)sh_env_level(&v->chik) + 9830, 0, 32767) << 4;
		v->chik.state = SH_ENV_DECAY;
		v->chik.step = 0;
		v->chik.peak = 32767;
	}
	if (v->choke) {
		s32 cm = sh_t60_mul(12 << 8);

		v->body.mul = (s16)cm;
		v->sfast.mul = (s16)cm;
		v->swash.mul = (s16)cm;
		comb_fb = 0;
	}

	/* ---- one envelope block ---- */
	if (reverse && v->body.state == SH_ENV_REVERSE && v->body.atk <= SH_BLOCK)
		fire_rev = 1;
	sh_env_block(&v->body, (s32)v->rev_total);
	sh_env_block(&v->sfast, 0);
	sh_env_block(&v->swash, 0);
	sh_env_block(&v->chik, 0);
	if (fire_rev) {
		v->tick_env = v->rev_tick;
		v->burst_left = 0;
	}

	/* ---- metal levels and the six phase increments ---- */
	l0 = (s32)m[SH_M_OSC14];
	l1 = l0;
	l2 = l0;
	l3 = l0;
	l4 = 32767;
	l5 = 32767;
	if (in->color >= (127 << 8))
		l0 = l1 = l2 = l3 = 0;
	total_lvl = l0 + l1 + l2 + l3 + l4 + l5;
	if (total_lvl < 1)
		total_lvl = 1;
	sum_gain_q15 = (32768 * 32768) / total_lvl;
	ring_gain_q15 = 32768 / 3;
	metal_share = (s32)m[SH_M_METAL];
	ring_share = (s32)m[SH_M_RING];
	body_share = (s32)m[SH_M_BODY_SHARE];
	chik_lvl = (s32)m[SH_M_CHIK];
	if (punch)
		chik_lvl = (chik_lvl * 11469) >> 15;            /* x 0.35 */
	in_gain = SH_IN_GAIN;
	master = SH_MASTER_Q12;

	note_base = in->note + SH_METAL_OFF + ((size_semi + chirp_scale) << 8);
	for (i = 0; i < 6; i++) {
		s32 n = in->note + SH_METAL_OFF + ((size_semi + chirp_scale) << 8)
		        + (((s32)SH_OSC_SEMI[i] + (s32)v->det[i]
		            + ((spread * (s32)SH_OSC_DET[i]) >> 8)) << 8);
		u32 inc = sh_phase_inc(n);

		if (inc >= SH_MUTE_INC)
			inc = 0;
		v->inc[i] = inc;
	}
	base_hz = sh_note_hz(note_base);
	v->d_base_hz = base_hz;

	ph0 = v->phase[0];
	ph1 = v->phase[1];
	ph2 = v->phase[2];
	ph3 = v->phase[3];
	ph4 = v->phase[4];
	ph5 = v->phase[5];

	/* chirp / spike decay for the next block */
	v->chirp_lvl = (v->chirp_lvl * v->chirp_mul) >> 15;
	v->spike_lvl = (v->spike_lvl * v->spike_mul) >> 15;

	/* ---- the 16-iteration loop: two 48 kHz samples, one core sample, two outputs ---- */
	for (j = 0; j < SH_BLOCK; j++) {
		s32 src0, noise0, src, noise, metal, x;
		s32 sum, ring;
		s32 hp1, tick, hp2, air;
		s32 t0, t1, a0, air0v;
		s32 body_l, sfast_l, swash_l, sizz_l, chik_l;
		s32 bp, hpx, body, sizz, voice, chik, shim, core;
		s32 y, z, z2, soft, dc;
		s32 up_even, up_odd, temp, idx, dly, inp;
		s32 yc, xin;
		s32 sb0, sb1, sb2, sb3, sb4, sb5;
		s32 gcur;
		s32 k;

		/* envelopes advance once per core sample; both 48 kHz samples and the core use this value */
		body_l = sh_env_sample(&v->body);
		if (reverse) {
			sfast_l = body_l;
			swash_l = 0;
			sizz_l = body_l;
		} else {
			sfast_l = sh_env_sample(&v->sfast);
			swash_l = sh_env_sample(&v->swash);
			sizz_l = sfast_l + (((swash_l - sfast_l) * bloom) >> 15);
		}
		chik_l = sh_env_sample(&v->chik);

		/* first 48 kHz sample */
		sum = 0;
		ring = 0;
		sb0 = (s32)((s32)ph0 >> 31);
		sum += ((l0 ^ sb0) - sb0);
		sb1 = (s32)((s32)ph1 >> 31);
		sum += ((l1 ^ sb1) - sb1);
		sb2 = (s32)((s32)ph2 >> 31);
		sum += ((l2 ^ sb2) - sb2);
		sb3 = (s32)((s32)ph3 >> 31);
		sum += ((l3 ^ sb3) - sb3);
		sb4 = (s32)((s32)ph4 >> 31);
		sum += ((l4 ^ sb4) - sb4);
		sb5 = (s32)((s32)ph5 >> 31);
		sum += ((l5 ^ sb5) - sb5);
		if (ring_share > 0) {
			s32 r0 = (s32)((s32)(ph0 ^ ph1) >> 31);
			s32 r1 = (s32)((s32)(ph2 ^ ph3) >> 31);
			s32 r2 = (s32)((s32)(ph4 ^ ph5) >> 31);

			ring = ((32767 ^ r0) - r0) + ((32767 ^ r1) - r1) + ((32767 ^ r2) - r2);
		}
		ph0 += v->inc[0];
		ph1 += v->inc[1];
		ph2 += v->inc[2];
		ph3 += v->inc[3];
		ph4 += v->inc[4];
		ph5 += v->inc[5];
		sum = (sum * sum_gain_q15) >> 15;
		ring = (ring * ring_gain_q15) >> 15;
		metal = ((sum * (32768 - ring_share)) >> 15) + ((ring * ring_share) >> 15);
		metal = sh_clamp(metal, -32768, 32767);
		v->rng = v->rng * 1664525u + 1013904223u;
		noise0 = (s32)v->rng >> 16;
		src0 = ((metal * metal_share) >> 15) + ((noise0 * (32768 - metal_share)) >> 15);
		src0 = sh_clamp(src0, -32768, 32767);

		hp1 = src0 - v->hp1_lp;
		v->hp1_lp += (hp1 * tick_a) >> 15;
		tick = (hp1 * (s32)v->tick_env) >> 15;
		v->tick_env = (s16)(((s32)v->tick_env * v->tick_mul) >> 15);
		if (v->tick_env < 1)
			v->tick_env = 0;
		hp2 = hp1 - v->hp2_lp;
		v->hp2_lp += (hp2 * hp2_a) >> 15;
		air = (hp2 * (((s32)air_amt * sizz_l) >> 15)) >> 15;
		t0 = sh_clamp((tick * SH_TICK_GAIN) >> 12, -32768, 32767);
		a0 = sh_clamp((air * SH_AIR_GAIN) >> 12, -32768, 32767);

		/* burst re-fire */
		if (v->burst_left && v->burst_timer) {
			if (--v->burst_timer == 0) {
				s32 bg = v->burst_gain;

				v->tick_env = (s16)sh_clamp(((s32)v->tick_peak * bg) >> 15, 0, 32767);
				if (!v->reverse) {
					v->sfast.pos = (s32)sh_clamp(((s32)v->sfast.peak * bg) >> 15, 0, 32767) << 4;
					v->sfast.state = SH_ENV_DECAY;
				}
				v->burst_gain = (s16)((bg * 3) >> 2);
				v->burst_timer = v->burst_space;
				if (--v->burst_left == 0)
					v->burst_space = 0;
			}
		}

		/* second 48 kHz sample */
		sum = 0;
		ring = 0;
		sb0 = (s32)((s32)ph0 >> 31);
		sum += ((l0 ^ sb0) - sb0);
		sb1 = (s32)((s32)ph1 >> 31);
		sum += ((l1 ^ sb1) - sb1);
		sb2 = (s32)((s32)ph2 >> 31);
		sum += ((l2 ^ sb2) - sb2);
		sb3 = (s32)((s32)ph3 >> 31);
		sum += ((l3 ^ sb3) - sb3);
		sb4 = (s32)((s32)ph4 >> 31);
		sum += ((l4 ^ sb4) - sb4);
		sb5 = (s32)((s32)ph5 >> 31);
		sum += ((l5 ^ sb5) - sb5);
		if (ring_share > 0) {
			s32 r0 = (s32)((s32)(ph0 ^ ph1) >> 31);
			s32 r1 = (s32)((s32)(ph2 ^ ph3) >> 31);
			s32 r2 = (s32)((s32)(ph4 ^ ph5) >> 31);

			ring = ((32767 ^ r0) - r0) + ((32767 ^ r1) - r1) + ((32767 ^ r2) - r2);
		}
		ph0 += v->inc[0];
		ph1 += v->inc[1];
		ph2 += v->inc[2];
		ph3 += v->inc[3];
		ph4 += v->inc[4];
		ph5 += v->inc[5];
		sum = (sum * sum_gain_q15) >> 15;
		ring = (ring * ring_gain_q15) >> 15;
		metal = ((sum * (32768 - ring_share)) >> 15) + ((ring * ring_share) >> 15);
		metal = sh_clamp(metal, -32768, 32767);
		v->rng = v->rng * 1664525u + 1013904223u;
		noise = (s32)v->rng >> 16;
		src = ((metal * metal_share) >> 15) + ((noise * (32768 - metal_share)) >> 15);
		src = sh_clamp(src, -32768, 32767);
		hp1 = src - v->hp1_lp;
		v->hp1_lp += (hp1 * tick_a) >> 15;
		tick = (hp1 * (s32)v->tick_env) >> 15;
		v->tick_env = (s16)(((s32)v->tick_env * v->tick_mul) >> 15);
		if (v->tick_env < 1)
			v->tick_env = 0;
		hp2 = hp1 - v->hp2_lp;
		v->hp2_lp += (hp2 * hp2_a) >> 15;
		air = (hp2 * (((s32)air_amt * sizz_l) >> 15)) >> 15;
		t1 = sh_clamp((tick * SH_TICK_GAIN) >> 12, -32768, 32767);
		air0v = sh_clamp((air * SH_AIR_GAIN) >> 12, -32768, 32767);

		/* core sample, from the first 48 kHz sample */
		x = src0;
		{
			s32 lp = v->pre_lp;
			s32 hp = x - lp;

			lp += (hp * pre_a) >> 15;
			v->pre_lp = lp;
			x = hp;
		}
		{
			s32 g = v->g, a1c = v->a1, a2c = v->a2, a3c = v->a3, kc = v->k;
			s32 v3q = x - v->ic2;
			s32 v1q = (a1c * v->ic1 + a2c * v3q) >> 12;
			s32 v2s = v->ic2 + ((a2c * v->ic1 + a3c * v3q) >> 12);

			v->ic1 = 2 * v1q - v->ic1;
			v->ic2 = 2 * v2s - v->ic2;
			v->ic1 = sh_clamp(v->ic1, -65536, 65536);
			v->ic2 = sh_clamp(v->ic2, -65536, 65536);
			bp = v1q;
			hpx = x - ((kc * v1q) >> 12) - v2s;
			(void)g;
		}
		v->g += v->gs;
		v->a1 += v->a1s;
		v->a2 += v->a2s;
		v->a3 += v->a3s;
		v->k += v->ks;

		body = (bp * body_l) >> 15;
		body = (body * body_share) >> 15;
		body = (body * SH_BODY_GAIN) >> 12;
		v->sizz_lp += ((hpx - v->sizz_lp) * v->dark_a) >> 15;
		sizz = (v->sizz_lp * sizz_l) >> 15;
		sizz = (sizz * (32768 - body_share)) >> 15;
		sizz = (sizz * SH_SIZZ_GAIN) >> 12;
		voice = sh_clamp(body + sizz, -32768, 32767);

		/* chik resonator */
		yc = (v->chik_a * v->chik_y1) >> 14;
		xin = noise0 >> 1;
		yc -= (22578 * v->chik_y2) >> 15;
		yc += (2786 * xin) >> 15;
		yc = sh_clamp(yc, -32768, 32767);
		v->chik_y2 = v->chik_y1;
		v->chik_y1 = yc;
		chik = (yc * chik_l) >> 15;
		chik = (chik * chik_lvl) >> 15;
		chik = (chik * SH_CHIK_GAIN) >> 12;

		/* shimmer */
		shim = 0;
		if (comb_delay > 0 && comb_lvl > 0) {
			idx = ((s32)v->widx + SH_COMB - comb_delay) & (SH_COMB - 1);
			dly = v->comb[idx];
			v->comb_lp += ((dly - v->comb_lp) * comb_damp) >> 15;
			inp = ((voice * (s32)m[SH_M_SEND]) >> 15) + ((v->comb_lp * comb_fb) >> 15);
			inp = sh_clamp(inp, -32768, 32767);
			v->comb[v->widx] = (s16)inp;
			v->widx = (u16)((v->widx + 1) & (SH_COMB - 1));
			shim = (dly * comb_lvl) >> 15;
		}

		core = voice + chik + shim;
		core = (core * in_gain) >> 12;

		/* heat */
		gcur = v->glue;
		core = (core * gcur) >> 15;
		{
			s32 gg = (s32)v->glue + v->glue_step;

			if (gg < 4096)
				gg = 4096;
			if (gg > 32767)
				gg = 32767;
			v->glue = (s16)gg;
		}
		core = (core * drive) >> 8;
		core = sh_clamp(core, -262144, 262143);
		z = sh_clamp(core, -32768, 32767);
		z2 = (z * z) >> 15;
		temp = (z * z2) >> 15;
		soft = z - ((temp * 10923) >> 15);
		y = (soft * (32768 - hard)) >> 15;
		y += (z * hard) >> 15;
		v->tilt_lp += ((y - v->tilt_lp) * 17830) >> 15;
		y += (tilt * (y - v->tilt_lp)) >> 12;
		if (v->crush_hold > 1) {
			if (--v->crush_cnt <= 0) {
				v->crush_cnt = v->crush_hold;
				v->crush_val = y;
			}
			y = v->crush_val;
		}
		if (v->crush_bits < 16)
			y = (s16)((u16)y & v->crush_mask);
		dc = y - v->dcx + ((32000 * v->dcy) >> 15);
		v->dcx = y;
		v->dcy = dc;
		y = dc;
		y = (y * makeup) >> 12;
		y = (y * v->vel_gain) >> 15;
		y = (y * v->rr_gain) >> 14;
		y = sh_clamp(y, -32768, 32767);

		/* upsampler (one core-sample latency); the 48 kHz tick/air arrive two samples late */
		up_even = v->m1;
		up_odd = (9 * (v->m1 + y) - (v->m2 + y)) >> 4;
		v->m2 = v->m1;
		v->m1 = (s16)y;

		temp = up_even;
		temp = sh_clamp(temp + v->tick_d0 + v->air_d0, -32768, 32767);
		temp = (temp * master) >> 12;
		temp = sh_clamp(temp, -32768, 32767);
		if (temp - v->last_out > 32767)
			temp = v->last_out + 32767;
		else if (v->last_out - temp > 32767)
			temp = v->last_out - 32767;
		v->last_out = temp;
		out[out_i++] = (s32)((u32)(s16)temp << 16);

		temp = up_odd;
		temp = sh_clamp(temp + v->tick_d1 + v->air_d1, -32768, 32767);
		temp = (temp * master) >> 12;
		temp = sh_clamp(temp, -32768, 32767);
		if (temp - v->last_out > 32767)
			temp = v->last_out + 32767;
		else if (v->last_out - temp > 32767)
			temp = v->last_out - 32767;
		v->last_out = temp;
		out[out_i++] = (s32)((u32)(s16)temp << 16);

		v->tick_d0 = (s16)t0;
		v->tick_d1 = (s16)t1;
		v->air_d0 = (s16)a0;
		v->air_d1 = (s16)air0v;
		(void)k;
	}
	(void)out_i;

	v->phase[0] = ph0;
	v->phase[1] = ph1;
	v->phase[2] = ph2;
	v->phase[3] = ph3;
	v->phase[4] = ph4;
	v->phase[5] = ph5;

	/* diagnostics for the host tests */
	v->d_cut_hz = cut_f_q8;

	/* silence: every audible envelope is gone; reset the states for the next trig */
	if (!in->trig && (sh_env_level(&v->body)) == 0 && (sh_env_level(&v->sfast)) == 0 && (sh_env_level(&v->swash)) == 0
	    && (sh_env_level(&v->chik)) == 0 && v->tick_env == 0) {
		v->active = 0;
		comb_fb = 0;
	}
	return 1;
}

#endif
