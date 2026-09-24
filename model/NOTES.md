# wren7 ARM7DI model -- notes

The AICA's sound CPU is an ARM7DI (ARMv3: 32-bit and 26-bit modes, no Thumb, no halfword/signed transfers, no long
multiply).  This model is the reference for the later RTL.  Status (2026-09-23): built from the data sheet; console
measurements started (see "Console measurements": bus timing per access class, multiplier, reset behaviour).  Everything below is either data sheet (section/table given), minicast, or an
explicit model choice marked **[H#]** = open hardware question.

The timing results are collected as an implementation reference in [TIMING.md](TIMING.md).

**Integration** ([../INTEGRATION.md](../INTEGRATION.md)): caique-rtl integrates wren7 — the ARM sits on the AICA's
bus, which caique owns.  **Co-simulation and every combined test (ARM + AICA) are caique's domain**
(`caique-rtl/rtl/v1/tb/`, caique console cases); wren7 keeps the core, the ARM-side console measurements and
`DcArmBus` (a measured timing reference and interrupt stand-in, not a second AICA model).  AICA-internal findings
made with ARM jobs are recorded in caique's `model/NOTES.md`.  Interfaces caique relies on (`Arm7DI::bus_cycle`,
`Arm7Bus`, `DcArmBus` members, `tools/hwjob.h`) change only together with caique; after changing them run caique's
`make -C rtl/v1 arb` and `rtl/v1/tb/armjob_all.sh`.

Sources:
- `docs/DDI0027D_7di_ds.pdf` (ARM7DI data sheet, 1997).  Text dump for grepping: `agents/arm7di.txt`
  (`pdftotext -layout`).  Chapter 9 (per-cycle bus tables) is the timing spec the model follows.
- minicast `libswirl/hw/arm7/SoundCPU.cpp` (memory map, e68k interrupt glue), `hw/aica/aica_mmio.cpp` (ARM-side
  interrupt registers), `gpl/vba-arm/arm-new.h` (instruction interpreter).
- `vendor/arm7di-tests-dreamcast`: armwrestler's ARM tests split into 45 standalone programs by `test.py`.
  `arm-eabi-as -mcpu=arm7` + `ld -Ttext 0 -N` + `objcopy -O binary` from `/opt/toolchains/dc-dca3/arm-eabi/bin`
  rebuilds all 45 `bins/` byte-identically, so new console tests can use the same toolchain.

## Model structure

- `src/arm7di.{h,cpp}`: the core.  The interface is `bus_cycle()`: one call = one bus cycle, so the caller owns
  time and can run other masters between any two cycles of an instruction.  Each instruction (or exception entry)
  is a small cycle sequencer (`seq_`, cycle index `k_`, in-flight latches `x_`) producing the exact bus-cycle
  sequence of its chapter 9 table; `at_boundary()` is true between instructions, where `Arm7Bus::tick()` runs
  and interrupts are taken.  Every cycle goes through `Arm7Bus::cycle()` with what the pins carry:
  address, cycle type N/S/I/C, nRW, nBW, nOPC, LOCK, nTRANS, write data.  The memory system returns the data, the
  nWAIT stretch and ABORT.  **No latency table anywhere**: an instruction's time is its cycles plus the waits the
  bus adds, so bus timing measured later goes into the bus model, not into the core.
- Cycle type: nMREQ/SEQ are output one cycle ahead (Table 6, 9.0), so each cycle's type is the "next" value the
  previous cycle announced; the core keeps it in `next_type_`.  An instruction's cycle 1 therefore gets its type from
  the previous instruction's last row, the convention behind the "incremental" counts in Table 30.
- Pipeline: execute slot, decode slot, `pc_` = the address incrementer = executing address + 8.  Cycle 1 of every
  instruction fetches `pc_` and increments it; register reads before that see R15 = pc+8, reads after it see pc+12.
  That single rule gives all the documented R15 offsets: +8 normally, +12 for register-specified shifts (4.4.5),
  STR Rd = R15 (4.7.4) and STM of R15 (4.8.1).  A PC write refills with fetch(target) N and fetch(target+4) S.
- Prefetch aborts ride with the fetched word; the abort is taken only if the word reaches execute (3.4.3).
- Exceptions are checked at every instruction boundary in the 3.4.7 priority order: data abort > FIQ > IRQ >
  prefetch abort; undefined/SWI are part of execution.  Entry sequence (Table 22): fetch pc+8 (discarded), fetch
  vector (N), fetch vector+4 (S).  R14 = (address in execute) + 4 gives every documented return address.  The
  interrupt inputs go through an optional synchroniser (`irq_sync_stages`, 0 = ISYNC high) clocked per core cycle.
