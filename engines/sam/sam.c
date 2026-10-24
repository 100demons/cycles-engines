/*
 * SAM: added machine of the Model:Cycles (OS 1.13), 36 words of the Braids Renaissance vocalist.
 *
 * The voice is in sam_voice.h; this file plugs it into the voice loop, like DXR and ACID, and gives the
 * four machine knobs their names and value text.
 *
 * Voice-loop contract (notes/14): update(pmod, v, p) then render(out, v), once per block of 32 frames
 * at 48 kHz. pmod = the trig's note (semitones << 16); v = the Cycles voice (0x31c bytes); p = the
 * track's parameters (big-endian 8.8 words: +0x12 MACHINE, +0x14 PITCH, +0x16 COLOR, +0x18 SHAPE,
 * +0x1a SWEEP, +0x1c CONTOUR, +0x1e PUNCH, +0x20 GATE, +0x22 FINE, +0x24 DECAY); v+0x34 / v+0x38: a
 * trig in this block / in the previous one; v+0x3c: the note ended. out: 32 x int32, Q31 / 2.
 *
 * Knobs: SHAPE = word, COLOR = speaking rate, SWEEP = scrub / start frame, CONTOUR = throat/mouth
 * character macro, DECAY = release, PITCH/FINE/keys = pitch, PUNCH = loop, GATE = hold. A silent voice
 * is not computed.
 */
#include "sam_voice.h"

#ifndef CYC_VOICE0                                  /* a host build gives its own */
#define CYC_VOICE0  0x42308828
#endif
#define CYC_VSTRIDE 0x31c
#define TRACK_OF(v) ((u32)(((uintptr_t)(v) - (uintptr_t)CYC_VOICE0) / CYC_VSTRIDE))
#define MARK_OFF    0x2c
#define MARK        0x53414d53                       /* 'SAMS' */

#define V32(o)      (*(s32 *)(v + (o)))
#define P16(o)      ((s16)((u8)p[o] << 8 | (u8)p[(o) + 1]))      /* a big-endian 8.8 word */

struct track {
	sam_voice voice;
	sam_in in;
};

static struct track trk[6];

static inline u8 knob(s32 x)                         /* 8.8, 0..127 -> 0..127 */
{
	return (u8)(x < 0 ? 0 : (x > 0x7f00 ? 127 : x >> 8));
}

void sam_update(s32 pmod, char *v, const char *p)
{
	u32 t = TRACK_OF(v);
	struct track *tr = &trk[t];
	sam_in *in = &tr->in;
	s32 tune;

	if (V32(MARK_OFF) != MARK) {                     /* first note on this machine: a clean voice */
		u8 *b = (u8 *)&tr->voice;
		u32 k;

		for (k = 0; k < sizeof(tr->voice); k++)
			b[k] = 0;
		sam_init(&tr->voice);
		V32(MARK_OFF) = MARK;
	}
	/* the note: the trig's, plus PITCH - 64 and FINE (+-2 semitones), as the stock machines */
	tune = ((s32)P16(0x14) << 8) + (((s32)P16(0x22) - 0x4000) << 3) - (64 << 16);
	in->note = (pmod < 0 ? 0 : (pmod > (127 << 16) ? 127 << 16 : pmod)) + tune;
	in->color = knob(P16(0x16));
	in->word = knob(P16(0x18));
	in->sweep = knob(P16(0x1a));
	in->character = knob(P16(0x1c));
	in->decay = knob(P16(0x24));
	in->loop = P16(0x1e) != 0;
	in->trig = V32(0x38) != 0;
	in->end = V32(0x3c) != 0;
	if (in->word >= SAM_WORD_COUNT)
		in->word = SAM_WORD_COUNT - 1;
}

void sam_render(s32 *out, char *v)
{
	struct track *tr = &trk[TRACK_OF(v)];
	int i;

	if (!sam_block(&tr->voice, &tr->in, out))       /* a silent voice is not computed: out is zeros */
		return;
	for (i = 0; i < SAM_BLOCK; i++)                 /* Q15 -> Q31 / 2 */
		out[i] <<= 15;
}

