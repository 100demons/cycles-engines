/* FIFTH's real core, for the no-fifths comparison. */
#include "fifth_dsp.h"

static fifth_t st;
static fifth_in_t in;

void f5_reset(void)
{
	zero32(&st, sizeof st / 4);
	in.note = 0;
	in.pitch = in.fine = 0x4000;
	in.color = in.shape = in.sweep = in.contour = 0;
	in.free = 0;
	in.trig = 0;
}

void f5_set(int note, int color, int shape, int sweep, int contour, int trig)
{
	in.note = note;
	in.color = color;
	in.shape = shape;
	in.sweep = sweep;
	in.contour = contour;
	in.trig = trig;
	in.pitch = in.fine = 0x4000;
	in.free = 0;
}

void f5_block(int *out)
{
	fifth_block(&st, &in, out);
	in.trig = 0;
}