- Physical register file of 31 GPRs + PC (3.3).  Mode changes just switch the bank view, as in hardware.
- `src/dc_arm_map.{h,cpp}`: the Dreamcast sound-system bus (below).
- Traces: `bus_trace` (one line per cycle: t, type, addr, R/W, b/w, O(pcode), L(ock), u(ser)/p(riv), data, waits),
  `insn_trace`.  Intended as the golden trace for cycle-by-cycle comparison with the RTL.

## Instruction cycle sequences (verified against the tables by tools/timing_check, 54 cases)

pc = instruction address; types are for cycles 1..k and then the type announced for the next instruction.

| instruction | cycles | next |
|---|---|---|
| unexecuted (Table 29) | S pc+8 | S |
| data op | S pc+8 | S |
| data op, shift by Rs | S pc+8, I pc+12 | S |
| data op to PC | S pc+8, N alu, S alu+4 | S (the shift-by-Rs form has I pc+12 before the N) |
| MRS/MSR | S pc+8 | S |
| MUL/MLA | S pc+8, m x I pc+12 | S |
| LDR | S pc+8, N data, I pc+12 | S (to PC: + N pc', S pc'+4) |
| STR | S pc+8, N data(W) | N |
| LDM n regs | S pc+8, N, (n-1) S data, I pc+12 | S (with PC: + N pc', S pc'+4) |
| STM n regs | S pc+8, N, (n-1) S data(W) | N |
| SWP | S pc+8, N read(L), N write(L), I pc+12 | S |
| B/BL, SWI, exception entry | S pc+8, N target, S target+4 | S |
| undefined / coprocessor absent (Table 28) | S pc+8, I pc+8, N 0x04, S 0x08 | S |

- Multiply (9.3, 4.6.3): the model implements the actual 2-bit Booth datapath.  Cycle 1 sets Rd := Rn (MLA) or 0 and
  loads Rs into the Booth shifter.  Then one internal cycle per step adds, subtracts or passes Rm << 2k (or 2k+1),
  with Rm re-read each step.  Step count m is the smallest m >= 1 with Rs >> (2m-1) == 0, capped at 16.  This equals
  the data sheet's ranges (timing_check derives m independently from the 4.6.3 text).  The early termination is
  unsigned: any Rs >= 2^29, negative values included, takes 16.  Rd == Rm then gives the documented zero for MUL,
  and garbage for MLA (4.6.1).
- The I cycles carry the address the tables show (pc+12; pc+8 for the undefined trap's handshake cycle), so a memory
  controller can merge an I with the following S into one N access (5.1, 9.2, 9.4).

## Data sheet semantics as implemented

- Shifter: LSL #0 keeps C; LSR/ASR #0 encode #32; ROR #0 = RRX; register amounts use Rs[7:0] with the 32 / >32
  rules of 4.4.2 (ROR by n > 32 = ROR by n mod 32, carry = bit 31 when n mod 32 == 0).
- Logical ops: C = shifter carry, V kept.  Arithmetic: C = ALU carry (= NOT borrow), V = signed overflow.
- Rd = R15 with S: CPSR := SPSR_mode (4.4.4).  TST/TEQ/CMP/CMN with Rd = R15 ("TEQP" form, 4.4.6): SPSR -> CPSR if
  privileged, nothing in user mode, no flag update.
- PSR: only N Z C V I F M[4:0] are stored; the reserved bits read 0 **[H1]**.  MSR in user mode writes only the flags.
- LDR misaligned word: rotated so the addressed byte lands in bits 7:0; LDRB takes byte lane addr[1:0]; STRB
  replicates the byte across D[31:0]; STR drives the unrotated word with the address as given (4.7.3).
- Base write-back of LDR/STR happens in cycle 2, so a load into the base register wins; STR Rd == Rn stores the old
  value (the store data is read in cycle 2, before the write-back).  Post-indexed W = 1 (LDRT/STRT) drives nTRANS
  low for the data cycle only.
- LDM/STM: write-back at the end of cycle 2 (4.8.6).  An STM with the base first in the list stores the old base,
  otherwise the new one; an LDM always ends with the loaded value.  S with R15 in an LDM restores the CPSR with the
  PC; otherwise S transfers the user bank (4.8.4).  Low address bits are passed through, not rotated (4.8.3).
  Aborts: registers stop being written, the base is restored (to the written-back value if W), and the trap is taken
  after the instruction.
- SWP: Rm is read after the load, so Rd == Rm works; the write happens even if the read aborted; Rd is left alone
  on an abort.
- Conditions: NV (1111) = never (4.2) **[H2]**.
- Coprocessors: none on the AICA bus (assumed), so CDP/LDC/STC/MCR/MRC all take the undefined trap with Table 28
  timing.  Undefined space (Figure 32): bits 27:25 = 011 with bit 4 = 1.

## Open hardware questions -- model choices (counted in `stats.undoc`, noted in `insn_trace`)

- **[H1]** Reserved PSR bits: stored or not (MRS after MSR 0x0FFFFF20)?  The model does not store them.
- **[H2]** NV condition: never, or executed?
- **[H3]** Bits 7 and 4 set in the data-processing space that don't match MUL/SWP (ARMv4 halfword and long-multiply
  encodings, "multiply with bit 6 set", 4.1 note): documented as *not* the undefined trap.  Model: bit 24 set ->
  SWP datapath, else MUL datapath, bits 6:5 ignored.  Pure guess; needs an encoding sweep.
