bl DrawText

Test0:
	stmfd sp!,{lr}
	
	ldr r0,=szALU1
	mov r1,#56
	mov r2,#1
	mov r3,#4
	bl DrawText
	

	@ ADC
	mov 	r1,#0
	mov 	r2,#0x80000000
	mov 	r3,#0xF
	adds 	r9,r9,r9	@ clear carry
	adcs 	r2,r2,r3
	orrcs 	r1,r1,#1
	orrpl 	r1,r1,#2
	orrvs 	r1,r1,#4
	orreq 	r1,r1,#8
	adcs 	r2,r2,r2	
	orrcc 	r1,r1,#1
	orrmi 	r1,r1,#2
	adc 	r3,r3,r3
	cmp 	r3,#0x1F
	orrne 	r1,r1,#BAD_Rd
	
	adds 	r9,r9,r9	@ clear carry
	mov 	r0,#0
	mov 	r2,#1
	adc 	r0,r0,r2,lsr#1
	cmp 	r0,#1
	@orrne 	r1,r1,#BAD_Rd
	
	ldr 	r0,=szADC
	bl 	DrawResult
	add 	r8,r8,#8

	@ ADD
	mov 	r1,#0
	ldr 	r2,=0xFFFFFFFE
	mov 	r3,#1
	adds 	r2,r2,r3
	orrcs 	r1,r1,#1
	orrpl 	r1,r1,#2
	orrvs 	r1,r1,#4
	orreq 	r1,r1,#8
	adds 	r2,r2,r3	
	orrcc 	r1,r1,#1
	orrmi 	r1,r1,#2
	orrvs 	r1,r1,#4

	orrne 	r1,r1,#8
	ldr 	r0,=szADD
	bl 	DrawResult
	add 	r8,r8,#8

	

	@ AND
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
	ldr 	r0,=szAND
	bl 	DrawResult
	add 	r8,r8,#8


	@ BIC
	mov 	r1,#0
	adds 	r9,r9,r9 @ clear carry
	ldr 	r2,=0xFFFFFFFF
	ldr 	r3,=0xC000000D
	bics 	r2,r2,r3,asr#1
	orrcc 	r1,r1,#1
	orrmi 	r1,r1,#2	
	orreq 	r1,r1,#8
	ldr 	r3,=0x1FFFFFF9
	cmp 	r2,r3
	orrne 	r1,r1,#16
	ldr 	r0,=szBIC
	bl 	DrawResult
	add 	r8,r8,#8
	
	@ CMN
	mov 	r1,#0
	adds 	r9,r9,r9 @ clear carry
	ldr 	r2,=0x7FFFFFFF
	ldr 	r3,=0x70000000
	cmns 	r2,r3
	orrcs 	r1,r1,#1
	orrpl 	r1,r1,#2
	orrvc 	r1,r1,#4
	orreq 	r1,r1,#8
	ldr 	r3,=0x7FFFFFFF
	cmp 	r2,r3
	orrne 	r1,r1,#16
	ldr 	r0,=szCMN
	bl 	DrawResult
	add 	r8,r8,#8

	
	@ EOR
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
	ldr 	r0,=szEOR
	bl 	DrawResult
	add 	r8,r8,#8

	
	@ MOV
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
	
	ldr 	r0,=szMOV
	bl 	DrawResult
	add 	r8,r8,#8


	@ MVN
	mov 	r1,#0
	ldr 	r2,=labelthree	
	ldr 	r3,=0xFFFFFFFF
	eor 	r2,r2,r3
	mvn 	r3,r15
	cmp 	r3,r2
labelthree:	
	orrne 	r1,r1,#BAD_Rd
	ldr 	r0,=szMVN
	bl 	DrawResult
	add 	r8,r8,#8
	
	@ ORR
	mov 	r1,#0
	mov 	r2,#2
	mov 	r3,#3
	movs 	r4,r3,lsr#1	@ set carry 
	orrs 	r3,r3,r2,rrx
	orrcs 	r1,r1,#1
	orrpl 	r1,r1,#2
	orreq 	r1,r1,#8
	ldr 	r4,=0x80000003
	cmp 	r4,r3
	orrne 	r1,r1,#BAD_Rd
	ldr 	r0,=szORR
	bl 	DrawResult
	add 	r8,r8,#8
	
	
	@ RSC
	mov 	r1,#0
	mov 	r2,#2
	mov 	r3,#3
	adds 	r9,r9,r9	@ clear carry
	rscs 	r3,r2,r3
	orrcc 	r1,r1,#1
	orrmi 	r1,r1,#2
	orrne 	r1,r1,#8
	cmp 	r2,#2
	orrne 	r1,r1,#BAD_Rd
	ldr 	r0,=szRSC
	bl 	DrawResult
	add 	r8,r8,#8

	
	@ SBC
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
	ldr 	r0,=szSBC
	bl 	DrawResult
	add 	r8,r8,#8
	

	@ MLA
	mov 	r1,#0
	ldr 	r2,=0xFFFFFFF6
	mov 	r3,#0x14
	ldr 	r4,=0xD0
	mlas 	r2,r3,r2,r4
	orrmi 	r1,r1,#2
	orreq 	r1,r1,#8
	cmp 	r2,#8
	orrne 	r1,r1,#16
	ldr 	r0,=szMLA
	bl 	DrawResult
	add 	r8,r8,#8


	@ MUL
	mov 	r1,#0
	ldr 	r2,=0xFFFFFFF6
	mov 	r3,#0x14
	ldr 	r4,=0xFFFFFF38
	muls 	r2,r3,r2
	orrpl 	r1,r1,#2
	orreq 	r1,r1,#8
	cmp 	r2,r4
	orrne 	r1,r1,#16
	ldr 	r0,=szMUL
	bl 	DrawResult
	add 	r8,r8,#8

	ldmfd 	sp!,{lr}
	mov 	pc,lr
