/*
 * Feral, 3e serie : GNDY (le moteur a marche aleatoire). Inclus par tiny2_dsp.h.
 */

/* ------------------------------------------------------------------------------------------------- GNDY */
/* Gendy : une forme d'onde faite de n points qui marchent au hasard (pas de Cauchy) a chaque periode, relies
 * en ligne droite. COLOR : le nombre de points et la taille des pas d'amplitude ; SWEEP : les durees des segments
 * (normalisees : la hauteur reste juste), puis le vagabondage des durees. */
#define GMAX 24
struct st_gndy {
	s32 a[GMAX], d[GMAX], B[GMAX + 1];
	s32 n, i, pos4, len4, y, dy;
};

static void gndy_seg(struct st_gndy *s)
{
	s32 j = s->i + 1 >= s->n ? 0 : s->i + 1;
	s->len4 = imax(16, s->B[s->i + 1] - s->B[s->i]);
	s->y = s->a[s->i];
	s->dy = (((s->a[j] - s->a[s->i]) >> 1) / s->len4) << 5;
}

/* frontieres des segments (en 1/16 d'echantillon) pour la hauteur et SWEEP du bloc */
static void gndy_bounds(struct st_gndy *s, const ctl_t *c, u32 inc)
{
	s32 ks = c->sweep >> 8, k, nw = T_GN_NW[ks], sum = 0, cum = 0, mean, fs, p4, rr;
	u32 p8 = 0xffffffffu / ((inc >> 8) | 1u);

	for (k = 0; k < s->n; k++)
		sum += s->d[k] >> 4;                                          /* Q22 */
	mean = (sum / s->n) >> 6;                                         /* Q16 */
	fs = nw + qmul(nw <= 0 ? ONE : (nw >= 65536 ? 0 : (65536 - nw) << 15), mean);
	p4 = (s32)(p8 >> 4);
	p4 = qmul(p4 << 8, fs << 8) >> 1;
	rr = (s32)(0xffffffffu / (((u32)sum >> 9) | 1u));
	s->B[0] = 0;
	for (k = 1; k < s->n; k++) {
		cum += s->d[k - 1] >> 4;
		s->B[k] = (qmul(cum, rr) * p4) >> 10;
	}
	s->B[s->n] = p4;
}

/* COLOR effectif : le reglage, plus la part tenue de CONTOUR (a droite +32 pas, a gauche -32). Le coup
 * (c->hr) s'y ajoute deja dans tiny_block pour les moteurs ordinaires. */
static s32 gndy_color(const ctl_t *c)
{
	return iclamp((c->color + ((c->contour - 0x4000) >> 1)) >> 8, 0, 127);
}

/* nombre de points du premier cycle, borne par la demi-periode comme dans gndy_cycle */
static s32 gndy_count(const ctl_t *c)
{
	u32 p4 = 0xffffffffu / ((c->inc >> 8) | 1u) >> 4;

	return imin(T_GN_N[gndy_color(c)], imax(3, (s32)(p4 / 32u)));
}

static void gndy_init(struct st_gndy *s, const ctl_t *c)
{
	u32 x = 0xC0FFEEu;
	s32 k, l0;

	for (k = 0; k < GMAX; k++) {
		x ^= x << 13;
		x ^= x >> 17;
		x ^= x << 5;
		s->a[k] = qmul((s32)(x ^ 0x80000000u), 1503238553) >> 1;    /* 0,7 x hasard signe, Q30 */
		s->d[k] = 67108864;                                           /* 1,0, Q26 */
	}
	s->n = gndy_count(c);                                             /* des le 1er cycle, COLOR et CONTOUR comptent */
	s->i = 0;
	s->pos4 = 0;
	gndy_bounds(s, c, c->inc0);                                       /* le 1er segment d'un coup suit la hauteur d'avant la chute : */
	l0 = s->B[1];                                                      /* un « poum » grave avant le chirp */
	gndy_bounds(s, c, c->inc);
	for (k = 1; k <= s->n; k++)
		s->B[k] += l0 - s->B[1];
	gndy_seg(s);
}

static inline s32 gndy_cauchy(u32 *rng)
{
	u32 r = rnd(rng), i = r >> 24;
	return T_CAUCHY[i] + qmul(T_CAUCHY[i + 1] - T_CAUCHY[i], (s32)(((r >> 16) & 255u) << 23));
}

static inline s32 gndy_mirror(s32 x, s32 lo, s32 hi)
{
	s32 r;
	for (r = 0; r < 4; r++) {
		if (x > hi)
			x = 2 * hi - x;
		else if (x < lo)
			x = 2 * lo - x;
		else
			break;
	}
	return iclamp(x, lo, hi);
}

/* nouvelle periode : le nombre de points, la marche, les frontieres */
static void gndy_cycle(struct st_gndy *s, u32 *rng, const ctl_t *c)
{
	s32 kc = gndy_color(c), ks = c->sweep >> 8, k, n = gndy_count(c), ast = qmul(T_GN_AST[kc], T_GN_WS[ks]), dst = T_GN_DST[ks];

	s->n = n;
	for (k = 0; k < n; k++) {
		s32 ca = gndy_cauchy(rng), x;
		x = (s->a[k] >> 2) + (qmul(ca, ast) << 1);                    /* Q28 */
		s->a[k] = gndy_mirror(x, -(1 << 28), 1 << 28) << 2;
		x = s->d[k] + (qmul(ca, dst) << 2);                           /* Q26 */
		s->d[k] = gndy_mirror(x, 10066330, 201326592);
	}
	s->i = 0;
	gndy_bounds(s, c, c->inc);
	gndy_seg(s);
}

static void eng_gndy(struct st_gndy *s, u32 *rng, const ctl_t *c, s32 *out)
{
	s32 i;

	if (c->restart)
		gndy_init(s, c);
	for (i = 0; i < 32; i++) {
		out[i] = s->y;
		s->y += s->dy;
		s->pos4 += 16;
		if (s->pos4 >= s->len4) {
			s->pos4 -= s->len4;
			if (++s->i >= s->n)
				gndy_cycle(s, rng, c);
			else
				gndy_seg(s);
		}
	}
}
