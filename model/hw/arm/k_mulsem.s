@ k_mulsem.s -- multiplier semantics probe.  For ITER input records (Rm, Rs, Rn, NZCV) at 0x20000 it stores 6 words
@ per record at 0x30000: MLA with Rd == Rm ("meaningless", 4.6.1), MULS result, CPSR after MULS, MLAS result,
@ CPSR after MLAS, MULS with Rd == Rm ("zero").  The Rd == Rm forms are .word (the assembler refuses them).
    .include "crt.inc"
_start:
    ldr r10, =0x20000
    ldr r11, =0x30000
    ldr r12, ITER
1:  ldmia r10!, {r4-r7}
    msr cpsr_f, r7
    mov r0, r4
    .word 0xE0206590        @ mla r0, r0, r5, r6
    msr cpsr_f, r7
    muls r1, r4, r5
    mrs r2, cpsr
    msr cpsr_f, r7
    mlas r3, r4, r5, r6
    mrs r8, cpsr
    mov r9, r4
    msr cpsr_f, r7
    .word 0xE0190599        @ muls r9, r9, r5
    stmia r11!, {r0-r3, r8, r9}
    subs r12, r12, #1
    bne 1b
    b STUB
    .ltorg