- **[H4] resolved on the console**: MULS/MLAS C = the shifter carry of the last Booth step (see Console
  measurements).
- **[H5]** Empty LDM/STM list: model = ARM7TDMI behaviour (R15 transferred, base +/- 0x40).
- **[H6]** SPSR access in user mode (MRS/MSR SPSR, data-op-to-PC with S, LDM^ with PC): model reads CPSR and ignores
  writes / keeps the CPSR.
- **[H7]** Illegal mode values (including the 26-bit modes, M4 = 0, which PROG32 = 1 may still allow; Appendix 12):
  the model banks them as user mode.  The data sheet says "unrecoverable".  Whether the AICA ties PROG32/DATA32 high
  is itself unverified.
- **[H8]** R15 where the data sheet says "shall not": the Rs of a register shift (model reads pc+8); MUL/MLA and SWP
  operands; LDR/STR offset register (pc+8); write-back to R15 (ignored); MRS Rd = R15 (treated as a PC write);
  LDM/STM base R15.
- **[H9]** Ignored "should be zero" fields: Rd of TST/TEQ/CMP/CMN, Rn of MOV/MVN and MUL.  The model ignores them
  (qemu UNDEFs on them, so qemu_diff keeps them zero).
- **[H10]** MSR field decode: the model writes the flags on bit 19 and the control bits on bit 16 (the two
  documented encodings are 1001 / 1000 in bits 19:16).  Other values and MSR-immediate to the control bits are
  unknown.
- **[H11]** User-bank LDM/STM with write-back ("shall not"): the model writes back to the current bank's base.  The
  vendor LDM_5..8 tests do this in IRQ mode and expect the write-back.
- **[H12]** Reset: register contents at power-on (model: 0) and across a reset (the model keeps them, per 3.5 only
  R14_svc/SPSR_svc/CPSR change -- the console agrees that registers survive ARMRST, see Console measurements).  Dummy-fetch cycle types while nRESET is low.  Minicast loads GBA register values
  (VBA leftovers: R13 = 0x03007F00 ...), which are certainly not the AICA's.
- **[H13]** Interrupts: FIQ synchroniser depth (ISYNC), the exact cycle at which the boundary check samples, and
  how long the AICA's L/M handshake takes to move nFIQ.  The model samples at the end of the last cycle, with no
  synchroniser.

## Dreamcast memory map (from minicast; unverified)

ARM view, A[31:24] ignored (`addr &= 0x00FFFFFF`):

| range | what |
|---|---|
| 0x000000-0x7FFFFF | wave RAM, 2 MB, mirrored (`& 0x1FFFFF`) |
| 0x800000-0xFFFFFF | AICA registers, `& 0x7FFF`: channel data 0x0000-0x27FF, common 0x2800-0x2FFF, DSP 0x3000-0x7FFF |
| 0x802D00 | L: latched FIQ level (read only) |
| 0x802D04 | M: write bit 0 = interrupt accept (re-arms the latch) |

- FIQ: SCIEB (0x289C) & SCIPD (0x28A0), pending bits 0-10.  The lowest set bit picks L from SCILV0-2
  (0x28A8/AC/B0; bits >= 7 share SCILV bit 7).  SCIPD bit 5 (SCPU) is settable by writing; SCIRE (0x28A4)
  clears.  The handshake is measured on the console (see "Interrupts"); minicast's version (any request, M write
  releases) is wrong.  nIRQ is not connected in minicast.
- Registers are 16 bits in 32-bit slots (caique).  How ARM word/byte accesses map onto them is unknown: the model
  returns the 16-bit value zero-extended, byte lanes 2/3 read 0 and drop writes.  `AicaRegs` is the hook for
  caique's AICA model.
- Misaligned word stores: the model's wave RAM ignores A[1:0] (stores the aligned word).  Unverified.

## Dreamcast bus timing (all unmeasured)

- Clock: the ARM7DI clock on the AICA is commonly given as 22.5792 MHz (= 512 x 44.1 kHz = 2/3 x 33.8688 MHz).
  Not verified here.
- minicast runs the ARM at 256 interpreter "ticks" per 44.1 kHz sample (`arm_sh4_bias = 2`), i.e. half of 512, as a
  stand-in for memory contention.  Its vba-arm timing is GBA-derived, not ARM7DI.
