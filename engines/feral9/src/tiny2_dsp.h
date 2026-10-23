/*
 * TINY2 : le coeur du son, 10 moteurs bruites, imprevisibles et legers, en virgule fixe (sans FPU, sans libgcc).
 *
 * Ce fichier sert deux fois : compile pour le Cycles (tiny2.c, -mcpu=54418 : l'EMAC en mode fractionnaire signe
 * sature, MACSR = 0xa0 pendant la boucle des voix) et pour l'ordinateur (tools/emu/tiny2_ref.c, tiny2_host.c) avec
 * la meme arithmetique a l'octet pres : la preuve compare les deux echantillon par echantillon (comme ACID).
 *
 * Formats : signaux Q30 (1,0 = 1 << 30 = la demi-echelle, comme MACRO) ; coefficients Q31 ; phases u32 (2^32 = un
 * cycle) ; « inc » = increment de phase par echantillon = 2^32 x f / 48000 ; hauteurs en demi-tons << 16 ; potards
 * 8.8 (0..0x7f00, le potard 127 = 0x7f00). Pas de division par zero, pas de 64 bits (rien a lier).
 *
 * Reglages communs a tous les modeles (notes/58 §3a) : COLOR = timbre, SWEEP = 2e macro, CONTOUR = enveloppe de
 * coup (a gauche la hauteur retombe de jusqu'a 48 demi-tons, a droite COLOR monte de jusqu'a 64 pas puis se pose ;
 * un quart du temps de DECAY, sans tenir compte de GATE), PUNCH = FREE (le moteur continue entre les trigs).
 * Sortie : bloqueur de continu (10 Hz), ecreteur doux, avant la chaine d'ampli de l'OS (tiny2.c).
 */
#ifndef TINY2_DSP_H
#define TINY2_DSP_H

typedef signed char s8;
typedef unsigned char u8;
typedef short s16;
typedef unsigned short u16;
typedef int s32;
typedef unsigned int u32;

#ifdef __mcoldfire__
/* a x b (Q31), par l'EMAC ; ACC0 est rendu vide, comme l'exige l'OS */
static inline s32 qmul(s32 a, s32 b)
{
	s32 r;
	__asm__ volatile("mac.l %1,%2,%%acc0\n\tmovclr.l %%acc0,%0" : "=d"(r) : "r"(a), "r"(b));
	return r;
}
/* a x b + c x d (Q31), une seule lecture de l'accumulateur */
static inline s32 qmac2(s32 a, s32 b, s32 c, s32 d)
{
	s32 r;
	__asm__ volatile("mac.l %1,%2,%%acc0\n\tmac.l %3,%4,%%acc0\n\tmovclr.l %%acc0,%0"
			 : "=d"(r) : "r"(a), "r"(b), "r"(c), "r"(d));
	return r;
}
/* a x b - c x d (Q31) */
static inline s32 qmsac2(s32 a, s32 b, s32 c, s32 d)
{
	s32 r;
	__asm__ volatile("mac.l %1,%2,%%acc0\n\tmsac.l %3,%4,%%acc0\n\tmovclr.l %%acc0,%0"
			 : "=d"(r) : "r"(a), "r"(b), "r"(c), "r"(d));
	return r;
}
/* a x b + c x d + e x f (Q31) */
static inline s32 qmac3(s32 a, s32 b, s32 c, s32 d, s32 e, s32 f)
{
	s32 r;
	__asm__ volatile("mac.l %1,%2,%%acc0\n\tmac.l %3,%4,%%acc0\n\tmac.l %5,%6,%%acc0\n\tmovclr.l %%acc0,%0"
			 : "=d"(r) : "r"(a), "r"(b), "r"(c), "r"(d), "r"(e), "r"(f));
	return r;
}
/* a x b + 2 c x d - e x f (Q31) : un biquad d'un seul trait */
static inline s32 qbiq(s32 a, s32 b, s32 c, s32 d, s32 e, s32 f)
{
	s32 r;
	__asm__ volatile("mac.l %1,%2,%%acc0\n\tmac.l %3,%4,%%acc0\n\tmac.l %3,%4,%%acc0\n\tmsac.l %5,%6,%%acc0\n\t"
			 "movclr.l %%acc0,%0"
			 : "=d"(r) : "r"(a), "r"(b), "r"(c), "r"(d), "r"(e), "r"(f));
	return r;
}
#else
/* l'EMAC, a l'identique (tools/emu/emac.py) : chaque produit tronque a 2^-39 (8 bits sous le LSB), somme,
 * puis decalage de 31 et saturation a 32 bits */
