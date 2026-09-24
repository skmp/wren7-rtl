# AICA ARM7DI timing on the Dreamcast

A reference for implementing the sound CPU's timing: RTL, emulators, cycle-exact models.  Everything here was
measured on a retail console, and the model in `src/` reproduces every measurement listed at the end.  How each
point was found, and the raw data, are in [NOTES.md](NOTES.md) and `tests/hw/`.

Conditions: unless a section says otherwise, the 64 sound channels are off, the SH4 is off the AICA bus (G2) and
the DSP program is empty.  Section 3.5 covers playing channels and SH4 traffic.

## 1. Clocks

| clock | value | how it was found |
|---|---|---|
| ARM7DI core clock (MCLK) | **22.5792 MHz = 512 x 44.1 kHz**, the AICA bus clock (33.8688 MHz x 2 / 3) | exact-cost poll loops see exactly 512 MCLK per sample |
| DSP step | **4 MCLK**; 128 steps per sample | DSP write blocks move ARM stalls by 4 MCLK per step |
| sample | 512 MCLK | |
| SH4 on this console | 199.49 MHz (4523.55 SH4 cycles per sample) | for converting console measurements only |

Internal (I) cycles run at the full 22.5792 MHz.  Only memory cycles are slow: each takes 8 MCLK.  (MAME clocks
the whole core at 22.5792 / 8 = 2.8224 MHz.  That is right for memory cycles and 8x too slow for internal ones.
minicast runs it at 11.29 MHz-equivalent with GBA-derived timings.)

## 2. The core: bus cycles per instruction

The core is a data sheet ARM7DI (DDI0027D).  Each instruction executes as the bus-cycle sequence of chapter 9, one
bus cycle per MCLK before wait states.  N/S/I are the cycle types; I cycles never touch memory.

| instruction | bus cycles |
|---|---|
| any instruction whose condition fails | S |
| data processing | S |
| data processing, shift amount in Rs | S, I |
| data processing writing R15 | S, N, S (shift by Rs: S, I, N, S) |
| MRS, MSR | S |
| MUL, MLA | S, m x I |
| LDR / LDRB | S, N (data), I |
| LDR to R15 | S, N, I, N, S |
| STR / STRB | S, N (data) |
| LDM, n registers | S, N, (n - 1) x S, I; with R15 add N, S |
| STM, n registers | S, N, (n - 1) x S |
| SWP / SWPB | S, N (read, LOCK), N (write, LOCK), I |
| B, BL, SWI, exception entry (IRQ/FIQ/abort) | S, N, S |
| undefined / any coprocessor instruction (no coprocessor) | S, I, N, S |

**Multiply**: m = the smallest m >= 1 with `(Rs >> (2m - 1)) == 0`, at most 16 (the data sheet's 2-bit Booth
multiplier).  Early termination is **unsigned**: any Rs >= 2^29, and every negative value, takes 16.  MLA costs the
same as MUL.  (Not the ARM7TDMI rule: that one terminates on all-ones and gives MLA one extra cycle.)

## 3. The memory system

### 3.1 The slot grid

- Time is divided into DSP steps of 4 MCLK.  **A memory cycle (N or S) may only start on a step boundary** and
  then **lasts 8 MCLK** (7 nWAIT).  An I cycle lasts 1 MCLK; the memory cycle after it waits for the next step
  boundary.
- **Every memory cycle costs the same**: N or S, read or write, byte or word, sequential or random, wave RAM or AICA
  register.  There is no page mode and no N/S difference.
- Exceptions:

| access | time |
|---|---|
| the locked write of SWP / SWPB | 4 MCLK |
| L (0x802D00) and M (0x802D04), the ARM interrupt controller | 1 MCLK, no slot, no grid wait |
| any other AICA register (0x800000-0x807FFF) | 8 MCLK on the grid, never delayed by wave RAM slots |
| wave RAM (0x000000-0x7FFFFF) | 8 MCLK on the grid, subject to the slot rule below |

### 3.2 Wave RAM slots

