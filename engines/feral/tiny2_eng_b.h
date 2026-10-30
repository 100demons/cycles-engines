/*
 * TINY2, 2e serie : RATI (le moteur a resonateurs) et les outils communs. Inclus par tiny2_dsp.h.
 */

static inline u32 isqrt32(u32 x)                     /* racine carree entiere, 16 passes */
{
	u32 r = 0, b = 0x40000000u;
	while (b > x)
		b >>= 2;
	while (b) {
		if (x >= r + b) {
			x -= r + b;
			r = (r >> 1) + b;
		} else {
			r >>= 1;
		}
		b >>= 2;
	}
	return r;
}

#define INC9K  805306368u        /* 9 kHz en increment de phase */

/* ------------------------------------------------------------------------------------------------- RATI */
/* Rhythmicon : des « pings » (sinus amortis) aux partiels 1..10 de la hauteur (COLOR au-dessus de 68) ou aux
 * sous-harmoniques 1/1..1/8 (en dessous de 60), chacun relance au rythme de sa propre hauteur multipliee par une
 * echelle de temps (SWEEP : des pulsations lentes aux tons). Chaque ping est un resonateur a 2 poles, excite par une
 * impulsion, calcule a demi-cadence. CONTOUR droit : des pings plus longs sur le coup. */
#define RMAX 16
#define RACTIVE 10                                   /* 10 partiels au plus : le cout reste sous TONE */
struct st_rati {
	u32 pp[RMAX];
	s32 y1[RMAX], y2[RMAX];
	s32 last;
};

static void init_rati(struct st_rati *s)
{
	s32 i;
	zero32(s, sizeof *s / 4);
	for (i = 0; i < RMAX; i++)
		s->pp[i] = 0xfffffff0u;
}

static void eng_rati(struct st_rati *s, const ctl_t *c, s32 *out)
{
	s32 col = c->color, n = 1, under = 0, i, k, d, r2, e, ts, r22h, wn;
	s32 acc[16];
	s32 sw = c->sweep;
	u32 fp, inc = c->inc;

	if (c->restart)
		init_rati(s);
	if (col <= 60 * 256) {
		under = 1;
		n = 1 + ((60 * 256 - col) * 7999) / (60 * 256 * 1000);
	} else if (col >= 68 * 256) {
		n = 1 + ((col - 68 * 256) * 15999) / (59 * 256 * 1000);
	}
	n = imin(n, RACTIVE);
	/* echelle de temps : 2^(-10 + 4 s / 64) puis 2^(-6 + 6 (s - 64) / 63), s en potard */
	e = sw <= 0x4000 ? -(10 << 16) + ((sw * 4) << 2) : -(6 << 16) + (((sw - 0x4000) * 1536) / 63);
	ts = (s32)exp2_q16(e);
	d = 65536 + 9 * (c->hr >> 15);
	r2 = (s32)exp2_q16(-(394 * 65536) / d);                              /* rayon des poles par demi-pas, Q16 */
	r22h = qmul(r2 << 15, r2 << 15) >> 1;                                 /* r2^2 / 2, Q31 */
	wn = qmul(T_RATI_NORM[n], 1932735283);                                /* x 0,9 / n^0,8 */
	zero32(acc, 16);
	fp = inc;
	for (i = 0; i < n; i++) {
		u32 p, rate2;
		s32 a1, imp, y1, y2, nr22h = -r22h;
		if (i) {
			if (under) {
				fp = inc / (u32)(i + 1);
			} else {
				fp += inc;                                             /* les partiels harmoniques montent : au-dela de 9 kHz, plus rien */
				if (fp < inc)
					break;
			}
		}
		if (fp > INC9K) {
			if (under)
				continue;                                              /* les sous-harmoniques descendent : les suivants peuvent entrer */
			break;
		}
		a1 = qmul(r2 << 15, cos31(fp << 1));                               /* r2 cos(theta2) = a1 / 2, Q31 */
		imp = qmul(sin31(fp << 1) >> 1, wn);                               /* l'impulsion, deja a son poids */
		rate2 = mulq16(fp, (u32)ts) << 1;
		p = s->pp[i];
		y1 = s->y1[i];
		y2 = s->y2[i];
#define RATI_STEP(K) do { \
			s32 y_, t_; \
			p += rate2; \
			t_ = qmac2(a1, y1, nr22h, y2); \
			y_ = sadd(t_, t_); \
			if (p < rate2) { y_ = imp; y2 = 0; } else { y2 = y1; } \
			y1 = y_; \
			acc[K] += y_; \
		} while (0)
		for (k = 0; k < 16; k += 2) {                              /* deux pas par tour : moins de sauts */
			RATI_STEP(k);
			RATI_STEP(k + 1);
		}
#undef RATI_STEP
		s->pp[i] = p;
		s->y1[i] = y1;
		s->y2[i] = y2;
	}
	for (k = 0; k < 16; k++) {
		s32 o = acc[k];
		out[2 * k] = (s->last >> 1) + (o >> 1);
		out[2 * k + 1] = o;
		s->last = o;
	}
}
