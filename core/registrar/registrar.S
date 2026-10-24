/* The registrar runtime (notes/60 §5, B2c): the detours of the added machines, assembled ONCE at STUBS
 * (gs.LAYOUT, 0x43033000) by tools/gen_registrar.py and committed in registrar.json. Everything that used to be a
 * per-build constant in gs.detours_asm (how many machines, their pictures, their descriptors, their records) is
 * read from the parameter block at REGS, which tools/compose.py fills for each build:
 *
 *   REGS+0   .byte n, first, top, nm        n machines added; first = 6; top = first + n - 1; nm = first + n
 *   REGS+4   .long last                     76 + 5 n: the first descriptor after the added machines'
 *   REGS+8   .long knob_tab                 DATA + 12 nm: knob_vec's table machine -> record
 *   REGS+12  .long machines_end             REGS + 16 + 16 n
 *   REGS+16  machine[n], 16 bytes each:
 *            .byte index, image, first_desc, rec_param     index = first + i; rec_param = first_desc - 51
 *            .long rec                                      RECS + 0x60 i, the machine's record (built on first use)
 *            .long knob_hook                                0, or a package's hook called by knob_vec (MACRO's lo-fi)
 *            .long 0
 *
 * Each entry keeps the register contract of the detour it replaces (notes/60 §5.2): the constants come from the
 * block with cmp.l / mvz.b from memory, the loops use a0 where the detour owned it (the whole-function entries and
 * shown) and a1 in builder_row, where the OS still reads a0 afterwards; knob_vec saves a0 around its hook.
 * Addresses are VAs of OS 1.13; REGS, DESCN and MARKS_ROW come from the assembler's command line. */

#define REC_BASE  0x40a71540
#define REC_SNARE (REC_BASE + 2 * 76)

	.text

/* --- descriptor -> interface object (state_at 0x4004df40, state_connect 0x4004dfa2; whole functions) ---------
 * map_state: d0 = descriptor -> d0 = the object's index: added descriptors (76 .. last - 1) share SNARE's five
 * (51 .. 55), as the generated detour did. Uses d0, d1. */
map_state:
	moveq	#76, %d1
	cmp.l	%d1, %d0
	bcs.w	9f
	cmp.l	REGS+4, %d0		/* last */
	bcs.s	1f
	moveq	#0, %d0
	rts
1:	moveq	#76, %d1
	sub.l	%d1, %d0
2:	moveq	#5, %d1
	cmp.l	%d1, %d0
	bcs.s	3f
	sub.l	%d1, %d0
	bra.s	2b
3:	moveq	#51, %d1
	add.l	%d1, %d0
9:	rts

/* A package may put a descriptor-state initializer in the last long of its machine entry
 * (REGS+16+16i+12): called with no argument, it returns the state object for its SHAPE descriptor
 * (first_desc + 1). Zero means the generic SNARE object. */
	.globl	state_at
state_at:
	move.l	%d2, -(%sp)
	move.l	%d3, -(%sp)
	move.l	%a0, -(%sp)
	move.l	16(%sp), %d0
	lea	REGS+16, %a0
sat_loop:
	cmpa.l	REGS+12, %a0
	beq.s	sat_generic
	mvz.b	2(%a0), %d3
	addq.l	#1, %d3
	cmp.l	%d3, %d0
	beq.s	sat_found
	lea	16(%a0), %a0
	bra.s	sat_loop
sat_found:
	move.l	12(%a0), %a1
	tst.l	%a1
	beq.s	sat_generic
	move.l	(%sp)+, %a0
	move.l	(%sp)+, %d3
	move.l	(%sp)+, %d2
	jsr	(%a1)
	rts
sat_generic:
	move.l	(%sp)+, %a0
	move.l	(%sp)+, %d3
	move.l	(%sp)+, %d2
	move.l	4(%sp), %d0
	bsr.w	map_state
	moveq	#100, %d1
	muls.l	%d1, %d0
	add.l	#0x40a71754, %d0
	rts

	.globl	state_connect
state_connect:
	move.l	%d2, -(%sp)
	move.l	%d3, -(%sp)
	move.l	%a0, -(%sp)
	move.l	16(%sp), %d0
	lea	REGS+16, %a0
