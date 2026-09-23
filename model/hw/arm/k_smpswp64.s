@ k_smpswp64.s -- ARM-side sample length with a poll loop of instructions the slot-grid model predicts exactly
@ (tests/hw/timing: add / swp / tst / beq = 64 MCLK per poll).  SWP reads SCIPD and writes back 0 (only a 1 in
@ bit 5 has an effect).  r8 = polls over ITER sample edges.  polls/sample = (S - E) / 64 with E = edge-path extra.
    .include "crt.inc"
_start:
    ldr r12, ITER
    ldr r4, =0x8028A0      @ SCIPD
    ldr r5, =0x8028A4      @ SCIRE
    mov r6, #0x400
    mov r8, #0
    mov r9, #0
    str r6, [r5]
1:  swp r0, r9, [r4]
    tst r0, r6
    beq 1b
    str r6, [r5]
    b 2f
    .balign 32
2:  add r8, r8, #1
    swp r0, r9, [r4]
    tst r0, r6
    beq 2b
    str r6, [r5]
    subs r12, r12, #1
    bne 2b
    b STUB
    .ltorg
