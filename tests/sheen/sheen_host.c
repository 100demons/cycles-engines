/*
 * Host renderer for SHEEN.  Includes the same sheen_dsp.h as the device, so the arithmetic is bit-identical.
 *
 * Script (one command per line, '#' starts a comment):
 *   note 60          MIDI note of the trig (semitones, 0..127)
 *   pitch 64         PITCH knob, 0..127 (8.8 value = knob * 256)
 *   fine 64          FINE knob, 0..127
 *   color 64         COLOR / Metal
 *   shape 64         SHAPE / Size
 *   sweep 64         SWEEP / Tick
 *   contour 64       CONTOUR / Heat
 *   decay 40         DECAY knob
 *   punch 0          0/1
 *   gate 0           0/1
 *   vel 100          velocity 0..127 (0 means "not set", read as 100)
 *   blocks 400       number of 32-frame blocks to render
 *   trig 32          sample position of a trig (repeatable; quantised to its block)
 *   end 1920         sample position of the note end (repeatable)
 *   reset 1024       sample position of a machine change (repeatable)
 *   seed 0           round-robin seed (track number)
 *   out file.wav     32-bit PCM WAV output
 *   raw file.raw     raw little-endian int32 output
 *   trace file.csv   per-block diagnostics
 *
 * The output is the value the OS's voice loop leaves after its arithmetic >>1, so an emulator render compares
 * directly with the raw file.  In that domain 1 << 30 is full scale (the DSP writes Q31 and the OS halves it).
 *
 *   cc -std=c99 -O2 -Wall -Wextra -Werror sheen_host.c -o sheen_host
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sheen_dsp.h"

#define MAX_EVENTS 512

static int ev_trig[MAX_EVENTS], ev_end[MAX_EVENTS], ev_reset[MAX_EVENTS];
static int ev_tv_pos[MAX_EVENTS], ev_tv_val[MAX_EVENTS], n_tv;
static int ev_tp_pos[MAX_EVENTS], ev_tp_val[MAX_EVENTS], n_tp;
static int n_trig, n_end, n_reset;

static int in_list(const int *v, int n, int lo, int hi)
{
	int i;

	for (i = 0; i < n; i++)
		if (v[i] >= lo && v[i] < hi)
			return 1;
	return 0;
}

static int event_val(const int *pos, const int *val, int n, int lo, int hi, int dflt)
{
	int i;

	for (i = 0; i < n; i++)
		if (pos[i] >= lo && pos[i] < hi)
			return val[i];
	return dflt;
}

static void put32le(FILE *f, u32 v)
{
	unsigned char b[4];

	b[0] = (unsigned char)(v & 0xff);
	b[1] = (unsigned char)((v >> 8) & 0xff);
	b[2] = (unsigned char)((v >> 16) & 0xff);
	b[3] = (unsigned char)((v >> 24) & 0xff);
	fwrite(b, 1, 4, f);
}

static void put16le(FILE *f, unsigned v)
{
	unsigned char b[2];

	b[0] = (unsigned char)(v & 0xff);
	b[1] = (unsigned char)((v >> 8) & 0xff);
	fwrite(b, 1, 2, f);
}

static int write_wav(const char *path, const s32 *x, long n)
{
	FILE *f = fopen(path, "wb");
	u32 bytes = (u32)(n * 4);

	if (!f)
		return 0;
	fwrite("RIFF", 1, 4, f);
	put32le(f, 36 + bytes);
	fwrite("WAVEfmt ", 1, 8, f);
	put32le(f, 16);
	put16le(f, 1);
	put16le(f, 1);
	put32le(f, 48000);
	put32le(f, 48000 * 4);
	put16le(f, 4);
	put16le(f, 32);
	fwrite("data", 1, 4, f);
	put32le(f, bytes);
	fwrite(x, 4, (size_t)n, f);
	fclose(f);
	return 1;
}

static int write_raw(const char *path, const s32 *x, long n)
{
	FILE *f = fopen(path, "wb");
	long i;
	unsigned char b[4];

	if (!f)
		return 0;
	for (i = 0; i < n; i++) {
		u32 v = (u32)x[i];

		b[0] = (unsigned char)(v & 0xff);
		b[1] = (unsigned char)((v >> 8) & 0xff);
		b[2] = (unsigned char)((v >> 16) & 0xff);
		b[3] = (unsigned char)((v >> 24) & 0xff);
		fwrite(b, 1, 4, f);
	}
	fclose(f);
	return 1;
}

int main(int argc, char **argv)
{
	sh_voice v;
	sh_in in;
	s32 out[32];
	s32 *all = NULL;
	const char *script = NULL, *outw = NULL, *outr = NULL, *outt = NULL;
	char path_w[512], path_r[512], path_t[512];
	FILE *ft = NULL;
	int blocks = 400, note = 60, pitch = 64, fine = 64, color = 64, shape = 64;
	int sweep = 64, contour = 64, decay = 40, punch = 0, gate = 0, vel = 100, seed = 0;
	int b, i;
	long n = 0, cap = 0;
	FILE *fs;
	char line[512];

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--dump-size")) {
			printf("%zu\n", sizeof(sh_voice));
			return 0;
		}
		if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
			printf("usage: %s script.txt [--trace file.csv]\n", argv[0]);
			return 0;
		}
		if (!strcmp(argv[i], "--trace") && i + 1 < argc) {
			outt = argv[++i];
			continue;
		}
		if (argv[i][0] == '-') {
			fprintf(stderr, "unknown option %s\n", argv[i]);
			return 2;
		}
		script = argv[i];
	}
	if (!script) {
		fprintf(stderr, "no script\n");
		return 2;
	}
	fs = fopen(script, "r");
	if (!fs) {
		perror(script);
		return 2;
	}
	memset(&in, 0, sizeof(in));
	while (fgets(line, sizeof(line), fs)) {
		char key[32], val[400];

		val[0] = 0;
		if (sscanf(line, "%31s %399[^\n]", key, val) < 1)
			continue;
		if (key[0] == '#')
			continue;
		if (!strcmp(key, "note"))
			note = atoi(val);
		else if (!strcmp(key, "pitch"))
			pitch = atoi(val);
		else if (!strcmp(key, "fine"))
			fine = atoi(val);
		else if (!strcmp(key, "color"))
			color = atoi(val);
		else if (!strcmp(key, "shape"))
			shape = atoi(val);
		else if (!strcmp(key, "sweep"))
			sweep = atoi(val);
		else if (!strcmp(key, "contour"))
			contour = atoi(val);
		else if (!strcmp(key, "decay"))
			decay = atoi(val);
		else if (!strcmp(key, "punch"))
			punch = atoi(val);
		else if (!strcmp(key, "gate"))
			gate = atoi(val);
		else if (!strcmp(key, "vel"))
			vel = atoi(val);
		else if (!strcmp(key, "seed"))
			seed = atoi(val);
		else if (!strcmp(key, "blocks"))
			blocks = atoi(val);
		else if (!strcmp(key, "trig") && n_trig < MAX_EVENTS)
			ev_trig[n_trig++] = atoi(val);
		else if (!strcmp(key, "trigvel") && n_tv < MAX_EVENTS) {
			int a = 0, b = 0;

			if (sscanf(val, "%d %d", &a, &b) == 2) {
				ev_tv_pos[n_tv] = a;
				ev_tv_val[n_tv++] = b;
			}
		} else if (!strcmp(key, "trigpunch") && n_tp < MAX_EVENTS) {
			int a = 0, b = 0;

			if (sscanf(val, "%d %d", &a, &b) == 2) {
				ev_tp_pos[n_tp] = a;
				ev_tp_val[n_tp++] = b;
			}
		}
		else if (!strcmp(key, "end") && n_end < MAX_EVENTS)
			ev_end[n_end++] = atoi(val);
		else if (!strcmp(key, "reset") && n_reset < MAX_EVENTS)
			ev_reset[n_reset++] = atoi(val);
		else if (!strcmp(key, "out")) {
			snprintf(path_w, sizeof(path_w), "%s", val);
			outw = path_w;
		} else if (!strcmp(key, "raw")) {
			snprintf(path_r, sizeof(path_r), "%s", val);
			outr = path_r;
		} else if (!strcmp(key, "trace")) {
			snprintf(path_t, sizeof(path_t), "%s", val);
			outt = path_t;
		} else
			fprintf(stderr, "unknown command %s\n", key);
	}
	fclose(fs);
	if (blocks < 1)
		blocks = 1;
	cap = (long)blocks * 32;
	all = (s32 *)calloc((size_t)cap, sizeof(s32));
	if (!all) {
		fprintf(stderr, "out of memory\n");
		return 2;
	}

	in.note = (note << 16) + ((pitch - 64) << 16) + ((fine - 64) << 11);
	in.color = (s16)(color * 256);
	in.shape = (s16)(shape * 256);
	in.sweep = (s16)(sweep * 256);
	in.contour = (s16)(contour * 256);
	in.decay = (s16)(decay * 256);
	in.punch = (u8)(punch != 0);
	in.gate = (u8)(gate != 0);
	in.vel = (u8)(vel < 0 ? 0 : (vel > 127 ? 127 : vel));
	in.seed = (u8)seed;
	sh_reset(&v, (u8)seed);

	if (outt && strcmp(outt, "-")) {
		ft = fopen(outt, "w");
		if (!ft) {
			perror(outt);
			free(all);
			return 2;
		}
		fprintf(ft, "block,time_ms,base_hz,cut_hz,body_env,sizz_env,chik_env,tick_env,air_env,"
		            "crush_hold,crush_bits,osc14,rev\n");
	}
	for (b = 0; b < blocks; b++) {
		int lo = b * 32, hi = lo + 32;

		if (in_list(ev_reset, n_reset, lo, hi)) {
			sh_reset(&v, (u8)seed);
			in.vel = (u8)(vel < 0 ? 0 : (vel > 127 ? 127 : vel));
		}
		in.trig = (u8)in_list(ev_trig, n_trig, lo, hi);
		in.end = (u8)in_list(ev_end, n_end, lo, hi);
		if (in.trig) {
			int nv = event_val(ev_tv_pos, ev_tv_val, n_tv, lo, hi, -1);
			int np = event_val(ev_tp_pos, ev_tp_val, n_tp, lo, hi, -1);

			if (nv >= 0)
				in.vel = (u8)(nv > 127 ? 127 : nv);
			if (np >= 0)
				in.punch = (u8)(np != 0);
		}
		sh_block(&v, &in, out);
		for (i = 0; i < 32; i++)
			all[n++] = out[i] >> 1;          /* the OS's arithmetic halving */
		if (ft) {
			int body = sh_env_level(&v.body);
			int bloom = v.m[SH_M_BLOOM] + (v.lat_punch ? 8192 : 0);
			int sizz;

			if (bloom > 29491)
				bloom = 29491;
			sizz = sh_env_level(&v.sfast)
			       + (((sh_env_level(&v.swash) - sh_env_level(&v.sfast)) * bloom) >> 15);
			int chik = sh_env_level(&v.chik);
			int rev = (v.body.state == SH_ENV_REVERSE || v.body.state == SH_ENV_CUT);

			fprintf(ft, "%d,%.3f,%.2f,%.2f,%d,%d,%d,%d,%d,%u,%u,%u,%u\n", b, b * 32.0 / 48.0,
			        v.d_base_hz / 256.0, v.d_cut_hz / 256.0, body, sizz, chik,
			        v.tick_env, 0, (unsigned)v.crush_hold, v.crush_bits,
			        (unsigned)(v.m[SH_M_OSC14] ? 15 : 0), (unsigned)rev);
		}
	}
	if (ft)
		fclose(ft);
	if (outw && !write_wav(outw, all, n)) {
		perror(outw);
		free(all);
		return 2;
	}
	if (outr && !write_raw(outr, all, n)) {
		perror(outr);
		free(all);
		return 2;
	}
	free(all);
	return 0;
}
