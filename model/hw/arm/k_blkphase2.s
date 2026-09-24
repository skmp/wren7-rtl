@ k_blkphase2.s -- k_blkphase.s with the probe moved out of the FIQ handler: the handler returns to sled2, which
@ probes after 2 adds; P1 now only sets the handler's length, i.e. where the main code starts.
@ (k_blkphase.s:) where are the two blocked phase-4 half slots relative to the sample interrupt?
@ The sample-interval interrupt (SCIPD bit 10) is taken as FIQ.  Between interrupts the core runs an add sled
@ (every fetch an 8-MCLK phase-0 access), so FIQ entry is phase-locked to the sample edge.  The handler burns a
@ delay sled of N0 = 64 - P1 NOPs, then two register-shift ORRs: the second one's fetch is the only phase-4 access
@ (the probe).  A probe on a blocked half slot costs the sample 8 MCLK (the probe +4, then +4 when the add sled
@ is flipped back to phase 0 by the next blocked slot) = one add less.  The handler restarts the add sled; after
@ ITER samples it ends in the stub with r7 = adds counted.
@ P7 = the value the handler writes to SCIRE.
@ P5 = M acknowledge writes skipped (4 - P5 done), P6 = address the handler reads before them (0x802D00 = L).
@ P2..P4 = SCILV0..2 written in the prologue; the registers found are recorded at 0x1FF100 (SCIEB SCIPD SCIRE
@ SCILV0 SCILV1 SCILV2 L).
@ FIQ bank: r8 SCIRE, r9 = P1 (sled entries skipped), r10 = 1, r11 = 0x400, r12 M (0x802D04), r13 = sled start
@ (its low byte 0 doubles as the zero shift amount), r0 (shared) = samples left.
    .include "crt.inc"
    .equ SCIEB, 0x80289C
    .equ SCIRE, 0x8028A4
    .equ MREG,  0x802D04
_start:
    ldr r1, =fiq                 @ FIQ vector: b fiq (a branch keeps the entry phase-0; LDR PC would not)
    sub r1, r1, #0x24
    mov r1, r1, lsr #2
    orr r1, r1, #0xEA000000
    mov r2, #0x1C
    str r1, [r2]
    mrs r1, cpsr
    bic r2, r1, #0x1F
    orr r2, r2, #0x11            @ FIQ mode, F still set
    msr cpsr_c, r2
    ldr r8, =SCIRE
    ldr r9, P1
    mov r10, #1
    ldr r11, P7                  @ SCIRE value written by the handler (0x400, or 0x7FF = every pending bit)
    ldr r12, =MREG
    ldr r13, =sled2
    msr cpsr_c, r1               @ back to SVC
    ldr r4, =0x802800            @ record the interrupt registers as found: DIAG = SCIEB SCIPD SCIRE SCILV0-2 L
    ldr r5, =0x1FF100
    ldr r6, [r4, #0x9C]
    str r6, [r5]
    ldr r6, [r4, #0xA0]
    str r6, [r5, #4]
    ldr r6, [r4, #0xA4]
    str r6, [r5, #8]
    ldr r6, [r4, #0xA8]
    str r6, [r5, #12]
    ldr r6, [r4, #0xAC]
    str r6, [r5, #16]
    ldr r6, [r4, #0xB0]
    str r6, [r5, #20]
    ldr r6, =0x802D00
    ldr r6, [r6]
    str r6, [r5, #24]
    ldr r6, P2                   @ SCILV0..2 = P2..P4
    str r6, [r4, #0xA8]
    ldr r6, P3
    str r6, [r4, #0xAC]
    ldr r6, P4
    str r6, [r4, #0xB0]
    ldr r4, =SCIEB
    ldr r5, =SCIRE
    ldr r6, =MREG
    ldr r0, =0x7FF
    str r0, [r5]                 @ clear every pending ARM interrupt
    mov r0, #1
    str r0, [r6]                 @ accept: re-arm the level latch
    mov r0, #0x400
    str r0, [r4]                 @ SCIEB: only the one-sample interval
    ldr r1, P5                   @ shared with the handler: M writes skipped (0 = all four)
    ldr r2, P6                   @ acknowledge read address
    ldr r0, ITER
    mov r7, #0
    mov r6, #0
    bic r1, r1, #0x40
    msr cpsr_c, r1               @ FIQ on
    b sled
    .ltorg
    .balign 256
sled2:                           @ main-code probe: 2 adds after the FIQ return, then the probe pair
    add r7, r7, #1
    add r7, r7, #1
    orr r3, r3, r3, lsl r6       @ r6 = 0 (set below): fetch at phase 0, I cycle -> phase 4
    orr r3, r3, r3, lsl r6       @ THE PROBE: fetch at phase 4, I cycle -> back to phase 0
sled:
    .rept 2048
    add r7, r7, #1
    .endr
    b STUB                       @ never reached: a sample is ~60 adds
fiq:
    add pc, pc, r9, lsl #2
    mov r0, r0                   @ not executed (pc + 8)
    .rept 64
    mov r0, r0
    .endr
    subs r0, r0, #1
    beq done
    str r11, [r8]                @ SCIRE (P7)
    ldr r3, [r2]                 @ acknowledge read: r2 = P6 (0x802D00 = L, the interrupt level; or a RAM word)
    orr r10, r10, r10, lsl r13   @ its I cycle undoes the LDR's, the handler stays phase 0
    add pc, pc, r1, lsl #2       @ r1 = P5: M writes skipped (4 - P5 are done)
    mov r0, r0                   @ not executed
    str r10, [r12]               @ M = 1 (KOS's ARM crt0 writes it 4 times)
    str r10, [r12]
    str r10, [r12]
    str r10, [r12]
    subs pc, r13, #0             @ restart the sled, CPSR <- SPSR_fiq
done:
    b STUB
    .ltorg
