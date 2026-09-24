@ k_fiqcost_ldrl.s -- cost of one FIQ-handler operation on the console.  The sample-interval FIQ (level 1)
@ interrupts an add sled; the handler acknowledges (L read, SCIRE, M), then does 4 - P1 copies of
@ "ldr r3, [r2]; orr r10, r10, r10, lsl r13", and restarts the sled.  Adds per sample fall by copies x cost / 8.  ITER = samples, r7 = adds.
    .include "crt.inc"
_start:
    ldr r1, =fiq
    sub r1, r1, #0x24
    mov r1, r1, lsr #2
    orr r1, r1, #0xEA000000
    mov r2, #0x1C
    str r1, [r2]
    mrs r1, cpsr
    bic r2, r1, #0x1F
    orr r2, r2, #0x11
    msr cpsr_c, r2
    ldr r8, =0x8028A4            @ SCIRE (r8 - 8 = SCIEB)
    ldr r9, P1
    mov r9, r9, lsl #1   @ words to skip = P1 x 2
    mov r10, #1
    mov r11, #0x400
    ldr r12, =0x802D04           @ M
    ldr r13, =sled
    msr cpsr_c, r1
    ldr r4, =0x802800
    mov r6, #0x80
    str r6, [r4, #0xA8]          @ SCILV0: level 1 for bits 7-10
    mov r6, #0
    str r6, [r4, #0xAC]
    str r6, [r4, #0xB0]
    ldr r6, =0x7FF
    str r6, [r4, #0xA4]
    ldr r5, =0x802D00
    ldr r6, [r5]
    mov r6, #1
    str r6, [r5, #4]
    mov r6, #0x400
    str r6, [r4, #0x9C]          @ SCIEB
    ldr r2, =0x802D00            @ shared with the handler: L address
    ldr r0, ITER
    mov r7, #0
    bic r1, r1, #0x40
    msr cpsr_c, r1
    b sled
    .ltorg
    .balign 256
sled:
    .rept 1024
    add r7, r7, #1
    .endr
    b STUB
fiq:
    ldr r3, [r2]                 @ acknowledge: read L
    orr r10, r10, r10, lsl r13
    str r11, [r8]                @ SCIRE = 0x400
    str r10, [r12]               @ M = 1
    add pc, pc, r9, lsl #2
    mov r0, r0
    .rept 4
    ldr r3, [r2]
    orr r10, r10, r10, lsl r13
    .endr
    subs r0, r0, #1
    beq 2f
    subs pc, r13, #0
2:  b STUB
    .ltorg
