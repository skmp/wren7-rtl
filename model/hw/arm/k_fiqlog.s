@ k_fiqlog.s -- FIQ bring-up log.  Enables the one-sample interrupt (SCIEB = 0x400) with SCILV0..2 = P2..P4 and
@ logs, per FIQ entry, 8 words at 0x1F0000 + 32*n: L (0x2D00), SCIPD, 0x2808 (bus request), the adds counted so far,
@ then L, SCIPD and 0x2808 again after SCIRE = P7 and 4 M writes, and the entry number.  Stops after ITER entries.
    .include "crt.inc"
_start:
    ldr r1, =fiq
    sub r1, r1, #0x24
    mov r1, r1, lsr #2
    orr r1, r1, #0xEA000000
    mov r2, #0x1C
    str r1, [r2]
    ldr r4, =0x802800
    ldr r6, P2
    str r6, [r4, #0xA8]
    ldr r6, P3
    str r6, [r4, #0xAC]
    ldr r6, P4
    str r6, [r4, #0xB0]
    ldr r6, =0x7FF
    str r6, [r4, #0xA4]          @ SCIRE: clear all
    ldr r5, =0x802D04
    mov r6, #1
    str r6, [r5]
    str r6, [r5]
    str r6, [r5]
    str r6, [r5]
    mov r6, #0x400
    str r6, [r4, #0x9C]          @ SCIEB
    ldr r0, ITER
    ldr r3, =0x1F0000            @ log pointer (shared with the handler)
    mov r7, #0
    mrs r1, cpsr
    bic r1, r1, #0x40
    msr cpsr_c, r1
1:  add r7, r7, #1
    b 1b
fiq:
    ldr r8, =0x802800
    ldr r9, =0x802D00
    ldr r10, [r9]
    ldr r11, [r8, #0xA0]
    ldr r12, [r8, #0x08]
    stmia r3!, {r7, r10, r11, r12}
    ldr r10, P7
    str r10, [r8, #0xA4]         @ SCIRE
    mov r10, #1
    str r10, [r9, #4]            @ M x4
    str r10, [r9, #4]
    str r10, [r9, #4]
    str r10, [r9, #4]
    ldr r10, [r9]
    ldr r11, [r8, #0xA0]
    ldr r12, [r8, #0x08]
    stmia r3!, {r0, r10, r11, r12}
    subs r0, r0, #1
    beq 2f
    subs pc, lr, #4
2:  b STUB
    .ltorg
