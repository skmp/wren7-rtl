@ AND

.equ BAD_Rd,	0x10
.equ BAD_Rn,	0x20
.equ VARBASE,	0x80000

.global start
start:
	mov 	r1,#0
	mov 	r2,#2
	mov 	r3,#5
	ands 	r2,r2,r3,lsr#1
	orrcc 	r1,r1,#1
	orreq 	r1,r1,#8
	cmp 	r2,#2
	orrne 	r1,r1,#BAD_Rd
	mov 	r2,#0xC00
	mov 	r3,r2
	mov 	r4,#0x80000000
	ands 	r2,r2,r4,asr#32
	orrcc 	r1,r1,#1
	orrmi 	r1,r1,#2
	orreq 	r1,r1,#8
	cmp 	r2,r3
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

