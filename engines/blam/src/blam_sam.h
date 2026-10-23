/*
 * SAM: the Software Automated Mouth, as ported to the Braids Renaissance firmware.
 *
 * This is a C port of the vocalist in boourns/eurorack-renaissance: the SAM frame
 * processor and the 32 pre-compiled words. The word data is the Braids word list;
 * the SAM core is the reverse-engineered C version by Sebastian Macke and Vidar
 * Hokstad. See THIRD-PARTY-NOTICES.md and LICENSES/LICENSE-SAM.
 *
 * One SAM voice per track, 32-sample blocks at 48 kHz. SAM itself runs at its
 * natural 22.05 kHz and the voice resamples it with a zero-order hold, as Braids
 * does: the note sets both the speed and the pitch. A silent voice is not
 * computed. All arithmetic is integer, like the rest of the machine.
 *
 * The trigger starts the word at the current scrub position; while it plays the
 * word advances at its natural speed. Without a trigger the current grain is
 * held, so SWEEP scrubs the word like the Timbre knob of Braids Renaissance.
 */
#ifndef BLAST_SAM_H
#define BLAST_SAM_H

#include "blam_sam_tables.h"
#include "blam_sam_words.h"

#define SAM_RING        500
#define SAM_BANKS       2
#define SAM_WORD_COUNT  16
#define SAM_NATURAL     72                  /* SAM's own speed, as on Braids */

/* The phase-per-SAM-sample constant for C4 at 48 kHz: note_inc(C4) x 48000/22050,
 * built in 32 bits (557/256 = 2.176, the resampling ratio). */
#define SAM_PHASE_PER_SAMPLE ((((u32)FM_SEMI_INC[0] << 5) >> 8) * 557u)

typedef struct {
	u16 ring_size;                          /* in 1/50 sample, as in SAM */
	u16 ring_start;
	u8 speed;
	const u8 *pitches;
	const u8 *frequency1, *frequency2, *frequency3;
	const u8 *amplitude1, *amplitude2, *amplitude3;
	const u8 *consonant_flag;
	u8 frames_remaining, total_frames, frame_pos;
	u8 speedcounter, phase1, phase2, phase3;
	u8 mem66, glottal, mem38, oldtt;
	u8 ring[SAM_RING];
} sam_state;

typedef struct {
	sam_state st;
	const u8 *absorb;
	u8 absorb_len;
	u8 bank, word, loaded, scan, loop, releasing;
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

static void sam_load(sam_voice *v, u8 bank, u8 word)
{
	sam_state *s = &v->st;
	const u8 *d = &SAM_WORDS[SAM_WORDPOS[bank][word]];
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
	v->absorb = &SAM_ABSORB_OFFSET[SAM_ABSORB_POS[bank][word]];
	v->absorb_len = SAM_ABSORB_LEN[bank][word];
	v->bank = bank;
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
}

static void sam_set_target(sam_voice *v, s32 sweep)
{
	sam_state *s = &v->st;
	u8 total = s->total_frames;
	u8 len = v->absorb_len;
	u8 offset_len = total > len ? (u8)(total - len) : 0;
	u32 off;
	u8 i;

	if (sweep < 0)
		sweep = 0;
	if (sweep > 0x7f00)
		sweep = 0x7f00;
	off = ((u32)offset_len * (u32)sweep) >> 15;
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
				s->phase1 += sam_rle(s->frequency1, (u8)(y + absorbed));
				s->phase2 += sam_rle(s->frequency2, (u8)(y + absorbed));
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

static int sam_block(sam_voice *v, const fm_in *in, s32 *out)
{
	sam_state *s = &v->st;
	u8 bank = (u8)(in->model - M_SAM1);
	u8 word = (u8)(in->color >> 3);
	u16 need = 0;
	u16 guard;
	int i;

	if (bank >= SAM_BANKS)
		bank = SAM_BANKS - 1;
	if (word >= SAM_WORD_COUNT)
		word = SAM_WORD_COUNT - 1;

	if (!v->loaded || v->bank != bank || v->word != word)
		sam_load(v, bank, word);

	sam_set_target(v, in->sweep);
	v->loop = in->punch != 0;
	s->speed = (u8)(24 + (((u32)in->contour * 120) >> 7));

	if (in->trig) {
		v->scan = 1;
		v->releasing = 0;
		v->amp = 0;
		s->mem66 = 0;
		sam_set_frame(s, (u8)v->target);
	} else if (v->releasing) {
		v->amp = ((v->amp >> 8) * FM_PDECAY[in->decay]) >> 8;
		if (v->amp < 256)
			v->amp = 0;
	} else if (v->amp < 65536) {
		v->amp += (65536 - v->amp) >> 2;
	}
	if (in->end)
		v->releasing = 1;
	if (!v->amp && !in->trig) {
		for (i = 0; i < FM_BLOCK; i++)
			out[i] = 0;
		return 0;
	}

	v->inc = note_inc(in->note);
	for (i = 0; i < FM_BLOCK; i++) {
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
				if (!v->scan && s->frame_pos != v->target)
					sam_set_frame(s, (u8)v->target);
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
