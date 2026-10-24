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
 * BLAST's SWEEP descriptor (78) gets its own interface object: a copy of SNARE's SWEEP object (53, which the added
 * machines' descriptors share, gen_syntakt_engines.detours_asm) with our text and FUNC step functions; its picture
 * function is SNARE's, the stock one. A detour in the OS's knob turn (0x4000b82a) gives the ratio steps.
 * SHAPE (77) shows the model's name, three letters (MTL, SIN .. TMP, ZAP), with its own object too: SNARE's SHAPE
 * object (52) with our text function.
 * The machine page's splash (tools/blast_ui.py): three Bitmap objects of the OS's class, built here from the image
 * and mask planes of blast_icons.h and drawn by the detours of gen_blast.py.
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
#include "blast_icons.h"

/* The OS's Bitmap (constructor 0x40070172): vtable, width, height, words per column, image, mask, flag (0: static,
 * the destructor frees nothing). A is the machine page's card (48 x 33), B its emblem (34 x 34), C the small emblem
 * (25 x 22); the image and mask planes are in blast_icons.h. */
struct bitmap {
	u32 vt, w, h, wpc, img, mask, flag;
};
#define BITMAP_VT 0x401117c8
#define BITMAP(name, w_, h_, img_, msk_) \
	const struct bitmap name = { BITMAP_VT, w_, h_, ((h_) + 31) >> 5, (u32)(img_), (u32)(msk_), 0 }

BITMAP(blast_bm_a, 48, 33, BLAST_IMG_A, BLAST_MSK_A);
BITMAP(blast_bm_b, 34, 34, BLAST_IMG_B, BLAST_MSK_B);
BITMAP(blast_bm_c, 25, 22, BLAST_IMG_C, BLAST_MSK_C);

/* ------------------------------------------------------------------------------------------------------------
 * SWEEP on the ratio models: the interface. Called by the OS's interface code (not the audio interrupt). The OS's
 * functions that return a pointer are declared to return u32: they return it in d0, where m68k-linux-gnu-gcc would
 * read a pointer from a0 (notes/42 par. 6).
 */
#ifndef SHAPE_DESC
#define SHAPE_DESC 77
#endif
#ifndef SWEEP_DESC
#define SWEEP_DESC 78
#endif                               /* BLAST's descriptors: 76 COLOR, 77 SHAPE, 78 SWEEP, 79 CONTOUR */
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

/* a turn without FUNC (from blast_turn_hook): the next or previous ratio, one per detent; -1: the OS's own step */
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

/* the interface objects of descriptors 77 and 78 (from state_at, gen_blast.py): built at the first call, after the
 * OS's own (0x400de244, at boot) */
u32 blast_state_at(s32 desc)
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
	return desc == SHAPE_DESC ? (u32)ui_shape : (u32)ui_sweep;
}

#define MC_STR2(x) #x
#define MC_STR(x) MC_STR2(x)

__asm__(
"	.text\n"
/* state_connect (gen_syntakt_engines.detours_asm, at 0x4004dfa2: the text of a descriptor's value in the buffer
 * 0x40a71500) for descriptors 77 and 78: our object's text function (+0x14), called by 0x400ddf60 (fn, value,
 * buffer) */
"	.globl	blast_state_connect\n"
"blast_state_connect:\n"
"	move.l	4(%sp), -(%sp)\n"
"	jsr	blast_state_at\n"
"	addq.l	#4, %sp\n"
"	addi.l	#0x14, %d0\n"
"	pea	0x40a71500\n"
"	move.l	12(%sp), -(%sp)\n"
"	move.l	%d0, -(%sp)\n"
"	jsr	0x400ddf60\n"
"	lea	12(%sp), %sp\n"
"	move.l	#0x40a71500, %d0\n"
"	rts\n"
/* the OS's knob turn (0x4000b7d6), at 0x4000b82a, after vt[28] (the current value, d0) and before vt[20] (the new
 * value): d2 = descriptor, d3 = delta, the two arguments of vt[28] still on the stack. Descriptor 78 on a ratio
 * model: the value of the next or previous step, then on at 0x4000b834 with the stack as after vt[20] (4 more
 * arguments, which 0x4000b834 pops). Otherwise: the three moved instructions, then on at 0x4000b830. */
"	.globl	blast_turn_hook\n"
"blast_turn_hook:\n"
"	moveq	#" MC_STR(SWEEP_DESC) ", %d1\n"
"	cmp.l	%d1, %d2\n"
"	bne.s	1f\n"
"	move.l	%d0, -(%sp)\n"
"	move.l	%d3, -(%sp)\n"
"	move.l	%d0, -(%sp)\n"
"	jsr	blast_turn\n"
"	addq.l	#8, %sp\n"
"	tst.l	%d0\n"
"	bmi.s	2f\n"
"	addq.l	#4, %sp\n"
"	lea	-16(%sp), %sp\n"
"	jmp	0x4000b834\n"
"2:	move.l	(%sp)+, %d0\n"
"1:	move.l	%d3, -(%sp)\n"
"	move.l	%d0, -(%sp)\n"
"	move.l	%d2, -(%sp)\n"
"	jmp	0x4000b830\n"
);
#endif
