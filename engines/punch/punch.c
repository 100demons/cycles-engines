/*
 * PUNCH: one added machine for the Model:Cycles (OS 1.13), built from PUNCH's OPL2-style voice. The FM engine is in
 * punch_dsp.h; this file plugs it into the voice loop, like ACID's bridge (notes/54) and TINY's. PUNCH exposes 12
 * FM engines, and steps SWEEP through the ratio list.
 *
 * Voice-loop contract (notes/14): update(pmod, v, p) then render(out, v), once per block of 32 frames at 48 kHz.
 * pmod = the trig's note (semitones << 16); v = the Cycles voice (0x31c bytes); p = the track's parameters
 * (big-endian 8.8 words: +0x12 MACHINE, +0x14 PITCH, +0x16 COLOR, +0x18 SHAPE, +0x1a SWEEP, +0x1c CONTOUR,
 * +0x1e PUNCH, +0x20 GATE, +0x22 FINE, +0x24 DECAY); v+0x34 / v+0x38: a trig in this block / in the previous one
 * (the block where the OS switches machines and resets the voice: the note starts there, as on TINY);
 * v+0x3c: the note ended (LEN elapsed, key released); out: 32 x int32, Q31 / 2, like TINY, PORT and MACRO.
 *
 * Knobs: SHAPE = the engine (0..11); PITCH, FINE and the keys = the note, as on the stock
 * machines; COLOR, SWEEP, CONTOUR and DECAY as each engine reads them; SWEEP is passed whole (8.8) so that a
 * fractional value glides the ratio between its steps. PUNCH turns the chip's LFO on the FM engines; GATE holds
 * the note until the trig's end. The trig's velocity (0x40fe4cdc) also opens the struck instruments' brightness;
 * the OS still sets the volume.
 *
 * The PUNCH engines make their own envelopes: the stock amp chain is not called. A silent voice is not computed. A
 * voice is reset at the first note after a machine change (our mark at v+0x2c is cleared by the OS's voice reset) and
 * when SHAPE picks another engine at a trig.
 *
 * SWEEP on every model, on the screen and under the knob (the end of this file):
 *   - the value window shows the ratio in three characters (1:4, 1:3, 1:2, 2:3, 1:1, 5:4, 3:2, 2:1, 5:2, 3:1,
 *     7:2, 4:1 ... 16X, 20X) instead of 0..127, and keeps SWEEP's stock picture;
 *   - on PUNCH a turn moves to the next or previous ratio, one per detent (the stored value is the step's knob
 *     position), and FUNC + a turn leaves the steps: 1/16 of a step per detent; the window keeps the nearest
 *     step's three-character name;
 * PUNCH's SWEEP descriptor (78) gets its own interface object: a copy of SNARE's SWEEP object (53, which the added
 * machines' descriptors share, gen_syntakt_engines.detours_asm) with our text function; the picture function is
 * SNARE's, the stock one. A detour in the OS's knob turn (0x4000b82a) gives PUNCH's ratio steps. SHAPE (77) shows
 * the engine's name, three letters: PUNCH's 12 names. It has its own object too: SNARE's SHAPE object (52) with our
 * The machine page's splash: three Bitmap objects of the OS's class, built here from the image and mask planes of
 * punch_icons.h and drawn by the detours of gen_punch.py.
 */
#include "punch_dsp.h"

#ifndef CYC_VOICE0                                  /* a host build (tools/emu/punch_host.c) gives its own */
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

#define PUNCH_MODELS 12

static inline u8 knob(s32 x)                         /* 8.8, 0..127 -> 0..127 */
{
	return (u8)(x < 0 ? 0 : (x > 0x7f00 ? 127 : x >> 8));
}

void punch_update(s32 pmod, char *v, const char *p)
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
	{
		u8 shape = knob(P16(0x18));

		in->model = shape < PUNCH_MODELS ? shape : PUNCH_MODELS - 1;
	}
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

