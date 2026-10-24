/*
 * TINY2, 1re serie de moteurs : BYTE, FOUR, CHAO, PLSR (notes/58). Inclus par tiny2_dsp.h, apres les outils
 * communs. Chaque moteur a son etat (st_xxx), une remise a zero (init_xxx, au premier trig sur ce modele) et un
 * bloc de 32 echantillons (eng_xxx -> out, Q30) ; c->restart : ce trig repart de zero (FREE eteint).
 */

/* ------------------------------------------------------------------------------------------------- BYTE */
/* Bytebeat : t = compteur a 8 kHz pour un do4, 16 formules classees du clairsemé au dense (COLOR), leurs
 * constantes mises a l'echelle par SWEEP (decalages de +-3, autres de x0,5 a x2) ; CONTOUR droit : SWEEP monte. */
struct st_byte {
	u32 ti, tf;
};

static void init_byte(struct st_byte *s)
{
	s->ti = s->tf = 0;
}

#define DV(x) ((x) ? (x) : 1u)
#define BBADV  { s->tf += sf; s->ti += si + (s->tf < sf); t = s->ti; }
#define BBLOOP(expr) \
	for (i = 0; i < 32; i++) { BBADV; v = (u8)(expr); out[i] = ((s32)v - 128) << 23; } break

static void eng_byte(struct st_byte *s, const ctl_t *c, s32 *out)
{
	s32 i, z, idx, n, sw, u, m, dsh, sq;
	u32 k[8], si, sf, t, v, shm;
	u32 k0, k1, k2, k3, k4, k5, k6, k7, mm;

	if (c->restart)
		init_byte(s);
	z = iclamp(c->color >> 11, 0, 15);
	idx = T_BB_ORDER[z];
	n = T_BB_N[idx];
	shm = (u32)T_BB_SH[idx];
	sw = kcl(c->sweep + qmul(c->hr, 63 << 8));
	u = (sw - 0x4000) << 2;                          /* octaves, Q16 : -1 .. +1 */
	m = (s32)exp2_q16(u);
	dsh = (3 * u + 0x8000) >> 16;
	for (i = 0; i < 8; i++) {
		s32 b = T_BB_K[idx * 8 + i];
		k[i] = i >= n ? 0 : ((shm >> i) & 1 ? (u32)iclamp(b + dsh, 0, 16) : (u32)((b * m + 0x8000) >> 16));
	}
	k0 = k[0]; k1 = k[1]; k2 = k[2]; k3 = k[3]; k4 = k[4]; k5 = k[5]; k6 = k[6]; k7 = k[7];
	/* pas du compteur par echantillon, Q16 : 8000 / 48000 x f / f(do4) */
	sq = (s32)exp2_q16((c->key - (60 << 16)) / 12) / 6;
	si = (u32)sq >> 16;
	sf = (u32)sq << 16;
	switch (idx) {
	case 0: BBLOOP(t & (t >> k0));
	case 1: BBLOOP(t * (k0 & (t >> k1)));
	case 2: BBLOOP(t * (((t >> k0) | (t >> k1)) & k2 & (t >> k3)));
	case 3: BBLOOP(((t * k0) & (t >> k1)) | ((t * k2) & (t >> k3)));
	case 4: BBLOOP(((t * k0) & (t >> k1)) % DV(k2));
	case 5: BBLOOP(t * ((t >> k0) & (t >> k1) & k2 & (t >> k3)));
	case 6: BBLOOP(t * ((((t >> k0) & k1) | ((t >> k2) & k3)) ^ ((t >> k4) & k5 & (t >> k6))));
	case 7: BBLOOP((t * (k0 + ((1u ^ (t >> k1)) & k2)) * (k3 + (k4 & (t >> k5)))) >> ((t >> k6) & (k7 & 7)));
	case 8: BBLOOP(((t * ((t >> k0) | (t >> k1))) & k2 & (t >> k3)) ^ ((t & (t >> k4)) | (t >> k5)));
	case 9: BBLOOP(((t >> k0) | t | (t >> k1)) * k2 + k3 * ((t & (t >> k4)) | (t >> k5)));
	case 10: BBLOOP(((t % DV(k0)) & (t >> k1)) | (t >> k2) | (((t * t) >> k3) & (t >> k4)));
	case 11: BBLOOP((t * (t >> k0) * k1 + (t >> k2) * (t >> k3) * (t >> k4) * k5) >> (((t >> k6) | (t >> k7)) & 1));
	case 12: BBLOOP((mm = (t >> k0) | (t >> k1), mm = mm ? t / mm : 0, mm ? t % mm : t));
	case 13: BBLOOP((mm = (t >> k0) | (t >> k1), mm ? t / DV(t % mm) : t));
	case 14: BBLOOP((t * (0u - ((t >> k0) | t | (t >> k1) | (t >> k2)))) ^ t);
	default: BBLOOP((t << k0) ^ (0u - ((t >> k1) & 1u)));
	}
}