- From memory (not re-read here): MAME's Dreamcast driver clocks the ARM at (33.8688 MHz x 2/3) / 8 with a comment
  that the ARM gets one bus cycle out of every 8.  If true, wave RAM accesses stall on an 8-cycle arbitration slot
  while I cycles run at full speed.
- The model carries all of this in `DcWaits` (nWAIT per access: wave RAM read/write x N/S, register read/write,
  internal).  **Default: zero waits.**  First console timing work: pin these from loops that are fetch-only,
  load-heavy, store-heavy and internal-heavy (MUL with large Rs).

## Console measurements (tests/hw/, 2026-09-23)

Method: `hw/runner.c` (KOS) runs a job file (`tests/hw/<suite>/jobs.txt`; `./run_hw.sh SUITE`).  For each job, ARMRST
is held while images and patches are loaded over G2.  Then MCIEB = SCPU only, t0 = PRFC0 (SH4 CPU cycles), ARMRST is
released, and the SH4 polls SB_ISTEXT bit 1 (Holly, so the SH4 stays off the AICA bus) until the ARM's done stub
(`hw/arm/stub.s`, 0x1FE000) writes MCIPD.SCPU.  The stub saves the registers to the result block at 0x1FF000.
`tools/hwjob_model` runs the same job file on the model; `tools/hw_suite` / `tools/hw_timing` compare.  Every
SH4-side wait is bounded (an unbounded one hung the console once and needed a power cycle).

### Semantics

- **Vendor suite** (tests/hw/vendor): 45/45; console and model agree on r1 and the exit path.  The 0xDEADBEEF end
  word is patched to a branch to the stub.
- **qemu_diff batches** (tests/hw/qemu_diff): 4000/4000 cases; console = qemu (sa1100) = model, every register,
  flag and scratch word.
- **Multiplier** (tests/hw/mulsem, `hw/arm/k_mulsem.s`, 1024 random records): MUL/MLA results, MUL with Rd == Rm
  (zero), and **MLA with Rd == Rm ("meaningless") are bit-exact** with the model's 2-bit Booth datapath, which
  re-reads Rm = Rd after every step.  **MULS/MLAS C = the barrel shifter's carry out on the last Booth step**
  (Rm << 2k or 2k+1; LSL #0 passes the old C); N, Z as documented, V kept.  1024/1024; the model's default
  (`MULC_SHIFTER_LAST`).  [H4 resolved]
- **Registers survive ARMRST** (tests/hw/bringup): a kernel started with r0 = the value the previous run's stub
  left.  R14_svc = the stub's spin address and SPSR_svc = the previous CPSR, as 3.5 describes.  The other registers
  held power-on/BIOS leftovers.  [H12, partly]
- STM of R15 stores the STM's address + 12 (the stub's own result block).

### Clocks

- **The ARM7DI core clock is 22.5792 MHz = 512 x 44.1 kHz**, the AICA bus clock (MAME: 33.8688 MHz x 2 / 3).
  - Locked to the sample clock: SWP-only poll loops the model predicts exactly (`hw/arm/k_smpswp64/72.s`) see exactly
    7.50000 and 6.66667 polls per sample, i.e. 512 MCLK per sample.
  - Internal cycles run at the full clock.  MUL costs 8 + 4*ceil(m/4) MCLK with m = the data sheet's 2-bit Booth
    count.  With I = 2 MCLK, m = 3 would already cost 16 (measured 12).  A slower core with a faster multiplier
    would need the MLA Rd == Rm results to differ, and they match the 2-bit datapath.
- The SH4 runs at 199.49 MHz on this console (4523.55 SH4 cycles per sample with interrupts off).  With interrupts
  on, the SH4-side sample count misses about 1 edge in 470 (handlers longer than a sample).
- minicast's `arm_sh4_bias = 2` (256 "ticks" per sample) is a rough stand-in for this; MAME clocks the whole core
  at 2.8224 MHz, which is right for memory cycles and 8x too slow for internal ones.

### Bus timing (tests/hw/timing, `tools/hw_timing gen|predict|check|fit|fitset`)

Model: **`DcWaits::dreamcast()`** (slot-grid mode of `dc_arm_map`), now the default of `wren7run`:
1. A memory cycle (N or S, read or write, byte or word, wave RAM or AICA register, any address) may start only on a
   **4-MCLK grid point** and lasts **8 MCLK** (7 nWAIT).  No page mode and no N/S difference: sequential,
   repeated or 512 KB-apart accesses all cost the same.
