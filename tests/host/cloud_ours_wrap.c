/* The copied cloud of Rhythmicon V5 Cloud, wrapped for the bit-exact comparison. */
#include "r5_cloud.h"

static halo_t st;
static halo_in_t in;

void ours_init(int t)
{
	r5_cloud_reset(&st, t);
	in.note = 60 << 16;
	in.mat = 64;
	in.exc = 30;
	in.dec = 90;
	in.trig = 0;
	in.held = 0;
}

void ours_params(int tim, int cld, int freeze)
{
	in.tim = tim;
	in.cld = cld;
	in.freeze = freeze;
}

void ours_wet(int *out, int dry)
{
	if (halo_cloud_busy(&st, &in, dry))
		halo_wet(&st, &in, out);
}