sc_loop:
	cmpa.l	REGS+12, %a0
	beq.s	sc_generic
	mvz.b	2(%a0), %d3
	addq.l	#1, %d3
	cmp.l	%d3, %d0
	beq.s	sc_found
	lea	16(%a0), %a0
	bra.s	sc_loop
sc_found:
	move.l	12(%a0), %a1
	tst.l	%a1
	beq.s	sc_generic
	move.l	(%sp)+, %a0
	move.l	(%sp)+, %d3
	move.l	(%sp)+, %d2
	jsr	(%a1)
	add.l	#0x14, %d0
	bra.s	sc_common
sc_generic:
	move.l	(%sp)+, %a0
	move.l	(%sp)+, %d3
	move.l	(%sp)+, %d2
	move.l	4(%sp), %d0
	bsr.w	map_state
	moveq	#100, %d1
	muls.l	%d1, %d0
	add.l	#0x40a71768, %d0
sc_common:
	pea	0x40a71500
	move.l	12(%sp), -(%sp)
	move.l	%d0, -(%sp)
	jsr	0x400ddf60
	lea	12(%sp), %sp
	move.l	#0x40a71500, %d0
	rts

/* --- CC received -> descriptor (0x4005a8f0, mid-function): d0 = machine, d1 = parameter; keeps d0 a0, uses d2 --- */
	.globl	cc_bounds
cc_bounds:
	moveq	#5, %d2
	cmp.l	%d1, %d2
	bcs.s	1f
	mvz.b	REGS+2, %d2		/* top */
	cmp.l	%d0, %d2
	bcs.s	2f
	jmp	0x4005a8fa
1:	jmp	0x4005a928
2:	jmp	0x4005a91c

/* --- the machine table ---------------------------------------------------------------------------------------
 * find_machine: d1 = machine index -> a0 = its entry, or 0. Uses a0 only (cmp.b against the entry's byte). */
find_machine:
	lea	REGS+16, %a0
1:	cmpa.l	REGS+12, %a0		/* machines_end */
	bcc.s	9f
	cmp.b	(%a0), %d1
	beq.s	8f
	lea	16(%a0), %a0
	bra.s	1b
9:	suba.l	%a0, %a0
8:	rts

/* shown: d0 = machine -> d0 = its picture (unchanged for a stock machine). Uses d0, d1, a0. */
shown:
	move.l	%d0, %d1
	bsr.w	find_machine
	move.l	%a0, %d1
	beq.s	9f
	mvz.b	1(%a0), %d0
9:	rts

/* --- MACHINES page: the big picture (0x400a2638 -> 0x400a2680, mid-function; d3 = machine, d2 = context) ------ */
	.globl	drum_icons
drum_icons:
	move.l	%d3, %d0
	bsr.w	shown
	move.l	%d0, %d4
	lsl.l	#2, %d0
	lsl.l	#5, %d4
	sub.l	%d0, %d4
	pea	0x1
	pea	0x17
	pea	0x20
	lea	0x40071da4, %a2
	movea.l	0x40fe32cc, %a1
	adda.l	%d4, %a1
	move.l	%a1, -(%sp)
	move.l	%d2, -(%sp)
	jsr	(%a2)
	lea	48(%sp), %sp
	pea	0x1
	pea	0x22
	add.l	0x40fe384c, %d4
	pea	0x60
	move.l	%d4, -(%sp)
	move.l	%d2, -(%sp)
	jsr	(%a2)
	lea	20(%sp), %sp
	jmp	0x400a2680

/* --- the small icon of a track (0x400a4dc4 -> 0x400a4dde, mid-function; d2 = machine, a0 = view) -------------- */
	.globl	small_icon
small_icon:
	lea	10(%a0), %a0
	move.l	%a0, -(%sp)
	move.l	16(%fp), -(%sp)
	move.l	%d2, %d0
	bpl.s	1f
	moveq	#0, %d0
1:	moveq	#5, %d1
	cmp.l	%d0, %d1
	bge.s	3f
	bsr.w	shown
	moveq	#5, %d1
	cmp.l	%d0, %d1
	bge.s	3f
	moveq	#5, %d0