.pool
.align

	
Test1:
	stmfd 	sp!,{lr}
	
	ldr 	r0,=szALU2
	mov 	r1,#60
	mov 	r2,#1
	mov 	r3,#4
	bl 	DrawText

	@ SWP
	mov 	r1,#0
	adds 	r1,r1,#1		@ Clear C,N,V,Z
	mov 	r1,#0
	ldr 	r5,=(VARBASE+0x100)
	str 	r1,[r5]
	mov 	r0,#0xC0000000
	swp 	r0,r0,[r5]
	orrcs 	r1,r1,#1
	orrmi 	r1,r1,#2
	orrvs 	r1,r1,#4
	orreq 	r1,r1,#8
	cmp 	r0,#0
	orrne 	r1,r1,#BAD_Rd
	ldr 	r0,[r5]
	cmp 	r0,#0xC0000000
	orrne 	r1,r1,#BAD_Rd
	ldr 	r0,=szSWP
	bl 	DrawResult
	add 	r8,r8,#8
	
	@ SWPB
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
	ldr 	r0,=szSWPB
	bl 	DrawResult
	add 	r8,r8,#8

	
	@ MRS
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
	ldr 	r0,=szMRS
	bl 	DrawResult
	add 	r8,r8,#8
	
	
	@ MSR
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
	ldr 	r0,=szMSR
	bl 	DrawResult
	add 	r8,r8,#8
	
	
	ldmfd 	sp!,{lr}
	mov 	pc,lr	
.pool
.align



