/*
 * SAM: the Software Automated Mouth, as ported to Braids Renaissance, as a standalone voice.
 *
 * This is the frame processor and word format of the Braids Renaissance vocalist (boourns/eurorack,
 * braids/vocalist), itself derived from the C reconstruction of SAM by Sebastian Macke and Vidar Hokstad.
 * See CREDITS-SAM.md. The word tables are precompiled; the machine has 36 words in SHAPE order.
 *
 * One SAM voice per track, 32-sample blocks at 48 kHz. SAM's natural output is 22.05 kHz and the voice
 * resamples it with a zero-order hold, as Braids does. The note sets the resampling pitch; COLOR sets a
 * separate speaking rate by changing SAM's frame speed counter (16-bit), compensated for the note so the
 * word does not speak faster when you play higher.
 *
 * THROAT and MOUTH are the real SAM formant parameters, not a post-filter. The word data carries a
 * one-byte flag per frame (SAM_SCALE_RLE) that is 1 on vowel frames. On those frames F1 and F2 are passed
 * through sam_trans(mouth, F1) / sam_trans(throat, F2), exactly the curve SAM's SetMouthThroat() applies
 * before its frame builder. Only the short transition frames between two phonemes are scaled after their
 * interpolation instead of before it; the sustained vowel frames are exact.
 */
#ifndef SAM_VOICE_H
#define SAM_VOICE_H

#include <stdint.h>

typedef int32_t s32;
typedef uint32_t u32;
typedef int16_t s16;
typedef uint16_t u16;
typedef int8_t s8;
typedef uint8_t u8;

#include "sam_fixed_tables.h"
#include "sam_tables.h"
#include "sam_words.h"

#define SAM_BLOCK       32
#define SAM_RING        2048
#define SAM_NATURAL     72

/* phase per SAM sample at C4, 48 kHz: the same constant as the PUNCH SAM port */
#define SAM_PHASE_PER_SAMPLE ((((u32)SAM_SEMI_INC[0] << 5) >> 8) * 557u)

typedef struct {
	u16 ring_size;                          /* in 1/50 sample, as in SAM */
	u16 ring_start;
	u16 speed;
	u16 speedcounter;
	const u8 *pitches;
	const u8 *frequency1, *frequency2, *frequency3;
	const u8 *amplitude1, *amplitude2, *amplitude3;
	const u8 *consonant_flag;
	const u8 *scale;                        /* 1 on vowel frames: scale F1/F2 by mouth/throat */
	u8 frames_remaining, total_frames, frame_pos;
	u8 phase1, phase2, phase3;
	u8 mem66, glottal, mem38, oldtt;
	u8 mouth, throat;
	u8 ring[SAM_RING];
} sam_state;

typedef struct {
	sam_state st;
	const u8 *absorb;
	u8 absorb_len;
	u8 word, loaded, scan, loop, releasing;
	u16 target;
	u32 phase, inc, amp;
	s16 samples[2];
} sam_voice;

/* the 50-unit timetable of the original SAM output stage */
static const u16 sam_timetable[5][5] = {
	{ 162, 167, 167, 127, 128 },
	{ 226, 60, 60, 0, 0 },
	{ 225, 60, 59, 0, 0 },
	{ 200, 0, 0, 54, 55 },
	{ 199, 0, 0, 54, 54 },
};

static u8 sam_rle(const u8 *r, u8 idx)
{
	while (idx >= r[0]) {
		idx -= r[0];
		r += 2;
	}
	return r[1];
}

/* phase increment (2^32 f / 48 kHz) of a note in semitones << 16; SAM_SEMI_INC is the twelve-semitone octave */
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
	a = SAM_SEMI_INC[semi];
	b = SAM_SEMI_INC[semi + 1];
	return (a + (((b - a) * frac) >> 8)) << oct;
}