static inline s32 emac_sat(long long a)
{
	return a > 0x7fffffffLL ? 0x7fffffff : (a < -0x80000000LL ? (s32)0x80000000 : (s32)a);
}
static inline s32 qmul(s32 a, s32 b)
{
	return emac_sat((((long long)a * b) >> 23) >> 8);
}
static inline s32 qmac2(s32 a, s32 b, s32 c, s32 d)
{
	return emac_sat(((((long long)a * b) >> 23) + (((long long)c * d) >> 23)) >> 8);
}
static inline s32 qmsac2(s32 a, s32 b, s32 c, s32 d)
{
	return emac_sat(((((long long)a * b) >> 23) - (((long long)c * d) >> 23)) >> 8);
}
static inline s32 qmac3(s32 a, s32 b, s32 c, s32 d, s32 e, s32 f)
{
	return emac_sat(((((long long)a * b) >> 23) + (((long long)c * d) >> 23) + (((long long)e * f) >> 23)) >> 8);
}
static inline s32 qbiq(s32 a, s32 b, s32 c, s32 d, s32 e, s32 f)
{
	return emac_sat(((((long long)a * b) >> 23) + 2 * (((long long)c * d) >> 23) - (((long long)e * f) >> 23)) >> 8);
}
#endif

#include "tiny2_tables.h"

#define ONE   0x7fffffff              /* 1,0 en Q31 */
#define SIG1  0x40000000              /* 1,0 en Q30 : la pleine echelle d'un modele */
#define KMAX  0x7f00                  /* potard 127 en 8.8 */
#define TL_K  1522891                 /* (log2(6,9078 x 32 / 48000) + 31) x 65536 */
#define NMODEL 9

/* ------------------------------------------------------------------------------------------------ outils */
static inline s32 imin(s32 a, s32 b) { return a < b ? a : b; }
static inline s32 imax(s32 a, s32 b) { return a > b ? a : b; }
static inline s32 iclamp(s32 x, s32 lo, s32 hi) { return x < lo ? lo : (x > hi ? hi : x); }
static inline s32 kcl(s32 k) { return iclamp(k, 0, KMAX); }
static inline u32 mag(s32 x) { return x < 0 ? 0u - (u32)x : (u32)x; }
static inline s32 sadd(s32 a, s32 b)                  /* somme saturee */
{
#ifdef __mcoldfire__
	__asm__("add.l %1,%0\n\tsats.l %0" : "+d"(a) : "r"(b) : "cc");     /* sats : sature si la somme a debordé */
	return a;
#else
	s32 r = (s32)((u32)a + (u32)b);
	if (((a ^ r) & (b ^ r)) < 0)
		r = a < 0 ? (s32)0x80000000 : 0x7fffffff;
	return r;
#endif
}

/* congruentiel lineaire (Numerical Recipes), modulo 2^32 : quatre instructions sur le ColdFire (un xorshift en coute une
 * quinzaine : ses decalages de 13 et 17 passent par un registre). Les bits de poids fort sont bons, ceux de poids faible
 * non (le bit 0 alterne) : on ne compare que des mots entiers ou on ne garde que le haut ; rndm() les melange. */
static inline u32 rnd(u32 *s)
{
	u32 x = *s * 1664525u + 1013904223u;
	*s = x;
	return x;
}

