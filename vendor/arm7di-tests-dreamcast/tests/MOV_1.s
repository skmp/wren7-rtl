@ MOV

.equ BAD_Rd,	0x10
.equ BAD_Rn,	0x20
.equ VARBASE,	0x80000

.global start
start:
	mov 	r1,#0
	ldr 	r2,=labelone
	mov 	r3,r15
	cmp 	r2,r3
labelone:
	orrne 	r1,r1,#BAD_Rd
	ldr 	r2,=labeltwo
	mov 	r3,#0
	@ XXX IDK if this is due to being on a different CPU or not,
	@     but GNU AS says using R15 leads to unpredictable behavior
	movs 	r4,r15,lsl r3	@ 0
	orreq 	r1,r1,#8	@ 4
	cmp 	r4,r2		@ 8
labeltwo: 			@ 12	
	orrne 	r1,r1,#BAD_Rd
	ldr 	r2,=0x80000001
	movs 	r3,r2,lsr#32
	orrcc 	r1,r1,#1
	orrmi 	r1,r1,#2
	orrne 	r1,r1,#8
	cmp 	r3,#0
	orrne 	r1,r1,#BAD_Rd
	@ Test ASR by reg==0
	mov 	r3,#3
	movs 	r4,r3,lsr#1	@ set carry 	
	mov 	r2,#0
	movs 	r3,r4,asr r2
	orrcc 	r1,r1,#1
	cmp 	r3,#1
	orrne 	r1,r1,#16
	@ Test ASR by reg==33
	ldr 	r2,=0x80000000
	mov 	r3,#33
	movs 	r2,r2,asr r3
	orrcc 	r1,r1,#1
	ldr 	r4,=0xFFFFFFFF
	cmp 	r2,r4
	orrne 	r1,r1,#16

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

