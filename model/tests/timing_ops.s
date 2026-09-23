@ timing_ops.s -- source of the instruction encodings used by tools/timing_check.cpp (assembled with
@ arm-eabi-as -mcpu=arm7; the checker places each one at PC = 0x10).  Branches: b/bl 0x200 from 0x10 = ea00007a / eb00007a;
@ add r1,pc,r3,lsl r4 = e08f1413; stmia r2!,{r2,r3} = e8a2000c.
.org 0x10
t_mov:  mov r1, #1
t_addrs: add r1, r2, r3, lsl r4
t_movpc: mov pc, r2
t_addpcrs: add pc, r2, r3, lsl r4
t_mul:  mul r1, r2, r3
t_mla:  mla r1, r2, r3, r4
t_ldr:  ldr r1, [r2]
t_ldrb: ldrb r1, [r2, #1]
t_ldrpc: ldr pc, [r2]
t_str:  str r1, [r2]
t_ldm1: ldmia r2, {r1}
t_ldm1pc: ldmia r2, {pc}
t_ldm3: ldmia r2, {r1, r3, r4}
t_ldm3pc: ldmia r2, {r1, r3, pc}
t_stm1: stmia r2, {r1}
t_stm3: stmia r2, {r1, r3, r4}
t_swp:  swp r1, r3, [r2]
t_swpb: swpb r1, r3, [r2]
t_swi:  swi 0
t_cdp:  cdp p1, 0, c0, c0, c0
t_mrs:  mrs r1, cpsr
t_msr:  msr cpsr_flg, r1
t_moveq: moveq r1, #1
t_ldc:  ldc p1, c0, [r2]
t_mcr:  mcr p1, 0, r1, c0, c0
t_mrc:  mrc p1, 0, r1, c0, c0
t_stmpc: stmia r2, {r1, pc}
t_strpc: str pc, [r2]
t_movrpc: mov r1, pc
t_stmdb_wb: stmdb r2!, {r1, r2, r3}
t_ldrwb: ldr r2, [r2, #4]!
