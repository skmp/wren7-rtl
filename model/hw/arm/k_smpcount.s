@ k_smpcount.s -- ARM-side sample clock: count polls of SCIPD bit 10 (one-sample interval) over ITER sample edges.
@ r8 = number of polls.  Poll loop = add, ldr (AICA register), tst, beq taken; the model gives its cost in MCLK.
    .include "crt.inc"
_start:
    ldr r12, ITER
    ldr r4, =0x8028A0      @ SCIPD
    ldr r5, =0x8028A4      @ SCIRE
    mov r6, #0x400
    mov r8, #0
    str r6, [r5]
1:  ldr r0, [r4]           @ first edge
    tst r0, r6
    beq 1b
    str r6, [r5]
2:  add r8, r8, #1
    ldr r0, [r4]
    tst r0, r6
    beq 2b
    str r6, [r5]
    subs r12, r12, #1
    bne 2b
    b STUB
    .ltorg