3:	jmp	0x400a4dde

/* --- records per machine (0x4004df5c record_at(index + 1), 0x4004df76 record_of(index); whole functions) -------
 * An added machine gets its own record, built on first use from SNARE's (rec_build); anything out of range gets
 * KICK's, as the OS would otherwise read 76 bytes before the table. */
	.globl	record_at
record_at:
	move.l	4(%sp), %d0
	move.l	%d0, %d1
	subq.l	#1, %d1
	bsr.w	find_machine
	move.l	%a0, %d1
	bne.s	7f
	moveq	#6, %d1
	cmp.l	%d0, %d1
	bcc.s	1f
	moveq	#1, %d0
1:	moveq	#76, %d1
	muls.l	%d1, %d0
	add.l	#REC_BASE, %d0
	rts
7:	mvz.b	3(%a0), %d0		/* rec_param */
	movea.l	4(%a0), %a0		/* rec */
	bra.w	rec_build

	.globl	record_of
record_of:
	move.l	4(%sp), %d0
	move.l	%d0, %d1
	bsr.w	find_machine
	move.l	%a0, %d1
	bne.s	7f
	moveq	#5, %d1
	cmp.l	%d0, %d1
	bcs.s	2f
	lea	0x401091b4, %a0
	mvs.b	(%a0,%d0.l), %d0
	moveq	#6, %d1
	cmp.l	%d0, %d1
	bcc.s	3f
2:	moveq	#1, %d0
3:	moveq	#76, %d1
	muls.l	%d1, %d0
	add.l	#REC_BASE, %d0
	rts
7:	mvz.b	3(%a0), %d0
	movea.l	4(%a0), %a0
	/* falls into rec_build */

/* rec_build: a0 = the record, d0 = the descriptor offset (first_desc - 51). Builds it once (flag at +76), from
 * SNARE's: two strings copied with their reference count (0x400f8f02), then the 17 descriptors with SNARE's
 * 51..55 shifted to the machine's. Returns d0 = a0. Saves d2, a2. */
rec_build:
	tst.b	76(%a0)
	bne.w	9f
	move.l	%d2, -(%sp)
	move.l	%a2, -(%sp)
	movea.l	%a0, %a2
	move.l	%d0, %d2
	pea	REC_SNARE
	move.l	%a2, -(%sp)
	jsr	0x400f8f02
	addq.l	#8, %sp
	pea	REC_SNARE + 4
	pea	4(%a2)
	jsr	0x400f8f02
	addq.l	#8, %sp
	lea	REC_SNARE + 8, %a0
	lea	8(%a2), %a1
	moveq	#16, %d1
	move.l	%d1, -(%sp)
1:	move.l	(%a0)+, %d0
	moveq	#51, %d1
	cmp.l	%d1, %d0
	blt.s	2f
	moveq	#55, %d1
	cmp.l	%d0, %d1
	blt.s	2f
	add.l	%d2, %d0
2:	move.l	%d0, (%a1)+
	subq.l	#1, (%sp)
	bpl.s	1b
	addq.l	#4, %sp
	moveq	#1, %d0
	move.b	%d0, 76(%a2)
	movea.l	%a2, %a0
	move.l	(%sp)+, %a2
	move.l	(%sp)+, %d2
9:	move.l	%a0, %d0
	rts

/* --- MACHINES page: the position marks on two rows (0x400a26a2 -> 0x400a26f2, when nm > MARKS_ROW) ------------ */
	.globl	marks
marks:
	lea	0x40070c4e, %a2		/* hollow */
	lea	0x40070efc, %a3		/* filled */
	clr.l	%d5
1:	move.l	%d5, %d4
	moveq	#11, %d1
	moveq	#MARKS_ROW, %d0
	cmp.l	%d0, %d4
	blt.s	2f
	sub.l	%d0, %d4
	moveq	#4, %d1