Test2:
	stmfd 	sp!,{lr}

	ldr 	r0,=szLS1
	mov 	r1,#52
	mov 	r2,#1
	mov 	r3,#4
	bl 	DrawText


	@ +#]
	mov 	r1,#0
	ldr 	r0,=romvar
	sub 	r2,r0,#3
	mov 	r3,r2
	ldr 	r0,[r0,#0]
	cmp 	r0,#0x80
	orrne 	r1,r1,#BAD_Rd
	ldr 	r0,[r2,#3]
	cmp 	r0,#0x80
	orrne 	r1,r1,#BAD_Rd
	cmp 	r2,r3
	orrne 	r1,r1,#BAD_Rn
	@ Test non word-aligned load
	ldr 	r0,=romvar2
	ldr 	r0,[r0,#1]
	ldr 	r2,=0x00ff008f
	cmp 	r0,r2
	orrne 	r1,r1,#BAD_Rd
	ldr 	r0,=romvar2
	ldr 	r0,[r0,#2]
	ldr 	r2,=0x8f00ff00
	cmp 	r0,r2
	orrne 	r1,r1,#BAD_Rd
	ldr 	r0,=romvar2
	ldr 	r0,[r0,#3]
	ldr 	r2,=0x008f00ff
	cmp 	r0,r2
	orrne 	r1,r1,#BAD_Rd

	mov 	r2,#0
	orr 	r1,r1,#0x80000000
	ldr 	r0,=szLDR
	bl 	DrawResult
	add 	r8,r8,#8


	@ -#]
	mov 	r1,#0
	ldr 	r0,=romvar
	mov 	r2,r0
	mov 	r3,r2
	add 	r0,r0,#206
	ldr 	r0,[r0,#-206]
	cmp 	r0,#0x80
	orrne 	r1,r1,#BAD_Rd
	ldr 	r0,[r2,#-0]
	cmp 	r0,#0x80
	orrne 	r1,r1,#BAD_Rd
	cmp 	r2,r3
	orrne 	r1,r1,#BAD_Rn
	@ Test non word-aligned load
	ldr 	r0,=romvar2+4
	ldr 	r0,[r0,#-2]
	ldr 	r2,=0x8f00ff00
	cmp 	r0,r2
	orrne 	r1,r1,#BAD_Rd

	mov 	r2,#1
	orr 	r1,r1,#0x80000000
	ldr 	r0,=szLDR
	bl 	DrawResult
	add 	r8,r8,#8


	@ +#]!
	mov 	r1,#0
	ldr 	r0,=romvar
	sub 	r2,r0,#3
	mov 	r3,r0
	ldr 	r0,[r0,#0]!
	cmp 	r0,#0x80
	orrne 	r1,r1,#BAD_Rd
	ldr 	r0,[r2,#3]!
	cmp 	r0,#0x80
	orrne 	r1,r1,#BAD_Rd
	cmp 	r2,r3
	orrne 	r1,r1,#BAD_Rn
	@ Test non word-aligned load
	ldr 	r0,=romvar2
	ldr 	r0,[r0,#2]!
	ldr 	r2,=0x8f00ff00
	cmp 	r0,r2
	orrne 	r1,r1,#BAD_Rd
	
	mov 	r2,#2
	orr 	r1,r1,#0x80000000
	ldr 	r0,=szLDR
	bl 	DrawResult
	add 	r8,r8,#8


	@ -#]!
	mov 	r1,#0
	ldr 	r0,=romvar
	add 	r2,r0,#1
	mov 	r3,r0
	add 	r0,r0,#206
	ldr 	r0,[r0,#-206]!
	cmp 	r0,#0x80
	orrne 	r1,r1,#BAD_Rd
	ldr 	r0,[r2,#-1]!
	cmp 	r0,#0x80
	orrne 	r1,r1,#BAD_Rd
	cmp 	r2,r3
	orrne 	r1,r1,#BAD_Rn
	@ Test non word-aligned load
	ldr 	r0,=romvar2+4
	ldr 	r0,[r0,#-2]!
	ldr 	r2,=0x8f00ff00
	cmp 	r0,r2
	orrne 	r1,r1,#BAD_Rd
	
	mov 	r2,#3
	orr 	r1,r1,#0x80000000
	ldr 	r0,=szLDR
	bl 	DrawResult
	add 	r8,r8,#8


	@ +R]
	mov 	r1,#0
	ldr 	r0,=romvar
	sub 	r2,r0,#8
	sub 	r0,r0,#1
	mov 	r3,r2
	mov 	r4,#2
	ldr 	r0,[r0,r4, lsr #1]
	cmp 	r0,#0x80
	orrne 	r1,r1,#BAD_Rd
	ldr 	r0,[r2,r4, lsl #2]
	cmp 	r0,#0x80
	orrne 	r1,r1,#BAD_Rd
	cmp 	r2,r3
	orrne 	r1,r1,#BAD_Rn

	ldr 	r2,=romvar
	mov 	r2,r2,lsr#1
	mov 	r3,#0xC0000000
	ldr 	r0,[r2,r2]
	cmp 	r0,#0x80
	orrne 	r1,r1,#BAD_Rd

	ldr 	r2,=romvar
	mov 	r3,#0x8
	ldr 	r0,[r2,r3, lsr #32]
	cmp 	r0,#0x80
	orrne 	r1,r1,#BAD_Rd
	
	ldr 	r2,=romvar
	add 	r2,r2,#1
	mov 	r3,#0xC0000000
	ldr 	r0,[r2,r3, asr #32]
	cmp 	r0,#0x80
	orrne 	r1,r1,#BAD_Rd

	ldr 	r2,=romvar
	add 	r2,r2,#2
	ldr 	r3,=0xfffffffc
	adds 	r4,r3,r3		@ set carry
	ldr 	r0,[r2,r3, rrx]
	orrcc 	r1,r1,#1
	cmp 	r0,#0x80
	orrne 	r1,r1,#BAD_Rd

	@ Test non word-aligned load
	ldr 	r0,=romvar2
	mov 	r2,#2
	ldr 	r0,[r0,r2]
	ldr 	r2,=0x8f00ff00
	cmp 	r0,r2
	orrne 	r1,r1,#BAD_Rd
	
	mov 	r2,#4
	orr 	r1,r1,#0x80000000
	ldr 	r0,=szLDR
	bl 	DrawResult
	add 	r8,r8,#8


	@ -R]
	mov 	r1,#0
	ldr 	r0,=romvar
	add 	r2,r0,#8
	add 	r0,r0,#1
	mov 	r3,r2
	mov 	r4,#2
	ldr 	r0,[r0,-r4, lsr #1]
	cmp 	r0,#0x80
	orrne 	r1,r1,#BAD_Rd
	ldr 	r0,[r2,-r4, lsl #2]
	cmp 	r0,#0x80
	orrne 	r1,r1,#BAD_Rd
	cmp 	r2,r3
	orrne 	r1,r1,#BAD_Rn

	ldr 	r2,=romvar
	mov 	r3,#0x8
	ldr 	r0,[r2,-r3, lsr #32]
	cmp 	r0,#0x80
	orrne 	r1,r1,#BAD_Rd

	ldr 	r2,=romvar
	sub 	r2,r2,#1
	mov 	r3,#0x80000000
	ldr 	r0,[r2,-r3, asr #32]
	cmp 	r0,#0x80
	orrne 	r1,r1,#BAD_Rd
	
	ldr 	r2,=romvar
	sub 	r2,r2,#4
	ldr 	r3,=0xfffffff8
	adds 	r4,r3,r3		@ set carry
	ldr 	r0,[r2,-r3, rrx]
	orrcc 	r1,r1,#1
	cmp 	r0,#0x80
	orrne 	r1,r1,#BAD_Rd

	@ Test non word-aligned load
	ldr 	r0,=romvar2+4
	mov 	r2,#1
	ldr 	r0,[r0,-r2, lsl #1]
	ldr 	r2,=0x8f00ff00
	cmp 	r0,r2
	orrne 	r1,r1,#BAD_Rd
	
	mov 	r2,#5
	orr 	r1,r1,#0x80000000
	ldr 	r0,=szLDR
	bl 	DrawResult
	add 	r8,r8,#8
	

	@ +R]!
	mov 	r1,#0
	ldr 	r0,=romvar
	mov 	r3,r0
	sub 	r2,r0,#8
	sub 	r0,r0,#1
	mov 	r4,#2
	ldr 	r0,[r0,r4, lsr #1]!
	cmp 	r0,#0x80
	orrne 	r1,r1,#BAD_Rd
	ldr 	r0,[r2,r4, lsl #2]!
	cmp 	r0,#0x80
	orrne 	r1,r1,#BAD_Rd
	cmp 	r2,r3
	orrne 	r1,r1,#BAD_Rn

	ldr 	r2,=romvar
	mov 	r4,r2
	mov 	r2,r2,lsr#1
	mov 	r3,#0xC0000000
	ldr 	r0,[r2,r2]!
	cmp 	r0,#0x80
	orrne 	r1,r1,#BAD_Rd
	cmp 	r2,r4
	orrne 	r1,r1,#BAD_Rn
	
	ldr 	r2,=romvar
	mov 	r4,r2
	add 	r2,r2,#1
	mov 	r3,#0xC0000000
	ldr 	r0,[r2,r3, asr #32]!
	cmp 	r0,#0x80
	orrne 	r1,r1,#BAD_Rd
	cmp 	r2,r4
	orrne 	r1,r1,#BAD_Rn

	ldr 	r2,=romvar
	mov 	r5,r2
	add 	r2,r2,#2
	ldr 	r3,=0xfffffffc
	adds 	r4,r3,r3		@ set carry
	ldr 	r0,[r2,r3, rrx]!
	orrcc 	r1,r1,#1
	cmp 	r0,#0x80
	orrne 	r1,r1,#BAD_Rd
	cmp 	r2,r5
	orrne 	r1,r1,#BAD_Rn

	@ Test non word-aligned load
	ldr 	r0,=romvar2
	mov 	r2,#2
	ldr 	r0,[r0,r2]!
	ldr 	r2,=0x8f00ff00
	cmp 	r0,r2
	orrne 	r1,r1,#BAD_Rd
	
	mov 	r2,#6
	orr 	r1,r1,#0x80000000
	ldr 	r0,=szLDR
	bl 	DrawResult
	add 	r8,r8,#8


	@ -R]!
	mov 	r1,#0
	ldr 	r0,=romvar
	mov 	r3,r0
	add 	r2,r0,#8
	add 	r0,r0,#1
	mov 	r4,#2
	ldr 	r0,[r0,-r4, lsr #1]!
	cmp 	r0,#0x80
	orrne 	r1,r1,#BAD_Rd
	ldr 	r0,[r2,-r4, lsl #2]!
	cmp 	r0,#0x80
	orrne 	r1,r1,#BAD_Rd
	cmp 	r2,r3
	orrne 	r1,r1,#BAD_Rn

	ldr 	r2,=romvar
	mov 	r4,r2
	sub 	r2,r2,#1
	mov 	r3,#0x80000000
	ldr 	r0,[r2,-r3, asr #32]!
	cmp 	r0,#0x80
	orrne 	r1,r1,#BAD_Rd
	cmp 	r2,r4
	orrne 	r1,r1,#BAD_Rn
	
	ldr 	r2,=romvar
	mov 	r5,r2
	sub 	r2,r2,#4
	ldr 	r3,=0xfffffff8
	adds 	r4,r3,r3		@ set carry
	ldr 	r0,[r2,-r3, rrx]!
	orrcc 	r1,r1,#1
	cmp 	r0,#0x80
	orrne 	r1,r1,#BAD_Rd
	cmp 	r2,r5
	orrne 	r1,r1,#BAD_Rn

	@ Test non word-aligned load
	ldr 	r0,=romvar2+4
	mov 	r2,#2
	ldr 	r0,[r0,-r2]!
	ldr 	r2,=0x8f00ff00
	cmp 	r0,r2
	orrne 	r1,r1,#BAD_Rd
	
	mov 	r2,#7
	orr 	r1,r1,#0x80000000
	ldr 	r0,=szLDR
	bl 	DrawResult
	add 	r8,r8,#8
	

	@ ]+#
	mov 	r1,#0
	ldr 	r0,=romvar
	add 	r3,r0,#3
	mov 	r2,r0
	ldr 	r0,[r0],#3
	cmp 	r0,#0x80
	orrne 	r1,r1,#BAD_Rd
	ldr 	r0,[r2],#3
	cmp 	r0,#0x80
	orrne 	r1,r1,#BAD_Rd
	cmp 	r2,r3
	orrne 	r1,r1,#BAD_Rn
	@ Test non word-aligned load
	ldr 	r0,=romvar2+2
	ldr 	r0,[r0],#5
	ldr 	r2,=0x8f00ff00
	cmp 	r0,r2
	orrne 	r1,r1,#BAD_Rd
	mov 	r2,#8
	orr 	r1,r1,#0x80000000
	ldr 	r0,=szLDR
	bl 	DrawResult
	add 	r8,r8,#8

	@ ]-#
	mov 	r1,#0
	ldr 	r0,=romvar
	mov 	r2,r0
	sub 	r3,r0,#0xff
	ldr 	r0,[r0],#-0xff
	cmp 	r0,#0x80
	orrne 	r1,r1,#BAD_Rd
	ldr 	r0,[r2],#-0xff
	cmp 	r0,#0x80
	orrne 	r1,r1,#BAD_Rd
	cmp 	r2,r3
	orrne 	r1,r1,#BAD_Rn
	@ Test non word-aligned load
	ldr 	r0,=romvar2+2
	ldr 	r0,[r0],#-5
	ldr 	r2,=0x8f00ff00
	cmp 	r0,r2
	orrne 	r1,r1,#BAD_Rd
	mov 	r2,#9
	orr 	r1,r1,#0x80000000
	ldr 	r0,=szLDR
	bl 	DrawResult
	add 	r8,r8,#8


	@ ]+R
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
	ldr 	r0,=szLDR
	bl 	DrawResult
	add 	r8,r8,#8


	@ ]-R
	mov 	r1,#0
	ldr 	r0,=romvar
	mov 	r2,r0
	sub 	r5,r0,#16
	mov 	r3,r0
	mov 	r4,#2
	ldr 	r0,[r0],-r4, lsr #1
	cmp 	r0,#0x80
	orrne 	r1,r1,#BAD_Rd
	ldr 	r0,[r2],-r4, lsl #3
	cmp 	r0,#0x80
	orrne 	r1,r1,#BAD_Rd
	cmp 	r2,r5
	orrne 	r1,r1,#BAD_Rn

	ldr	r2,=romvar
	mov 	r0,#123
	sub 	r3,r2,r0
	ldr 	r0,[r2],-r0
	cmp 	r0,#0x80
	orrne 	r1,r1,#BAD_Rd
	cmp 	r2,r3
	orrne 	r1,r1,#BAD_Rn
	
	ldr 	r2,=romvar
	add 	r4,r2,#1
	mov 	r3,#0xC0000000
	ldr 	r0,[r2],-r3, asr #32
	cmp 	r0,#0x80
	orrne 	r1,r1,#BAD_Rd
	cmp 	r2,r4
	orrne 	r1,r1,#BAD_Rn

	ldr 	r2,=romvar
	add 	r4,r2,#2
	ldr 	r3,=0xfffffffc
	adds 	r5,r3,r3		@ set carry
	ldr 	r0,[r2],-r3, rrx
	orrcc 	r1,r1,#1
	cmp 	r0,#0x80
	orrne 	r1,r1,#BAD_Rd
	cmp 	r2,r4
	orrne 	r1,r1,#BAD_Rn

	@ Test non word-aligned load
	ldr 	r0,=romvar2+2
	mov 	r2,#5
	ldr 	r0,[r0],-r2
	ldr 	r2,=0x8f00ff00
	cmp 	r0,r2
	orrne 	r1,r1,#BAD_Rd
	
	mov 	r2,#11
	orr 	r1,r1,#0x80000000
	ldr 	r0,=szLDR
	bl 	DrawResult
	add 	r8,r8,#8
	
	ldmfd 	sp!,{lr}
	mov 	pc,lr
.pool
.align



Test3:
	stmfd 	sp!,{lr}

	ldr 	r0,=szLS2
	mov 	r1,#52
	mov 	r2,#1
	mov 	r3,#4
	bl 	DrawText

	

	@ +#]
	mov 	r1,#0
	ldr 	r0,=romvar2
	sub 	r2,r0,#1
	mov 	r3,r2
	ldrb 	r0,[r0,#3]
	cmp 	r0,#0xff
	orrne 	r1,r1,#BAD_Rd
	ldrb 	r0,[r2,#3]
	cmp 	r0,#0
	orrne 	r1,r1,#BAD_Rd
	cmp 	r2,r3
	orrne 	r1,r1,#BAD_Rn
	mov 	r2,#0
	orr 	r1,r1,#0x80000000
	ldr 	r0,=szLDRB
	bl 	DrawResult
	add 	r8,r8,#8

	@ -#]
	mov 	r1,#0
	ldr 	r0,=romvar2
	add 	r0,r0,#4
	add 	r2,r0,#1
	mov 	r3,r2
	ldrb 	r0,[r0,#-1]
	cmp 	r0,#0xff
	orrne 	r1,r1,#BAD_Rd
	ldrb 	r0,[r2,#-3]
	cmp 	r0,#0
	orrne 	r1,r1,#BAD_Rd
	cmp 	r2,r3
	orrne 	r1,r1,#BAD_Rn
	mov 	r2,#1
	orr 	r1,r1,#0x80000000
	ldr 	r0,=szLDRB
	bl 	DrawResult
	add 	r8,r8,#8

	@ +#]!
	mov 	r1,#0
	ldr 	r0,=romvar2
	add 	r3,r0,#2
	sub 	r2,r0,#3
	ldrb 	r0,[r0,#3]!
	cmp 	r0,#0xff
	orrne 	r1,r1,#BAD_Rd
	ldrb 	r0,[r2,#5]!
	cmp 	r0,#0
	orrne 	r1,r1,#BAD_Rd
	cmp 	r2,r3
	orrne 	r1,r1,#BAD_Rn
	mov 	r2,#2
	orr 	r1,r1,#0x80000000
	ldr 	r0,=szLDRB
	bl 	DrawResult
	add 	r8,r8,#8

	@ -#]!
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
	ldr 	r0,=szLDRB
	bl 	DrawResult
	add 	r8,r8,#8
	
	

	ldmfd 	sp!,{lr}
	mov 	pc,lr
.pool
.align



Test4:
	stmfd 	sp!,{lr}

	ldr 	r0,=szLDM1
	mov 	r1,#52
	mov 	r2,#1
	mov 	r3,#4
	bl 	DrawText

	@ LDMIB!
	mov 	r1,#0
	ldr 	r3,=var64
	sub 	r3,r3,#4
	ldmib 	r3!,{r4,r5}
	ldr 	r0,=var64+4
	cmp 	r3,r0
	orrne 	r1,r1,#BAD_Rn
	mov 	r4,#5

	@ Test writeback for when the base register is included in the

	@ register list.

	ldr 	r3,=var64
	sub 	r3,r3,#4
	ldmib 	r3!,{r2,r3}
	ldr 	r0,=var64+4
	mov 	r5,r2
	ldr 	r2,[r0]
	cmp 	r3,r2
	orrne 	r1,r1,#BAD_Rn
	ldrne 	r2,=rnVal
	strne 	r3,[r2]

	ldr 	r3,=var64
	sub 	r3,r3,#4
	ldmib 	r3!,{r3,r5}
	ldr 	r2,=var64+4
	ldr r2, [r2, #-4]
	cmp 	r3,r2
	orrne 	r1,r1,#BAD_Rn
	ldrne 	r2,=rnVal
	strne 	r3,[r2]
	
	ldr 	r2,[r0]
	cmp 	r5,r2
	orrne 	r1,r1,#BAD_Rd
	cmp 	r4,#5
	orrne 	r1,r1,#BAD_Rd
	mov 	r2,#0
	orr 	r1,r1,#0x40000000
	ldr 	r0,=szLDM
	bl 	DrawResult
	add 	r8,r8,#8


	@ LDMIA!
	mov 	r1,#0
	ldr 	r3,=var64
	ldmia 	r3!,{r4,r5}
	ldr 	r0,=var64+8
	cmp 	r3,r0
	orrne 	r1,r1,#BAD_Rn
	mov 	r4,#5

	@ Test writeback for when the base register is included in the
	@ register list.
	ldr 	r3,=var64
	ldmia 	r3!,{r2,r3}
	ldr 	r0,=var64+4
	mov 	r5,r2
	ldr 	r2,[r0]
	cmp 	r3,r2
	orrne 	r1,r1,#BAD_Rn
	ldrne 	r2,=rnVal
	strne 	r3,[r2]

	ldr 	r3,=var64
	ldmia 	r3!,{r3,r5}
	ldr 	r2,=var64+8
	ldr r2, [r2, #-8]
	cmp 	r3,r2
	orrne 	r1,r1,#BAD_Rn
	ldrne 	r2,=rnVal
	strne 	r3,[r2]
	
	ldr 	r2,[r0]
	cmp 	r5,r2
	orrne 	r1,r1,#BAD_Rd
	cmp 	r4,#5
	orrne 	r1,r1,#BAD_Rd
	mov 	r2,#1
	orr 	r1,r1,#0x40000000
	ldr 	r0,=szLDM
	bl 	DrawResult
	add 	r8,r8,#8


	@ LDMDB!
	mov 	r1,#0
	ldr 	r3,=var64+8
	ldmdb 	r3!,{r4,r5}
	ldr 	r0,=var64
	cmp 	r3,r0
	orrne 	r1,r1,#BAD_Rn
	mov 	r4,#5

	@ Test writeback for when the base register is included in the
	@ register list.
	ldr 	r3,=var64+8
	ldmdb 	r3!,{r2,r3}
	ldr 	r0,=var64+4
	mov 	r5,r2
	ldr 	r2,[r0]
	cmp 	r3,r2
	orrne 	r1,r1,#BAD_Rn
	ldrne 	r2,=rnVal
	strne 	r3,[r2]

	ldr 	r3,=var64+8
	ldmdb 	r3!,{r3,r5}
	ldr 	r2,=var64
	ldr r2, [r2]
	cmp 	r3,r2
	orrne 	r1,r1,#BAD_Rn
	ldrne 	r2,=rnVal
	strne 	r3,[r2]
	
	ldr 	r2,[r0]
	cmp 	r5,r2
	orrne 	r1,r1,#BAD_Rd
	cmp 	r4,#5
	orrne 	r1,r1,#BAD_Rd
	mov 	r2,#2
	orr 	r1,r1,#0x40000000
	ldr 	r0,=szLDM
	bl 	DrawResult
	add 	r8,r8,#8
	

	@ LDMDA!
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
	ldr 	r0,=szLDM
	bl 	DrawResult
	add 	r8,r8,#8

	
	@ LDMIBS!
	mov	r0, #0xd2	@ Switch to IRQ mode (XXX: keep irqs disabled)
	msr	cpsr, r0
	mov	r1,#0
	mov	r14,#123
	ldr	r0,=var64-4
	ldmib	r0!,{r3,r14}^
	ldr	r2,=var64+4
	cmp	r0,r2
	orrne	r1,r1,#BAD_Rn
	ldrne 	r5,=rnVal
	strne 	r0,[r5]
	sub	r2,r2,#4
	ldr	r2,[r2]
	cmp	r2,r3
	orrne	r1,r1,#BAD_Rd
	cmp	r14,#123
	orrne	r1,r1,#BAD_Rd
	mov 	r2,#4
	mov	r0, #0xd3	@ Switch to supervisor mode (XXX was system mode)
	msr	cpsr, r0
	orr 	r1,r1,#0x40000000
	ldr 	r0,=szLDM
	bl 	DrawResult
	add 	r8,r8,#8

	@ LDMIAS!
	mov	r0, #0xd2	@ Switch to IRQ mode (XXX: keep irqs disabled)
	msr	cpsr, r0
	mov	r1,#0
	mov	r14,#123
	ldr	r0,=var64
	ldmia	r0!,{r3,r14}^
	ldr	r2,=var64+8
	cmp	r0,r2
	orrne	r1,r1,#BAD_Rn
	ldrne 	r5,=rnVal
	strne 	r0,[r5]
	sub	r2,r2,#8
	ldr	r2,[r2]
	cmp	r2,r3
	orrne	r1,r1,#BAD_Rd
	cmp	r14,#123
	orrne	r1,r1,#BAD_Rd
	mov 	r2,#5
	mov	r0, #0xd3	@ Switch to supervisor mode (XXX was system mode)
	msr	cpsr, r0
	orr 	r1,r1,#0x40000000
	ldr 	r0,=szLDM
	bl 	DrawResult
	add 	r8,r8,#8

	@ LDMDBS!
	mov	r0, #0xd2	@ Switch to IRQ mode (XXX: keep irqs disabled)
	msr	cpsr, r0
	mov	r1,#0
	mov	r14,#123
	ldr	r0,=var64+8
	ldmdb	r0!,{r3,r14}^
	ldr	r2,=var64
	cmp	r0,r2
	orrne	r1,r1,#BAD_Rn
	ldrne 	r5,=rnVal
	strne 	r0,[r5]
	ldr	r2,[r2]
	cmp	r2,r3
	orrne	r1,r1,#BAD_Rd
	cmp	r14,#123
	orrne	r1,r1,#BAD_Rd
	mov 	r2,#6
	mov	r0, #0xd3	@ Switch to supervisor mode (XXX was system mode)
	msr	cpsr, r0
	orr 	r1,r1,#0x40000000
	ldr 	r0,=szLDM
	bl 	DrawResult
	add 	r8,r8,#8

	@ LDMDAS!
	mov	r0, #0xd2	@ Switch to IRQ mode (XXX: keep irqs disabled)
	msr	cpsr, r0
	mov	r1,#0
	mov	r14,#123
	ldr	r0,=var64+4
	ldmda	r0!,{r3,r14}^
	ldr	r2,=var64-4
	cmp	r0,r2
	orrne	r1,r1,#BAD_Rn
	ldrne 	r5,=rnVal
	strne 	r0,[r5]
	add	r2,r2,#4
	ldr	r2,[r2]
	cmp	r2,r3
	orrne	r1,r1,#BAD_Rd
	cmp	r14,#123
	orrne	r1,r1,#BAD_Rd
	mov 	r2,#7
	mov	r0, #0xd3	@ Switch to supervisor mode (XXX was system mode)
	msr	cpsr, r0
	orr 	r1,r1,#0x40000000
	ldr 	r0,=szLDM
	bl 	DrawResult
	add 	r8,r8,#8
	
	@ STMIB!
	mov 	r1,#0
	ldr 	r3,=(VARBASE+0x1FC)
	mov 	r4,#5
	stmib 	r3!,{r3,r4,r5}
	ldr 	r0,=(VARBASE+0x208)
	cmp 	r3,r0
	orrne 	r1,r1,#BAD_Rn
	ldrne 	r5,=rnVal
	strne 	r3,[r5]
	sub 	r0,r0,#8
	ldr 	r2,[r0]
	sub 	r0,r0,#4
	cmp 	r2,r0
	@orrne 	r1,r1,#0x80
	@ldrne	r0,=memVal
	@strne	r2,[r0]

	ldr 	r3,=(VARBASE+0x1FC)
	mov 	r4,#5
	stmib 	r3!,{r2,r3,r4}
	ldr 	r0,=(VARBASE+0x208)
	cmp 	r3,r0
	orrne 	r1,r1,#BAD_Rn
	ldrne 	r5,=rnVal
	strne 	r3,[r5]
	ldr	r2,[r0]
	cmp	r4,r2
	orrne	r1,r1,#0x80
	ldrne	r0,=memVal
	strne	r4,[r0] @r2,[r0]
	

	@ldr 	r0,=(VARBASE+0x204)
	@ldr 	r2,[r0]
	@ldr	r3,=(VARBASE+0x208)
	@cmp 	r3,r2
	@orrne 	r1,r1,#0x80
	@ldrne	r0,=memVal
	@strne	r2,[r0]
	
	mov 	r2,#0
	orr 	r1,r1,#0x40000000
	ldr 	r0,=szSTM
	bl 	DrawResult
	add 	r8,r8,#8
	
	@ STMIA!
	mov 	r1,#0
	ldr 	r3,=(VARBASE+0x200)
	mov 	r4,#5
	stmia 	r3!,{r3,r4,r5}
	ldr 	r0,=(VARBASE+0x20C)
	cmp 	r3,r0
	orrne 	r1,r1,#BAD_Rn
	ldrne 	r5,=rnVal
	strne 	r3,[r5]
	sub 	r0,r0,#0xC
	ldr 	r2,[r0]
	cmp 	r2,r0
	orrne 	r1,r1,#0x80
	ldrne	r4,=memVal
	strne	r0,[r4] @r2,[r4]

	ldr 	r3,=(VARBASE+0x200)
	mov 	r4,#5
	stmia 	r3!,{r2,r3,r4}
	ldr 	r0,=(VARBASE+0x20C)
	cmp 	r3,r0
	orrne 	r1,r1,#BAD_Rn
	ldrne 	r5,=rnVal
	strne 	r3,[r5]
	ldr 	r0,=(VARBASE+0x208)
	ldr	r2,[r0]
	cmp	r4,r2
	@orrne	r1,r1,#0x80
	@ldrne	r0,=memVal
	@strne	r2,[r0]
	

	@ldr 	r0,=(VARBASE+0x204)
	@ldr 	r2,[r0]
	@ldr	r3,=(VARBASE+0x20C)
	@cmp 	r3,r2
	@orrne 	r1,r1,#0x80
	@ldrne	r0,=memVal
	@strne	r2,[r0]
	
	mov 	r2,#1
	orr 	r1,r1,#0x40000000
	ldr 	r0,=szSTM
	bl 	DrawResult
	add 	r8,r8,#8	


	@ STMDB!
	mov 	r1,#0
	ldr 	r3,=(VARBASE+0x20C)
	mov 	r4,#5
	stmdb 	r3!,{r3,r4,r5}
	ldr 	r0,=(VARBASE+0x200)
	cmp 	r3,r0
	orrne 	r1,r1,#BAD_Rn
	ldrne 	r5,=rnVal
	strne 	r3,[r5]
	ldr 	r2,[r0]
	add	r0,r0,#0xC
	cmp 	r2,r0
	orrne 	r1,r1,#0x80
	ldrne	r0,=memVal
	strne	r2,[r0]

	ldr 	r3,=(VARBASE+0x20C)
	mov 	r4,#5
	stmdb 	r3!,{r2,r3,r4}
	ldr 	r0,=(VARBASE+0x200)
	cmp 	r3,r0
	orrne 	r1,r1,#BAD_Rn
	ldrne 	r5,=rnVal
	strne 	r3,[r5]
	add	r0,r0,#8
	ldr	r2,[r0]
	cmp	r4,r2
	orrne	r1,r1,#0x80
	ldrne	r0,=memVal
	strne	r2,[r0]
	@ldr 	r0,=(VARBASE+0x204)
	@ldr 	r2,[r0]
	@ldr	r3,=(VARBASE+0x200)
	@cmp 	r3,r2
	@orrne 	r1,r1,#0x80
	@ldrne	r0,=memVal
	@strne	r2,[r0]
	
	mov 	r2,#2
	orr 	r1,r1,#0x40000000
	ldr 	r0,=szSTM
	bl 	DrawResult
	add 	r8,r8,#8


	@ STMDA!
	mov 	r1,#0
	ldr 	r3,=(VARBASE+0x208)
	mov 	r4,#5
	stmda 	r3!,{r3,r4,r5}
	ldr 	r0,=(VARBASE+0x1FC)
	cmp 	r3,r0
	orrne 	r1,r1,#BAD_Rn
	ldrne 	r5,=rnVal
	strne 	r3,[r5]
	add	r0,r0,#4
	ldr 	r2,[r0]
	add	r0,r0,#8
	cmp 	r2,r0
	orrne 	r1,r1,#0x80
	ldrne	r0,=memVal
	strne	r2,[r0]

	ldr 	r3,=(VARBASE+0x208)
	mov 	r4,#5
	stmda 	r3!,{r2,r3,r4}
	ldr 	r0,=(VARBASE+0x1FC)
	cmp 	r3,r0
	orrne 	r1,r1,#BAD_Rn
	ldrne 	r5,=rnVal
	strne 	r3,[r5]
	add	r0,r0,#0xC
	ldr	r2,[r0]
	cmp	r4,r2
	orrne	r1,r1,#0x80
	ldrne	r0,=memVal
	strne	r2,[r0]
	@ldr 	r0,=(VARBASE+0x204)
	@ldr 	r2,[r0]
	@ldr	r3,=(VARBASE+0x1FC)
	@cmp 	r3,r2
	@orrne 	r1,r1,#0x80
	@ldrne	r0,=memVal
	@strne	r2,[r0]
	
	mov 	r2,#3
	orr 	r1,r1,#0x40000000
	ldr 	r0,=szSTM
	bl 	DrawResult