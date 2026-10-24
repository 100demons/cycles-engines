/* Crochet de demarrage (notes/17) : au debut de 0x400004b2 (remise a zero du BSS de l'OS Cycles),
 * recopie la charge utile ajoutee a la fin de l'image (section 3 agrandie) vers 0x43000000, au-dessus
 * du BSS, puis refait les deux instructions deplacees et reprend a 0x400004ba.
 * PAYLOAD_SRC, PAYLOAD_DST et PAYLOAD_LONGS sont fournis par tools/gen_sdvintage_exact.py (-D) ; SRAM_RUN_AT et
 * SRAM_LONGS par tools/gen_syntakt_engines.py. PACK sans CHAIN_TO : machine MACRO seule (tools/gen_macro.py).
 * APLIB, AFTER et BOOT2 : machine MACRO avec les moteurs du Syntakt (tools/gen_macro_syntakt.py, notes/50) : le crochet
 * decompresse (APLIB) puis appelle (AFTER) un 2e etage range dans la charge utile (BOOT2 : CHORD_AT et SRAM), car
 * l'arpegiateur prend la suite du masque de sprite (gen_arp.py, .cave_menu : 192 o pour le crochet). */
	.text
	.globl	boot_stub
boot_stub:
#if defined(BOOT2)
	/* 2e etage (notes/50), dans la charge utile decompressee : appele par le crochet (AFTER), finit par rts.
	 * Seuls d0, a0 et a1 servent. */
#elif defined(APLIB)
	/* MACRO avec les moteurs du Syntakt (notes/50) : la charge utile entiere (zeros compris) est rangee compressee,
	 * au format aPLib d'Elektron (tools/mtlib/aplib.py, sans l'en-tete de 8 o), et decompressee ici de PAYLOAD_SRC
	 * vers PAYLOAD_DST. Ce decompresseur est ecrit d'apres depack() de tools/mtlib/aplib.py (drumkilla, licence MIT,
	 * tools/mtlib/LICENSE), lui-meme d'apres aplib.c d'elektron-firmware-tool (Marcel Bierling, licence MIT,
	 * tools/mtlib/LICENSE-elektron-firmware-tool). Bits en attente dans d6 : (d6 & 0xff) == 0, lire un octet. Les
	 * registres que l'appelant garde (d2..d7, a2) sont remis a la fin. */
	lea	-24(%sp), %sp
	movem.l	%d2-%d6/%a2, (%sp)
	lea	PAYLOAD_SRC, %a0
	lea	PAYLOAD_DST, %a1
	moveq	#1, %d4			/* dernier decalage */
	moveq	#0, %d6
10:	bsr.s	50f
	beq.s	11f
	move.b	(%a0)+, (%a1)+		/* litteral */
	bra.s	10b
11:	bsr.s	60f			/* correspondance : gamma -> d3 */
	moveq	#2, %d0
	cmp.l	%d0, %d3
	beq.s	12f			/* meme decalage que la precedente */
	lsl.l	#8, %d3
	moveq	#0, %d0
	move.b	(%a0)+, %d0
	add.l	%d0, %d3
	subi.l	#767, %d3		/* OFFSET_BIAS ; 0 : fin du flux */
	beq.s	19f
	move.l	%d3, %d4
12:	bsr.s	50f			/* longueur : 2 bits, ou gamma + 2 s'ils sont nuls */
	move.l	%d0, %d2
	add.l	%d2, %d2
	bsr.s	50f
	add.l	%d0, %d2
	bne.s	13f
	bsr.s	60f
	move.l	%d3, %d2
	addq.l	#2, %d2
13:	cmpi.l	#3328, %d4		/* FAR_THRESHOLD : une de plus au-dela */
	bls.s	14f
	addq.l	#1, %d2
14:	addq.l	#1, %d2
	movea.l	%a1, %a2
	suba.l	%d4, %a2
15:	move.b	(%a2)+, (%a1)+		/* octet par octet : la source peut chevaucher la destination */
	subq.l	#1, %d2
	bne.s	15b
	bra.s	10b
50:	add.l	%d6, %d6		/* un bit -> d0 (0 ou 1), Z s'il est nul */
	move.l	%d6, %d0
	andi.l	#0xff, %d0
	bne.s	51f
	moveq	#0, %d6
	move.b	(%a0)+, %d6
	add.l	%d6, %d6
	addq.l	#1, %d6
51:	move.l	%d6, %d0
	lsr.l	#8, %d0
	andi.l	#1, %d0
	rts
60:	moveq	#1, %d3			/* gamma d'Elias entrelace -> d3 */
61:	bsr.s	50b
	add.l	%d3, %d3
	add.l	%d0, %d3
	bsr.s	50b
	beq.s	61b
	rts
