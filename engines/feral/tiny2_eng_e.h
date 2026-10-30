/*
 * TINY2, 5e serie : BENJ, HAVC (les deux ruglers : le Benjolin avec son filtre, le HAVOC quantifie).
 * Fichier inclus par tiny2_dsp.h (types et outils deja definis). Pas de 64 bits, pas de division dans les boucles
 * d'echantillons (sauf les fronts PolyBLEP de HAVC : deux par cycle).
 */

/* ------------------------------------------------------------------------------------------------- BENJ */
/* Le Benjolin : deux triangles ; le front montant de l'impulsion de l'OSC 2 fait avancer un registre de 8 bits
 * (donnee = impulsion de l'OSC 1 XOR le bit sorti, ou le bit sorti seul quand PUNCH verrouille la boucle) ; ses
 * 3 bits de poids fort font un CV a 8 marches qui entre dans les deux oscillateurs (SWEEP = RUN 1, CONTOUR = RUN 2,
 * en octaves) et dans le filtre. Sortie : le comparateur (tri 1 > tri 2) dans un passe-bas resonant (Q 5) et un
 * ecreteur tanh ; FINE = la coupure (127 : pas de filtre, le signal brut adouci). Il ne repart jamais d'un trig. */
struct st_benj {
	u32 p1, p2, reg, cv, prev2;
	s32 ic1, ic2, sm;
	u32 k_inc0, k_sweep, k_color, k_contour, k_fine;          /* reglages pour lesquels les tables ci-dessous valent */
	u32 m1, m2, mf;                                           /* entrees valides, un bit par valeur du CV */
	u32 inc1[8], inc2[8];
	s32 co[8][3];
};

/* base x 2^(run (cv / 7 - 1/2)), borne a 16 kHz */
static u32 bj_inc(u32 base, s32 run, u32 cv)
{
	u32 r = mulq16(base, exp2_q16(run * ((s32)cv * 2 - 7) / 14));
	return r > 0x55555555u ? 0x55555555u : r;
}

static inline u32 bj_i1(struct st_benj *s, const ctl_t *c, u32 cv)
{
	if (!((s->m1 >> cv) & 1u)) {
		s->inc1[cv] = bj_inc(c->inc0, lutk(T_BJ_RUN, c->sweep), cv);
		s->m1 |= 1u << cv;
	}
	return s->inc1[cv];
}

static inline u32 bj_i2(struct st_benj *s, const ctl_t *c, u32 cv)
{
	if (!((s->m2 >> cv) & 1u)) {
		s->inc2[cv] = bj_inc(lutku(T_BJ_F2, c->color), lutk(T_BJ_RUN, c->contour), cv);
		s->m2 |= 1u << cv;
	}
	return s->inc2[cv];
}

static inline const s32 *bj_co(struct st_benj *s, const ctl_t *c, u32 cv)
{
	if (!((s->mf >> cv) & 1u)) {
		u32 inc = mulq16(lutku(T_BJ_FC, c->fine), T_BJ_FR[cv]);
		inc = inc > 0x55555555u ? 0x55555555u : inc;
		svf_coef(lut_inc(T_TAN, inc), 3355443, &s->co[cv][0], &s->co[cv][1], &s->co[cv][2]);   /* k = 1 / 5 */
		s->mf |= 1u << cv;
	}
	return s->co[cv];
}

/* tanh(1,5 v), v en Q28 -> Q30 */
static inline s32 bj_tanh(s32 v)
{
	u32 u = mag(v), i;
	s32 t;
	u += u >> 1;
	i = (u + 0x200000u) >> 22;                        /* 257 points : arrondi au plus proche, sans interpolation */
	t = i >= 256 ? T_TANH[256] : T_TANH[i];
	return v < 0 ? -t : t;
}