/* SAM's trans(): the 8-bit multiply/shift that SetMouthThroat() applies to the formant frequencies. */
static u8 sam_trans(u8 mem39212, u8 mem39213)
{
	u8 carry, a, mem39214, mem39215;
	int temp, x;

	a = 0;
	mem39215 = 0;
	mem39214 = 0;
	x = 8;
	do {
		carry = mem39212 & 1;
		mem39212 >>= 1;
		if (carry) {
			a = mem39215;
			temp = (int)a + (int)mem39213;
			a = (u8)(a + mem39213);
			carry = temp > 255 ? 1 : 0;
			mem39215 = a;
		}
		temp = mem39215 & 1;
		mem39215 = (u8)((mem39215 >> 1) | (carry ? 128 : 0));
		carry = (u8)temp;
		x--;
	} while (x);
	temp = mem39214 & 128;
	mem39214 = (u8)((mem39214 << 1) | (carry ? 1 : 0));
	carry = (u8)temp;
	temp = mem39215 & 128;
	mem39215 = (u8)((mem39215 << 1) | (carry ? 1 : 0));
	return mem39215;
}

static u8 sam_f1(sam_state *s, u8 y)
{
	u8 v = sam_rle(s->frequency1, y);

	if (s->mouth != 128 && sam_rle(s->scale, y))
		v = sam_trans(s->mouth, v);
	return v;
}

static u8 sam_f2(sam_state *s, u8 y)
{
	u8 v = sam_rle(s->frequency2, y);

	if (s->throat != 128 && sam_rle(s->scale, y))
		v = sam_trans(s->throat, v);
	return v;
}

static void sam_output(sam_state *s, u8 index, u8 a)
{
	u16 add = sam_timetable[s->oldtt][index];
	u16 at = (u16)((s->ring_size + add) / 50);
	u8 k;

	s->oldtt = index;
	if (at + 5 >= SAM_RING)                       /* full: drop the new sample, keep the FIFO */
		return;
	s->ring_size = (u16)(s->ring_size + add);
	for (k = 0; k < 5; k++)
		s->ring[(s->ring_start + at + k) % SAM_RING] = a;
}

static u8 sam_render_voiced(sam_state *s, u16 hi, u8 off, u8 phase1)
{
	do {
		u8 sample = SAM_SAMPLE_TABLE[hi + off];
		u8 bit = 8;

		do {
			if (sample & 128)
				sam_output(s, 3, (26 & 0xf) << 4);
			else
				sam_output(s, 4, 6 << 4);
			sample <<= 1;
		} while (--bit != 0);
		off++;
	} while (++phase1 != 0);
	return off;
}

static void sam_render_unvoiced(sam_state *s, u16 hi, u8 off, u8 mem53)
{
	do {
		u8 bit = 8;
		u8 sample = SAM_SAMPLE_TABLE[hi + off];

		do {
			if (sample & 128)
				sam_output(s, 2, 5 << 4);
			else
				sam_output(s, 1, (mem53 & 0xf) << 4);
			sample <<= 1;
		} while (--bit != 0);
	} while (++off != 0);
}

static void sam_render_sample(sam_state *s, u8 *mem66, u8 flags, u8 pos)
{
	u8 hibyte = (u8)((flags & 7) - 1);
	u16 hi = (u16)(hibyte * 256);
	u8 pitch = flags & 248;

	if (!pitch) {
		pitch = (u8)(sam_rle(s->pitches, pos) >> 4);
		*mem66 = sam_render_voiced(s, hi, *mem66, (u8)(pitch ^ 255));
		return;
	}
	sam_render_unvoiced(s, hi, (u8)(pitch ^ 255), SAM_TAB48426[hibyte]);
}

static void sam_combine(sam_state *s, u8 y)
{
	u32 tmp;

	tmp  = SAM_MULTTABLE[SAM_SINUS[s->phase1] | sam_rle(s->amplitude1, y)];
	tmp += SAM_MULTTABLE[SAM_SINUS[s->phase2] | sam_rle(s->amplitude2, y)];
	tmp += tmp > 255 ? 1 : 0;
	tmp += SAM_MULTTABLE[SAM_RECTANGLE[s->phase3] | sam_rle(s->amplitude3, y)];
	tmp += 136;
	sam_output(s, 0, (u8)tmp);
}

static void sam_init_frame(sam_state *s)
{
	s->frame_pos = 0;
	s->glottal = sam_rle(s->pitches, 0);
	s->mem38 = (u8)(s->glottal - (s->glottal >> 2));
	s->ring_size = 0;
	s->ring_start = 0;
}

