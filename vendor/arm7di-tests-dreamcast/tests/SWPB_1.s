@ SWPB

.equ BAD_Rd,	0x10
.equ BAD_Rn,	0x20
.equ VARBASE,	0x80000

.global start
start:
	mov 	r1,#0
	adds 	r1,r1,#0		@ Clear C,N,V
	ldr 	r5,=(VARBASE+0x100)
	mov 	r4,#0xff
	add 	r4,r4,#0x80
	str 	r4,[r5]
	mov 	r0,#0xC0000000
	orr 	r0,r0,#0x80
	swpb 	r0,r0,[r5]
	orrcs 	r1,r1,#1
	orrmi 	r1,r1,#2
	orrvs 	r1,r1,#4
	orrne 	r1,r1,#8
	cmp 	r0,#0x7f
	orrne 	r1,r1,#BAD_Rd
	ldr 	r0,[r5]
	cmp 	r0,#0x180
	orrne 	r1,r1,#BAD_Rd

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

