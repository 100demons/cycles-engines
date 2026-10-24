/* Feral's custom SHAPE state: the machine's five descriptors share SNARE's state objects (index 52
 * for SHAPE). This initializer copies that object once per call and points its formatter and drawing
 * methods at Feral's model names. The registrar runtime calls it through the machine entry's
 * state-init field; the package does not patch the OS or the runtime. */

	.text

	.globl	feral_state_init
feral_state_init:
	tst.l	feral_state_ready
	bne.s	2f
	lea	0x40a71754+5200, %a1
	lea	feral_state, %a0
	moveq	#24, %d1
1:	move.l	(%a1)+, (%a0)+
	subq.l	#1, %d1
	bpl.s	1b
	lea	feral_state, %a0
	move.l	#feral_fmt, %d1
	move.l	%d1, 0x20(%a0)
	move.l	#feral_draw, %d1
	move.l	%d1, 0x30(%a0)
	moveq	#1, %d1
	move.l	%d1, feral_state_ready
2:	lea	feral_state, %a0
	move.l	%a0, %d0
	rts

feral_fmt:
	move.l	8(%sp), %d0
	addi.l	#0x80, %d0
	asr.l	#8, %d0
	bpl.s	1f
	moveq	#0, %d0
1:	cmpi.l	#8, %d0
	ble.s	2f
	moveq	#8, %d0
2:	lsl.l	#2, %d0
	lea	feral_names, %a0
	adda.l	%d0, %a0
	move.l	%a0, -(%sp)
	pea	feral_s_s
	move.l	20(%sp), -(%sp)
	jsr	0x40000e6e
	lea	12(%sp), %sp
	rts

feral_draw:
	lea	-24(%sp), %sp
	move.l	%sp, %d0
	move.l	%d0, -(%sp)
	move.l	36(%sp), -(%sp)
	clr.l	-(%sp)
	bsr.w	feral_fmt
	lea	12(%sp), %sp
	pea	0x401429ec
	pea	20(%sp)
	jsr	0x40072260
	addq.l	#8, %sp
	move.l	%sp, %d0
	move.l	%d0, -(%sp)
	pea	0x40124b58
	pea	2
	move.l	56(%sp), -(%sp)
	move.l	56(%sp), -(%sp)
	pea	36(%sp)
	move.l	60(%sp), -(%sp)
	jsr	0x40071a04
	lea	28(%sp), %sp
	pea	16(%sp)
	jsr	0x40072080
	lea	28(%sp), %sp
	rts

feral_s_s:	.asciz	"%s"
	.balign	2
	.balign	4
feral_names:
	.long	feral_n0
	.long	feral_n1
	.long	feral_n2
	.long	feral_n3
	.long	feral_n4
	.long	feral_n5
	.long	feral_n6
	.long	feral_n7
	.long	feral_n8
feral_n0:	.asciz	"NYB"
feral_n1:	.asciz	"GND"
feral_n2:	.asciz	"BNJ"
feral_n3:	.asciz	"RYT"
feral_n4:	.asciz	"FRS"
feral_n5:	.asciz	"RNG"
feral_n6:	.asciz	"PLS"
feral_n7:	.asciz	"SCN"
feral_n8:	.asciz	"KOS"
	.balign	4

	.bss
	.balign	4
feral_state:	.space	100
feral_state_ready:	.space	4