static void sam_set_frame(sam_state *s, u8 pos)
{
	s->frame_pos = pos;
	s->frames_remaining = (u8)(s->total_frames > pos ? s->total_frames - pos : 0);
}

static void sam_load(sam_voice *v, u8 word)
{
	sam_state *s = &v->st;
	const u8 *d = &SAM_WORDS[SAM_WORD_POS[word]];
	const u8 *len = d;
	u16 off = 8;

	s->frequency1 = &d[off];
	off = (u16)(off + len[0]);
	s->frequency2 = &d[off];
	off = (u16)(off + len[1]);
	s->frequency3 = &d[off];
	off = (u16)(off + len[2]);
	s->pitches = &d[off];
	off = (u16)(off + len[3]);
	s->amplitude1 = &d[off];
	off = (u16)(off + len[4]);
	s->amplitude2 = &d[off];
	off = (u16)(off + len[5]);
	s->amplitude3 = &d[off];
	off = (u16)(off + len[6]);
	s->consonant_flag = &d[off];
	off = (u16)(off + len[7]);

	s->frames_remaining = 0;
	off = 0;
	while (off < len[0]) {
		s->frames_remaining = (u8)(s->frames_remaining + s->frequency1[off]);
		off = (u16)(off + 2);
	}
	s->total_frames = s->frames_remaining;
	s->frame_pos = 0;
	s->scale = &SAM_SCALE_RLE[SAM_SCALE_POS[word]];
	v->absorb = &SAM_ABSORB_OFFSET[SAM_ABSORB_POS[word]];
	v->absorb_len = SAM_ABSORB_LEN[word];
	v->word = word;
	v->loaded = 1;
	v->scan = 0;
	v->target = 0;
	sam_init_frame(s);
}

static void sam_init(sam_voice *v)
{
	u8 *b = (u8 *)v;
	u32 k;

	for (k = 0; k < sizeof(*v); k++)
		b[k] = 0;
	v->st.speed = SAM_NATURAL;
	v->st.speedcounter = SAM_NATURAL;
	v->st.mouth = 128;
	v->st.throat = 128;
}

static void sam_set_target(sam_voice *v, u8 sweep)
{
	sam_state *s = &v->st;
	u8 total = s->total_frames;
	u8 len = v->absorb_len;
	u8 offset_len = total > len ? (u8)(total - len) : 0;
	u32 off;
	u8 i;

	if (sweep > 127)
		sweep = 127;
	off = ((u32)offset_len * (u32)sweep) >> 7;
	for (i = 0; i < len; i++)
		if (v->absorb[i] < off)
			off++;
	if (off >= total)
		off = total ? total - 1 : 0;
	v->target = (u16)off;
}

static u8 sam_pop(sam_state *s, u8 *sample)
{
	if (s->ring_size < 50)
		return 0;
	*sample = s->ring[s->ring_start];
	s->ring_size = (u16)(s->ring_size - 50);
	s->ring_start = (u16)((s->ring_start + 1) % SAM_RING);
	return 1;
}

/* The frame processor, kept as close to the Braids code as the types allow. */
static u8 sam_frame(sam_state *s, u8 y, u8 mem48)
{
	u8 flags = sam_rle(s->consonant_flag, y);
	u8 absorbed = 0;

	if (flags & 248) {
		sam_render_sample(s, &s->mem66, flags, y);
		s->speedcounter = s->speed;
		absorbed = 2;
	} else {
		sam_combine(s, y);
		s->speedcounter--;
		if (s->speedcounter == 0) {
			absorbed = 1;
			if (mem48 == 1)
				return absorbed;
			s->speedcounter = s->speed;
		}
		--s->glottal;
		if (s->glottal != 0) {
			--s->mem38;
			if (s->mem38 != 0 || flags == 0) {
				s->phase1 += sam_f1(s, (u8)(y + absorbed));
				s->phase2 += sam_f2(s, (u8)(y + absorbed));
				s->phase3 += sam_rle(s->frequency3, (u8)(y + absorbed));
				return absorbed;
			}
			sam_render_sample(s, &s->mem66, flags, (u8)(y + absorbed));
		}
	}
	s->glottal = sam_rle(s->pitches, (u8)(y + absorbed));
	s->mem38 = (u8)(s->glottal - (s->glottal >> 2));
	s->phase1 = 0;
	s->phase2 = 0;
	s->phase3 = 0;
	return absorbed;
}