2. The locked write of a **SWP/SWPB lasts 4** (SWP = 24 MCLK in all).
3. Internal (I) cycles take **1 MCLK**; the next memory cycle waits for the next grid point.
4. In every 512-MCLK sample frame **two slots belong to another wave RAM user: DSP steps 109 and 111**, i.e. two
   adjacent odd-step (phase-4) slots 36 and 28 MCLK before the sample edge (first fitted from these kernels as a
   pair 8 MCLK apart, then placed with the DSP: see "DSP step clock and wave RAM slots").  An ARM wave RAM access
   that would start there waits for the next step; AICA register accesses are unaffected.
With all channels off (every channel's monitor reads 0x7FFF) and the DSP program zeroed, this predicts all 105
kernels to within 0.1 MCLK per iteration (84 fit variants: total squared error 0.011 MCLK^2).  Validation used
kernels not in the fit, predicted before the console run (`tests/hw/timing/PREDICT.txt`).  16 random instruction
mixes (ALU, register shifts, MUL/MLA, LDR/STR[B], LDM/STM, SWP[B], AICA register reads/writes, branches,
unexecuted instructions) all landed within 0.06 MCLK of 530-730-MCLK loops.  So did SWI + `movs pc, lr`, the
undefined trap + return, LDR PC, a data op to PC and LDM {PC}.

### Interrupts (tests/hw/fiqdiag, fiqcost, blkphase, blkshot; `hw/arm/k_fiqlog.s`, `k_fiqcost_*.s`, `k_blk*.s`)

- **The one-sample interval (SCIPD bit 10) reaches FIQ only with a non-zero level.**  With SCILV0-2 = 0 the
  pending, enabled request never interrupts; SCILV0 = 0x80 (level 1) or 0xFF/0xFF/0xFF (level 7) do.  Level 0
  means "no interrupt", as on the 68000 this controller was designed for.
- **68000-style handshake**: the request is latched and drives nFIQ.  **Reading L (0x802D00) is the acknowledge**
  and releases nFIQ; the same handler with a RAM read of identical timing re-enters forever.  **Writing M
  (0x802D04) = 1 ends the service**: with the L read and no M write the FIQ is released and never comes back.  One M
  write is enough (KOS's crt0 writes four).
- SCILV0-2 read back 0 (write-only).  L reads the level latched for the last request (1 after a level-1 job, 7
  after a level-7 one).
- **L and M are local to the ARM interface**: LDR L or M = fetch + a 1-MCLK read (no slot), STR M = fetch + 1 MCLK;
  every other AICA register access takes the 8-MCLK slot (`k_fiqcost_*`: NOP 8, STR SCIRE/SCIEB 16, LDR SCIEB + ORR
  32, LDR RAM + ORR 32, LDR L/M + ORR 24, STR M 12, console = model on all 8 ops).
- The model (`dc_arm_map`: sample clock every 512 MCLK, level gating, L-read acknowledge, M re-arm, local L/M)
  reproduces all 66 phase-locked FIQ sweep points (tests/hw/blkphase: adds per sample, error 0.009).
- **ARM reset release is frame-synchronous**: single-shot jobs (one FIQ-timed probe, then a count to the next
  FIQ) give the same count in 3 separate runs at 63 of 66 points, the rest +-1.  So the phase between the ARM's
  clock count from reset and the sample frame is a constant of the console.  This also explains why the
  slowed timing kernels reproduced to a few MCLK.

### DSP step clock and wave RAM slots (tests/hw/dspport, dspmem, dspmem1, dspmem1r; `hw_suite check ...`)

The AICA manual's buffer-timing table: each DSP step is two slots, the first for the DSP's own read (DSPR), the
second the CPU port (DMSP), which TEMP and EFREG share with DSP writes (DSPW).  Everything below is measured against
it and against DSP programs written by the ARM into MPRO (`hw/arm/k_dspport*.s`, `k_dspmem_*.s`; programs in
`tests/hw/dsp*/mpro_*.bin`).
- **One DSP step = 4 MCLK** (128 steps = 512 MCLK = one sample), and the step boundaries are the 4-MCLK grid
  every ARM memory cycle starts on.  A block of 16 TEMP-writing steps (TWT) delays an FIQ-locked ARM read of TEMP
  by exactly the rest of the block (a 64-MCLK plateau); moving the block by 32 steps moves it by 128 MCLK.
  TEMP accesses outside writing steps cost the normal 8.  (EFREG is modelled from the manual, not measured.)
- **Wave RAM has one slot per DSP step.**  An ARM access may start only at the boundary of a step whose slot is
  free and then takes 8 MCLK; the next step's slot stays usable by others (DSP reads at odd steps do not slow a
  phase-0 stream).  **Every DSP MRD or MWT takes the slot of its own step**: reads and writes, odd or even steps,
  all the same (single access at each of the 128 steps x read/write x nop/rsh kernels: 512/512).
- **The "blocked pair" is another user holding the slots of DSP steps 109 and 111 in every sample** (the only
  odd steps where a single DSP access slows an even-step ARM stream).  When the DSP holds one of them it moves one
  step earlier or later, the two staying >= 2 steps apart, earliest first (DSP at 109 -> 108/111, at 111 ->
  109/112, every odd step -> 108/110: 44/44 multi-access programs).  Identity unknown (SDRAM refresh would fit:
  2 per sample = 88,200/s).
- **Phase, unique across 478 FIQ-timed points** (dspport 330, blkshot 66, blkphase 66, fiqcost 16; 8 of 1584
  combinations fit, all the same except for an irrelevant port offset; next best 21 misses): **DSP step 0 starts
  exactly 40 MCLK (10 steps) after the one-sample interrupt edge; the edge lies on a step boundary (step 118)
  and is recognised at the end of the instruction in which it falls, with no extra latency.**  So the other
  user's slots are 36 and 28 MCLK before each sample edge.
- Why an idle ARM stream never sees them: it settles on the even steps; the pair is on odd steps.  A phase-4
  (odd) access is delayed by 4 only when it meets one of them.
- Model: `DcWaits::dreamcast()` (`dsp_slots`), `DcArmBus::dsp_phase` = 40, `sample_phase` = 0, `edge_latency` = 0,
  `fixed_steps` = {109, 111}; slots and TEMP/EFREG port conflicts are decoded from MPRO as the CPU wrote it (the
  DSP itself is not executed by this model).

### Contention: sound channels and SH4 traffic (tests/hw/sgc, sgcadp, sh4load, combo, collide, collide2)

- **A keyed-on channel K takes one wave RAM slot per sample, at DSP step (2K - 14) mod 128**, always an even step:
  channel 7 at step 0, channel 0 at 114, channel 62 at 110 (between the fixed pair).  Each of the 64 channels alone
  reproduces, point for point, the single DSP access map at that step: an even-step loop always loses 8.6 U per
  iteration, and the register-shift loop shows that step's signature (channel 62's unique 7.8 = step 110's).
  All 64 channels = every even step: the exact mirror of 64 odd-step DSP reads (nop +17.6, rsh +144.8, ldr +72.2,
  str +9.3 on both).  (runner: `quiet` + `areg` directives; silent looped data, no sends.)
- **PCM16 and PCM8: one fetch per sample at every pitch measured** (OCT -2..+7).
- **ADPCM** (PCMS 2 and 3 alike), share of samples with a fetch: OCT -2 0.31, -1 0.37, 0 and +1 0.5, +2 1, +3 0
  (the channel apparently stops).
  - **Rule (2026-09-24, from these measurements and caique rtl/v1): the channel holds one 16-bit word of its
    stream; a sample fetches when the play position enters another word, or when the look-ahead nibble (position
    + 1, the interpolation's second sample) lies in the next word; at most one fetch per sample; none at OCT 3..7.**
    Shares 5/16, 3/8, 1/2, 1/2, 1 -- the measured ones.  No 8- or 32-bit unit fits (a byte gives 1 at OCT 0 and +1;
    a 32-bit word 1/4 at OCT 0 and 3/16 at -1), nor does fetching only when the position crosses (OCT 0: 1/4).
  - The pattern matters, not only the rate: the register-shift kernels at OCT -1 / 0 / +1 moved by up to 0.33 MCLK
    per iteration between the old evenly spread fetches and the rule (sgc f_adp_o0_rsh: console 416.86, spread
    417.19, rule 416.87).  With the rule: sgc 176/176 within 0.1 (was 175/176), sgcadp 36/36 within 0.1, worst
    0.04 (was 0.32, checked at 0.5 before; `hw_suite check sgcadp` now uses 0.1).
  - Model: `DcArmBus::adpcm_fetch` replaces `adpcm_rate`.  The position runs from `adpcm_origin` (the SGC sample
    where it is 0; default 0 = the ARM's reset release) with the pitch accumulator ((1024 + FNS) << (OCT + 4)) /
    2^14 nibbles per sample; loops are not modelled (the suites' LEA is never reached).  The SGC sample of a
    slot is its DSP sample, except channels 0-6 (steps 114-126, the end of the DSP sample), whose fetch already
    belongs to the next one (caique rtl/v1's frame order).  The console's phase between key-on and the ARM's
    reset release is not known; the per-iteration costs do not depend on it.
- **SH4 register traffic costs the ARM nothing**: register reads (12.8 per sample, flat out) and writes (43 per
  sample) leave every ARM kernel within 0.05 U.
- **SH4 wave RAM writes** (posted; 42.5 per sample flat out) **take one slot each**, the first free step boundary
  after they arrive, ahead of the ARM: the model reproduces the flat-out cost to 0.5% and paced writes to ~0.2 U.
- **SH4 wave RAM reads take two slots 8 MCLK apart; the data is back 24 MCLK after the second, and the next read
  issues then** (one read per 40 MCLK = 12.8 per sample flat out).  That round trip is what makes flat-out reads
  cost twice what random single slots would: it matches all four kernels at the flat-out rate within 0.06 U.
  Paced reads are within 0.5 U, except ldr at a 2000-SH4-cycle gap (1.8 vs 3.8 U).
- G2 is not synchronous to the AICA, so SH4 arrival times are an input to the model (`DcArmBus::sh4_access(t,
  slots)`, or the periodic generator used by the tests, averaged over 8 phases).
- **Composition, predicted before the console run** (tests/hw/combo): channels + DSP programs together 12/12
  within 0.11 U per iteration (0.04%); with SH4 writes added, 0.02-1.7%.  The worst case is flat-out writes with
  4 channels (+1.7%): the model keeps the idle-bus write rate, while on the console occupied slots presumably also
  slow the SH4's write stream.
- **A channel fetch and a DSP access on the same slot: the channel wins, and the collision takes one slot.**
  - Timing (tests/hw/collide, `hw_suite check collide`): channel K plus a DSP MRD or MWT at its step 2K - 14, for
    K = 10, 20, 30, 40, 61, 62, 63 (61-63 = steps 108/110/112, around the fixed pair).  Each ARM kernel loses exactly
    what it loses to either access alone, and the fixed pair does not move: 196/196 within 0.1 U, model unchanged
    (`slot_used` already counts a step once).  The same DSP access 2 steps later adds its own slot.
  - Functional (tests/hw/collide2, `hw_suite check collide2`, 32/32; read back with the ARM in reset):
    - a DSP **MWT** in a channel's slot is **dropped**: word 32 keeps its prefill 0x5555 (4/4; 28/28 kernels in
      collide with the ARM running), while the same write alone or 2 steps later stores its 0x1E1E;
    - a DSP **MRD** in a channel's slot **returns the channel's word**: MEMS0 = the ramp word 0xA0xx << 8 in place of
      the 0x1234 it addressed (4/4), and 0x123400 alone or 2 steps later;
    - the **channel's fetch is unaffected**: MIXS0 follows its ramp every sample in both collision cases.
  - For the AICA side this means: the channel owns the slot, the DSP's write strobe is suppressed, and the DSP's read
    takes whatever the channel put in the memory read latch.
- **ARM reads can replace an even-step MRD's result** (side finding of collide, not needed for the ARM's timing).
  With the ARM running, an MRD at an even step s with its IWT at s+3 often returns an ARM word instead of its own:
  low halves of ARM fetches, e.g. 0x0211 (from `add r0, r0, r1, lsl r2` = 0xE0800211) and 0xF100 (the 0x1FF100
  literal).  It never happens at steps 108 and 110, whose next slot (109, 111) belongs to the fixed user (12/12
  clean), nor with the ARM in reset (collide2 4/4).  That fits an ARM read in slot s+1 reaching the shared read latch
  before the DSP's IWT copy (caique: an even-step MRD lands at s+3, "until the next read lands").  Which ARM
  reads do it is not pinned: MEMS0 is taken in the kernel's last sample, whose ARM slot pattern depends on the
  ARMRST phase the model does not know.  Test writers: read DSP results with the ARM in reset (runner `rreg`).
- Harness: the runner's `quiet` now runs *before* the loads.  It used to run after them, and the previous job's DSP
  program kept writing the freshly loaded RAM until `quiet` cleared MPRO (the first collide2 run showed it on the
  jobs with no program).

Open (timing):
- **Who owns the slots of DSP steps 109 and 111**, and what the "move by one, stay >= 2 apart" rule really is
  (measured only for DSP accesses at 109, 111, 110 and every odd step).
- Why ADPCM stops at OCT +3 (and what the channel outputs then); SH4 arrival timing beyond the two generator
  models (G2 DMA is not measured at all).
  EFREG port conflicts are not measured.
- Functional, not timing: exactly which ARM reads replace an even-step MRD's result (slot s+1 is the hypothesis).

### Cross-check: caique rtl/v1 as the bus (2026-09-24)

(The harnesses and their current results are caique's: `caique-rtl/INTEGRATION.md`, "Combined tests".)

caique-rtl `rtl/v1` (the AICA RTL) implements this section's slot rules in hardware form: one wave RAM slot per DSP
step, claimed by the step's MRD / MWT, by channel K's fetch at step 2K - 14 and by the fixed pair (placed from MPRO
rows 108-112 read ahead), then the SH4, then the ARM; a separate register lane (TEMP / EFREG wait for TWT / EWT);
an ARM cycle of 8 MCLK from its step boundary (locked write 4), L / M in one.  Its `tb/armjob_tb.cpp` runs a job
file on this model's `Arm7DI` with the RTL as the bus (`bus_cycle()` makes that possible: the harness owns time),
with a shadow `DcArmBus` fed the same cycles as the per-cycle oracle and as the interrupt controller (not in the
RTL yet).  Time: t_rtl = t + 24 + 512 x 8 (the RTL's DSP step 0 is 64 clocks into its slot sweep).
- **All 19 suites, 3424 jobs: every job ends on the same clock and every one of 143 M memory cycles has the same
  wait** (`rtl/v1/tb/armjob_all.sh`; SH4 suites at 8 stream phases, as `check_timed` averages).  Per kernel, the
  RTL is therefore within the tolerances of section "Contention" of the console too
  (`build/rtl_v1/armjob_console`: dspmem 44/44, dspmem1 511/512, dspmem1r 7/7, sgc 176/176, collide 196/196,
  sgcadp 36/36, sh4load and combo as the model).
- It found the ADPCM rule above: the RTL's channel fetches as the hardware would (a word register), which matched
  the console where the evenly spread model did not; the rule then went into `adpcm_fetch`.  The only other
  differences were in the harness (SH4 stream arrivals truncated to the MCLK, the read's last slot).
- Functional readouts (`armjob_tb -w DIR`, `hw_suite check collide2 DIR`): collide2 32/32 on the RTL.  The channel's
  MIXS0 ramp read at any phase needed the RTL's MIXS writes aligned with the DSP's sample and the CPU reading the
  DSP's bank (reading the bank being filled fails 12/12 channel cases).  The channel-wins rule is pinned sample by
  sample by caique `model/tests/dsp_coll` (console = caique model 18/18 runs, RTL = model bit-exact): the DSP's
  MWT in the slot is dropped exactly in the samples the channel fetches (ADPCM OCT -1: 3 of 8), and its MRD returns
  the 16-bit word holding the channel's sample CA + 1; channels 0-6 (steps 114-126) collide with their fetch of the
  next sample; the first fetch after a key-on is in the sample that outputs CA 0.  Not modelled on either side: the
  shared read latch of the side finding above (collide with the ARM running: MEMS0 of even-step reads).
- Harness changes here: `hwjob.h` parses `rreg` (into `HwJob::rregs`, for AICA harnesses; the model has no DSP to
  read), `run_job_model(..., adpcm_origin)`, `hw_suite check collide2 [DIR]`.

## minicast deviations (for reference, not used)

minicast's ARM core is vba-arm (a GBA ARM7TDMI interpreter).  It implements ARMv4 LDRH/STRH/LDRSB/LDRSH and BX,
which the ARM7DI does not have (long multiplies are `#if 0`, "only on arm7tm").  It keeps no bus timing
(`CPUUpdateTicks*` return 1).  Undefined instructions only print "SOMETHING WENT WRONG".

## Verification status

- Data sheet chapter 9 tables: `tools/timing_check`, 54/54 (`tests/timing_summary.txt`).
- Vendor suite: `tools/vendor_tests`, 45/45 (`tests/vendor_summary.txt`).  Pass = reached 0xDEADBEEF with
  (r1 & 0x3FFFFFFF) == 0.  Bits 31/30 are group markers.
- Differential vs qemu-arm `-cpu sa1100`: `tools/qemu_diff`.  Kept batches `tests/qemu_diff/s1..s4` (4000 cases)
  plus a 1.1 M-case sweep (seeds 1000-1024 and 100000-100249, not kept, reproducible with `--gen --seed`): 0
  mismatches.  qemu-user does not rotate misaligned LDR (verified: it returns the unaligned word), so misaligned
  words are excluded there; the vendor LDR tests cover rotation.
- The console job suites on caique rtl/v1 as the bus: 3424 jobs cycle-identical to `DcArmBus` (see "Cross-check:
  caique rtl/v1").
- Mutation checks: a wrong STM write-back point fails STM_2/3/4, missing LDR rotation fails 12 LDR tests, ADC without
  carry fails ADC_1, RSC with a forced carry fails 56 qemu_diff cases, a wrong LSL-by-32 carry is caught by qemu_diff.

## Console tests to write (next)

- A KOS runner in the caique style (SH4 loads an ARM image into wave RAM, releases ARMRST at 0x00702C00, polls a
  done flag, reads results back), through `shrike4-rtl/tools/hw/hwrun.sh`.
- Vendor bins: linked at 0, so their own code covers the vector table.  The runner must replace the 0xDEADBEEF word
  with a branch to a stub past the image that saves r0-r15/CPSR and sets the flag.  The word itself is an
  LE-conditional CDP, so it would either trap or fall through into data.
- `tests/qemu_diff/s*`: runnable as-is (see its README): reset -> b 0x10000, SWI vector -> done stub.
- Then H1-H13 and the DcWaits timing.
