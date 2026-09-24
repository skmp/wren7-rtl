@ k_dspmem_rsh.s -- ARM wave RAM timing while the DSP accesses wave RAM: the prologue copies a DSP program (image at
@ 0x20000) into MPRO, sets MADRS[0] = 0 and RBP = 0x1C0000 (RBL 0), then 32 x "add r0, r0, r1, lsl r2" per iteration, ITER times.
@ SH4-timed like tests/hw/timing; the epilogue saves MEMS0 at 0x1FF100 (r2 = 1 for the register shift, r3 = 0x100000).
    .include "crt.inc"
_start:
    ldr r5, =0x20000
    ldr r6, =0x803400            @ MPRO
    mov r4, #64
1:  ldmia r5!, {r7-r14}
    stmia r6!, {r7-r14}
    subs r4, r4, #1
    bne 1b
    ldr r4, =0x802800
    ldr r6, =0x380               @ RBP = 0x1C0000 / 2048, RBL 0
    str r6, [r4, #4]
    ldr r4, =0x803200
    mov r6, #0
    str r6, [r4]                 @ MADRS[0] = 0
    ldr r12, ITER
    ldr r1, =0x12345679
    mov r2, #1
    ldr r3, =0x100000
    b loop
    .ltorg
    .balign 64
loop:
    .rept 32
    add r0, r0, r1, lsl r2
    .endr
    subs r12, r12, #1
    bne loop
    ldr r4, =0x804400            @ epilogue: MEMS0 (bits 7:0, 23:8) -> 0x1FF100 for the collision tests
    ldr r5, =0x1FF100
    ldmia r4, {r6, r7}
    stmia r5, {r6, r7}
    b STUB
    .ltorg
