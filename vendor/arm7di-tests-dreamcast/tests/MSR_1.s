@ MSR

.equ BAD_Rd,	0x10
.equ BAD_Rn,	0x20
.equ VARBASE,	0x80000

.global start
start:
	mov 	r1,#0
	movs 	r2,#0
	msr 	cpsr_flg,#0x90000000
	orrcs 	r1,r1,#1
	orrpl 	r1,r1,#2
	orrvc 	r1,r1,#4
	orreq 	r1,r1,#8
	mov 	r11,#1
	mrs 	r2,cpsr
	bic 	r2,r2,#0x1f
	orr 	r2,r2,#0x11	
	msr 	cpsr,r2		@ Set FIQ mode
	mov 	r11,#2
	orr 	r2,r2,#0x13
	msr 	cpsr,r2		@ Set supervisor mode (XXX was originally system mode)
	cmp 	r11,#1
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