static inline u32 rndm(u32 *s)                        /* un mot dont tous les bits sont bons (rare : motifs de bits) */
{
	u32 x = rnd(s);
	return x ^ (x >> 15) ^ (x >> 7);
}

/* hasard signe uniforme, Q31 : la meme valeur que le « 2 u - 1 » du prototype en virgule flottante (u : les 24 bits de poids fort) */
static inline s32 rnds(u32 *s)
{
	return (s32)(rnd(s) ^ 0x80000000u);
}

static inline void zero32(void *p, u32 n)             /* n mots a zero (pas de memset : -fno-builtin) */
{
	u32 *q = (u32 *)p;
	while (n--)
		*q++ = 0;
}

/* table de 129 points sur les potards 0..127, lue au potard 8.8 (interpolation lineaire) */
static inline s32 lutk(const s32 *t, s32 k)
{
	s32 i = k >> 8;
	return t[i] + qmul(t[i + 1] - t[i], (k & 0xff) << 23);
}

static inline u32 lutku(const u32 *t, s32 k)
{
	s32 i = k >> 8;
	return t[i] + (u32)qmul((s32)(t[i + 1] - t[i]), (k & 0xff) << 23);
}

/* sinus de la phase ph, Q31 */
static inline s32 sin31(u32 ph)
{
	u32 i = ph >> 22;
	s32 a = T_SIN[i], b = T_SIN[i + 1];
	return a + qmul(b - a, (s32)((ph & 0x3fffff) << 9));
}

static inline s32 cos31(u32 ph) { return sin31(ph + 0x40000000u); }

/* increment de phase de la hauteur key (demi-tons << 16), bornee a 0..127 demi-tons */
static inline u32 inc_of(s32 key)
{
	s32 i;
	key = iclamp(key, 0, 127 << 16);
	i = key >> 16;
	return T_INC[i] + (u32)qmul((s32)(T_INC[i + 1] - T_INC[i]), (key & 0xffff) << 15);
}

/* table de 257 points sur un increment de phase (indice inc >> 23 : pas de 93,75 Hz), jusqu'a 24 kHz */
static inline s32 lut_inc(const s32 *t, u32 inc)
{
	u32 i;
	if (inc >= 0x80000000u)
		return t[256];
	i = inc >> 23;
	return t[i] + qmul(t[i + 1] - t[i], (s32)((inc & 0x7fffff) << 8));
}

/* a x b / 65536 (a et b non signes, b en Q16 : jusqu'a 65535,99), arrondi vers le bas, sature a 0xffffffff. Pour les
 * rapports de frequence (pas dans les boucles d'echantillons : une division). b = bh 2^16 + bl, a = ah 2^16 + al :
 * a b / 2^16 = a bh + ah bl + (al bl) / 2^16, chaque terme tient dans 32 bits ou on le voit (le produit de deux
 * demi-mots, puis la somme). */
static inline u32 mulq16(u32 a, u32 b)
{
	u32 bh = b >> 16, bl = b & 0xffff, ah = a >> 16, al = a & 0xffff, r, t;
	if (bh && a > 0xffffffffu / bh)
		return 0xffffffffu;
	r = a * bh;
	t = ah * bl + ((al * bl) >> 16);
	r += t;
	return r < t ? 0xffffffffu : r;
}

/* 2^x, x en Q16 signe -> Q16 (sature a 0x7fffffff, 0 sous 2^-17) */
static inline u32 exp2_q16(s32 x)
{
	s32 n = x >> 16;
	u32 fr = (u32)x & 0xffff, i = fr >> 8, a = T_EXP2[i], b = T_EXP2[i + 1], m;
	m = a + (((b - a) * (fr & 0xff)) >> 8);           /* Q30, de 1 a 2 */
	if (n >= 15)
		return 0x7fffffffu;
	if (n <= -17)
		return 0;
	m >>= 14;                                        /* Q16 */
	return n >= 0 ? m << n : m >> -n;
}

