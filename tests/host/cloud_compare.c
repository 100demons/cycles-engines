/*
 * Bit-exact proof of the copied cloud against HALO New's cloud.
 *
 * Both wrappers receive the same 32-sample Q31 blocks, the same TIME / CLOUD / FREEZE settings and the same reset
 * seed. Every output sample must be identical.
 */
#include <stdio.h>
#include <string.h>

void ref_init(int t);
void ref_params(int tim, int cld, int freeze);
void ref_wet(int *out, int dry);
void ours_init(int t);
void ours_params(int tim, int cld, int freeze);
void ours_wet(int *out, int dry);

static unsigned rng = 0x12345678u;

static void fill(int *x, int n)
{
	int i;
	for (i = 0; i < n; i++) {
		rng = rng * 1664525u + 1013904223u;
		x[i] = (int)(rng >> 2) - 0x20000000;
		if ((i & 31) == 5)
			x[i] = 0x30000000;                 /* a loud transient */
	}
}

static int run(const char *name, int tim, int cld, int freeze, int blocks, int loud)
{
	int a[32], b[32], i, blk, bad = 0;
	unsigned seed = rng;
	ref_init(3);
	ours_init(3);
	ref_params(tim, cld, freeze);
	ours_params(tim, cld, freeze);
	for (blk = 0; blk < blocks; blk++) {
		fill(a, 32);
		if (loud)
			for (i = 0; i < 32; i++)
				a[i] = (i & 1) ? 0x7ffffff0 : -0x7ffffff0;
		memcpy(b, a, sizeof a);
		ref_wet(a, 1);
		ours_wet(b, 1);
		for (i = 0; i < 32; i++)
			if (a[i] != b[i]) {
				if (!bad)
					printf("    first difference: block %d sample %d: HALO %d, copy %d\n", blk, i, a[i], b[i]);
				bad++;
			}
	}
	printf("  %-24s %s (%d blocks%s)\n", name, bad ? "FAIL" : "ok", blocks, bad ? "" : ", identical");
	(void)seed;
	return bad;
}

int main(void)
{
	int bad = 0;
	printf("cloud bit-exact comparison (copied cloud vs HALO New's halo_dsp.h)\n");
	bad += run("min: TIME 0, CLOUD 0", 0, 0, 0, 1200, 0);
	bad += run("default: TIME 70, CLOUD 40", 70, 40, 0, 3000, 0);
	bad += run("max: TIME 127, CLOUD 127", 127, 127, 0, 3000, 0);
	bad += run("max + Freeze", 127, 127, 1, 3000, 0);
	bad += run("default + Freeze", 70, 40, 1, 2000, 0);
	bad += run("loud input, default", 70, 40, 0, 600, 1);
	bad += run("loud input, max", 127, 127, 0, 600, 1);
	printf("%s\n", bad ? "CLOUD COMPARISON FAILED" : "ALL OK");
	return bad ? 1 : 0;
}
