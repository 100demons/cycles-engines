/* HALO New's cloud, wrapped for the bit-exact comparison. Compile with -I <HALO tools/machines/halo>. */
#include "halo_dsp.h"

static halo_t st;
static halo_in_t in;

void ref_init(int t)
{
	halo_reset(&st, t);
	in.note = 60 << 16;
	in.mat = 64;
	in.exc = 30;
	in.dec = 90;
	in.trig = 0;
	in.held = 0;
}

void ref_params(int tim, int cld, int freeze)
{
	in.tim = tim;
	in.cld = cld;
	in.freeze = freeze;
}

void ref_wet(int *out, int dry)
{
	if (halo_cloud_busy(&st, &in, dry))
		halo_wet(&st, &in, out);
}
