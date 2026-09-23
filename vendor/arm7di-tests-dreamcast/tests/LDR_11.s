@ LDR: ]+R

.equ BAD_Rd,	0x10
.equ BAD_Rn,	0x20
.equ VARBASE,	0x80000

.global start
start:
	mov 	r1,#0
	ldr 	r0,=romvar
	mov 	r2,r0
	add 	r5,r0,#8
	mov 	r3,r0
	mov 	r4,#2
	ldr 	r0,[r0],r4, lsr #1
	cmp 	r0,#0x80
	orrne 	r1,r1,#BAD_Rd
	ldr 	r0,[r2],r4, lsl #2
	cmp 	r0,#0x80
	orrne 	r1,r1,#BAD_Rd
	cmp 	r2,r5
	orrne 	r1,r1,#BAD_Rn
	ldr 	r2,=romvar
	mov 	r0,#123
	add 	r3,r2,r0
	ldr 	r0,[r2],r0
	cmp 	r0,#0x80
	orrne 	r1,r1,#BAD_Rd
	cmp 	r2,r3
	orrne 	r1,r1,#BAD_Rn
	ldr 	r2,=romvar
	sub 	r4,r2,#1
	mov 	r3,#0xC0000000
	ldr 	r0,[r2],r3, asr #32
	cmp 	r0,#0x80
	orrne 	r1,r1,#BAD_Rd
	cmp 	r2,r4
	orrne 	r1,r1,#BAD_Rn
	ldr 	r2,=romvar
	sub 	r4,r2,#2
	ldr 	r3,=0xfffffffc
	adds 	r5,r3,r3		@ set carry
	ldr 	r0,[r2],r3, rrx
	orrcc 	r1,r1,#1
	cmp 	r0,#0x80
	orrne 	r1,r1,#BAD_Rd
	cmp 	r2,r4
	orrne 	r1,r1,#BAD_Rn
	@ Test non word-aligned load
	ldr 	r0,=romvar2+2
	mov 	r2,#1
	ldr 	r0,[r0],r2
	ldr 	r2,=0x8f00ff00
	cmp 	r0,r2
	orrne 	r1,r1,#BAD_Rd
	mov 	r2,#10
	orr 	r1,r1,#0x80000000

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

