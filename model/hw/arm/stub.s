@ stub.s -- the done stub of every wren7 console job, linked at 0x1FE000 (STUB).
@ Entry k at STUB + 16*k (k = 0 normal completion, 1..7 = exception vector 0x04..0x1C) saves the registers of the
@ current mode to the result block RES and raises the ARM -> SH4 interrupt (MCIPD bit 5, SCPU), then spins.
@ RES: +0 'DONE' (0x454E4F44), +4 k, +8 r0..r15 (r15 = stub address), +0x48 CPSR, +0x4C SPSR, +0x50 r0 before entry.
    .equ RES, 0x1FF000
    .equ MCIPD, 0x8028B8
    .text
    .global _stub
_stub:
    .irp k, 0, 1, 2, 3, 4, 5, 6, 7
    str r0, save_r0
    mov r0, #\k
    b common
    .word 0
    .endr
common:
    str r0, save_k
    mrs r0, cpsr
    str r0, save_cpsr
    ldr r0, =RES + 12
    stmia r0, {r1-r15}
    ldr r1, save_r0
    str r1, [r0, #-4]
    ldr r0, =RES
    ldr r1, save_k
    str r1, [r0, #4]
    ldr r1, save_cpsr
    str r1, [r0, #0x48]
    mrs r1, spsr
    str r1, [r0, #0x4C]
    ldr r1, =0x454E4F44
    str r1, [r0]
    ldr r0, =MCIPD
    mov r1, #0x20
    str r1, [r0]
spin:
    b spin
save_r0:   .word 0
save_k:    .word 0
save_cpsr: .word 0
    .ltorg
