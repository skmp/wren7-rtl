@ crt.inc -- common head of every wren7 console ARM program (.include first, link with -Ttext=0).
@ 0x00-0x1C vectors: reset -> _start, every exception -> the done stub entry for that vector (stub.s, 0x1FE000).
@ 0x20-0x3C parameters, patched per job by the runner ("word" lines): ITER + P1..P7.
@ A program ends with `b STUB` (entry 0 = normal completion).
    .equ STUB, 0x1FE000
    .equ RES,  0x1FF000
    .text
    .global _start
vectors:
    b _start
    b STUB + 0x10
    b STUB + 0x20
    b STUB + 0x30
    b STUB + 0x40
    b STUB + 0x50
    b STUB + 0x60
    b STUB + 0x70
ITER: .word 1
P1:   .word 0
P2:   .word 0
P3:   .word 0
P4:   .word 0
P5:   .word 0
P6:   .word 0
P7:   .word 0
