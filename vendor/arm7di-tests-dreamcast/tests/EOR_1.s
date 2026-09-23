@ EOR

.equ BAD_Rd,	0x10
.equ BAD_Rn,	0x20
.equ VARBASE,	0x80000

.global start
start:
	mov 	r1,#0
	mov 	r2,#1
	mov 	r3,#3
	eors 	r2,r2,r3,lsl#31
	eors 	r2,r2,r3,lsl#0
	orrcc 	r1,r1,#1
	orrpl 	r1,r1,#2
	orreq 	r1,r1,#8
	ldr 	r4,=0x80000002
	cmp 	r4,r2
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

