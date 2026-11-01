# wren7-rtl -- the ARM7DI (AICA sound CPU): C++ model (model/, its own Makefile) and the v1 RTL (rtl/v1).
#
#   make v1-lint      Verilator -Wall on wren7_core and wren7_aica
#   make v1           build the lockstep testbench (tb/core_tb.cpp: RTL vs model/src/arm7di.cpp, cycle by cycle) and
#                     run the quick gate: vendor suite, qemu_diff batches, 200 chaos seeds, 4 console job suites
#   make v1-full      every console job suite (model/tests/hw/*, 3392 jobs) and 2000 chaos seeds, in parallel
#   make quartus-lint the Quartus testing project's fileset as Quartus reads it (SYNTHESIS, qip order)
#   make clean        build/rtl_v1
# Build outputs go to build/ (git-ignored).  The model side needs `make -C model test` once (it assembles the
# qemu_diff images) and the ARM kernels in build/hw/arm (`make -C model/hw arm`, for the job suites).
VERILATOR ?= verilator
NJOBS     := $(shell nproc 2>/dev/null || echo 4)
CWD       := $(shell pwd)
BUILD      = build/rtl_v1
V1_SRC     = rtl/v1/wren7_regfile.sv rtl/v1/wren7_shifter.sv rtl/v1/wren7_core.sv
V1_TOP_SRC = $(V1_SRC) rtl/v1/wren7_aica.sv
LINT       = -Wall -Wno-DECLFILENAME -Wno-UNUSEDSIGNAL -Wno-PINCONNECTEMPTY
MODEL_SRC  = $(CWD)/model/src/arm7di.cpp $(CWD)/model/src/dc_arm_map.cpp
TB         = $(BUILD)/obj/core_tb
QUICK_JOBS = timing,vendor,fiqcost,mulsem
SUITES     = $(notdir $(patsubst %/jobs.txt,%,$(wildcard model/tests/hw/*/jobs.txt)))

.PHONY: v1-lint v1 v1-build v1-full quartus-lint clean

v1-lint:
	$(VERILATOR) --lint-only $(LINT) --top-module wren7_core $(V1_SRC)
	$(VERILATOR) --lint-only $(LINT) --top-module wren7_aica $(V1_TOP_SRC)

$(TB): $(V1_SRC) tb/core_tb.cpp model/src/arm7di.cpp model/src/arm7di.h model/src/dc_arm_map.cpp \
       model/src/dc_arm_map.h model/tools/hwjob.h
	@mkdir -p $(BUILD)
	+$(VERILATOR) --cc --exe --build -O3 --x-assign fast --x-initial fast -Wno-fatal -Wno-WIDTH -j $(NJOBS) \
	  -CFLAGS "-O2 -std=c++17 -I$(CWD)/model/src -I$(CWD)/model/tools" \
	  --top-module wren7_core $(V1_SRC) $(CWD)/tb/core_tb.cpp $(MODEL_SRC) --Mdir $(BUILD)/obj -o core_tb

v1-build: $(TB)

v1: $(TB)
	cd model && ../$(TB) vendor qemu chaos 200 jobs $(QUICK_JOBS)

v1-full: $(TB)
	@mkdir -p $(BUILD)/full
	cd model && (for s in $(SUITES); do echo "jobs $$s"; done; for k in $$(seq 0 15); do echo "chaos 125 $$((1 + 125 * k))"; done; \
	  echo vendor; echo qemu) | xargs -P $(NJOBS) -I{} sh -c '../$(TB) {} > ../$(BUILD)/full/"$$(echo {} | tr " " _)".txt 2>&1; true'
	@cat $(BUILD)/full/*.txt | grep -v "^MISMATCH" | sort
	@if grep -q "^MISMATCH" $(BUILD)/full/*.txt; then grep -h "^MISMATCH" $(BUILD)/full/*.txt | head -20; exit 1; fi

quartus-lint:
	$(VERILATOR) --lint-only $(LINT) +define+SYNTHESIS=1 --top-module wren7_rtl_test \
	  $$(sed -n 's/^set_global_assignment -name SYSTEMVERILOG_FILE //p' quartus/testing/wren7-rtl/wren7-rtl.files.qip | \
	     sed 's|^\.\./\.\./\.\./||; s|^wren7_rtl_test.sv$$|quartus/testing/wren7-rtl/wren7_rtl_test.sv|')

clean:
	rm -rf $(BUILD)
