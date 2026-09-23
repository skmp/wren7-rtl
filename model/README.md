# wren7 ARM7DI model

Bus-cycle accurate C++ model of the ARM7DI, the Dreamcast AICA's sound CPU.  It is the reference for the RTL that
comes next.  Timing comes from the data sheet's per-cycle bus sequences plus the memory system.
`DcWaits::dreamcast()` holds the timing measured on the console (22.5792 MHz core, 4-MCLK slot grid, 8-MCLK
accesses).  It predicts every console timing kernel to within 0.1 MCLK per iteration.  Findings, choices and open hardware questions: **[NOTES.md](NOTES.md)**.

## Layout

| path | what |
|---|---|
| `src/arm7di.{h,cpp}` | the core: `step()` = one instruction / exception entry as its chapter 9 bus-cycle sequence |
| `src/dc_arm_map.{h,cpp}` | the Dreamcast sound bus as an `Arm7Bus`: 2 MB wave RAM, AICA register window, FIQ glue, `DcWaits` |
| `tools/wren7run.cpp` | run a raw binary in the Dreamcast map (traces, waits, dumps) |
| `tools/vendor_tests.cpp` | the vendor suite (`../vendor/arm7di-tests-dreamcast/bins`) |
| `tools/timing_check.cpp` | per-cycle comparison with the data sheet tables (encodings from `tests/timing_ops.s`) |
| `tools/qemu_diff.cpp` | differential fuzz vs qemu-arm sa1100; generated batches are kept in `tests/qemu_diff/` |
| `tools/hwjob.h`, `tools/hwjob_model.cpp` | console job files parsed and executed on the model |
| `tools/hw_suite.cpp` | console correctness suites: vendor, qemu_diff, mulsem (gen / check) |
| `tools/hw_timing.cpp` | console timing kernels: gen / predict / check / fit |
| `hw/runner.c`, `hw/arm/` | KOS job runner; ARM side: `crt.inc` (vectors, parameters), `stub.s` (done stub), probe kernels |
| `run_hw.sh SUITE` | build and run `tests/hw/SUITE/jobs.txt` on the console through shrike4 `hwrun.sh` |
| `tests/` | kept tests and the latest summaries (`*_summary.txt`); console suites in `tests/hw/<suite>/` (jobs, console and model results, SUMMARY.txt) |

Build outputs go to `../build/model/` (git-ignored); scratch work to `../agents/`.

## Running (from wren7-rtl/model)

    make                 # model + tools -> ../build/model/
    make test            # vendor suite, timing checks, kept qemu_diff batches
    ../build/model/wren7run --bus-trace - prog.bin              # stops at 0xDEADBEEF by default
    ../build/model/qemu_diff --gen --seed 100 --batches 10 --cases 4000 --tests ../agents/qd   # new batches

Console suites (one upload each, a few seconds):

    ../build/model/hw_suite gen vendor && ./run_hw.sh vendor && ../build/model/hw_suite check vendor
    ../build/model/hw_timing gen && ../build/model/hw_timing predict && ./run_hw.sh timing && ../build/model/hw_timing check

Toolchain: `/opt/toolchains/dc-dca3/arm-eabi/bin` (`arm-eabi-as -mcpu=arm7`; override with `DC_ARM_BIN`);
`qemu-arm-static` only for `qemu_diff --gen`.