static void eng_benj(struct st_benj *s, const ctl_t *c, s32 *out)
{
	u32 p1, p2, reg, cv, prev2, inc1, inc2, i;
	s32 ic1, ic2, sm, a1 = 0, a2 = 0, a3 = 0, raw = c->fine >= KMAX;
	const s32 *co;

	if (c->first) {
		s->p1 = 558345748u;                                          /* 0,13 */
		s->p2 = 3049426780u;                                         /* 0,71 */
		s->reg = 0xb2;
		s->cv = 0;
		s->prev2 = 0;
		s->ic1 = s->ic2 = s->sm = 0;
		s->m1 = s->m2 = s->mf = 0;
	}
	if (c->inc0 != s->k_inc0 || (u32)c->sweep != s->k_sweep) {
		s->k_inc0 = c->inc0;
		s->k_sweep = (u32)c->sweep;
		s->m1 = 0;
	}
	if ((u32)c->color != s->k_color || (u32)c->contour != s->k_contour) {
		s->k_color = (u32)c->color;
		s->k_contour = (u32)c->contour;
		s->m2 = 0;
	}
	if ((u32)c->fine != s->k_fine) {
		s->k_fine = (u32)c->fine;
		s->mf = 0;
	}
	p1 = s->p1;
	p2 = s->p2;
	reg = s->reg;
	cv = s->cv;
	prev2 = s->prev2;
	ic1 = s->ic1;
	ic2 = s->ic2;
	sm = s->sm;
	inc1 = bj_i1(s, c, cv);
	inc2 = bj_i2(s, c, cv);
	if (!raw) {
		co = bj_co(s, c, cv);
		a1 = co[0];
		a2 = co[1];
		a3 = co[2];
	}
	for (i = 0; i < 32; i++) {
		s32 d1 = (s32)(p1 + 0x80000000u), d2 = (s32)(p2 + 0x80000000u), hi = mag(d1) < mag(d2);   /* tri 1 > tri 2 */
		u32 pu1, pu2;

		p1 += inc1;
		p2 += inc2;
		pu1 = (p1 - 0x40000000u) < 0x80000000u;                       /* impulsions : phase dans [1/4, 3/4[ */
		pu2 = (p2 - 0x40000000u) < 0x80000000u;
		if (pu2 && !prev2) {
			u32 last = (reg >> 7) & 1u;

			reg = ((reg << 1) | (c->free ? last : (pu1 ^ last))) & 0xffu;
			cv = (reg >> 5) & 7u;
			inc1 = bj_i1(s, c, cv);
			inc2 = bj_i2(s, c, cv);
			if (!raw) {
				co = bj_co(s, c, cv);
				a1 = co[0];
				a2 = co[1];
				a3 = co[2];
			}
		}
		prev2 = pu2;
		if (raw) {
			s32 x = hi ? 751619277 : -751619277;                      /* +-0,7 : le brut, adouci (passe-bas de 6 kHz) */

			sm += qmul(x - sm, 1168363974);
			out[i] = sm;
		} else {
			s32 x = hi ? 1 << 27 : -(1 << 27), v1, v2, v3;            /* +-0,5, Q28 */

			v3 = x - ic2;
			v1 = qmac2(a1, ic1, a2, v3);
			v2 = sadd(ic2, qmac2(a2, ic1, a3, v3));
			ic1 = sadd(v1, v1 - ic1);
			ic2 = sadd(v2, v2 - ic2);
			out[i] = bj_tanh(v2);
		}
	}
	s->p1 = p1;
	s->p2 = p2;
	s->reg = reg;
	s->cv = cv;
	s->prev2 = prev2;
	s->ic1 = ic1;
	s->ic2 = ic2;
	s->sm = sm;
}

/* ------------------------------------------------------------------------------------------------- HAVC */
/* Le HAVOC quantifie : une horloge (la note / 16) et un oscillateur de donnees (a un rapport p/q de l'horloge)
 * ecrivent un registre de 8 bits ; ses 3 bits de poids fort jouent une note de la gamme (quintes et octaves).
 * COLOR : le rapport (0..55 injection, arpeges de 2 a 8 pas ; 56..95 XOR, riffs ; 96..127 derive) ; SWEEP : la
 * memoire (boucles verrouillees de 8 a 2 pas, puis 8 pas avec 0 -> 25 % de donnees neuves, puis tout neuf) ;
 * CONTOUR : de la pincee (30 ms) au rebond (150 ms, resonance) au glissando (porte ouverte). Voix : triangle +
 * carre PolyBLEP dans une porte passe-bas. Sans FREE, un trig rembobine le riff a son premier pas. */
struct st_havc {
	u32 reg, dph, ph;
	s32 L, rot, sleft, sidx, pkey, init;
	s32 semi, target, e, g, ic1, ic2;
};

static const s32 HV_SCALE[8] = {0, 0, 7, 7, 12, 12, 19, 19};