/* ------------------------------------------------------------------------------------------------- FOUR */
/* Fourses : quatre points qui rebondissent chacun entre ses voisins, a des vitesses en progression (COLOR :
 * 1,5 a 3 fois) ; SWEEP (et CONTOUR droit) : symetrie de leur aller-retour. Sortie : leur moyenne. */
struct st_four {
	s32 pos[4], dir[4];
};

static void init_four(struct st_four *s)
{
	static const s32 p0[4] = { -644245094, -214748365, 214748365, 644245094 };
	s32 i;
	for (i = 0; i < 4; i++) {
		s->pos[i] = p0[i];
		s->dir[i] = (i & 1) ? -1 : 1;
	}
}

#define FOUR_ADV(P, D, UP, DN, LO, HI) \
	do { \
		s32 x_ = (P); \
		if ((D) > 0) { \
			s32 y_ = x_ + (UP); \
			x_ = y_ < x_ ? 0x7fffffff : y_; \
			if (x_ >= (HI)) { x_ = (HI); (D) = -1; } \
		} else { \
			s32 y_ = x_ - (DN); \
			x_ = y_ > x_ ? -0x7fffffff : y_; \
			if (x_ <= (LO)) { x_ = (LO); (D) = 1; } \
		} \
		(P) = x_; \
	} while (0)

static void eng_four(struct st_four *s, const ctl_t *c, s32 *out)
{
	s32 i, j, sw, ru, rd;
	s32 up0 = 0, up1 = 0, up2 = 0, up3 = 0, dn0 = 0, dn1 = 0, dn2 = 0, dn3 = 0;
	s32 p0, p1, p2, p3, d0, d1, d2, d3;
	u32 rate = c->inc, spread = lutku((const u32 *)T_FOUR_SPREAD, c->color);

	if (c->restart)
		init_four(s);
	sw = kcl(c->sweep + qmul(c->hr, 63 << 8));
	ru = lutk(T_FOUR_RUP, sw);
	rd = lutk(T_FOUR_RDN, sw);
	for (j = 0; j < 4; j++) {                        /* pas par echantillon, Q30 : un tour = 4,0 = 2^32 */
		s32 a = qmul((s32)(rate >> 1), ru), b = qmul((s32)(rate >> 1), rd);
		a = (a > 0x3ffffff ? 0x3ffffff : a) << 4;
		b = (b > 0x3ffffff ? 0x3ffffff : b) << 4;
		switch (j) {
		case 0: up0 = a; dn0 = b; break;
		case 1: up1 = a; dn1 = b; break;
		case 2: up2 = a; dn2 = b; break;
		default: up3 = a; dn3 = b; break;
		}
		rate = mulq16(rate, spread);
	}
	p0 = s->pos[0]; p1 = s->pos[1]; p2 = s->pos[2]; p3 = s->pos[3];
	d0 = s->dir[0]; d1 = s->dir[1]; d2 = s->dir[2]; d3 = s->dir[3];
	for (i = 0; i < 32; i++) {
		FOUR_ADV(p0, d0, up0, dn0, -0x40000000, p1);
		FOUR_ADV(p1, d1, up1, dn1, p0, p2);
		FOUR_ADV(p2, d2, up2, dn2, p1, p3);
		FOUR_ADV(p3, d3, up3, dn3, p2, 0x40000000);
		out[i] = (p0 >> 2) + (p1 >> 2) + (p2 >> 2) + (p3 >> 2);
	}
	s->pos[0] = p0; s->pos[1] = p1; s->pos[2] = p2; s->pos[3] = p3;
	s->dir[0] = d0; s->dir[1] = d1; s->dir[2] = d2; s->dir[3] = d3;
}

