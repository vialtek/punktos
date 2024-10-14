# Copyright 2016 The Fuchsia Authors
# Copyright (c) 2008-2015 Travis Geiselbrecht
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

WITH_KERNEL_VM=1
WITH_LINKER_GC ?= 1

ifeq ($(SUBARCH),x86-32)
MEMBASE ?= 0x00000000
KERNEL_BASE ?= 0x80000000
KERNEL_SIZE ?= 0x40000000 # 1GB
KERNEL_LOAD_OFFSET ?= 0x00100000
HEADER_LOAD_OFFSET ?= 0x00010000
PHYS_HEADER_LOAD_OFFSET ?= 0x00002000
KERNEL_ASPACE_BASE ?= 0x80000000
KERNEL_ASPACE_SIZE ?= 0x7ff00000
USER_ASPACE_BASE   ?= 0x01000000 # 16MB
USER_ASPACE_SIZE   ?= 0x7e000000 # full 2GB - 32MB

SUBARCH_DIR := $(LOCAL_DIR)/32
endif
ifeq ($(SUBARCH),x86-64)
GLOBAL_DEFINES += \
	IS_64BIT=1 \

MEMBASE ?= 0
KERNEL_BASE ?= 0xffffffff80000000
KERNEL_SIZE ?= 0x40000000 # 1GB
KERNEL_LOAD_OFFSET ?= 0x00100000
HEADER_LOAD_OFFSET ?= 0x00010000
PHYS_HEADER_LOAD_OFFSET ?= 0x00002000
KERNEL_ASPACE_BASE ?= 0xffffff8000000000UL # -512GB
KERNEL_ASPACE_SIZE ?= 0x0000008000000000UL
USER_ASPACE_BASE   ?= 0x0000000001000000UL # 16MB
USER_ASPACE_SIZE   ?= 0x00007fffff000000UL # full user address space size - 16MB
SUBARCH_DIR := $(LOCAL_DIR)/64
endif

SUBARCH_BUILDDIR := $(call TOBUILDDIR,$(SUBARCH_DIR))

GLOBAL_DEFINES += \
	ARCH_$(SUBARCH)=1 \
	MEMBASE=$(MEMBASE) \
	KERNEL_BASE=$(KERNEL_BASE) \
	KERNEL_SIZE=$(KERNEL_SIZE) \
	KERNEL_LOAD_OFFSET=$(KERNEL_LOAD_OFFSET) \
	KERNEL_ASPACE_BASE=$(KERNEL_ASPACE_BASE) \
	KERNEL_ASPACE_SIZE=$(KERNEL_ASPACE_SIZE) \
	PHYS_HEADER_LOAD_OFFSET=$(PHYS_HEADER_LOAD_OFFSET) \
	USER_ASPACE_BASE=$(USER_ASPACE_BASE) \
	USER_ASPACE_SIZE=$(USER_ASPACE_SIZE) \
	PLATFORM_HAS_DYNAMIC_TIMER=1

MODULE_SRCS += \
	$(SUBARCH_DIR)/start.S \
	$(SUBARCH_DIR)/asm.S \
	$(SUBARCH_DIR)/exceptions.S \
	$(SUBARCH_DIR)/ops.S \
\
	$(LOCAL_DIR)/arch.c \
	$(LOCAL_DIR)/cache.c \
	$(LOCAL_DIR)/descriptor.c \
	$(LOCAL_DIR)/faults.c \
	$(LOCAL_DIR)/feature.c \
	$(LOCAL_DIR)/fpu.c \
	$(LOCAL_DIR)/gdt.S \
	$(LOCAL_DIR)/header.S \
	$(LOCAL_DIR)/idt.c \
	$(LOCAL_DIR)/ioapic.c \
	$(LOCAL_DIR)/ioport.c \
	$(LOCAL_DIR)/lapic.c \
	$(LOCAL_DIR)/mmu.cpp \
	$(LOCAL_DIR)/mmu_mem_types.c \
	$(LOCAL_DIR)/mp.c \
	$(LOCAL_DIR)/thread.c \
	$(LOCAL_DIR)/user_copy.c \

ifeq ($(SUBARCH),x86-64)
MODULE_SRCS += \
	$(SUBARCH_DIR)/syscall.S \
	$(SUBARCH_DIR)/user_copy.S
endif

include $(LOCAL_DIR)/toolchain.mk

# Enable SMP for x86-64
ifeq ($(SUBARCH),x86-64)
GLOBAL_DEFINES += \
	WITH_SMP=1 \
	SMP_MAX_CPUS=16
