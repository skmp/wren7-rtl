@ LDRB: -#]!

.equ BAD_Rd,	0x10
.equ BAD_Rn,	0x20
.equ VARBASE,	0x80000

.global start
start:
	mov 	r1,#0
	ldr 	r0,=romvar2
	add 	r3,r0,#2
	add 	r0,r0,#4
	add 	r2,r0,#1
	ldrb 	r0,[r0,#-1]!
	cmp 	r0,#0xff
	orrne 	r1,r1,#BAD_Rd
	ldrb 	r0,[r2,#-3]!
	cmp 	r0,#0
	orrne 	r1,r1,#BAD_Rd
	cmp 	r2,r3
	orrne 	r1,r1,#BAD_Rn
	mov 	r2,#3
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

