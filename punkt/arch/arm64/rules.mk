# Copyright 2016 The Fuchsia Authors
# Copyright (c) 2008-2015 Travis Geiselbrecht
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

# set some options based on the core
ifeq ($(ARM_CPU),cortex-a53)
ARCH_COMPILEFLAGS += -mcpu=$(ARM_CPU)
else
$(error $(LOCAL_DIR)/rules.mk doesnt have logic for arm core $(ARM_CPU))
endif

GLOBAL_DEFINES += \
	ARM64_CPU_$(ARM_CPU)=1 \
	ARM_ISA_ARMV8=1 \
	ARM_ISA_ARMV8A=1 \
	IS_64BIT=1

MODULE_SRCS += \
	$(LOCAL_DIR)/arch.c \
	$(LOCAL_DIR)/asm.S \
	$(LOCAL_DIR)/cache-ops.S \
	$(LOCAL_DIR)/exceptions.S \
	$(LOCAL_DIR)/exceptions_c.c \
	$(LOCAL_DIR)/fpu.c \
	$(LOCAL_DIR)/spinlock.S \
	$(LOCAL_DIR)/start.S \
	$(LOCAL_DIR)/thread.c \
	$(LOCAL_DIR)/user_copy.S \
	$(LOCAL_DIR)/user_copy_c.c \
	$(LOCAL_DIR)/uspace_entry.S

GLOBAL_DEFINES += \
	ARCH_DEFAULT_STACK_SIZE=4096

# if its requested we build with SMP, arm generically supports 4 cpus
ifeq ($(WITH_SMP),1)
SMP_MAX_CPUS ?= 4
SMP_CPU_CLUSTER_SHIFT ?= 8
SMP_CPU_ID_BITS ?= 24 # Ignore aff3 bits for now since they are not next to aff2

GLOBAL_DEFINES += \
    WITH_SMP=1 \
    SMP_MAX_CPUS=$(SMP_MAX_CPUS) \
    SMP_CPU_CLUSTER_SHIFT=$(SMP_CPU_CLUSTER_SHIFT) \
    SMP_CPU_ID_BITS=$(SMP_CPU_ID_BITS)

MODULE_SRCS += \
    $(LOCAL_DIR)/mp.c
else
GLOBAL_DEFINES += \
    SMP_MAX_CPUS=1
endif

ARCH_OPTFLAGS := -O2
WITH_LINKER_GC ?= 1

# we have a mmu and want the vmm/pmm
WITH_KERNEL_VM ?= 1

ifeq ($(WITH_KERNEL_VM),1)

MODULE_SRCS += \
	$(LOCAL_DIR)/mmu.c

KERNEL_ASPACE_BASE ?= 0xffff000000000000
KERNEL_ASPACE_SIZE ?= 0x0001000000000000
USER_ASPACE_BASE   ?= 0x0000000001000000
USER_ASPACE_SIZE   ?= 0x0000fffffe000000

GLOBAL_DEFINES += \
    KERNEL_ASPACE_BASE=$(KERNEL_ASPACE_BASE) \
    KERNEL_ASPACE_SIZE=$(KERNEL_ASPACE_SIZE) \
    USER_ASPACE_BASE=$(USER_ASPACE_BASE) \
    USER_ASPACE_SIZE=$(USER_ASPACE_SIZE)

KERNEL_BASE ?= $(KERNEL_ASPACE_BASE)
KERNEL_LOAD_OFFSET ?= 0

GLOBAL_DEFINES += \
    KERNEL_BASE=$(KERNEL_BASE) \
    KERNEL_LOAD_OFFSET=$(KERNEL_LOAD_OFFSET)

else

KERNEL_BASE ?= $(MEMBASE)
KERNEL_LOAD_OFFSET ?= 0

endif

GLOBAL_DEFINES += \
	MEMBASE=$(MEMBASE) \
	MEMSIZE=$(MEMSIZE)

# try to find the toolchain
include $(LOCAL_DIR)/toolchain.mk
TOOLCHAIN_PREFIX := $(ARCH_$(ARCH)_TOOLCHAIN_PREFIX)
$(info TOOLCHAIN_PREFIX = $(TOOLCHAIN_PREFIX))

ARCH_COMPILEFLAGS += $(ARCH_$(ARCH)_COMPILEFLAGS)

# user space linker script
USER_LINKER_SCRIPT := $(LOCAL_DIR)/user.ld

ifeq ($(CLANG),1)
GLOBAL_LDFLAGS = -m aarch64elf
GLOBAL_MODULE_LDFLAGS= -m aarch64elf
endif
GLOBAL_LDFLAGS += -z max-page-size=4096

# kernel hard disables floating point
KERNEL_COMPILEFLAGS += -mgeneral-regs-only -DWITH_NO_FP=1

ifeq ($(CLANG),1)
GLOBAL_COMPILEFLAGS += -target aarch64-elf -integrated-as
endif

ifeq ($(CLANG),1)
ifeq ($(LIBGCC),)
$(error cannot find runtime library, please set LIBGCC)
endif
else
# figure out which libgcc we need based on our compile flags
LIBGCC := $(shell $(TOOLCHAIN_PREFIX)gcc $(GLOBAL_COMPILEFLAGS) -print-libgcc-file-name)
endif

# make sure some bits were set up
MEMVARS_SET := 0
ifneq ($(MEMBASE),)
MEMVARS_SET := 1
endif
ifneq ($(MEMSIZE),)
MEMVARS_SET := 1
endif
ifeq ($(MEMVARS_SET),0)
$(error missing MEMBASE or MEMSIZE variable, please set in target rules.mk)
endif

# potentially generated files that should be cleaned out with clean make rule
GENERATED += \
	$(BUILDDIR)/system-onesegment.ld

# rules for generating the linker script
$(BUILDDIR)/system-onesegment.ld: $(LOCAL_DIR)/system-onesegment.ld $(wildcard arch/*.ld) linkerscript.phony
	@echo generating $@
	@$(MKDIR)
	$(NOECHO)sed "s/%MEMBASE%/$(MEMBASE)/;s/%MEMSIZE%/$(MEMSIZE)/;s/%KERNEL_BASE%/$(KERNEL_BASE)/;s/%KERNEL_LOAD_OFFSET%/$(KERNEL_LOAD_OFFSET)/" < $< > $@.tmp
	@$(call TESTANDREPLACEFILE,$@.tmp,$@)

linkerscript.phony:
.PHONY: linkerscript.phony

include make/module.mk