- **There is one wave RAM slot per DSP step.**  An ARM wave RAM access may start at a step boundary only if that
  step's slot is free.  Once started it takes 8 MCLK, but it does not hold the next step's slot.
- **Every DSP MRD or MWT takes the slot of its own step**, reads and writes alike, odd or even steps.
- **Another user takes the slots of steps 109 and 111 in every sample.**  The owner is not known; two SDRAM refreshes
  per sample would fit.  When the DSP holds one of those steps, that access moves one step earlier or later: the two
  stay >= 2 steps apart, and the earliest valid placement is used.

| DSP accesses at | the other user moves to |
|---|---|
| none of 108-112 | 109, 111 |
| 109 | 108, 111 |
| 111 | 109, 112 |
| 110 | 109, 111 (unchanged) |
| every odd step | 108, 110 |

This rule is fitted to the four DSP-program cases in the table; no other case was measured.

- The ARM's own slot pattern follows from this.  Back-to-back memory cycles are 8 MCLK apart, so a stream of them
  keeps to one parity of steps.  With the DSP idle, a stream on the odd steps hits step 109 or 111, is pushed by 4
  MCLK onto the even steps, and stays there.  Idle ARM code therefore runs on the even steps and never sees the
  other user.  An instruction that ends in an I cycle (or an odd number of 4-MCLK shifts) moves the stream to the
  odd steps until something pushes it back.

### 3.3 The DSP buffer port (TEMP, EFREG)

The AICA manual's buffer table has each DSP step's first half for the DSP's own read and its second half for the CPU
port, which TEMP and EFREG share with the DSP's writes.  **A CPU access to TEMP (0x4000-0x43FF) waits while its step
is a TEMP-writing step (TWT), until the first step that does not write.**  The measured stall is the rest of the
write block, in 4-MCLK steps.  EFREG (0x4580-0x45BF) is modelled the same way with EWT, from the manual, not
measured.  COEF, MADRS and MPRO are only read by the DSP, so CPU accesses to them never conflict.

### 3.4 Phase: where the steps sit in time

- **The one-sample interrupt edge (SCIPD / MCIPD bit 10) lies on a DSP step boundary, and DSP step 0 starts exactly
  40 MCLK (10 steps) after it.**  So the edge is at the start of step 118, and the other user's slots (steps 109 and
  111) are 36 and 28 MCLK before every sample edge.
- **FIQ is recognised at the end of the instruction in which the edge falls, with no extra latency.**
- The release of ARMRST is synchronous to the sample frame, so a given ARM program sees the same phase on every
  run.  This is why the console measurements repeat to a few MCLK.

### 3.5 Playing channels and SH4 traffic

- **Sound channels.**  A keyed-on channel K takes one wave RAM slot per sample: **DSP step (2K - 14) mod 128**, always
  an even step (channel 7 at step 0, channel 0 at 114, channel 63 at 112).  PCM16 and PCM8 fetch once per sample at
  every pitch measured (OCT -2..+7).  **ADPCM holds one 16-bit word of its stream**: a sample fetches when the play
  position enters another word, or when the look-ahead nibble (position + 1, the interpolation's second sample)
  lies in the next word; at most one fetch per sample, none at OCT +3 and above (the channel stops):

| ADPCM OCT | -2 | -1 | 0 | +1 | +2 | +3 |
|---|---|---|---|---|---|---|
| samples with a fetch, console | 0.31 | 0.37 | 0.5 | 0.5 | 1 | 0 |
| the word rule | 5/16 | 3/8 | 1/2 | 1/2 | 1 | 0 |

  The rule also gives the pattern within the samples, which the register-shift kernels are sensitive to: every
  ADPCM kernel within 0.04 MCLK per iteration of the console, where evenly spread fetches were up to 0.33 off.
  All 64 channels together take every even step.  That is the
  ARM's own parity, so the ARM is pushed to the odd steps and back by the fixed pair: 16 MCLK per sample for a
  loop of plain memory cycles.
- **SH4 register accesses** (reads or writes, at any rate) cost the ARM nothing.
- **SH4 wave RAM writes** are posted.  Each takes one slot, at the first free step boundary after it reaches the
  AICA, ahead of the ARM.  Flat out that is 42.5 per sample.
- **SH4 wave RAM reads** take two slots 8 MCLK apart.  The data is back 24 MCLK after the second slot, and a CPU
  read loop issues its next read then: one read per 40 MCLK flat out, 12.8 per sample.
- G2 is not synchronous to the AICA: the arrival time of each SH4 access has to come from the SH4 side of the
  system model.
- **A channel and a DSP access on the same step share one slot, and the channel wins.**  The ARM sees one used
  slot, and the fixed pair stays put (measured at steps 6-66 and 108/110/112).  The DSP's write is dropped, and its
  read returns the channel's word; the channel plays normally.  (The data effects belong to the AICA model; the ARM
  timing only needs "one slot".)

