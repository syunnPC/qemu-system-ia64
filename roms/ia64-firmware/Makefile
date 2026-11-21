# SPDX-License-Identifier: GPL-2.0-or-later
# Thin wrapper around the canonical firmware build script.

# Pass tool overrides without supplying Make's built-in host tool names.
AS      =
CC      =
LD      =
OBJCOPY =
SIZE    =
PYTHON  ?= python3

ifneq ($(origin CROSS),undefined)
export CROSS
endif

BUILD_SCRIPT := build_firmware.sh
MANIFEST := firmware.sources
SOURCES := $(shell sed -e '/^[[:space:]]*$$/d' -e '/^[[:space:]]*#/d' $(MANIFEST))
OUTPUTS := ia64-firmware.bin ia64-firmware.elf ia64-firmware.map \
	ia64-firmware.sections
DEPFILE := ia64-firmware.d

all: $(OUTPUTS)

$(OUTPUTS) &: ia64-firmware.commit $(BUILD_SCRIPT) $(MANIFEST) $(SOURCES) \
	firmware_layout.py
	AS="$(AS)" CC="$(CC)" LD="$(LD)" OBJCOPY="$(OBJCOPY)" SIZE="$(SIZE)" \
		PYTHON="$(PYTHON)" \
		./$(BUILD_SCRIPT) ia64-firmware.bin ia64-firmware.elf \
		ia64-firmware.map ia64-firmware.sections . $(DEPFILE)

ia64-firmware.commit: FORCE
	@printf '%s' "$$(sh ../../scripts/git-source-commit.sh ../..)" > $@.tmp
	@cmp -s $@.tmp $@ && rm -f $@.tmp || mv -f $@.tmp $@

FORCE:

-include $(DEPFILE)

clean:
	rm -f ia64-fw-*.o ia64-fw-*.o.d $(OUTPUTS) $(DEPFILE) ia64-firmware.commit

.PHONY: all clean FORCE
