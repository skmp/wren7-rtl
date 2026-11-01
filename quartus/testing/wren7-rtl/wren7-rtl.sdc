# wren7_rtl_test runs on a real top-level `clk` pin: the core's clock. In the AICA the core runs at the sound clock,
# 22.5792 MHz (44.29 ns, measured: model/NOTES.md "Clocks"), either directly or as ce pulses of a faster clock; ce is
# a clock enable here, so every path is timed as single-cycle.
#
# Target 10 ns (100 MHz), as shrike4's and polly2's harnesses, to show the margin over 44.29 ns. The actual Fmax is
# read from the STA Fmax Summary regardless of this period.
create_clock -name clk -period 10.000 [get_ports clk]
derive_clock_uncertainty