## 4. Cost of each instruction

What the rules give when an instruction starts on a step boundary and meets no occupied slot.  "+4" means the
instruction leaves the next memory cycle on the other step parity.

| instruction | MCLK |
|---|---|
| data op, MRS, MSR, condition failed | 8 |
| data op, register-specified shift | 12 (+4) |
| data op writing R15, B, BL | 24 |
| MUL / MLA | 8 + 4 x ceil(m / 4): 12, 16, 20, 24 (+4 when ceil(m/4) is odd) |
| LDR / LDRB (wave RAM or AICA register) | 20 (+4) |
| LDR of L or M | 12 (+4) |
| LDR to R15 | 36 (+4) |
| STR / STRB (wave RAM or AICA register) | 16 |
| STR to M | 12 (+4) |
| LDM, n registers | 12 + 8n (+4); with R15 28 + 8n (+4) |
| STM, n registers | 8 + 8n |
| SWP / SWPB | 24 |
| SWI, IRQ / FIQ / abort entry | 24 |
| undefined, coprocessor instruction | 28 (+4) |
| movs pc, lr / subs pc, lr, #4 (data op to R15) | 24 |

Example: a loop of 16 `ldr r0, [r3]` plus `subs` / `bne` costs 16 x 20 + 8 + 24 = 352 MCLK per iteration.  On the
console it measures 354.47 MCLK: the extra 2.47 comes from the odd-parity LDRs meeting steps 109 and 111.

## 5. Interrupt path costs

The ARM's only interrupt is FIQ: the sample-interval interrupt or any other SCIPD source.  NOTES.md "Interrupts" has
the protocol:
- the level from SCILV0-2 must be non-zero;
- reading L acknowledges and releases nFIQ;
- writing M = 1 re-arms.

| handler operation | MCLK |
|---|---|
| NOP | 8 |
| LDR L or LDR M (+ an ORR to restore the step parity) | 24 (12 + 12) |
| STR M | 12 |
| STR SCIRE / SCIEB (any other AICA register) | 16 |
| LDR SCIEB + ORR, LDR wave RAM + ORR | 32 |

FIQ entry is the 3-cycle exception sequence of section 2 (24 MCLK).

## 6. Implementing it

To be exact, keep the core's bus-cycle sequences (section 2) and put all timing in the memory interface:

1. Keep an MCLK counter.  Keep the DSP step phase: step = ((t - t_edge - 40) mod 512) / 4, where t_edge is the
   time of a sample edge.
2. I cycle: t += 1.
3. Memory cycle:
   - start = the next multiple of 4 at or after t (step boundaries).
   - For wave RAM, while the step at `start` has its slot taken, start += 4.  A slot is taken by:
     - a DSP MRD/MWT in the current MPRO;
     - the other user's two slots, placed by the rule in 3.2;
     - a keyed-on channel's fetch (3.5);
     - an SH4 access that arrived earlier (3.5).
   - For TEMP / EFREG, while the step at `start` is a writing step for that buffer, start += 4.
   - t = start + 8, or start + 4 for a SWP's locked write.
   - L / M: t += 1, no alignment.
4. Evaluate the sample edge (SCIPD bit 10, FIQ request) at every bus cycle start and at every instruction boundary.
   The FIQ check at the boundary sees an edge that happened during the last instruction.