/* ------------------------------------------------------------------------------------------------- CHAO */
/* Carte logistique x -> r x (1 - x), seize iterations par periode, relue entre les iterations. COLOR : r (de
 * la periode 2 aux accroches 4, 8, 3, 6, 5 puis du bord du chaos a r = 4) ; CONTOUR droit : le coup part de r = 4
 * et se pose ; SWEEP : de l'interpolation cubique a la lineaire (filtree), puis maintenue ; au-dela de 96,
 * x est ecrase de 24 a 6 bits. */
struct st_chao {
	s32 x, h[4];
	u32 acc;
	s32 lp;
};

static void init_chao(struct st_chao *s)
{
	s32 i;
	s->x = 672886554;                                /* 0,31337 */
	for (i = 0; i < 4; i++)
		s->h[i] = s->x;
	s->acc = 0;
	s->lp = 0;
}

static void eng_chao(struct st_chao *s, u32 *rng, const ctl_t *c, s32 *out)
{
	s32 i, sw = c->sweep >> 8, r4, a = 0, lpc = 0, xmask = 0, xmin = 4;
	u32 inc2 = c->inc > (0x7fffffffu >> 4) ? 0x7fffffffu : c->inc << 4;   /* 16 iterations par periode */
	s32 x;

	if (c->restart)
		init_chao(s);
	r4 = lutk(T_CHAO_R4, c->color);
	r4 += qmul(ONE - r4, c->hr);
	if (sw <= 64) {
		a = sw >= 64 ? ONE : sw << 25;               /* 0 .. 1,0 (Q31) */
		if (sw < 64) {
			u32 fc = mulq16(c->inc * 3u, exp2_q16(((sw << 16) / 64) * 10966 / 1000));    /* 3 f x 2000^a */
			lpc = lut_inc(T_LP1, fc > 0x7fffffffu ? 0x7fffffffu : fc);
		}
	} else if (sw <= 96) {
		a = sw >= 96 ? ONE : (sw - 64) << 26;        /* 0 .. 1,0 */
	} else {
		s32 bits = 24 - (18 * (sw - 96)) / 31;       /* 24 .. 6 */
		xmask = ~((1 << (31 - bits)) - 1);
		xmin = 1 << (31 - bits);
	}
	for (i = 0; i < 32; i++) {
		s32 o, t, lin, h0, h1, h2, h3;
		s->acc += inc2;
		if (s->acc < inc2) {                         /* une iteration de la carte */
			s32 p = qmul(s->x, ONE - s->x), y = qmul(r4, p);
			x = y > 0x1fffffff ? 0x7fffffff : y << 2;
			if (sw < 96) {
				x += (s32)rnd(rng) >> 24;
			} else {
				x &= xmask;
				x = x < xmin ? xmin : x;
			}
			s->x = x = iclamp(x, 4, 0x7ffffffb);
			s->h[0] = s->h[1]; s->h[1] = s->h[2]; s->h[2] = s->h[3]; s->h[3] = x;
		}
		h0 = s->h[0]; h1 = s->h[1]; h2 = s->h[2]; h3 = s->h[3];
		(void)h3;
		t = (s32)(s->acc >> 1);                      /* 0 .. 1,0 (Q31) */
		lin = h1 + qmul(h2 - h1, t);
		if (sw <= 64) {
			/* Catmull-Rom entre h1 et h2, en differences avec h1 et divisee par 8 pour ne rien deborder */
			s32 da = (h0 - h1) >> 3, db = (h2 - h1) >> 3, dc = (h3 - h1) >> 3;
			s32 c1 = db - da, c2 = 2 * da + 4 * db - dc, c3 = dc - da - 3 * db;
			s32 q = qmul(t, c1 + qmul(t, c2 + qmul(t, c3)));            /* (cub - h1) / 4 */
			s32 cub = sadd(h1, sadd(q, sadd(q, sadd(q, q))));
			cub = iclamp(cub, 0, ONE);
			o = cub + qmul(lin - cub, a);
			if (sw < 64) {
				s->lp += qmul(lpc, o - s->lp);
				o = s->lp;
			}
		} else if (sw <= 96) {
			o = lin + qmul(h2 - lin, a);
		} else {
			o = h2;
		}
		out[i] = qmul(o - 0x40000000, 1932735283);   /* (o - 0,5) x 2 x 0,9 */
	}
}

