/*
 * TINY2 : machine ajoutee du Model:Cycles (OS 1.13), 9 moteurs de synthese bruites, imprevisibles et legers
 * (notes/58). Le son est dans tiny2_dsp.h (+ tiny2_eng_*.h, tiny2_tables.h) ; ce fichier le branche sur la boucle
 * des voix, comme acid.c et macro.cc.
 *
 * Contrat de la boucle des voix (notes/14) : update(pmod, v, p) puis render(out, v), une fois par bloc de
 * 32 trames a 48 kHz. pmod = note du trig (demi-tons << 16) ; v = voix du Cycles (0x31c o) ; p = parametres
 * de la piste (mots 8.8 : +0x12 MACHINE, +0x14 PITCH, +0x16 COLOR, +0x18 SHAPE, +0x1a SWEEP, +0x1c CONTOUR,
 * +0x1e PUNCH, +0x20 GATE, +0x22 FINE, +0x24 DECAY) ; v+0x34 / v+0x38 : trig de ce bloc / du bloc precedent,
 * v+0x3c : fin de la note (LEN ecoulee, touche relachee) ; out : 32 x int32 (Q30 : 1,0 = 1 << 30, comme MACRO).
 *
 * Potards : PITCH = la note (BENJ : l'OSC 1 seul) ; SHAPE = le modele (0..8) ; COLOR et SWEEP = les deux macros
 * du modele ; CONTOUR = l'enveloppe de coup (a gauche la hauteur retombe, a droite COLOR monte), ou un troisieme
 * reglage (BENJ : RUN 2 ; HAVC : pincee -> glissando) ; PUNCH = FREE (le moteur continue
 * entre les trigs ; BENJ : la boucle du rungler se verrouille) ; FINE (FUNC + PITCH) = l'accord fin, +-2
 * demi-tons (BENJ : la coupure de son filtre) ; DECAY et GATE : la chaine d'ampli d'origine, comme TONE
 * (enveloppe 0x400a9252, VCA 0x400a9430). L'etage PUNCH de l'OS (0x400a967a) reste neutre ici : le potard PUNCH
 * n'a pas le meme sens, et l'appeler coutait 466 instructions par bloc. Il n'est donc pas appele.
 *
 * Etat : un tiny_t par piste (BSS de la charge utile, mis a zero au demarrage), remis a zero a la premiere note
 * apres un changement de machine (marque en v+0x2c, effacee par la remise a zero de la voix, 0x400a7ab8). Le
 * hasard (congruence lineaire 1664525 / 1013904223) n'est jamais remis a zero par un trig. Une voix muette (enveloppe d'ampli fermee, pas de
 * trig) n'est pas calculee : un moteur qui « tourne librement » (BENJ, le riff de HAVC avec FREE) ne tourne que
 * tant qu'il s'entend.
 */
#include "tiny2_dsp.h"

#ifdef __mcoldfire__
#include "feral_icons.h"

/* Les trois images de la page MACHINES (classe Bitmap de l'OS, constructeur 0x40070172) : carte 48 x 33,
 * embleme 34 x 34, petit embleme 25 x 22. Les plans image et masque sont dans feral_icons.h. */
struct feral_bitmap {
	u32 vt, w, h, wpc, img, mask, flag;
};
#define FERAL_BITMAP_VT 0x401117c8
#define FERAL_BITMAP(name, w_, h_, img_, msk_) \
	const struct feral_bitmap name = { FERAL_BITMAP_VT, w_, h_, ((h_) + 31) >> 5, (u32)(img_), (u32)(msk_), 0 }

FERAL_BITMAP(feral_bm_a, 48, 33, FERAL_IMG_A, FERAL_MSK_A);
FERAL_BITMAP(feral_bm_b, 34, 34, FERAL_IMG_B, FERAL_MSK_B);
FERAL_BITMAP(feral_bm_c, 25, 22, FERAL_IMG_C, FERAL_MSK_C);
#endif

#define CYC_VOICE0  0x42308828
#define CYC_VSTRIDE 0x31c
#define MARK_OFF    0x2c
#define MARK        0x46455241                       /* 'FERA' en gros-boutiste */

#define AMP_ENV(v)  ((void (*)(char *))0x400a9252)(v)
#define VCA(o, v)   ((void (*)(s32 *, char *))0x400a9430)(o, v)
#define LUT_DECAY   ((const s32 *)0x4011d84c)        /* DECAY -> multiplicateur par bloc (celui de TONE) */

#define V32(o)      (*(s32 *)(v + (o)))
#define P16(o)      (*(const s16 *)(p + (o)))

#define IDLE_LEVEL  (1 << 14)                        /* enveloppe d'ampli sous -102 dB : voix muette */

/* Table des descripteurs recopiee au build a 0x43034000 (gs.DESCN) : 76 COLOR, 77 SHAPE, 78 SWEEP,
 * 79 CONTOUR, 80 Amp Decay. Le nom long est en +0x2c, le nom court en +0x34. Feral reecrit les trois noms longs
 * quand le modele d'une piste change : la fenetre de valeur montre alors le vrai reglage du modele. */
#ifdef CM_SYMBOLS
/* package: the composer gives this machine its first descriptor index (76, 81, ...). */
extern char cm_descriptor_base[];
#define FERAL_DESC(k)  (0x43034000 + ((int)(long)cm_descriptor_base + (k)) * 0x38 + 0x2c)
#define FERAL_DESC_COLOR   FERAL_DESC(0)
#define FERAL_DESC_SWEEP   FERAL_DESC(2)
#define FERAL_DESC_CONTOUR FERAL_DESC(3)
#elif !defined(FERAL_DESC_COLOR)
#define FERAL_DESC_COLOR  (0x43034000 + 76 * 0x38 + 0x2c)
#define FERAL_DESC_SWEEP  (0x43034000 + 78 * 0x38 + 0x2c)
#define FERAL_DESC_CONTOUR (0x43034000 + 79 * 0x38 + 0x2c)
#endif