/* 2^-y, y >= 0 en Q16 -> Q31 (0 au-dela de 2^-31) */
static inline s32 exp2neg_q31(u32 y)
{
	u32 n = y >> 16, u = 65536u - (y & 0xffff), i = u >> 8, a = T_EXP2[i], b = T_EXP2[i + 1], m;
	if (n >= 31)
		return 0;
	m = a + (((b - a) >> 8) * (u & 0xff));            /* 2^u en Q30, de 1 (exclu) a 2 */
	m >>= n;                                         /* x 2^-n : 2^-y en Q31 */
	return m > 0x7fffffffu ? 0x7fffffff : (s32)m;
}

/* log2(v) x 65536, v > 0 */
static inline s32 log2_q16(u32 v)
{
	s32 s = __builtin_clz(v);
	u32 m = v << s, f = m << 1, i = f >> 25, r = (f >> 9) & 0xffff;
	s32 a = T_LOG2[i], b = T_LOG2[i + 1];
	return ((31 - s) << 16) + a + (s32)(((u32)(b - a) * r) >> 16);
}

/* ecreteur doux, en Q28 (1,0 = 1 << 28) : transparent jusqu'a 0,8, rejoint 1,0 (sans pente) en 1,2 ; rend du Q30 */
static inline s32 sclip(s32 y)
{
	u32 a, v, w, f;
	if ((u32)(y + 0x0ccccccd) < 0x1999999au)           /* -0,8 <= y < 0,8 : le cas courant */
		return y << 2;
	a = y < 0 ? 0u - (u32)y : (u32)y;
	if (a >= 0x13333333u) {
		a = 0x10000000u;
	} else {
		v = ((a - 0x0ccccccdu) * 5u) >> 1;            /* 0..1 sur 0,8..1,2 */
		w = 0x10000000u - v;
		f = 0x10000000u - (u32)(qmul((s32)w, (s32)w) << 3);   /* 1 - (1 - v)^2 */
		a = 0x0ccccccdu + (u32)qmul((s32)f, 429496730);
	}
	return y < 0 ? -(s32)(a << 2) : (s32)(a << 2);
}

/* ----------------------------------------------------------------------------------- voix et reglages */
/* reglages d'un bloc, remplis par la passerelle (tiny2.c) ou la reference de l'ordinateur */
typedef struct {
	s32 note;          /* note du trig, demi-tons << 16 */
	s32 pitch;         /* PITCH, 8.8 (64 : la note) */
	s32 fine;          /* FINE, 8.8 (64 : juste) ; BENJ : la coupure de son filtre */
	s32 color, shape, sweep, contour;      /* potards 8.8 */
	s32 decay;         /* DECAY, 0..127 */
	s32 decmul;        /* LUT_DECAY[decay] : multiplicateur par bloc de l'enveloppe d'ampli, Q31 */
	s32 free;          /* PUNCH allume */
	s32 gate;          /* GATE allume et note tenue */
	s32 trig;          /* nouvelle note a ce bloc */
} tiny_in_t;

/* reglages calcules pour les moteurs */
typedef struct {
	u32 inc;           /* hauteur (avec la chute de CONTOUR) */
	s32 key;           /* idem, en demi-tons << 16 */
	s32 key1;          /* la meme a la fin du bloc */
	u32 inc0;          /* hauteur sans la chute */
	s32 key0;
	s32 color, sweep, contour;    /* potards 8.8 ; color avec la poussee de CONTOUR pour les moteurs ordinaires */
	s32 fine;
	s32 h;             /* enveloppe de coup, Q31 */
	s32 hr;            /* poussee de droite : (CONTOUR a droite) x h, Q31 */
	s32 ramt;          /* CONTOUR a droite, sans l'enveloppe, Q31 */
	s32 decay;
	s32 decmul;
	s32 gate, free, trig, restart;
	s32 first;          /* le moteur vient d'etre initialise (modele change) */
	s32 tl;             /* log2 du temps -60 dB de l'ampli en secondes, Q16 */
} ctl_t;