#ifdef __mcoldfire__
/* ------------------------------------------------------------------------------------------------------------
 * Knob text. Called by the OS's interface code, not the audio interrupt. The OS functions that return a
 * pointer are declared to return u32: they return it in d0.
 */
#define UI_COLOR    (0x40a71754 + 100 * 51)          /* SNARE's COLOR, SHAPE, SWEEP, CONTOUR objects */
#define UI_SHAPE    (0x40a71754 + 100 * 52)
#define UI_SWEEP    (0x40a71754 + 100 * 53)
#define UI_CONTOUR  (0x40a71754 + 100 * 54)
#define OS_SPRINTF  ((int (*)(char *, const char *, ...))0x40000e6e)

static u32 ui_color[25], ui_shape[25], ui_sweep[25], ui_contour[25];
static u8 ui_ready;

int sam_color_fmt(void *fn, s32 v, char *buf)
{
	(void)fn;
	return OS_SPRINTF(buf, "%d", SAM_RATE_PCT[knob(v)]);
}

int sam_word_fmt(void *fn, s32 v, char *buf)
{
	u8 w = knob(v);

	(void)fn;
	if (w >= SAM_WORD_COUNT)
		w = SAM_WORD_COUNT - 1;
	return OS_SPRINTF(buf, "%s", SAM_WORD_NAME[w]);
}

int sam_scrub_fmt(void *fn, s32 v, char *buf)
{
	(void)fn;
	return OS_SPRINTF(buf, "%d", (knob(v) * 100) / 127);
}

int sam_char_fmt(void *fn, s32 v, char *buf)
{
	u8 c = knob(v);
	const char *s;

	(void)fn;
	if (c < 16)
		s = "DEEP";
	else if (c < 48)
		s = "DARK";
	else if (c < 80)
		s = "SAM";
	else if (c < 112)
		s = "BRIT";
	else
		s = "THIN";
	return OS_SPRINTF(buf, "%s", s);
}

/* The interface objects of descriptors 76..79: copies of SNARE's, with our text functions. */
u32 sam_state_at(s32 desc)
{
	if (!ui_ready) {
		const u32 *color = (const u32 *)UI_COLOR;
		const u32 *shape = (const u32 *)UI_SHAPE;
		const u32 *sweep = (const u32 *)UI_SWEEP;
		const u32 *contour = (const u32 *)UI_CONTOUR;
		int i;

		for (i = 0; i < 25; i++) {
			ui_color[i] = color[i];
			ui_shape[i] = shape[i];
			ui_sweep[i] = sweep[i];
			ui_contour[i] = contour[i];
		}
		ui_color[8] = (u32)sam_color_fmt;
		ui_shape[8] = (u32)sam_word_fmt;
		ui_sweep[8] = (u32)sam_scrub_fmt;
		ui_contour[8] = (u32)sam_char_fmt;
		ui_ready = 1;
	}
	return desc == 76 ? (u32)ui_color
		: (desc == 77 ? (u32)ui_shape : (desc == 78 ? (u32)ui_sweep : (u32)ui_contour));
}

__asm__(
"	.text\n"
/* state_connect (gen_syntakt_engines.detours_asm): the text of a descriptor's value in the buffer
 * 0x40a71500. Our object's text function (+0x14) is called by 0x400ddf60 (fn, value, buffer). */
"	.globl	sam_state_connect\n"
"sam_state_connect:\n"
"	move.l	4(%sp), -(%sp)\n"
"	jsr	sam_state_at\n"
"	addq.l	#4, %sp\n"
"	addi.l	#0x14, %d0\n"
"	pea	0x40a71500\n"
"	move.l	12(%sp), -(%sp)\n"
"	move.l	%d0, -(%sp)\n"
"	jsr	0x400ddf60\n"
"	lea	12(%sp), %sp\n"
"	move.l	#0x40a71500, %d0\n"
"	rts\n"
);
#endif
