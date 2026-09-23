@ LDM: LDMDBS!

.equ BAD_Rd,	0x10
.equ BAD_Rn,	0x20
.equ VARBASE,	0x80000

.global start
start:
	mov	r0, #0xd2	@ Switch to IRQ mode (XXX: keep irqs disabled)
	msr	cpsr, r0
	mov	r1,#0
	mov	r14,#123
	ldr	r0,=var64+8
	ldmdb	r0!,{r3,r14}^
	ldr	r2,=var64
	cmp	r0,r2
	orrne	r1,r1,#BAD_Rn
	ldrne 	r5,=rnVal
	strne 	r0,[r5]
	ldr	r2,[r2]
	cmp	r2,r3
	orrne	r1,r1,#BAD_Rd
	cmp	r14,#123
	orrne	r1,r1,#BAD_Rd
	mov 	r2,#6
	mov	r0, #0xd3	@ Switch to supervisor mode (XXX was system mode)
	msr	cpsr, r0
	orr 	r1,r1,#0x40000000

.word 0xDEADBEEF

.align 3
var64:		.word 0x11223344,0x55667788

rdVal:		.word 0
rnVal:		.word 0
memVal:		.word 0

.align 2
exceptionFlag: .word 0

romvar:  	.byte 0x80,0,0,0
romvar2: 	.byte 0x00,0x8f,0,0xff
romvar3: 	.byte 0x80,0x7f,0,0

