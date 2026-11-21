# SPDX-License-Identifier: GPL-2.0-or-later
CC ?= cc
LLVM_CONFIG ?= llvm-config
BUILD ?= build
CFLAGS ?= -O2
LLVM_CFLAGS := $(shell $(LLVM_CONFIG) --cflags)
OBJ = $(addprefix $(BUILD)/,frontend.o lower.o decoder.o decode.o)
$(BUILD)/ia64-llvm.so: $(OBJ)
	$(CC) $(LDFLAGS) -shared -Wl,--gc-sections,-Bsymbolic,-z,defs $^ -o $@.tmp \
	    $(shell $(LLVM_CONFIG) --ldflags --libs core orcjit native passes --system-libs)
	mv $@.tmp $@
$(BUILD)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -I../.. $(LLVM_CFLAGS) $(CFLAGS) -std=gnu11 -fPIC \
	    -fvisibility=hidden -ffunction-sections -fdata-sections -MMD -MP -c $< -o $@
vpath %.c ../../target/ia64 ../../target/ia64/decode
-include $(OBJ:.o=.d) $(BUILD)/test.d

CROSS_IA64 ?= ia64-linux-gnu-
.PHONY: check
check: $(BUILD)/ia64-llvm.so $(BUILD)/test $(BUILD)/region-test.bin \
       $(BUILD)/region-memory.bin $(BUILD)/region-integer.bin
	$(BUILD)/test $(abspath $(BUILD)/ia64-llvm.so) \
	    $(BUILD)/region-test.bin $(BUILD)/region-memory.bin \
	    $(BUILD)/region-integer.bin
$(BUILD)/test: test.c region.h
	@mkdir -p $(@D)
	$(CC) -I../.. -O2 -Wall -Wextra -MMD -MP $< -ldl -o $@
$(BUILD)/region-test.bin: region-test.s
	@mkdir -p $(@D)
	$(CROSS_IA64)as -o $(BUILD)/region-test.o $<
	$(CROSS_IA64)objcopy -j .text -O binary $(BUILD)/region-test.o $@

QEMU ?= ../../build/qemu-system-ia64
PYTHON ?= python3
.PHONY: check-qemu
check-qemu: $(BUILD)/ia64-llvm.so $(BUILD)/region-smc.bin \
            $(BUILD)/region-fault.bin $(BUILD)/region-vector.bin \
            $(BUILD)/region-alat.bin $(BUILD)/region-boundary.bin
	CROSS_IA64="$(CROSS_IA64)" $(PYTHON) tests/check-qemu.py \
	    "$(abspath $(QEMU))" "$(abspath $(BUILD))"
$(BUILD)/region-%.bin: $(BUILD)/region-%.elf
	$(CROSS_IA64)objcopy -j .text -O binary $< $@
$(BUILD)/region-%.elf: tests/region-%.s
	@mkdir -p $(@D)
	$(CROSS_IA64)as -o $@.o $<
	$(CROSS_IA64)ld -Ttext=0x2000000 -e 0x2000000 -o $@ $@.o
.PRECIOUS: $(BUILD)/region-%.elf
