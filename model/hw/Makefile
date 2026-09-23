# wren7 console runner (KOS) + the ARM programs (arm/).  Use ../run_hw.sh SUITE.
# make needs the KOS environment: source /opt/toolchains/dc/kos/environ.sh
# Output: ../../build/hw/runner.elf, ../../build/hw/arm/*  (build/ is git-ignored).
KOS_CFLAGS += -O2 -std=gnu11
BUILD = ../../build/hw
all: $(BUILD)/runner.elf arm
include $(KOS_BASE)/Makefile.rules
.PHONY: all clean arm
arm:
	$(MAKE) -C arm
$(BUILD)/runner.elf: runner.c
	@mkdir -p $(BUILD)
	kos-cc $(CFLAGS) -DMODEL_ROOT=\"$(abspath ..)\" -o $@ runner.c
clean:
	-rm -rf $(BUILD)
