@ k_hello.s -- runner bring-up: counts ITER down, returns r1 = P1 + 1.
    .include "crt.inc"
_start:
    ldr r12, ITER
    ldr r1, P1
    add r1, r1, #1
1:  subs r12, r12, #1
    bne 1b
    b STUB
