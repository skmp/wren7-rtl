# wren7 v1 RTL: the ARM7DI

wren7 v1 is the Dreamcast AICA's sound CPU in SystemVerilog, written for Cyclone V (DE10-Nano / MiSTer). It is cycle-exact to the C++ model (`model/src/arm7di.cpp`, `Arm7DI::bus_cycle`). Every bus cycle has the model's type, address, flags and write data, and the architectural state matches at every instruction boundary. The model is the reference: its console-measured semantics (MUL carry, the FIQ handshake timing, the "shall not" choices in `model/NOTES.md`) are what the RTL implements.

| file | what |
|---|---|
| `wren7_core.sv` | the core: cycle sequencer, decode, datapath, the generic ARM7 bus |
| `wren7_regfile.sv` | 30 banked registers: two MLAB copies (two read ports), one write port, a one-clock write bypass |
| `wren7_shifter.sv` | the barrel shifter: one right rotator plus masks for all four types and both amount encodings |
| `wren7_aica.sv` | the core on caique's ARM7DI port: the module the AICA instantiates |
| `../../tb/core_tb.cpp` | lockstep testbench against the model (Verilator) |
| `../../quartus/testing/wren7-rtl/` | Quartus testing project (Cyclone V 5CSEBA6U23I7): Fmax and area of `wren7_aica` |

## Interfaces

**`wren7_core`.** One bus cycle is in progress in every clock with `ce = 1`. The bus outputs come from registers only, so a bus may answer combinationally (caique acknowledges L/M in the request's clock).

- `bus_type` is this cycle's N/S/I, announced by the previous cycle. C cycles never occur: there is no coprocessor.
- `bus_req` (N or S) holds `bus_addr`, `bus_we`, `bus_byte`, `bus_lock` and `bus_wdata` until `bus_ack`. Every earlier clock is an nWAIT stretch. `bus_rdata` (the whole word) and `bus_abort` are taken with the ack.
- `bus_opc` and `bus_user` are nOPC and nTRANS.
- I cycles last one clock, with no ack and no waits.

`fiq`, `irq` (asserted = 1) and `hold` (nRESET asserted) are taken in the first clock of the cycle that begins an instruction, as the model's `begin()` takes them. `rst` is the model's `power_on` (reset entry from address 0), except that the register file keeps its contents, as on the console. `dbg_*` expose pc, CPSR and the execute-stage word for the testbench. Synthesis drops them where they are unconnected.

**`wren7_aica`** maps the core onto the port in caique-rtl `rtl/v1/README.md` ("ARM7DI port contract"):

- `arm_ram` = A[23] = 0;
- `arm_addr` = A[20:2] (wave RAM) or A[14:2] (registers);
- `arm_be` = the byte lane for byte cycles, otherwise all four;
- `arm_lock` = SWP.

There is no ABORT, nIRQ is not driven, `rst` = ARMRST, and `arm_fiq` = nFIQ. `arm_ack` is seen only in `ce` clocks.

## Structure (FPGA choices)

- **The sequencer is the model's `seq_ / k_`.**
  - `S_BOUND` is the first cycle of every instruction, exception entry or reset dummy fetch. It is always a fetch from pc, so its request needs no decode; what happens at its end is decided during it (reset > reset entry > data abort > FIQ > IRQ > prefetch abort > the instruction).
  - The other states are the later cycles of each class: `S_DP2`, `S_MUL`, `S_SWP1-3`, `S_SDT2/3`, `S_BDTX`, `S_BDTF`, `S_UND2`, and the refill `S_REF0/1`.
- **One datapath.**
  - One barrel shifter, shared by operand 2, the LDR/STR register offset and the Booth step's `Rm << 2k(+1)`.
  - One ALU adder for data operations, the LDR/STR address, the LDM/STM write-back value and the Booth step.
  - One adder for the branch target and the LDM/STM start address.
  - Incrementers for pc and the transfer address.
  - The multiplier is the measured 2-bit Booth sequence, one internal cycle per step. It needs no DSP block, and the cycle count and C flag are the model's.
- **Register file.**
  - Two asynchronous read ports and one write port; each read port has its own 32 x 32 MLAB pair.
  - An LDM writes each loaded register one cycle late (`pw_*`): in the next transfer, or in the final internal cycle the instruction has anyway. Its base write-back and first register therefore never need two write ports.
  - The last write is bypassed for one clock. Reads never depend on the MLAB's mid-clock write, and `no_rw_check` holds.
- **In-flight latches.** Classes that never need them at the same time share four 32-bit latches (`x_addr`, `x_data`, `x_calc`, `x_base`).
- **`ce` is a clock enable throughout.** The core can run at the AICA's 22.5792 MHz directly or as `ce` pulses of a faster clock.

**Not modelled** (the model has them, the AICA does not use them): the nFIQ/nIRQ synchroniser (`irq_sync_stages` > 0; ISYNC is high, measured edge latency 0) and the alternative MUL carry rules (`mul_carry`; the RTL has the measured shifter carry).

## Verification

```
make v1-lint        # Verilator -Wall on wren7_core and wren7_aica
make v1             # quick gate, ~45 s: vendor, qemu_diff, 200 chaos seeds, 4 console job suites
make v1-full        # every console job suite (3392 jobs) + 2000 chaos seeds, ~2 min on 24 cores
make quartus-lint   # the Quartus fileset as Quartus reads it (SYNTHESIS, qip order)
agents/rtl_v1/mutate.sh   # checker sanity: 23 deliberate RTL bugs, each must fail the quick gate
```

`tb/core_tb.cpp` runs the model on its bus and replays every bus cycle on the RTL in the same clock. The RTL's request must equal the model's, and the RTL gets the model's waits, read data and abort. At every boundary pc, CPSR, the execute word, all 30 registers and the 5 SPSRs are compared. The sources are:

- the vendor bins and the qemu_diff batches, each at zero waits and on the console's bus timing;
- every console job suite on `DcArmBus` (sample FIQ, SH4 traffic, DSP slots, channels);
- chaos: the same programs over a background of weighted random instruction soup, with random waits, aborts, FIQ/IRQ, nRESET pulses, `ce = 0` clocks and stray acks and aborts.

Status 2026-11-01: `make v1-full`, 0 mismatches.

| source | runs | clocks |
|---|---|---|
| job suites | 3392 | 1.14 G |
| chaos | 2000 | 600 M (about 17 M exceptions) |
| vendor | 90 | — |
| qemu_diff | 8 | — |

The mutation check catches 23 of 23 mutants:
- the shifter edges (ASR fill, RRX, LSL by 32);
- MUL carry, ADC carry;
- LDM write-back, the delayed register, STM^ bank;
- FIQ banking, TEQP, MSR in user mode, LDRT nTRANS, SWP LOCK;
- the cycle types, exception and undefined R14, prefetch and data aborts, dummy fetches, `ce`.

Weakest is LSL by 32 through a register: caught in 1 of the quick gate's 100 chaos runs.

**Quartus.** `cd quartus/testing/wren7-rtl && quartus_sh --flow compile wren7-rtl`; Fmax is in `output_files/wren7-rtl.sta.rpt`. The SDC targets 10 ns, like shrike4's harness; the AICA needs 44.29 ns. Not compiled yet: Quartus is not installed on the development machine, and the fileset passes `make quartus-lint`. The longest path is expected to be the data operation's first cycle: execute word → register address → MLAB read → shifter → adder → flags.