#include "tiny2_eng_a.h"
#include "tiny2_eng_b.h"
#include "tiny2_eng_c.h"
#include "tiny2_eng_d.h"
#include "tiny2_eng_e.h"


/* etat d'une voix */
typedef struct {
	s32 model, started;
	u32 rng;               /* hasard libre : jamais remis a zero par un trig */
	s32 hit;               /* enveloppe de coup, Q31 */
	s32 x1, y1;            /* bloqueur de continu (Q28) */
	union {
		struct st_byte byte;
		struct st_benj benj;
		struct st_havc havc;
		struct st_four four;
		struct st_chao chao;
		struct st_gndy gndy;
		struct st_plsr plsr;
		struct st_rati rati;
		struct st_scan scan;
	} e;
} tiny_t;

#define HIT_MIN 1463063578     /* 12 ms jusqu'a -60 dB, par bloc de 32 : 10^(-3 / 18) en Q31 */
#define RDC     2144672598     /* bloqueur de continu a 10 Hz : 1 - 2 pi 10 / 48000 */

/* Ordre des modeles a l'ecran (SHAPE) -> moteur : NYB, GND, RYT, FRS, RNG, PLS, SCN, KOS, CEL, BNJ. */
static const u8 MODEL_ENGINE[NMODEL] = { 0, 6, 2, 8, 4, 3, 7, 9, 5 };

/* 0 : COLOR poussee par CONTOUR (a droite), chute de hauteur (a gauche) ; 1 : le moteur gere la droite ; 2 : le
 * moteur gere tout CONTOUR (indexe par moteur, pas par modele) */
static const u8 CUSTOM[NMODEL] = { 1, 2, 2, 2, 1, 1, 0, 0, 1 };

#define ZST(f) zero32(&t->e.f, sizeof t->e.f / 4)

static void eng_init(tiny_t *t, s32 m)
{
	s32 e = MODEL_ENGINE[m];

	t->model = m;
	t->hit = ONE;
	switch (e) {
	case 0: ZST(byte); init_byte(&t->e.byte); break;
	case 2: ZST(benj); break;
	case 3: ZST(havc); break;
	case 4: ZST(four); init_four(&t->e.four); break;
	case 5: ZST(chao); init_chao(&t->e.chao); break;
	case 6: ZST(gndy); break;
	case 7: ZST(plsr); init_plsr(&t->e.plsr); break;
	case 8: ZST(rati); init_rati(&t->e.rati); break;
	case 9: ZST(scan); break;
	default: break;
	}
}