MODULE_SRCS += \
	$(SUBARCH_DIR)/bootstrap16.c \
	$(SUBARCH_DIR)/start16.S \
	$(SUBARCH_DIR)/uspace_entry.S \
	$(SUBARCH_DIR)/smp.c
endif # SUBARCH x86-64

# set the default toolchain to x86 elf and set a #define
ifeq ($(SUBARCH),x86-64)
ifndef TOOLCHAIN_PREFIX
TOOLCHAIN_PREFIX := $(ARCH_x86_64_TOOLCHAIN_PREFIX)
endif
endif # SUBARCH x86-64

#$(warning ARCH_x86_TOOLCHAIN_PREFIX = $(ARCH_x86_TOOLCHAIN_PREFIX))
#$(warning ARCH_x86_64_TOOLCHAIN_PREFIX = $(ARCH_x86_64_TOOLCHAIN_PREFIX))
#$(warning TOOLCHAIN_PREFIX = $(TOOLCHAIN_PREFIX))

ifeq ($(CLANG),1)
ifeq ($(LIBGCC),)
$(error cannot find runtime library, please set LIBGCC)
endif
else
LIBGCC := $(shell $(TOOLCHAIN_PREFIX)gcc $(CFLAGS) -print-libgcc-file-name)
endif

cc-option = $(shell if test -z "`$(1) $(2) -S -o /dev/null -xc /dev/null 2>&1`"; \
	then echo "$(2)"; else echo "$(3)"; fi ;)

# disable SSP if the compiler supports it; it will break stuff
GLOBAL_CFLAGS += $(call cc-option,$(CC),-fno-stack-protector,)

GLOBAL_COMPILEFLAGS += -gdwarf-2
GLOBAL_COMPILEFLAGS += -fno-pic
ifeq ($(CLANG),1)
GLOBAL_LDFLAGS = -m elf_x86_64
GLOBAL_MODULE_LDFLAGS= -m elf_x86_64
endif
GLOBAL_LDFLAGS += -z max-page-size=4096
ifneq ($(CLANG),1)
KERNEL_COMPILEFLAGS += -falign-jumps=1 -falign-loops=1 -falign-functions=4
endif

# hard disable floating point in the kernel
KERNEL_COMPILEFLAGS += -msoft-float -mno-mmx -mno-sse -mno-sse2 -mno-3dnow -mno-avx -mno-avx2 -DWITH_NO_FP=1
ifneq ($(CLANG),1)
KERNEL_COMPILEFLAGS += -mno-80387 -mno-fp-ret-in-387
endif

ifeq ($(CLANG),1)
GLOBAL_COMPILEFLAGS += -target x86_64-elf -integrated-as
endif

ifeq ($(SUBARCH),x86-64)
KERNEL_COMPILEFLAGS += -mcmodel=kernel
KERNEL_COMPILEFLAGS += -mno-red-zone

# optimization: since fpu is disabled, do not pass flag in rax to varargs routines
# that floating point args are in use.
ifneq ($(CLANG),1)
KERNEL_COMPILEFLAGS += -mskip-rax-setup
endif
endif # SUBARCH x86-64

ARCH_OPTFLAGS := -O2

USER_LINKER_SCRIPT := $(SUBARCH_DIR)/user.ld
LINKER_SCRIPT += $(SUBARCH_BUILDDIR)/kernel.ld

# potentially generated files that should be cleaned out with clean make rule
GENERATED += $(SUBARCH_BUILDDIR)/kernel.ld

# rules for generating the linker scripts
$(SUBARCH_BUILDDIR)/kernel.ld: $(SUBARCH_DIR)/kernel.ld $(wildcard arch/*.ld)
	@echo generating $@
	@$(MKDIR)
	$(NOECHO)sed "s/%MEMBASE%/$(MEMBASE)/;s/%MEMSIZE%/$(MEMSIZE)/;s/%KERNEL_BASE%/$(KERNEL_BASE)/;s/%KERNEL_LOAD_OFFSET%/$(KERNEL_LOAD_OFFSET)/;s/%HEADER_LOAD_OFFSET%/$(HEADER_LOAD_OFFSET)/;s/%PHYS_HEADER_LOAD_OFFSET%/$(PHYS_HEADER_LOAD_OFFSET)/;" < $< > $@.tmp
	@$(call TESTANDREPLACEFILE,$@.tmp,$@)

include make/module.mk
