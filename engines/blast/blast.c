/*
 * BLAST: added machine of the Model:Cycles (OS 1.13), 14 OPL2-style FM models on SHAPE. The sound is in
 * blast_dsp.h; this file plugs it into the voice loop, like ACID's bridge (notes/54) and TINY's.
 *
 * Voice-loop contract (notes/14): update(pmod, v, p) then render(out, v), once per block of 32 frames at 48 kHz.
 * pmod = the trig's note (semitones << 16); v = the Cycles voice (0x31c bytes); p = the track's parameters
 * (big-endian 8.8 words: +0x12 MACHINE, +0x14 PITCH, +0x16 COLOR, +0x18 SHAPE, +0x1a SWEEP, +0x1c CONTOUR,
 * +0x1e PUNCH, +0x20 GATE, +0x22 FINE, +0x24 DECAY); v+0x34 / v+0x38: a trig in this block / in the previous one
 * (the block where the OS switches machines and resets the voice: the note starts there, as on TINY);
 * v+0x3c: the note ended (LEN elapsed, key released); out: 32 x int32, Q31 / 2, like TINY, PORT and MACRO.
 *
 * Knobs: SHAPE = the model (0..14); PITCH, FINE and the keys = the note, as on the stock machines; COLOR, SWEEP,
 * CONTOUR and DECAY as each model reads them (blast_dsp.h); SWEEP is passed whole (8.8) so that a fractional value
 * glides the ratio between its steps; PUNCH turns the chip's LFO on; GATE holds the note until the trig's end.
 * The trig's velocity (0x40fe4cdc) also opens the struck instruments' brightness; the OS still sets the volume.
 *
 * The engine makes its own envelopes: the stock amp chain is not called. A silent voice is not computed. A voice is
 * reset at the first note after a machine change (our mark at v+0x2c is cleared by the OS's voice reset) and when
 * SHAPE picks another model at a trig.
 *
 * SWEEP on every model, on the screen and under the knob (the end of this file):
 *   - the value window shows the ratio in three characters (1:2, 1:1 .. 9:1, 10X, 12X, 15X) instead of 0..127, and
 *     keeps SWEEP's stock picture;
 *   - a turn moves to the next or previous ratio, one per detent (the stored value is the step's knob position);
 *   - FUNC + a turn leaves the steps: 1/16 of a step per detent, so the ratio glides between them; the window keeps
 *     the nearest step's three-character name.
 * As a package, the display and the stepped turn go through the registrar runtime: blast_state_init(knob) answers
 * the SHAPE (1) and SWEEP (2) interface objects (copies of SNARE's, with our text and FUNC step functions; the
 * picture stays SNARE's, the stock one) and 0 otherwise; blast_turn(cur, delta) is the machine's turn hook. The
 * MACHINES-page splash is not ported yet: BLAST keeps a stock picture (TONE).
 */
#include "blast_dsp.h"

#ifndef CYC_VOICE0                                  /* a host build (tools/emu/blast_host.c) gives its own */
#define CYC_VOICE0  0x42308828
#define CYC_VEL     ((const u8 *)0x40fe4cdc)        /* per track, a 16-bit word: the last trig's velocity << 8 */
#endif
#define CYC_VSTRIDE 0x31c
#define TRACK_OF(v) ((u32)(((uintptr_t)(v) - (uintptr_t)(CYC_VOICE0)) / CYC_VSTRIDE))
#define MARK_OFF    0x2c
#define MARK        0x424c5354                       /* 'BLST' */

#define V32(o)      (*(s32 *)(v + (o)))
#define P16(o)      ((s16)((u8)p[o] << 8 | (u8)p[(o) + 1]))      /* a big-endian 8.8 word */

struct track {
	fm_voice voice;
	fm_in in;
};

static struct track trk[6];
u8 blast_shape[6];                                   /* each track's SHAPE (model), for the interface */

static inline u8 knob(s32 x)                         /* 8.8, 0..127 -> 0..127 */
{
	return (u8)(x < 0 ? 0 : (x > 0x7f00 ? 127 : x >> 8));
}

void blast_update(s32 pmod, char *v, const char *p)
{
	u32 t = TRACK_OF(v);
	struct track *tr = &trk[t];
	fm_in *in = &tr->in;
	s32 tune;

	if (V32(MARK_OFF) != MARK) {                     /* first note on this machine: a clean voice */
		u8 *b = (u8 *)&tr->voice;
		u32 k;

		for (k = 0; k < sizeof(tr->voice); k++)
			b[k] = 0;
		tr->voice.model = 0xff;                      /* no model yet: the first trig sets one */
		V32(MARK_OFF) = MARK;
	}
	/* the note: the trig's, plus PITCH - 64 and FINE (+-2 semitones), as the stock machines */
	tune = ((s32)P16(0x14) << 8) + (((s32)P16(0x22) - 0x4000) << 3) - (64 << 16);
	in->note = (pmod < 0 ? 0 : (pmod > (127 << 16) ? 127 << 16 : pmod)) + tune;
	in->model = knob(P16(0x18));
	if (in->model >= FM_MODELS)
		in->model = FM_MODELS - 1;
	blast_shape[t] = in->model;
	in->color = knob(P16(0x16));
	in->sweep = P16(0x1a) < 0 ? 0 : P16(0x1a);
	in->contour = knob(P16(0x1c));
	in->decay = knob(P16(0x24));
	in->punch = P16(0x1e) != 0;
	in->gate = P16(0x20) != 0;
	in->trig = V32(0x38) != 0;
	in->end = V32(0x3c) != 0;
	if (in->trig) {
		u8 vel = CYC_VEL[2 * t];

		in->vel = vel ? (vel > 127 ? 127 : vel) : 100;
	} else if (!in->vel)
		in->vel = 100;
}

