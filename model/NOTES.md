# wren7 ARM7DI model -- notes

The AICA's sound CPU is an ARM7DI (ARMv3: 32-bit and 26-bit modes, no Thumb, no halfword/signed transfers, no long
multiply).  This model is the reference for the later RTL.  Status (2026-09-23): built from the data sheet, **nothing
measured on the console yet**.  Everything below is either data sheet (section/table given), minicast, or an
explicit model choice marked **[H#]** = open hardware question.

Sources:
- `docs/DDI0027D_7di_ds.pdf` (ARM7DI data sheet, 1997).  Text dump for grepping: `agents/arm7di.txt`
  (`pdftotext -layout`).  Chapter 9 (per-cycle bus tables) is the timing spec the model follows.
- minicast `libswirl/hw/arm7/SoundCPU.cpp` (memory map, e68k interrupt glue), `hw/aica/aica_mmio.cpp` (ARM-side
  interrupt registers), `gpl/vba-arm/arm-new.h` (instruction interpreter).
- `vendor/arm7di-tests-dreamcast`: armwrestler's ARM tests split into 45 standalone programs by `test.py`.
  `arm-eabi-as -mcpu=arm7` + `ld -Ttext 0 -N` + `objcopy -O binary` from `/opt/toolchains/dc-dca3/arm-eabi/bin`
  rebuilds all 45 `bins/` byte-identically, so new console tests can use the same toolchain.

## Model structure

- `src/arm7di.{h,cpp}`: the core.  `step()` executes one instruction (or one exception entry) as the exact bus-cycle
  sequence of its chapter 9 table.  Every cycle goes through `Arm7Bus::cycle()` with what the pins carry:
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
- **[H4]** MUL/MLA with S: C is "meaningless".  Model default: carry out of the last add/sub Booth step (0 when that
  step was a pass).  `Arm7DI::mul_carry` also has KEEP and SHIFTER_LAST.  Sweep Rs/Rm to pin it.
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
  R14_svc/SPSR_svc/CPSR change).  Dummy-fetch cycle types while nRESET is low.  Minicast loads GBA register values
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
  (0x28A8/AC/B0; bits >= 7 share SCILV bit 7).  An "e68k" latch holds L and drives nFIQ until M is written.
  SCIPD bit 5 (SCPU) is settable by writing; SCIRE (0x28A4) clears.  nIRQ is not connected in minicast.
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
