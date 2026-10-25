/*
 * Host access to Rhythmicon V5 Cloud's real fixed-point core, for the Python tests and demos.
 *
 * The same rhythmicon5_dsp.h / r5_cloud.h compiled by the computer compiler, with the EMAC arithmetic reproduced.
 * One voice, reset explicitly. Arrays are int32 (Q30 for the dry path, before the OS amp chain).
 */
#include "rhythmicon5_dsp.h"

static r5_t voice;
static r5_in_t in;

void r5h_reset(int t)
{
	r5_reset(&voice, t);
}

void r5h_reset_dry(void)
{
	r5_reset_dry(&voice);
}

void r5h_set(int note, int pitch, int fine, int color, int shape, int tim, int cld, int freeze, int trig)
{
	in.note = note;
	in.pitch = pitch;
	in.fine = fine;
	in.color = color;
	in.shape = shape;
	in.tim = tim;
	in.cld = cld;
	in.freeze = freeze;
	in.trig = trig;
}

void r5h_dry(int *out)
{
	r5_dry(&voice, &in, out);
}

void r5h_wet(int *out, int dry)
{
	r5_wet(&voice, &in, out, dry);
}

int r5h_shape_q16(void)
{
	return voice.shape_q16;
}

int r5h_cld(void)
{
	return in.cld;
}

/* the sweep rate table value the engine uses for a SHAPE setting, 8.8 input */
unsigned r5h_rate(int shape)
{
	int q16 = (shape & 0x7f00) << 8;
	return lutku_q16(T_RATE, (q16 * 76) / 127);
}

/* the resonator pole radius (Q16) the engine uses for a SHAPE setting */
int r5h_ring(int shape)
{
	int q16 = (shape & 0x7f00) << 8;
	return lutk_q16(T_R5_RING, q16);
}