2:	move.l	%d4, %d0
	lsl.l	#3, %d0
	sub.l	%d4, %d0		/* 7 x column */
	add.l	#80, %d0
	move.l	%d1, %d4
	addq.l	#4, %d4
	pea	0x1
	move.l	%d4, -(%sp)
	move.l	%d0, -(%sp)
	move.l	%d1, -(%sp)
	subq.l	#4, %d0
	move.l	%d0, -(%sp)
	move.l	%d2, -(%sp)
	cmp.l	%d5, %d3
	bne.s	3f
	jsr	(%a3)
	bra.s	4f
3:	jsr	(%a2)
4:	lea	24(%sp), %sp
	addq.l	#1, %d5
	mvz.b	REGS+3, %d0		/* nm */
	cmp.l	%d5, %d0
	bne.s	1b
	jmp	0x400a26f2

/* --- knob -> descriptor (0x4001e8da, jsr + nop; back at 0x4001e8e2): d0 = machine (36(sp) behind our return
 * address), a1 = the OS's table machine -> record; a0 holds the OS's record_at pointer and must stay. An added
 * machine reads the moved table; its knob_hook, when set, is called with d0, a1 and the caller's d2 d3 d5
 * (knob, FUNC), a0 saved around it; it may use d1. Out of range: index 0, KICK's knobs. */
	.globl	knob_vec
knob_vec:
	move.l	36(%sp), %d0
	movea.l	104(%a2), %a1
	mvz.b	REGS+2, %d1		/* top */
	cmp.l	%d0, %d1
	bcc.s	1f
	moveq	#0, %d0
	rts
1:	mvz.b	REGS+1, %d1		/* first */
	cmp.l	%d1, %d0
	blt.s	2f
	movea.l	REGS+8, %a1		/* knob_tab */
	move.l	%a0, -(%sp)
	move.l	%d0, %d1
	bsr.w	find_machine
	move.l	%a0, %d1
	beq.s	3f
	move.l	8(%a0), %d1		/* knob_hook */
	beq.s	3f
	movea.l	%d1, %a0
	jsr	(%a0)
3:	movea.l	(%sp)+, %a0
2:	rts

/* --- descriptor -> machine (0x4005a50a, whole function): an added machine's four knobs -> its index; else the
 * descriptor's own field in the moved table. find_desc: d0 = descriptor -> a0 = entry or 0, uses d1. */
find_desc:
	lea	REGS+16, %a0
1:	cmpa.l	REGS+12, %a0
	bcc.s	9f
	mvz.b	2(%a0), %d1		/* first_desc */
	cmp.l	%d1, %d0
	bcs.s	2f
	addq.l	#3, %d1
	cmp.l	%d1, %d0
	bls.s	8f
2:	lea	16(%a0), %a0
	bra.s	1b
9:	suba.l	%a0, %a0
8:	rts

	.globl	desc_machine
desc_machine:
	move.l	4(%sp), %d0
	bsr.w	find_desc
	move.l	%a0, %d1
	beq.s	1f
	mvz.b	(%a0), %d0
	rts
1:	cmp.l	REGS+4, %d0		/* last */
	bcs.s	2f
	moveq	#0, %d0
2:	move.l	%d0, %d1
	lsl.l	#3, %d1
	lsl.l	#6, %d0
	sub.l	%d1, %d0
	lea	DESCN, %a0
	move.l	(%a0,%d0.l), %d0
	rts

/* --- the row builder (0x4005a340 -> 0x4005a346 / 0x4005a384, mid-function): d2 = descriptor, d5 = its machine
 * field; an added machine's knob descriptor gets the machine's index. a0 is the OS's and stays; the loop runs
 * on a1 (written before read on both exits), temps d0 d1. */
	.globl	builder_row
builder_row:
	moveq	#6, %d0
	cmp.l	%d5, %d0
	bcs.w	1f
	lea	REGS+16, %a1
3:	cmpa.l	REGS+12, %a1
	bcc.s	2f
	mvz.b	2(%a1), %d1
	cmp.l	%d1, %d2
	bcs.s	4f
	addq.l	#3, %d1
	cmp.l	%d1, %d2
	bhi.s	4f
	mvz.b	(%a1), %d5
	bra.s	2f
4:	lea	16(%a1), %a1
	bra.s	3b
2:	jmp	0x4005a384
1:	jmp	0x4005a346
