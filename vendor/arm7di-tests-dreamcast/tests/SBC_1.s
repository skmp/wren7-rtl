@ SBC

.equ BAD_Rd,	0x10
.equ BAD_Rn,	0x20
.equ VARBASE,	0x80000

.global start
start:
	mov 	r1,#0
	ldr 	r2,=0xFFFFFFFF
	adds 	r3,r2,r2	@ set carry
	sbcs 	r2,r2,r2
	orrcc 	r1,r1,#1
	orrmi 	r1,r1,#2
	orrne 	r1,r1,#8
	adds 	r9,r9,r9	@ clear carry
	sbcs 	r2,r2,#0
	orreq 	r1,r1,#8
	orrcs 	r1,r1,#1
	orrpl 	r1,r1,#2

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

