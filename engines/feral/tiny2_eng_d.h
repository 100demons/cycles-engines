/*
 * TINY2, 4e serie : SCAN (synthese par balayage) et les filtres a variable d'etat. Inclus par tiny2_dsp.h.
 */

/* ------------------------------------------------------------------------------------------------- SCAN */
/* Synthese par balayage (Verplank, Mathews, Shaw) : 32 masses reliees en anneau vibrent lentement (une mise a jour par
 * bloc), un lecteur de la hauteur de la note en balaie la forme. COLOR : la raideur (la vitesse de l'evolution) ;
 * SWEEP : la forme que chaque trig frappe (large, quart de l'anneau, pic) ; DECAY : l'amortissement. Positions Q28.
 * Le lecteur lit deux copies de l'anneau en Q26 (debut et fin du bloc, bornees, avec trois cases de bord pour que
 * l'anneau se referme sans masque) : la forme glisse de l'une a l'autre sur le bloc. Les quatre coefficients de la
 * cubique (Catmull-Rom) sont recalcules a chaque changement de case de la table, puis suivis d'un echantillon a
 * l'autre (la cubique est lineaire dans les points : le fondu de l'une a l'autre est un pas constant).
 * 32 masses et non 64 : moitie moins de masses a mettre a jour, de cases a lire et de changements de case (notes/58
 * §Coût) ; la raideur est divisee par 4 pour que les premiers modes de l'anneau gardent leur frequence. */
#define NMASS 32
#define NMSH  5                                 /* log2 NMASS */
#define SCLIM 100000000                         /* borne des points du lecteur, Q26 */
struct st_scan {
	s32 xg[NMASS + 2];                          /* positions Q28 ; xg[0] = x[NMASS - 1], xg[NMASS + 1] = x[0] */
	s32 v[NMASS];
	s32 q[2][NMASS + 3];                        /* copies du lecteur : q[j + 1] = x[j] >> 2, q[0] = x[NMASS - 1], q[NMASS + 1] = x[0], q[NMASS + 2] = x[1] */
	u32 ph;
	s32 sw;                                     /* laquelle des deux copies est le debut du bloc */
};

static inline void scan_edges(s32 *xg)
{
	xg[0] = xg[NMASS];
	xg[NMASS + 1] = xg[1];
}

/* retire la moyenne m des positions (somme des x >> NMSH), et en fait la copie du lecteur : Q26, bornee, bords refermes */
static void scan_copy(s32 *x, s32 *q, s32 m)
{
	s32 i;
	for (i = 0; i < NMASS; i++) {
		s32 xi = x[i] - m;
		x[i] = xi;
		q[i + 1] = iclamp(xi >> 2, -SCLIM, SCLIM);
	}
	q[0] = q[NMASS];
	q[NMASS + 1] = q[1];
	q[NMASS + 2] = q[2];
}