/* un bloc de 32 echantillons dans out (Q30, avant la chaine d'ampli de l'OS) */
static void tiny_block(tiny_t *t, const tiny_in_t *in, s32 *out)
{
	ctl_t c;
	s32 m, ct, key, drop = 0, drop1 = 0, i, g, x, y, x1, y1;
	s32 hm, h1;

	if (!in->trig && !t->started) {                   /* pas encore de note sur cette voix : silence */
		for (i = 0; i < 32; i++)
			out[i] = 0;
		return;
	}
	c.first = 0;
	if (in->trig) {
		m = iclamp(in->shape >> 8, 0, NMODEL - 1);
		if (!t->started || t->model != m) {
			eng_init(t, m);
			c.restart = 1;
			c.first = 1;
		} else {
			c.restart = !in->free;
		}
		t->started = 1;
		t->hit = ONE;
	} else {
		c.restart = 0;
	}
	m = t->model;
	c.trig = in->trig;
	c.gate = in->gate;
	c.free = in->free;
	c.decay = in->decay;
	c.decmul = in->decmul;
	c.fine = kcl(in->fine);
	/* temps -60 dB de l'ampli (secondes, en log2 Q16) d'apres le multiplicateur par bloc : 0,0046 s / (1 - decmul) */
	c.tl = TL_K - log2_q16((u32)(ONE - iclamp(in->decmul, 0x40000000, ONE - 1)));

	/* hauteur : note du trig + PITCH - 64 + FINE (BENJ en fait la coupure de son filtre) */
	key = in->note + ((in->pitch << 8) - (64 << 16));
	if (m != 4)
		key += (c.fine - 0x4000) << 3;
	key = iclamp(key, 0, 127 << 16);
	c.key0 = key;
	c.inc0 = inc_of(key);

	/* CONTOUR : a gauche la hauteur retombe de jusqu'a 48 demi-tons (courbe carree), a droite COLOR monte de jusqu'a
	 * 64 pas, sur une enveloppe de coup qui dure un quart de DECAY */
	ct = kcl(in->contour);
	c.contour = ct;
	c.h = t->hit;
	c.ramt = 0;
	/* l'enveloppe de coup retombe a un quart du temps de l'ampli : multiplicateur par bloc a la puissance 4 */
	hm = iclamp(c.decmul, 0x40000000, ONE);
	hm = qmul(hm, hm);
	hm = qmul(hm, hm);
	hm = hm < HIT_MIN ? HIT_MIN : hm;
	h1 = qmul(c.h, hm);                               /* a la fin du bloc */
	if (ct < 0x4000) {
		s32 l = (0x4000 - ct) << 17;                  /* gauche, Q31 */
		l = qmul(l, l);
		drop = qmul(qmul(l, c.h), 48 << 16);
		drop1 = qmul(qmul(l, h1), 48 << 16);
	} else if (ct > 0x4000) {
		c.ramt = (ct - 0x4000) * 133152;              /* 2^31 / (63 x 256) */
		c.ramt = c.ramt < 0 ? ONE : c.ramt;
	}
	c.hr = qmul(c.ramt, c.h);
	c.color = kcl(in->color);
	if (CUSTOM[MODEL_ENGINE[m]] == 0)
		c.color = kcl(c.color + qmul(c.hr, 64 << 8));
	c.sweep = kcl(in->sweep);
	if (CUSTOM[MODEL_ENGINE[m]] == 2)
		drop = drop1 = 0;
	c.key = iclamp(key + drop, 0, 127 << 16);
	c.key1 = iclamp(key + drop1, 0, 127 << 16);     /* la hauteur a la fin du bloc (pour les lignes a retard) */
	c.inc = drop ? inc_of(c.key) : c.inc0;

	switch (MODEL_ENGINE[m]) {
	case 0: eng_byte(&t->e.byte, &c, out); break;
	case 2: eng_benj(&t->e.benj, &c, out); break;
	case 3: eng_havc(&t->e.havc, &t->rng, &c, out); break;
	case 4: eng_four(&t->e.four, &c, out); break;
	case 5: eng_chao(&t->e.chao, &t->rng, &c, out); break;
	case 6: eng_gndy(&t->e.gndy, &t->rng, &c, out); break;
	case 7: eng_plsr(&t->e.plsr, &t->rng, &c, out); break;
	case 8: eng_rati(&t->e.rati, &c, out); break;
	case 9: eng_scan(&t->e.scan, &t->rng, &c, out); break;
	default:
		for (i = 0; i < 32; i++)
			out[i] = 0;
		break;
	}

	/* niveau du modele (T_GAIN, Q29 : 1,0 = 1 << 29, moins de 2), bloqueur de continu (en Q28), ecreteur doux */
	g = T_GAIN[m];
	x1 = t->x1;
	y1 = t->y1;
	for (i = 0; i < 32; i++) {
		x = qmul(out[i], g);                           /* |x| < 2^30 : x - x1 ne deborde pas */
		y = sadd(x - x1, qmul(y1, RDC));
		x1 = x;
		y1 = y;
		out[i] = sclip(y);
	}
	t->x1 = x1;
	t->y1 = y1;

	t->hit = h1;
}

#endif