/* COLOR 0..127: speaking rate, independent of the note. Center 64 = natural. */
static u16 sam_speed(s32 note, u8 color)
{
	u32 n, p, inv, sp;

	if (note < 0)
		n = 0;
	else {
		n = (u32)note >> 16;
		if (n > 127)
			n = 127;
	}
	p = SAM_PITCH_Q8[n];
	inv = SAM_INV_RATE_Q8[color > 127 ? 127 : color];
	sp = (72u * p * inv) >> 16;
	if (sp < 8)
		sp = 8;
	if (sp > 300)
		sp = 300;
	return (u16)sp;
}

typedef struct {
	s32 note;               /* the trig's note plus PITCH/FINE, semitones << 16 */
	u8 color, word, sweep, character, decay;
	u8 loop, trig, end;
} sam_in;

static int sam_block(sam_voice *v, const sam_in *in, s32 *out)
{
	sam_state *s = &v->st;
	u8 word = in->word;
	u16 need = 0;
	u16 guard;
	int i;

	if (word >= SAM_WORD_COUNT)
		word = SAM_WORD_COUNT - 1;
	if (!v->loaded || v->word != word)
		sam_load(v, word);

	sam_set_target(v, in->sweep);
	v->loop = in->loop != 0;
	s->speed = sam_speed(in->note, in->color);
	s->mouth = SAM_CHARACTER[in->character > 127 ? 127 : in->character][0];
	s->throat = SAM_CHARACTER[in->character > 127 ? 127 : in->character][1];

	if (in->trig) {
		u16 target = v->target;

		v->scan = 1;
		v->releasing = 0;
		v->amp = 0;
		s->mem66 = 0;
		s->speedcounter = s->speed;
		if (target > 255)
			target = 255;
		sam_set_frame(s, (u8)target);
	} else if (v->releasing) {
		v->amp = ((v->amp >> 8) * SAM_PDECAY[in->decay > 127 ? 127 : in->decay]) >> 8;
		if (v->amp < 256)
			v->amp = 0;
	} else if (v->amp < 65536) {
		v->amp += (65536 - v->amp) >> 2;
	}
	if (in->end)
		v->releasing = 1;
	if (!v->amp && !in->trig) {
		for (i = 0; i < SAM_BLOCK; i++)
			out[i] = 0;
		return 0;
	}

	v->inc = note_inc(in->note);
	for (i = 0; i < SAM_BLOCK; i++) {
		u8 sample;

		v->phase += v->inc;
		while (v->phase >= SAM_PHASE_PER_SAMPLE) {
			v->phase -= SAM_PHASE_PER_SAMPLE;
			if (need < 64)
				need++;
		}
		guard = 0;
		while (need) {
			if (sam_pop(s, &sample)) {
				v->samples[0] = v->samples[1];
				v->samples[1] = (s16)(((s32)sample - 127) << 8);
				need--;
			} else {
				u8 absorbed;

				if (v->scan && !s->frames_remaining) {
					if (v->loop) {
						sam_set_frame(s, 0);
					} else {
						v->scan = 0;
					}
				}
				if (!v->scan && s->frame_pos != v->target) {
					u16 target = v->target;

					if (target > 255)
						target = 255;
					sam_set_frame(s, (u8)target);
				}
				absorbed = sam_frame(s, s->frame_pos, s->frames_remaining);
				if (v->scan) {
					s->frame_pos = (u8)(s->frame_pos + absorbed);
					s->frames_remaining = s->frames_remaining > absorbed
						? (u8)(s->frames_remaining - absorbed) : 0;
				}
				if (++guard > 96)
					break;
			}
		}
		out[i] = ((s32)v->samples[0] * (s32)(v->amp >> 8)) >> 8;
	}
	return 1;
}

#endif