static void eng_scan(struct st_scan *s, u32 *rng, const ctl_t *c, s32 *out)
{
	s32 i, sw = c->sweep, k = lutk(T_SC_K, c->color), damp, a, crazy = 0, mv = 0, *x = s->xg + 1, *qa, *qb;
	u32 inc = c->inc, ph, cur;
	s32 c0 = 0, c1 = 0, c2 = 0, c3 = 0, e0 = 0, e1 = 0, e2 = 0, e3 = 0;

	if (c->restart && !c->first) {                  /* (un moteur neuf est deja a zero) */
		zero32(s->xg, NMASS + 2);
		zero32(s->v, NMASS);
		zero32(&s->q[0][0], 2 * (NMASS + 3));
	}
	if (c->trig) {
		s32 u = sw < 0x4000 ? sw << 17 : (sw == 0x4000 ? ONE : imin((sw - 0x4000) * 133152, ONE)), bu = ONE - u, m = 0;
		if (sw <= 0x4000) {                         /* large -> quart de l'anneau */
			for (i = 0; i < NMASS; i++) {
				s32 xi = sadd(x[i], qmac2(T_SC_WIDE[i], bu, T_SC_QUART[i], u) >> 2);
				x[i] = xi;
				m += xi >> NMSH;
			}
		} else {                                    /* quart de l'anneau -> pic (au hasard autour de la case NMASS / 4) */
			s32 amp = imin(ONE, 751619277 + ((sw - 0x4000) << 14));   /* Strike haut : plus de grains */
			for (i = 0; i < NMASS; i++) {
				s32 spike = (i == NMASS / 4 ? 1 << 30 : 0) + qmul(rnds(rng) >> 1, amp), xi;
				xi = sadd(x[i], qmac2(T_SC_QUART[i], bu, spike, u) >> 2);
				x[i] = xi;
				m += xi >> NMSH;
			}
		}
		scan_copy(x, s->q[s->sw], m);
		scan_edges(s->xg);
	}
	/* amortissement : 6,91 / (max(t60, 0,05) x 1500), au plus 0,5 */
	damp = (s32)exp2_q16(-508725 - imax(c->tl, -283242)) << 15;
	damp = imin(damp, 0x40000000);
	a = ONE - damp;
	/* Strike et Env hauts : des vitesses aleatoires secouent l'anneau a chaque bloc. */
	if (c->sweep > 0x4000 && c->contour > 0x4000) {
		crazy = qmul((c->sweep - 0x4000) << 16, (c->contour - 0x4000) << 16);
		if (crazy) {
			for (i = 0; i < NMASS; i++)
				s->v[i] = sadd(s->v[i], qmul(rnds(rng) >> 3, crazy) >> 1);
		}
	}
	/* v <- (1 - damp) v + k (g + d - 2 c), la vitesse moyenne retiree ensuite de la position */
	for (i = 0; i < NMASS; i++) {
		s32 lap = s->xg[i] + s->xg[i + 2] - 2 * s->xg[i + 1], vn = qmac2(a, s->v[i], k, lap);
		s->v[i] = vn;
		mv += vn >> NMSH;
	}
	qa = s->q[s->sw];
	qb = s->q[s->sw ^ 1];
	for (i = 0; i < NMASS; i++) {
		s32 xn = sadd(x[i], s->v[i] - mv);
		xn = iclamp(xn, -4 * SCLIM, 4 * SCLIM);
		x[i] = xn;
		qb[i + 1] = xn >> 2;
	}
	scan_edges(s->xg);
	qb[0] = qb[NMASS];
	qb[NMASS + 1] = qb[1];
	qb[NMASS + 2] = qb[2];
	ph = s->ph;
	cur = 0xffffffffu;
	if (inc >= 0x01000000u) {                           /* une case tous les huit echantillons ou plus vite : lecture lineaire (le bruit prime) */
		for (i = 0; i < 32; i++) {
			const s32 *pa, *pb;
			s32 t, f = i << 26, z1, z2;
			ph += inc;
			pa = qa + (ph >> 27);
			pb = qb + (ph >> 27);
			t = (s32)((ph & 0x7ffffffu) << 4);
			z1 = pa[1] + qmul(pb[1] - pa[1], f);
			z2 = pa[2] + qmul(pb[2] - pa[2], f);
			out[i] = iclamp(z1 + qmul(z2 - z1, t), -(1 << 26) + 1, (1 << 26) - 1) << 4;
		}
		s->ph = ph;
		s->sw ^= 1;
		return;
	}
	for (i = 0; i < 32; i++) {
		s32 t, y;
		u32 i1;
		ph += inc;
		i1 = ph >> 27;
		if (i1 != cur) {                                /* nouvelle case : coefficients au fondu d'ici (i / 32) */
			const s32 *pa = qa + i1, *pb = qb + i1;
			s32 a0 = pa[1], a1 = (pa[2] - pa[0]) >> 1, a2 = (2 * pa[0] - 5 * pa[1] + 4 * pa[2] - pa[3]) >> 1;
			s32 a3 = (3 * (pa[1] - pa[2]) + pa[3] - pa[0]) >> 1, bt = i << 26;
			s32 b0 = pb[1], b1 = (pb[2] - pb[0]) >> 1, b2 = (2 * pb[0] - 5 * pb[1] + 4 * pb[2] - pb[3]) >> 1;
			s32 b3 = (3 * (pb[1] - pb[2]) + pb[3] - pb[0]) >> 1;
			cur = i1;
			e0 = (b0 - a0) >> 5; e1 = (b1 - a1) >> 5; e2 = (b2 - a2) >> 5; e3 = (b3 - a3) >> 5;
			c0 = a0 + qmul(b0 - a0, bt);
			c1 = a1 + qmul(b1 - a1, bt);
			c2 = a2 + qmul(b2 - a2, bt);
			c3 = a3 + qmul(b3 - a3, bt);
		} else {
			c0 += e0; c1 += e1; c2 += e2; c3 += e3;
		}
		t = (s32)((ph & 0x7ffffffu) << 4);
		y = c0 + qmul(t, c1 + qmul(t, c2 + qmul(t, c3)));                         /* Q26 */
		out[i] = iclamp(y, -(1 << 26) + 1, (1 << 26) - 1) << 4;                    /* Q30 */
	}
	s->ph = ph;
	s->sw ^= 1;
}

/* coefficients d'un filtre a variable d'etat (TPT) : g (Q24), k (Q24) -> a1, a2, a3 (Q31) */
static inline void svf_coef(s32 g, s32 k24, s32 *a1, s32 *a2, s32 *a3)
{
	s32 t = qmul(g, g + k24) << 7;
	u32 den = (1u << 24) + (u32)t;
	s32 q = (s32)((0x7fffffffu / (den >> 16)) << 8);                       /* 1 / den, 8 bits */
	s32 e;

	e = (1 << 24) - qmul((s32)den, q);                                     /* Newton : q <- q (2 - den q), deux fois */
	q = sadd(q, qmul(q, e << 7));
	e = (1 << 24) - qmul((s32)den, q);
	q = sadd(q, qmul(q, e << 7));
	*a1 = q;
	*a2 = qmul(g, q) << 7;
	*a3 = qmul(g, *a2) << 7;
}
