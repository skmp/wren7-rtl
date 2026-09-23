# wren7 console ARM programs: every <name>.s here -> ../../../build/hw/arm/<name>.{elf,bin,sym}.
# stub.s is linked at 0x1FE000, everything else at 0.  Toolchain: $(DC_ARM_BIN) (default dc-dca3 arm-eabi).
DC_ARM_BIN ?= /opt/toolchains/dc-dca3/arm-eabi/bin
T = $(DC_ARM_BIN)/arm-eabi-
BUILD = ../../../build/hw/arm
PROGS = $(basename $(wildcard *.s))
.PHONY: all clean
.SECONDARY:
all: $(PROGS:%=$(BUILD)/%.bin)
$(BUILD)/stub.elf: LINK = 0x1FE000
$(BUILD)/%.elf: %.s crt.inc
	@mkdir -p $(BUILD)
	$(T)as -mcpu=arm7 -I. $< -o $(BUILD)/$*.o
	$(T)ld -Ttext=$(or $(LINK),0) -N -e 0 --no-warn-rwx-segments -o $@ $(BUILD)/$*.o
$(BUILD)/%.bin: $(BUILD)/%.elf
	$(T)objcopy -O binary $< $@
	$(T)nm $< > $(BUILD)/$*.sym
clean:
	-rm -rf $(BUILD)
