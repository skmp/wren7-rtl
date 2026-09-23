@ MRS

.equ BAD_Rd,	0x10
.equ BAD_Rn,	0x20
.equ VARBASE,	0x80000

.global start
start:
	mov 	r1,#0
	mov 	r0,#0xC0000000
	adds 	r0,r0,r0		@ Z=0, C=1, V=0, N=1
	mov 	r2,#0x50000000
	mrs 	r2,cpsr
	tsts 	r2,#0x20000000
	orreq 	r1,r1,#1
	tsts 	r2,#0x80000000
	orreq 	r1,r1,#2
	tsts 	r2,#0x10000000
	orrne 	r1,r1,#4
	tsts 	r2,#0x40000000
	orrne 	r1,r1,#8

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