`src/dc_arm_map.cpp` (`DcArmBus::cycle`, `slot_used`) is the reference implementation, and `DcWaits::dreamcast()`
selects it.

Cheaper approximations, from the measured kernels:
- Section 4's fixed costs with the 4-MCLK grid, and no slots: within 1% of the console for any code with the DSP
  idle.  The worst measured kernel is 0.74% slower on the console: `ldmia` of 7 registers, whose parity keeps
  meeting steps 109 and 111.
- 8 MCLK per memory cycle, 1 per I cycle, no grid: under-counts every I cycle by up to 3 MCLK, e.g. 9 instead of 12
  for a register-shift data op.
- Ignore DSP memory accesses: odd-parity code then misses up to 4 MCLK for every DSP MRD/MWT slot it meets (a
  register-shift loop against 16 accesses per sample: about 38 MCLK per sample).  Even-parity code misses about 8
  MCLK per sample once any DSP access sits on an even step (it is pushed to the odd steps and back once per frame).

## 7. Verification

Model (`DcWaits::dreamcast()`) against the console:

| suite (tests/hw/) | what | result |
|---|---|---|
| timing | 105 kernels: every instruction class, MUL m sweeps, LDM/STM 1-8, 16 random mixes and 5 flow kernels predicted before measuring | all within 0.1 MCLK per iteration |
| dspmem | 11 DSP programs (0-64 MRD, MWT, odd/even) x 4 ARM kernels | 44/44 within 0.1 |
| dspmem1, dspmem1r | one DSP access at each of the 128 steps, read and write, x 2 kernels | 512/512 within 0.1 (5 borderline one-shot runs confirmed with 5 repeats) |
| dspport | TEMP write blocks at 4 positions x 2 probe placements x 33 delays | 330/330 exact |
| blkphase, blkshot | FIQ-locked and single-shot probes of the slot phase | 66/66, 66/66 exact |
| fiqcost | 8 handler operations | 8/8 exact |
| sgc | channels playing: N = 1-64, each channel alone, formats and pitches | 176/176 within 0.1 |
| sgcadp | ADPCM at OCT -2..+3, PCMS 2 and 3 | 36/36 within 0.1 (worst 0.04) |
| sh4load | SH4 reads / writes of wave RAM and registers, flat out and paced | registers 0 cost; wave RAM within 0.5-1.7% (asynchronous arrivals) |
| combo | channels + DSP programs (+ SH4 writes), predicted before measuring | 12/12 within 0.11; with SH4 writes 0.02-1.7% |
| collide | a channel and a DSP read/write on the same step (and 2 steps apart), 7 channels x 4 kernels | 196/196 within 0.1 |
| collide2 | the same collisions read back with the ARM in reset: who wins (functional) | 32/32 as expected |

Check them with `../build/model/hw_timing check` and `../build/model/hw_suite check <suite>` (from
`wren7-rtl/model`).  Rerun on the console with `./run_hw.sh <suite>`.

An AICA RTL implementing this section (caique-rtl rtl/v1) runs every suite above as the bus of the model's
ARM7DI (`Arm7DI::bus_cycle`): all 3424 jobs end on the same clock as with `DcArmBus`, every one of 143 M memory
cycles with the same wait (caique-rtl `rtl/v1/tb/armjob_all.sh`; NOTES.md "Cross-check: caique rtl/v1").

## 8. Not covered yet

- **Why an ADPCM channel stops at OCT +3**, and what it outputs then.
- **SH4 arrival timing** beyond the two generator models: G2 DMA to wave RAM is not measured, and neither is
  write-stream throttling by occupied slots (the +1.7% case).
- **Who owns steps 109 and 111**, and the general form of its move-by-one rule (measured only for DSP accesses at
  109, 110, 111 and every odd step).
- **EFREG port conflicts** (modelled from the manual only).
- Reset: where in the frame the first cycle after ARMRST release falls (the phase is fixed per console run, but its
  value has not been isolated from the sample edge).