void punch_render(s32 *out, char *v)
{
	struct track *tr = &trk[TRACK_OF(v)];
	int i;

	if (!fm_block(&tr->voice, &tr->in, out))  /* a silent voice is not computed: out is zeros */
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
#define UI_COLOR    (0x40a71754 + 100 * 51)          /* SNARE's COLOR, SHAPE and SWEEP interface objects (100 bytes) */
#define UI_SHAPE    (0x40a71754 + 100 * 52)
#define UI_SWEEP    (0x40a71754 + 100 * 53)
#define OS_SPRINTF  ((int (*)(char *, const char *, ...))0x40000e6e)
#define FINE_STEP   ((128 * 256) / (FM_RATIO_COUNT * 16))   /* FUNC: 1/16 of a ratio step, 8.8 */

static u32 ui_sweep[25], ui_shape[25], ui_color[25]; /* PUNCH SWEEP, SHAPE and COLOR objects */
static u8 ui_ready;

/* PUNCH's SHAPE names: the 12 FM engines */
static const char model_name_punch[PUNCH_MODELS][5] = {
	"KIK", "TOM", "BNG", "CNG", "TAI", "TIM", "MAR", "DJB", "BLK", "GNG", "BUB", "ZAP",
};

/* the ratios of SWEEP, three characters each as the value window fits them; 4:1 is the percussion default */
static const char ratio_name[FM_RATIO_COUNT][4] = {
	"1:4", "1:3", "1:2", "2:3", "1:1", "5:4", "3:2", "2:1", "5:2", "3:1",
	"7:2", "4:1", "9:2", "5:1", "6:1", "7:1", "8:1", "9:1", "10X", "12X", "15X",
};

/* the knob position of ratio step s, 8.8: the first whole position that snaps to the step */
static s32 step_value(u32 s)
{
	return (s32)(((s * 128 + (u32)FM_RATIO_COUNT - 1) / (u32)FM_RATIO_COUNT) << 8);
}

/* PUNCH's SHAPE text: the curated engine's name */
int punch_model_fmt(void *fn, s32 v, char *buf)
{
	(void)fn;
	return OS_SPRINTF(buf, "%s", model_name_punch[knob(v) >= PUNCH_MODELS ? PUNCH_MODELS - 1 : knob(v)]);
}

/* COLOR's text: the FM index, a number */
int punch_color_fmt(void *fn, s32 v, char *buf)
{
	(void)fn;
	return OS_SPRINTF(buf, "%d", knob(v));
}

/* the value as text: the nearest ratio step, in three characters (a FUNC turn between steps keeps the nearest name) */
static int fmt_ratio(u32 r, char *buf)
{
	u32 best = 0, bd, d, i;

	bd = r > FM_MULT[0] ? r - FM_MULT[0] : FM_MULT[0] - r;
	for (i = 1; i < (u32)FM_RATIO_COUNT; i++) {
		d = r > FM_MULT[i] ? r - FM_MULT[i] : FM_MULT[i] - r;
		if (d < bd) {
			bd = d;
			best = i;
		}
	}
	return OS_SPRINTF(buf, "%s", ratio_name[best]);
}

int punch_fmt(void *fn, s32 v, char *buf)
{
	(void)fn;
	return fmt_ratio(snap_ratio(v), buf);
}

/* FUNC + a turn: 1/16 of a step per detent (at least one), the value kept off the whole positions */
s32 punch_fine(void *fn, s32 v, s32 delta, s32 lo, s32 hi, s32 def)
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
s32 punch_turn(s32 cur, s32 delta)
{
	u32 c, s, n, last = (u32)FM_RATIO_COUNT - 1;

	if (!delta)
		return -1;
	c = cur < 0 ? 0 : (cur > 0x7f00 ? 0x7f00 : (u32)cur);
	s = ((c * (u32)FM_RATIO_COUNT) >> 7) >> 8;
	if (s > last)
		s = last;
	n = ((u32)(delta < 0 ? -delta : delta) + 0xff) >> 8;
	if (delta > 0) {
		if (s >= last)                               /* already on the last ratio: the value stays */
			return (s32)c;
		s = s + n > last ? last : s + n;
	} else {
		if ((s32)c != step_value(s))                 /* between two steps: the first detent goes to this one */
			n--;
		s = s > n ? s - n : 0;
	}
	return step_value(s);
}

/* the descriptor-state initializer: the registrar runtime calls it with the knob index (0 = COLOR .. 3 = CONTOUR).
 * For COLOR (0) it returns the FM-index object, for SHAPE (1) the model-name object and for SWEEP (2) the ratio
 * object; CONTOUR (3) answers 0, so the runtime keeps the generic SNARE object. The objects are copies of SNARE's,
 * built at the first call, after the OS's own (0x400de244, at boot). */
u32 punch_state_init(s32 knob)
{
	if (!ui_ready) {
		const u32 *sweep = (const u32 *)UI_SWEEP, *shape = (const u32 *)UI_SHAPE;
		const u32 *color = (const u32 *)UI_COLOR;
		int i;

		for (i = 0; i < 25; i++) {
			ui_sweep[i] = sweep[i];
			ui_shape[i] = shape[i];
			ui_color[i] = color[i];
		}
		ui_sweep[8] = (u32)punch_fmt;                /* +0x20: the text function's invoker */
		ui_sweep[20] = (u32)punch_fine;
		ui_shape[8] = (u32)punch_model_fmt;
		ui_color[8] = (u32)punch_color_fmt;
		ui_ready = 1;
	}
	return knob == 2 ? (u32)ui_sweep : (knob == 1 ? (u32)ui_shape : (knob == 0 ? (u32)ui_color : 0));
}
#endif