static void hv_map(const ctl_t *c, s32 *mode, u32 *r, s32 *L, u32 *pnew, s32 *fd, s32 *idx)
{
	s32 col = c->color >> 8, sw = c->sweep >> 8;

	if (col < 56) {
		*mode = 0;
		*idx = col >> 3;
		*r = T_HV_INJ[*idx];
	} else if (col < 96) {
		*mode = 1;
		*idx = 8 + ((col - 56) >> 3);
		*r = T_HV_XOR[*idx - 8];
	} else {
		*mode = 1;
		*idx = 16 + col - 96;
		*r = T_HV_DRIFT[col - 96];
	}
	*fd = 0;
	*pnew = 0;
	*L = 8;
	if (sw <= 40)
		*L = 8 - (sw * 6 + 20) / 40;
	else if (sw <= 100)
		*pnew = T_HV_PNEW[sw];
	else
		*fd = 1;
}

/* le registre recoit 8 pas de l'oscillateur de donnees : l'arpege de depart, que les boucles verrouillees gardent */
static void hv_prefill(struct st_havc *s, const ctl_t *c)
{
	s32 mode, L, fd, idx, i;
	u32 r, pn, mask;

	hv_map(c, &mode, &r, &L, &pn, &fd, &idx);
	mask = (1u << L) - 1u;
	s->reg = 0xb2u & mask;
	if (!s->reg)
		s->reg = 1;
	s->dph = 429496730u;                                              /* 0,1 */
	for (i = 0; i < 8; i++) {
		u32 d, last;

		s->dph += r;
		d = s->dph < 0x80000000u;
		last = (s->reg >> (L - 1)) & 1u;
		s->reg = ((s->reg << 1) | (mode ? (d ^ last) : d)) & mask;
	}
	s->L = L;
	s->rot = 0;
	s->pkey = (mode << 12) | (idx << 4) | L;
}

static s32 hv_key(const ctl_t *c)
{
	s32 mode, L, fd, idx;
	u32 r, pn;

	hv_map(c, &mode, &r, &L, &pn, &fd, &idx);
	return (mode << 12) | (idx << 4) | L;
}

/* longueur d'un pas : 16 periodes de la note */
static s32 hv_steplen(const ctl_t *c)
{
	s32 sl = (s32)(0xffffffffu / ((c->inc0 >> 4) | 1u));

	return sl < 8 ? 8 : sl;
}

static void hv_step(struct st_havc *s, u32 *rng, const ctl_t *c, s32 v)
{
	s32 mode, L, fd, idx, take, b2, b1, b0;
	u32 r, pn, mask, d, last, nb;

	hv_map(c, &mode, &r, &L, &pn, &fd, &idx);
	if (L != s->L) {
		s->L = L;
		s->reg &= (1u << L) - 1u;
		s->rot = 0;
	}
	mask = (1u << L) - 1u;
	s->dph += r;
	d = s->dph < 0x80000000u;
	last = (s->reg >> (L - 1)) & 1u;
	take = fd ? 1 : (pn && rnd(rng) < pn);
	nb = take ? (mode ? (d ^ last) : d) : last;
	s->reg = ((s->reg << 1) | nb) & mask;
	if (++s->rot >= L)
		s->rot = 0;
	b2 = (s->reg >> (L - 1)) & 1;
	b1 = (s->reg >> (L - 2)) & 1;
	b0 = L >= 3 ? (s32)((s->reg >> (L - 3)) & 1u) : 0;
	s->target = HV_SCALE[(b2 << 2) | (b1 << 1) | b0] << 16;
	if (v <= 0 || !s->init)
		s->semi = s->target;
	s->init = 1;
	s->sleft = hv_steplen(c);
	s->sidx = 0;
	s->e = ONE - qmul(v, 322122547);                                  /* 1 - 0,15 v */
	s->g = lutk(T_HV_E07, c->contour);
}

/* residu PolyBLEP en Q30 d'un front a la phase p (increment dt) */
static inline s32 hv_blep(u32 p, u32 dt)
{
	u32 sh, f, w;

	if (p < dt) {
		sh = __builtin_clz(dt);
		f = (p << sh) / ((dt << sh) >> 16);                           /* p / dt, Q16 */
		w = (65536u - f) >> 1;
		return -(s32)(w * w);                                         /* -(1 - u)^2 */
	}
	if (p + dt < dt) {
		sh = __builtin_clz(dt);
		f = ((p + dt) << sh) / ((dt << sh) >> 16);
		w = f >> 1;
		return (s32)(w * w);
	}
	return 0;
}