/* COLOR, SWEEP, CONTOUR par modele (7 caracteres au plus). */
static const char feral_param[NMODEL][3][8] = {
	{ "Formula", "Const",  "Env" },                  /* NYB  : banque de formules, constantes, enveloppe de coup */
	{ "Points",  "Timing", "Env" },                  /* GND  : points, durees, enveloppe */
	{ "Osc2",    "Run1",   "Run2" },                 /* BNJ  : OSC 2, RUN 1, RUN 2 */
	{ "Partial", "Rate",   "Length" },               /* RYT  : partiels, echelle de temps, longueur des pings */
	{ "Spread",  "Symm",   "Env" },                  /* FRS  : vitesse, symetrie, enveloppe de coup */
	{ "Ratio",   "Memory", "Pluck" },                /* RNG  : rapport, memoire, pincee -> glissando */
	{ "Carrier", "Divide", "Env" },                  /* PLS  : porteuse, divisions / trous, enveloppe */
	{ "Stiff",   "Strike", "Env" },                  /* SCN  : raideur, forme frappee, enveloppe */
	{ "Chaos",   "Smooth", "Env" },                  /* KOS  : r, interpolation / ecrasement, enveloppe */
};

static s32 feral_label_model;                         /* modele + 1 ; 0 : pas encore choisi */

static void feral_set_labels(s32 m)
{
#ifdef __mcoldfire__
	if (m < 0)
		m = 0;
	if (m >= NMODEL)
		m = NMODEL - 1;
	if (m + 1 == feral_label_model)
		return;
	*(const char **)FERAL_DESC_COLOR = feral_param[m][0];
	*(const char **)FERAL_DESC_SWEEP = feral_param[m][1];
	*(const char **)FERAL_DESC_CONTOUR = feral_param[m][2];
	feral_label_model = m + 1;
#else
	(void)m;
#endif
}

struct track {
	tiny_t s;
	tiny_in_t in;
	s32 held;                                        /* une note joue et n'a pas ete relachee */
	s32 shape;                                       /* dernier modele vu, pour les noms des potards */
};

static struct track trk[6];

void tiny2_update(s32 pmod, char *v, const char *p)
{
	s32 t = ((u32)v - CYC_VOICE0) / CYC_VSTRIDE;
	struct track *tr = &trk[t];
	s32 dec;

	if (V32(MARK_OFF) != MARK) {                     /* 1re note sur cette machine : voix neuve */
		/* seulement l'en-tete : chaque moteur remet lui-meme son etat a zero quand il demarre (eng_init) */
		zero32(&tr->s, (u32)__builtin_offsetof(tiny_t, e) / 4);
		zero32(&tr->in, (u32)sizeof tr->in / 4);
		tr->held = 0;
		tr->shape = -1;
		tr->s.rng = 0x9e3779b9u ^ ((u32)(t + 1) * 2654435761u);
		V32(MARK_OFF) = MARK;
	}
	/* chaine d'ampli d'origine, reglee comme TONE (0x400aa7b8), sans PUNCH (ici c'est FREE) */
	dec = (s8)p[0x24];
	dec = dec < 0 ? 0 : (dec > 127 ? 127 : dec);
	V32(0x250) = LUT_DECAY[dec];
	if (V32(0x38)) {
		V32(0x274) = V32(0x278) = 0x012c0f26;
		V32(0x280) = V32(0x284) = 0x001f7c5c;
		V32(0x290) = V32(0x294) = 0x7fffffff;
		V32(0x27c) = 0x82581e4b;
		V32(0x288) = 0x803ef8b8;
		V32(0x29c) = 0x7fffffff; V32(0x2a0) = 0x20000000; V32(0x2a4) = 0x20000000;
		V32(0x48) = 0;
	}
	if (V32(0x34))
		tr->held = 1;
	if (V32(0x3c))                                   /* note relachee (avec un trig dans le meme bloc : apres) */
		tr->held = 0;

	/* au bloc d'un trig, l'ancienne note finit (fondu de l'ampli) : la nouvelle ne passe qu'au bloc suivant */
	if (!V32(0x34))
		tr->in.note = pmod < 0 ? 0 : (pmod > (127 << 16) ? 127 << 16 : pmod);
	tr->in.pitch = P16(0x14);
	tr->in.fine = P16(0x22);
	tr->in.color = P16(0x16);
	tr->in.shape = P16(0x18);
	if (tr->in.shape != tr->shape) {                  /* le modele de cette piste vient de changer */
		tr->shape = tr->in.shape;
		feral_set_labels(tr->in.shape >> 8);
	}
	tr->in.sweep = P16(0x1a);
	tr->in.contour = P16(0x1c);
	tr->in.decay = dec;
	tr->in.decmul = LUT_DECAY[dec];
	tr->in.free = P16(0x1e) != 0;
	tr->in.gate = P16(0x20) != 0 && tr->held;
	tr->in.trig = V32(0x38) != 0;
}

void tiny2_render(s32 *out, char *v)
{
	struct track *tr = &trk[((u32)v - CYC_VOICE0) / CYC_VSTRIDE];
	int i;

	if (!V32(0x34) && !V32(0x38) && mag(V32(0x230)) < IDLE_LEVEL && mag(V32(0x234)) < IDLE_LEVEL) {
		for (i = 0; i < 32; i++)                     /* voix muette : pas calculee */
			out[i] = 0;
		return;
	}
	tiny_block(&tr->s, &tr->in, out);
	AMP_ENV(v);
	VCA(out, v);
}