void blast_render(s32 *out, char *v)
{
	struct track *tr = &trk[TRACK_OF(v)];
	int i;

	if (!fm_block(&tr->voice, &tr->in, out))         /* a silent voice is not computed: out is zeros */
		return;
	for (i = 0; i < FM_BLOCK; i++)                   /* Q15 -> Q31 / 2 */
		out[i] <<= 15;
}

#ifdef __mcoldfire__
/* ------------------------------------------------------------------------------------------------------------
 * SWEEP on the ratio models: the interface. Called by the OS's interface code (not the audio interrupt). The OS's
 * functions that return a pointer are declared to return u32: they return it in d0, where m68k-linux-gnu-gcc would
 * read a pointer from a0 (notes/42 par. 6).
 */
#define UI_SHAPE    (0x40a71754 + 100 * 52)          /* SNARE's SHAPE and SWEEP interface objects (100 bytes) */
#define UI_SWEEP    (0x40a71754 + 100 * 53)
#define OS_SPRINTF  ((int (*)(char *, const char *, ...))0x40000e6e)
#define FINE_STEP   158                              /* FUNC: 1/16 of a ratio step (128 / 13 knob steps), 8.8 */

static u32 ui_sweep[25], ui_shape[25];               /* BLAST's SWEEP and SHAPE interface objects */
static u8 ui_ready;

/* the models' names on SHAPE: three characters, as the value window fits (its large font has capitals and digits) */
static const char model_name[FM_MODELS][4] = {
	"SIN", "HLF", "ABS", "PLS", "BAS", "PIA", "MAR", "GLK", "BRS", "ACC", "STR", "STL", "TMP", "ZAP",
};

/* the thirteen ratios of SWEEP, three characters each as the value window fits them */
static const char ratio_name[13][4] = {
	"1:2", "1:1", "2:1", "3:1", "4:1", "5:1", "6:1", "7:1", "8:1", "9:1", "10X", "12X", "15X",
};

/* the knob position of ratio step s (0..12), 8.8: the first whole position of the step */
static s32 step_value(u32 s)
{
	return (s32)(((s * 128 + 12) / 13) << 8);
}

/* SHAPE's value as text: the model's name (the model the bridge plays: the whole part, as knob() reads it) */
int blast_model_fmt(void *fn, s32 v, char *buf)
{
	(void)fn;
	return OS_SPRINTF(buf, "%s", model_name[knob(v) >= FM_MODELS ? FM_MODELS - 1 : knob(v)]);
}

/* the value as text: the nearest ratio step, in three characters (a FUNC turn between steps keeps the nearest name) */
int blast_fmt(void *fn, s32 v, char *buf)
{
	u32 r = snap_ratio(v), best = 0, bd, d, i;

	(void)fn;
	bd = r > FM_MULT[0] ? r - FM_MULT[0] : FM_MULT[0] - r;
	for (i = 1; i < 13; i++) {
		d = r > FM_MULT[i] ? r - FM_MULT[i] : FM_MULT[i] - r;
		if (d < bd) {
			bd = d;
			best = i;
		}
	}
	return OS_SPRINTF(buf, "%s", ratio_name[best]);
}

/* FUNC + a turn: 1/16 of a step per detent (at least one), the value kept off the whole positions */
s32 blast_fine(void *fn, s32 v, s32 delta, s32 lo, s32 hi, s32 def)
{
	s32 n;

	(void)fn;
	(void)def;
	n = ((delta < 0 ? -delta : delta) + 0xff) >> 8;
	if (!n)
		n = 1;
	v += (delta < 0 ? -n : n) * FINE_STEP;
	if (!(v & 0xff))
		v += delta < 0 ? -1 : 1;
	return v < lo ? lo : (v > hi ? hi : v);
}

/* a turn without FUNC (from the registrar runtime's turn entry): the next or previous ratio, one per detent;
 * -1: the OS's own step */
s32 blast_turn(s32 cur, s32 delta)
{
	u32 c, s, n;

	if (!delta)
		return -1;
	c = cur < 0 ? 0 : (cur > 0x7f00 ? 0x7f00 : (u32)cur);
	s = ((c * 13) >> 7) >> 8;
	if (s > 12)
		s = 12;
	n = ((u32)(delta < 0 ? -delta : delta) + 0xff) >> 8;
	if (delta > 0) {
		if (s >= 12)                                 /* already on the last ratio: the value stays */
			return (s32)c;
		s = s + n > 12 ? 12 : s + n;
	} else {
		if ((s32)c != step_value(s))                 /* between two steps: the first detent goes to this one */
			n--;
		s = s > n ? s - n : 0;
	}
	return step_value(s);
}

/* the descriptor-state initializer: the registrar runtime calls it with the knob index (0 = COLOR .. 3 = CONTOUR).
 * For SHAPE (1) it returns the model-name object and for SWEEP (2) the ratio object; any other knob answers 0, so
 * the runtime keeps the generic SNARE object. The objects are copies of SNARE's, built at the first call, after the
 * OS's own (0x400de244, at boot). */
u32 blast_state_init(s32 knob)
{
	if (!ui_ready) {
		const u32 *sweep = (const u32 *)UI_SWEEP, *shape = (const u32 *)UI_SHAPE;
		int i;

		for (i = 0; i < 25; i++) {
			ui_sweep[i] = sweep[i];
			ui_shape[i] = shape[i];
		}
		ui_sweep[8] = (u32)blast_fmt;                /* +0x20: the text function's invoker */
		ui_sweep[20] = (u32)blast_fine;
		ui_shape[8] = (u32)blast_model_fmt;
		ui_ready = 1;
	}
	return knob == 1 ? (u32)ui_shape : (knob == 2 ? (u32)ui_sweep : 0);
}
#endif