static void eng_havc(struct st_havc *s, u32 *rng, const ctl_t *c, s32 *out)
{
	s32 i = 0, v = lutk(T_HV_V, c->contour), k24 = lutk(T_HV_K, c->contour);
	s32 d1 = lutk(T_HV_D1, c->contour), d07 = lutk(T_HV_D07, c->contour), nv = ONE - v;
	u32 y2 = 0;

	if (c->first) {
		hv_prefill(s, c);
		s->ph = 0;
		s->ic1 = s->ic2 = 0;
		s->sleft = 0;
		s->init = 0;
	} else if (c->restart) {                                          /* le riff repart de son premier pas */
		u32 mask = (1u << s->L) - 1u;
		s32 q = s->rot % s->L;

		s->reg = ((s->reg >> q) | (s->reg << (s->L - q))) & mask;
		s->rot = 0;
		if (hv_key(c) != s->pkey)
			hv_prefill(s, c);
		s->sleft = 0;
	}
	if (v > 0) {                                                      /* glissando : constante de temps de 0,35 pas x v */
		s32 sl = hv_steplen(c);
		u32 w;

		sl = sl > 32767 ? 32767 : sl;
		w = ((u32)sl * (u32)(v >> 15)) >> 4;
		y2 = 1106487146u / (w | 1u);                                  /* log2(e) / (0,35 sl v), Q16 */
		y2 = y2 > 0x4000000u ? 0x4000000u : y2;
	}
	while (i < 32) {
		s32 n, semi1, a1, a2, a3, bp, lp, v3, ga, ddt, j, ic1 = s->ic1, ic2 = s->ic2, at, ef;
		u32 inc0, inc1, dt, fc;

		if (s->sleft <= 0)
			hv_step(s, rng, c, v);
		n = imin(32 - i, s->sleft);
		if (v > 0)
			semi1 = s->semi + qmul(s->target - s->semi, ONE - exp2neg_q31(y2 * (u32)n));
		else
			semi1 = s->semi = s->target;
		inc0 = inc_of(c->key0 + s->semi);
		inc1 = inc_of(c->key0 + semi1);
		dt = inc0;
		ddt = ((s32)inc1 - (s32)inc0) / n;
		/* coupure de la porte : 160 + 740 v + 5200 (1 - v) env^2 + (2 + v) f, bornee a 18 kHz ; env : l'enveloppe, attaque comprise */
		at = s->sidx + n / 2 < 96 ? (s->sidx + n / 2) * 22369621 : ONE;
		ef = qmul(s->e, ONE - qmul(ONE - at, nv));
		fc = 14316558u + (u32)qmul(66214079, v) + (u32)qmul(465288124, qmul(qmul(ef, ef), nv)) + 2u * inc0 + (u32)qmul((s32)inc0, v);
		fc = fc > 0x60000000u ? 0x60000000u : fc;
		svf_coef(lut_inc(T_TAN, fc), k24, &a1, &a2, &a3);
		for (j = 0; j < n; j++) {
			s32 tri, sq, x;
			u32 cur = dt;

			s->ph += cur;
			dt = (u32)((s32)dt + ddt);
			tri = (s32)(SIG1 - mag((s32)(s->ph + 0x80000000u)));
			sq = (s->ph < 0x80000000u ? SIG1 : -SIG1) + hv_blep(s->ph, cur | 1u) - hv_blep(s->ph + 0x80000000u, cur | 1u);
			x = (qmul(tri, 1181116006) + qmul(sq, 966367642)) >> 2;     /* 0,55 tri + 0,45 carre, Q28 */
			v3 = x - ic2;
			bp = qmac2(a1, ic1, a2, v3);
			lp = sadd(ic2, qmac2(a2, ic1, a3, v3));
			ic1 = sadd(bp, bp - ic1);
			ic2 = sadd(lp, lp - ic2);
			ga = s->g;
			if (s->sidx < 96) {
				s32 a = T_HV_ATT[s->sidx];

				ga = qmul(ga, a + qmul(ONE - a, v));                    /* attaque de 2 ms, fondue dans le glissando */
			}
			out[i + j] = iclamp(qmul(lp, ga), -(1 << 29) + 1, (1 << 29) - 1) << 2;
			s->g = qmul(s->g, d07);
			s->e = qmul(s->e, d1);
			s->sidx++;
		}
		s->ic1 = ic1;
		s->ic2 = ic2;
		s->semi = semi1;
		s->sleft -= n;
		i += n;
	}
}
