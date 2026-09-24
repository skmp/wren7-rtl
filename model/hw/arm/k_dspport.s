@ k_dspport.s -- where does the DSP's step counter sit in the ARM's timeline?  A DSP program (image at 0x20000,
@ copied into MPRO by the prologue) writes TEMP in some steps; per the AICA manual those writes (DSPW) share the
@ CPU port slot of the step with CPU access (DMSP).  The sample-interval FIQ (phase-locked, as in k_blkphase.s)
@ runs a handler that waits N0 = 64 - P1 NOPs, then reads TEMP[0] (the probe), acknowledges and restarts the add
@ sled.  A probe that meets a DSP write should wait for the port: fewer adds per sample.  ITER = samples, r7 = adds.
    .include "crt.inc"
_start:
    ldr r1, =fiq                 @ FIQ vector: b fiq
    sub r1, r1, #0x24
    mov r1, r1, lsr #2
    orr r1, r1, #0xEA000000
    mov r2, #0x1C
    str r1, [r2]
    ldr r5, =0x20000             @ DSP program image (4 bytes per 16-bit MPRO word)
    ldr r6, =0x803400            @ MPRO
    mov r3, #64
1:  ldmia r5!, {r7-r14}
    stmia r6!, {r7-r14}
    subs r3, r3, #1
    bne 1b
    mrs r1, cpsr
    bic r2, r1, #0x1F
    orr r2, r2, #0x11            @ FIQ mode, F still set
    msr cpsr_c, r2
    ldr r8, =0x8028A4            @ SCIRE
    ldr r9, P1                   @ delay-sled entries skipped
    mov r10, #1
    mov r11, #0x400
    ldr r12, =0x802D04           @ M
    ldr r13, =sled              @ return point (low byte 0 = zero shift amount)
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
    ldr r2, =0x802D00            @ L, for the handler
    ldr r4, =0x804000            @ TEMP[0], the probe target
    ldr r0, ITER
    mov r7, #0
    mov r6, #0
    bic r1, r1, #0x40
    msr cpsr_c, r1
    b sled
    .ltorg
    .balign 256
sled:
    .rept 2048
    add r7, r7, #1
    .endr
    b STUB
fiq:
    add pc, pc, r9, lsl #2
    mov r0, r0                   @ not executed
    .rept 64
    mov r0, r0
    .endr
    ldr r3, [r4]                 @ THE PROBE: read TEMP[0] through the CPU port
    orr r10, r10, r10, lsl r13   @ undo the LDR's phase shift
    subs r0, r0, #1
    beq done
    ldr r3, [r2]                 @ acknowledge: read L
    orr r10, r10, r10, lsl r13
    str r11, [r8]                @ SCIRE = 0x400
    str r10, [r12]               @ M = 1
    orr r10, r10, r10, lsl r13
    subs pc, r13, #0             @ back to the sled, CPSR <- SPSR_fiq
done:
    b STUB
    .ltorg
