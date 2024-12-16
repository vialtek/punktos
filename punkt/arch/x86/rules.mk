LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_OPTIONS := extra_warnings

GLOBAL_DEFINES += \
	IS_64BIT=1 \

MEMBASE ?= 0
KERNEL_BASE ?= 0xffffffff80000000
KERNEL_LOAD_OFFSET ?= 0x00200000
KERNEL_ASPACE_BASE ?= 0xffffff8000000000UL # -512GB
KERNEL_ASPACE_SIZE ?= 0x0000008000000000UL
USER_ASPACE_BASE   ?= 0x0000000000000000UL
USER_ASPACE_SIZE   ?= 0x0000800000000000UL

GLOBAL_DEFINES += \
	ARCH_$(SUBARCH)=1 \
	MEMBASE=$(MEMBASE) \
	KERNEL_BASE=$(KERNEL_BASE) \
	KERNEL_LOAD_OFFSET=$(KERNEL_LOAD_OFFSET) \
	KERNEL_ASPACE_BASE=$(KERNEL_ASPACE_BASE) \
	KERNEL_ASPACE_SIZE=$(KERNEL_ASPACE_SIZE) \
	SMP_MAX_CPUS=1 \
	X86_WITH_FPU=1

MODULE_SRCS += \
	$(LOCAL_DIR)/start.S \
	$(LOCAL_DIR)/asm.S \
	$(LOCAL_DIR)/exceptions.S \
	$(LOCAL_DIR)/mmu.c \
	$(LOCAL_DIR)/ops.S \
	$(LOCAL_DIR)/user_copy.S \
	$(LOCAL_DIR)/uspace_entry.S \
	$(LOCAL_DIR)/arch.c \
	$(LOCAL_DIR)/cache.c \
	$(LOCAL_DIR)/descriptor.c \
	$(LOCAL_DIR)/faults.c \
	$(LOCAL_DIR)/feature.c \
	$(LOCAL_DIR)/gdt.S \
	$(LOCAL_DIR)/thread.c \
	$(LOCAL_DIR)/user_copy.c \
	$(LOCAL_DIR)/fpu.c

include $(LOCAL_DIR)/toolchain.mk

ifndef TOOLCHAIN_PREFIX
TOOLCHAIN_PREFIX := $(ARCH_x86_64_TOOLCHAIN_PREFIX)
endif

$(warning ARCH_x86_TOOLCHAIN_PREFIX = $(ARCH_x86_TOOLCHAIN_PREFIX))
$(warning ARCH_x86_64_TOOLCHAIN_PREFIX = $(ARCH_x86_64_TOOLCHAIN_PREFIX))
$(warning TOOLCHAIN_PREFIX = $(TOOLCHAIN_PREFIX))

cc-option = $(shell if test -z "`$(1) $(2) -S -o /dev/null -xc /dev/null 2>&1`"; \
	then echo "$(2)"; else echo "$(3)"; fi ;)

# disable SSP if the compiler supports it; it will break stuff
GLOBAL_CFLAGS += $(call cc-option,$(CC),-fno-stack-protector,)

ARCH_COMPILEFLAGS += -fasynchronous-unwind-tables
ARCH_COMPILEFLAGS += -gdwarf-2
ARCH_COMPILEFLAGS += -fno-pic
ARCH_LDFLAGS += -z max-page-size=4096

ARCH_COMPILEFLAGS += -fno-stack-protector
ARCH_COMPILEFLAGS += -mcmodel=kernel
ARCH_COMPILEFLAGS += -mno-red-zone

# set switches to generate/not generate fpu code
ARCH_COMPILEFLAGS_FLOAT +=
ARCH_COMPILEFLAGS_NOFLOAT += -mgeneral-regs-only

ARCH_COMPILEFLAGS += -march=x86-64
ARCH_OPTFLAGS := -O2

LIBGCC := $(shell $(TOOLCHAIN_PREFIX)gcc $(GLOBAL_COMPILEFLAGS) $(ARCH_COMPILEFLAGS) -print-libgcc-file-name)
$(warning LIBGCC = $(LIBGCC))

USER_LINKER_SCRIPT := $(LOCAL_DIR)/user.ld
LINKER_SCRIPT += $(BUILDDIR)/kernel.ld

# potentially generated files that should be cleaned out with clean make rule
GENERATED += $(BUILDDIR)/kernel.ld

# rules for generating the linker scripts
$(BUILDDIR)/kernel.ld: $(LOCAL_DIR)/kernel.ld $(wildcard arch/*.ld)
	@echo generating $@
	@$(MKDIR)
	$(NOECHO)sed "s/%MEMBASE%/$(MEMBASE)/;s/%MEMSIZE%/$(MEMSIZE)/;s/%KERNEL_BASE%/$(KERNEL_BASE)/;s/%KERNEL_LOAD_OFFSET%/$(KERNEL_LOAD_OFFSET)/" < $< > $@.tmp
	@$(call TESTANDREPLACEFILE,$@.tmp,$@)

include make/module.mk