/* ------------------------------------------------------------------------------------------------- PLSR */
/* Pulsar : une impulsion par periode (porteuse sinus a COLOR fois la hauteur, fenetree), SWEEP : une periode
 * sur 4, 3, 2, toutes, puis des trous au hasard de plus en plus nombreux. */
struct st_plsr {
	u32 ph, pw, pc;
	s32 cnt, keep;
};

static void plsr_decide(struct st_plsr *s, u32 *rng, s32 sw)
{
	s->cnt++;
	if (sw < 16)
		s->keep = (s->cnt & 3) == 0;
	else if (sw < 32)
		s->keep = (s->cnt % 3) == 0;
	else if (sw < 56)
		s->keep = (s->cnt & 1) == 0;
	else if (sw < 70)
		s->keep = 1;
	else {
		s32 u = ((sw - 64) << 24) / 63, u31 = u >= (1 << 24) ? ONE : u << 7;     /* 0 .. 1,0 */
		u32 thr = (u32)qmul(qmul(u31, u31), 1932735283);                          /* 0,9 u^2, Q31 */
		s->keep = (rnd(rng) >> 1) >= thr;
	}
}

static void init_plsr(struct st_plsr *s)
{
	zero32(s, sizeof *s / 4);
	s->cnt = -1;
	s->keep = 1;
}

static void eng_plsr(struct st_plsr *s, u32 *rng, const ctl_t *c, s32 *out)
{
	s32 i, sw = c->sweep >> 8;
	u32 mult = lutku((const u32 *)T_PLSR_SMUL, c->color), wmul = lutku((const u32 *)T_PLSR_WMUL, c->color);
	u32 inc = c->inc, pwinc = mulq16(inc, wmul), pcinc = mulq16(inc, mult);

	if (c->restart) {
		init_plsr(s);
		plsr_decide(s, rng, sw);
	}
	for (i = 0; i < 32; i++) {
		s->ph += inc;
		if (s->ph < inc) {                           /* nouvelle periode : fenetre et porteuse repartent */
			s->pw = mulq16(s->ph, wmul);
			s->pc = mulq16(s->ph, mult);
			plsr_decide(s, rng, sw);
		} else {
			s->pw += pwinc;
			s->pc += pcinc;
			if (s->pw < pwinc)
				s->pw = 0xffffffffu;                   /* fenetre finie */
		}
		if (!s->keep || s->pw == 0xffffffffu) {
			out[i] = 0;
		} else {
			s32 w = 0x40000000 - (cos31(s->pw) >> 1);                       /* 0,5 - 0,5 cos */
			out[i] = qmul(qmul(w, sin31(s->pc)), 1932735283) >> 1;
		}
	}
}
