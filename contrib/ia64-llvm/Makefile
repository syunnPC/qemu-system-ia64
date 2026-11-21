# SPDX-License-Identifier: GPL-2.0-or-later
# make VMM_SRC=path/to/ia64-vmm-new [BUILD=dir]
CC ?= cc
LLVM_CONFIG ?= llvm-config
BUILD ?= build
ifndef VMM_SRC
$(error Set VMM_SRC to an ia64-vmm-new checkout)
endif
VMM_SRC := $(abspath $(VMM_SRC))
CFLAGS += -std=gnu11 -O2 -fPIC -fvisibility=hidden -ffunction-sections -fdata-sections \
          -I$(VMM_SRC)/include -I$(VMM_SRC) -DIA64VMM_HOSTED=1 $(shell $(LLVM_CONFIG) --cflags)
DONOR = engine/tier1/form engine/tier1/lift engine/tier1/ir engine/tier1/helpers \
        engine/tier1/verify engine/tier1/stats engine/cache/cache ia64/state ia64/decoder \
        ia64/semantics ia64/decode/decode core/log
OBJ = $(addprefix $(BUILD)/,$(addsuffix .o,$(DONOR) frontend lower))
$(BUILD)/ia64-llvm.so: $(OBJ)
	$(CC) -shared -Wl,--gc-sections,-Bsymbolic,-z,defs $^ -o $@.tmp \
	    $(shell $(LLVM_CONFIG) --ldflags --libs core orcjit native passes --system-libs)
	mv $@.tmp $@
$(BUILD)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@
$(BUILD)/%.o: $(VMM_SRC)/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@
-include $(OBJ:.o=.d)

CROSS_IA64 ?= ia64-linux-gnu-
.PHONY: check
check: $(BUILD)/ia64-llvm.so $(BUILD)/test $(BUILD)/region-test.bin
	$(BUILD)/test $(abspath $(BUILD)/ia64-llvm.so) $(BUILD)/region-test.bin
$(BUILD)/test: test.c region.h
	@mkdir -p $(@D)
	$(CC) -O2 -Wall -Wextra $< -ldl -o $@
$(BUILD)/region-test.bin: region-test.s
	@mkdir -p $(@D)
	$(CROSS_IA64)as -o $(BUILD)/region-test.o $<
	$(CROSS_IA64)objcopy -j .text -O binary $(BUILD)/region-test.o $@

QEMU ?= ../../build/qemu-system-ia64
PYTHON ?= python3
.PHONY: check-qemu
check-qemu: $(BUILD)/ia64-llvm.so $(BUILD)/region-smc.bin \
            $(BUILD)/region-fault.bin $(BUILD)/region-vector.bin
	CROSS_IA64="$(CROSS_IA64)" $(PYTHON) tests/check-qemu.py \
	    "$(abspath $(QEMU))" "$(abspath $(BUILD))"
$(BUILD)/region-%.bin: $(BUILD)/region-%.elf
	$(CROSS_IA64)objcopy -j .text -O binary $< $@
$(BUILD)/region-%.elf: tests/region-%.s
	@mkdir -p $(@D)
	$(CROSS_IA64)as -o $@.o $<
	$(CROSS_IA64)ld -Ttext=0x2000000 -e 0x2000000 -o $@ $@.o
.PRECIOUS: $(BUILD)/region-%.elf