19:	movem.l	(%sp), %d2-%d6/%a2
	lea	24(%sp), %sp
#ifdef AFTER
	/* Le 2e etage est du code qu'on vient d'ecrire comme des donnees : le cache d'instructions, que le bootstrap
	 * laisse actif (CACR 0x0008c000), peut deja tenir ces adresses (lues d'avance pendant la decompression, avec ce
	 * qu'il y avait avant en SDRAM) et ne voit pas les ecritures. Sans cela, la machine reste sur le logo Elektron
	 * (essai du 10/10/2026, notes/50). On l'invalide donc, avec le cache de branchements, avant d'y sauter : la
	 * valeur du bootstrap (0x80000c40), ICINVA + BCINVA, le cache de donnees reste coupe. */
	nop				/* ecritures terminees */
	move.l	#0x000cc100, %d0
	movec	%d0, %cacr
	nop
	jsr	AFTER			/* 2e etage (BOOT2), dans la charge utile qu'on vient de decompresser */
#endif
#elif defined(PACK)
	/* avec Model-TG (notes/31), et pour la machine MACRO (notes/43) : la charge utile est rangee en morceaux
	 * (segs.inc : adresse, mots) ; tout a zero, puis chaque morceau a sa place. Seuls d0, d1, a0 et a1 servent.
	 * Avec Model-TG : appele par le jsr de 0x40000530, a la place de boot_extra_hook, ou l'on finit (CHAIN_TO). */
	lea	PAYLOAD_DST, %a1
	move.l	#PAYLOAD_LONGS, %d0
0:	clr.l	(%a1)+
	subq.l	#1, %d0
	bne.s	0b
	lea	PAYLOAD_SRC, %a0
	lea	segs, %a1
	move.l	%a1, %d1
4:	movea.l	%d1, %a1
	move.l	4(%a1), %d0
	beq.s	6f
	movea.l	(%a1), %a1
	addq.l	#8, %d1
5:	move.l	(%a0)+, (%a1)+
	subq.l	#1, %d0
	bne.s	5b
	bra.s	4b
6:
#else
	lea	PAYLOAD_SRC, %a0
	lea	PAYLOAD_DST, %a1
	move.l	#PAYLOAD_LONGS, %d0
1:	move.l	(%a0)+, (%a1)+
	subq.l	#1, %d0
	bne.s	1b
#endif
#ifdef CHORD_AT
	/* 2e etage de MACRO avec les moteurs du Syntakt (notes/50) : les tables d'ondes de CHORD ne sont pas dans la
	 * charge utile ; l'OS vient de les mettre en SRAM (0x4000045c), on les recopie a leur place dans la charge utile
	 * avant que les deux zones recoivent le code du Syntakt */
	lea	SRAM_RUN_AT, %a0
	lea	CHORD_AT, %a1
	move.l	#SRAM_LONGS, %d0
7:	move.l	(%a0)+, (%a1)+
	subq.l	#1, %d0
	bne.s	7b
	lea	SRAM2_RUN_AT, %a0
	move.l	#SRAM2_LONGS, %d0
8:	move.l	(%a0)+, (%a1)+
	subq.l	#1, %d0
	bne.s	8b
#endif
#ifdef SRAM_RUN_AT
	/* gen_syntakt_engines.py (notes/28) : le debut de la charge utile (code et tables du Syntakt) passe en SRAM
	 * interne, a la place des tables d'ondes de CHORD, apres l'initialisation de la SRAM par l'OS (0x4000045c) */
	lea	PAYLOAD_DST, %a0
	lea	SRAM_RUN_AT, %a1
	move.l	#SRAM_LONGS, %d0
2:	move.l	(%a0)+, (%a1)+
	subq.l	#1, %d0
	bne.s	2b
#endif
#ifdef SRAM2_RUN_AT
	/* puis, a la suite dans la charge utile, la 2e zone (etats des voix du Syntakt, notes/29) */
	lea	SRAM2_RUN_AT, %a1
	move.l	#SRAM2_LONGS, %d0
3:	move.l	(%a0)+, (%a1)+
	subq.l	#1, %d0
	bne.s	3b
#endif
#if defined(BOOT2)
	rts
#elif defined(CHAIN_TO)
	jmp	CHAIN_TO		/* boot_extra_hook de Model-TG (ACR1, remise a zero du BSS sauf son bloc) */
#else
	lea	-16(%sp), %sp		/* instructions deplacees de 0x400004b2 */
	movem.l	%d4-%d7, (%sp)
	jmp	0x400004ba
#endif
#if defined(PACK) && !defined(APLIB)
	.align	2
segs:
#include "segs.inc"
	.long	0, 0
#endif
