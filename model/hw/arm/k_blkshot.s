@ k_blkshot.s -- single-shot blocked-slot probe in the FIQ handler.  After a 4-frame pre-sled with no interrupt
@ source enabled (the core ends on its own phase-0 slots), the one-sample interrupt is enabled phase-neutrally, so
@ the first FIQ comes on a sample edge, phase-locked.  Its handler waits N0 = 64 - P1 NOPs, makes one phase-4
@ probe fetch, acknowledges (L read, SCIRE, M) and returns to the add sled with r7 = 0; the second FIQ ends the job:
@ r7 = adds between the two.  A probe on a blocked half slot shifts everything after it by 4 MCLK.
    .include "crt.inc"
_start:
    ldr r1, =fiq                 @ FIQ vector: b fiq (phase-neutral)
    sub r1, r1, #0x24
    mov r1, r1, lsr #2
    orr r1, r1, #0xEA000000
    mov r2, #0x1C
    str r1, [r2]
    mrs r1, cpsr
    bic r2, r1, #0x1F
    orr r2, r2, #0x11            @ FIQ mode, F still set
    msr cpsr_c, r2
    ldr r8, =0x8028A4            @ SCIRE
    ldr r9, P1                   @ delay-sled entries skipped
    mov r10, #1
    mov r11, #0x400
    ldr r12, =0x802D04           @ M
    ldr r13, =sled              @ where the handler returns (low byte 0: also the zero shift amount)
    msr cpsr_c, r1               @ back to SVC
    ldr r4, =0x802800
    mov r6, #0
    str r6, [r4, #0x9C]          @ SCIEB = 0: nothing may latch during the pre-sled
    ldr r6, =0x7FF
    str r6, [r4, #0xA4]          @ SCIRE: clear everything
    ldr r5, =0x802D00
    ldr r6, [r5]                 @ acknowledge (L read) and re-arm (M) anything stale
    mov r6, #1
    str r6, [r5, #4]
    mov r6, #0x80
    str r6, [r4, #0xA8]          @ SCILV0: level 1 for bits 7-10
    mov r6, #0
    str r6, [r4, #0xAC]
    str r6, [r4, #0xB0]
    ldr r2, =0x802D00            @ L address for the handler
    mov r0, #2                   @ FIQ entries: the probe one, then the stop
    mov r7, #0
    mov r6, #0x400
    bic r1, r1, #0x40            @ CPSR value with F clear
    .rept 256
    add r3, r3, #1               @ pre-sled, 4 frames: a phase-4 core meets a blocked slot and drops to phase 0
    .endr
    str r6, [r4, #0xA4]          @ SCIRE = 0x400 (phase-neutral: fetch + write, no I cycle)
    str r6, [r4, #0x9C]          @ SCIEB = 0x400
    msr cpsr_c, r1               @ FIQ on: the first request is the next sample edge
    b sled
    .ltorg
    .balign 256
sled:
    .rept 2048
    add r7, r7, #1
    .endr
    b STUB
fiq:
    subs r0, r0, #1
    beq done
    add pc, pc, r9, lsl #2
    mov r0, r0                   @ not executed
    .rept 64
    mov r0, r0
    .endr
    orr r10, r10, r10, lsl r13   @ fetch at phase 0, I cycle -> next fetch at phase 4
    orr r10, r10, r10, lsl r13   @ THE PROBE: fetch at phase 4, I cycle -> back to phase 0
    ldr r3, [r2]                 @ acknowledge: read L (1 MCLK; its I cycle moves to phase 4)
    orr r10, r10, r10, lsl r13   @ back to phase 0
    str r11, [r8]                @ SCIRE = 0x400
    str r10, [r12]               @ M = 1 (1 MCLK: moves to phase 4)
    orr r10, r10, r10, lsl r13   @ back to phase 0
    mov r7, #0
    subs pc, r13, #0             @ CPSR <- SPSR_fiq
done:
    b STUB
    .ltorg
