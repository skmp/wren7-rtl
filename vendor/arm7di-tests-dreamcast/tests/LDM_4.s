@ LDM: LDMDA!

.equ BAD_Rd,	0x10
.equ BAD_Rn,	0x20
.equ VARBASE,	0x80000

.global start
start:
	mov 	r1,#0
	ldr 	r3,=var64+4
	ldmda 	r3!,{r4,r5}
	ldr 	r0,=var64-4
	cmp 	r3,r0
	orrne 	r1,r1,#BAD_Rn
	mov 	r4,#5
	@ Test writeback for when the base register is included in the
	@ register list.
	ldr 	r3,=var64+4
	ldmda 	r3!,{r2,r3}
	ldr 	r0,=var64+4
	mov 	r5,r2
	ldr 	r2,[r0]
	cmp 	r3,r2
	orrne 	r1,r1,#BAD_Rn	@ r3 should contain the value loaded from memory
	ldrne 	r2,=rnVal
	strne 	r3,[r2]
	ldr 	r3,=var64+4
	ldmda 	r3!,{r3,r5}
	ldr 	r2,=var64-4
	ldr r2, [r2, #4]
	cmp 	r3,r2
	orrne 	r1,r1,#BAD_Rn	@ r3 should contain the updated base
	ldrne 	r2,=rnVal
	strne 	r3,[r2]
	ldr 	r2,[r0]
	cmp 	r5,r2
	orrne 	r1,r1,#BAD_Rd
	cmp 	r4,#5
	orrne 	r1,r1,#BAD_Rd	@ Make sure that the LDM didn't touch other registers
	mov 	r2,#3
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

