# wren7 in the AICA: integration with caique

wren7 is the ARM7DI, the Dreamcast AICA's sound CPU. On the console the ARM sits on the AICA's bus, and the AICA owns that bus. **caique-rtl integrates wren7, and all co-simulation and combined tests are caique's domain.** caique's `INTEGRATION.md` is the full reference: interfaces, port contract, time base, combined tests and change protocol. This page covers what that means for work in wren7.

## What wren7 owns

- **The ARM7DI core:** instruction semantics and the per-cycle bus sequences (`model/src/arm7di.*`), and the RTL core later.
- **ARM-side console measurements:** the timing kernels, the FIQ handshake and the multiplier (`model/tests/hw/<suite>`, `model/NOTES.md`, `model/TIMING.md`).
- **`DcArmBus` / `DcWaits::dreamcast()`.** This is the measured reference model of the sound bus as the ARM sees it: wave RAM slots, the fixed pair, channel fetch slots, SH4 traffic, TEMP/EFREG ports, and the interrupt controller with L/M. It serves as wren7's own bus for ARM-only work. It is also caique's per-cycle oracle.

## What stays out of wren7

- **No combined tests.** wren7 has no AICA RTL, no co-simulation harness, and no test that needs sound generator or DSP behaviour. Those live in caique (`rtl/v1/tb/`, `model/cases/`).
- **`DcArmBus` stays a timing reference and an interrupt stand-in, not a second AICA.** It does not execute the DSP or play channels. Its slot rules come from measurements, and it decodes only what a rule needs, such as MRD/MWT from MPRO or KYONB and the pitch registers.
- **AICA-internal findings go to caique's `model/NOTES.md`,** even when an ARM job found them. An example is the channel/DSP collision rule from `tests/hw/collide2`. wren7's NOTES may cite them.

## What caique relies on (keep stable, or change it together with caique)

- **The core API:** `Arm7DI::bus_cycle()` (one bus cycle per call), `at_boundary()` and `set_fiq()`.
- **The `Arm7Bus` interface:** `cycle(BusCycle, BusResult)` and `tick(t)`, with the fields and flags of `arm7di.h`.
- **`DcArmBus` and the members caique's harnesses use:**
  - setup and data: `waits`, `load`, `wr32`, `reg_write`, `attach`;
  - SH4 traffic: `sh4_period`, `sh4_phase`, `sh4_read_latency`;
  - ADPCM alignment: `adpcm_origin`;
  - completion: `scpu_raised`, `scpu_t`.
- **`tools/hwjob.h`:** `parse_jobs`, `HwJob` (including `rregs`), `sh4_setup`, and `run_job_model(job, waits, max_cycles, trace, mul_carry, sh4_phase, adpcm_origin)`.
- **Console material:** the job file format, `tests/hw/<suite>/jobs.txt` with its console results in `tests/hw/<suite>/hw/`, and the ARM kernels built into `../build/hw/arm/` (`make -C model/hw arm`).
- **`hw_suite check collide2 [DIR]`,** which judges caique's readouts.

## The RTL core, when it comes

- **Bus contract.** The core's bus side is caique's ARM port, not a wren7-defined bus. The contract is in caique-rtl `rtl/v1/README.md`, "ARM7DI port contract".
  - `arm_req` is held from the cycle's first clock until `arm_ack`.
  - `arm_ack` and `arm_rdata` come in the cycle's last clock.
  - A cycle starts on a DSP step boundary and lasts 8 clocks (a locked write 4).
  - L/M are acknowledged in their own clock.
  - Internal cycles make no request.
- **Clock.** The core runs on the AICA's clock (22.5792 MHz, measured).
- **nFIQ** comes from caique's interrupt controller.
- **Verification.** caique's `rtl/v1/tb/armjob_tb.cpp` will run the core in place of the `Arm7DI` model. The target is cycle-identical results to the model run on every console suite.

## When you change wren7

Changes to `DcArmBus` / `DcWaits` timing rules, `Arm7Bus` / `bus_cycle()` or `hwjob.h` can move caique's results. Run caique's two ARM-side checks:

```
make -C ../caique-rtl/rtl/v1 arb               # random ARM streams vs DcArmBus: 12 seeds, 0 mismatches
../caique-rtl/rtl/v1/tb/armjob_all.sh          # every suite here on the RTL: 0 wait mismatches expected
```

A difference means one side is wrong, and console data decides which. On 2026-09-24, for example, the RTL's ADPCM fetch rule matched the console's kernels where `DcArmBus` did not. The rule then moved here as `adpcm_fetch`. Record ARM-timing conclusions in `model/NOTES.md` / `model/TIMING.md`, and cross-check results in caique.
