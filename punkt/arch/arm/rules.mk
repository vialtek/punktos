LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

# can override this in local.mk
ENABLE_THUMB?=true

# default to the regular arm subarch
SUBARCH := arm

GLOBAL_DEFINES += \
	ARM_CPU_$(ARM_CPU)=1

# do set some options based on the cpu core
HANDLED_CORE := false
ifeq ($(ARM_CPU),cortex-a7)
GLOBAL_DEFINES += \
	ARM_WITH_CP15=1 \
	ARM_ISA_ARMv7=1 \
	ARM_ISA_ARMv7A=1 \
	ARM_WITH_VFP=1 \
	ARM_WITH_NEON=1 \
	ARM_WITH_THUMB=1 \
	ARM_WITH_THUMB2=1 \
	ARM_WITH_CACHE=1 \
	ARM_WITH_HYP=1
HANDLED_CORE := true
endif
ifeq ($(ARM_CPU),cortex-a15)
GLOBAL_DEFINES += \
	ARM_WITH_CP15=1 \
	ARM_ISA_ARMv7=1 \
	ARM_ISA_ARMv7A=1 \
	ARM_WITH_THUMB=1 \
	ARM_WITH_THUMB2=1 \
	ARM_WITH_CACHE=1 \
	ARM_WITH_L2=1
ifneq ($(ARM_WITHOUT_VFP_NEON),true)
GLOBAL_DEFINES += \
	ARM_WITH_VFP=1 \
	ARM_WITH_NEON=1
endif
HANDLED_CORE := true
endif
ifeq ($(ARM_CPU),cortex-a8)
GLOBAL_DEFINES += \
	ARM_WITH_CP15=1 \
	ARM_ISA_ARMv7=1 \
	ARM_ISA_ARMv7A=1 \
	ARM_WITH_VFP=1 \
	ARM_WITH_NEON=1 \
	ARM_WITH_THUMB=1 \
	ARM_WITH_THUMB2=1 \
	ARM_WITH_CACHE=1 \
	ARM_WITH_L2=1
HANDLED_CORE := true
endif
ifeq ($(ARM_CPU),cortex-a9)
GLOBAL_DEFINES += \
	ARM_WITH_CP15=1 \
	ARM_ISA_ARMv7=1 \
	ARM_ISA_ARMv7A=1 \
	ARM_WITH_THUMB=1 \
	ARM_WITH_THUMB2=1 \
	ARM_WITH_CACHE=1
HANDLED_CORE := true
endif
ifeq ($(ARM_CPU),cortex-a9-neon)
GLOBAL_DEFINES += \
	ARM_CPU_CORTEX_A9=1 \
	ARM_WITH_CP15=1 \
	ARM_ISA_ARMv7=1 \
	ARM_ISA_ARMv7A=1 \
	ARM_WITH_VFP=1 \
	ARM_WITH_NEON=1 \
	ARM_WITH_THUMB=1 \
	ARM_WITH_THUMB2=1 \
	ARM_WITH_CACHE=1
HANDLED_CORE := true
endif
ifeq ($(ARM_CPU),arm1136j-s)
GLOBAL_DEFINES += \
	ARM_WITH_CP15=1 \
	ARM_ISA_ARMv6=1 \
	ARM_WITH_THUMB=1 \
	ARM_WITH_CACHE=1 \
	ARM_CPU_ARM1136=1
HANDLED_CORE := true
endif
ifeq ($(ARM_CPU),arm1176jzf-s)
GLOBAL_DEFINES += \
	ARM_WITH_CP15=1 \
	ARM_ISA_ARMv6=1 \
	ARM_WITH_VFP=1 \
	ARM_WITH_THUMB=1 \
	ARM_WITH_CACHE=1 \
	ARM_CPU_ARM1136=1
HANDLED_CORE := true
endif

ifneq ($(HANDLED_CORE),true)
$(error $(LOCAL_DIR)/rules.mk doesnt have logic for arm core $(ARM_CPU))
endif

THUMBCFLAGS :=
THUMBINTERWORK :=
ifeq ($(ENABLE_THUMB),true)
THUMBCFLAGS := -mthumb -D__thumb__
endif

GLOBAL_INCLUDES += \
	$(LOCAL_DIR)/$(SUBARCH)/include

ifeq ($(SUBARCH),arm)
MODULE_SRCS += \
	$(LOCAL_DIR)/arm/start.S \
	$(LOCAL_DIR)/arm/asm.S \
	$(LOCAL_DIR)/arm/cache-ops.S \
	$(LOCAL_DIR)/arm/cache.c \
	$(LOCAL_DIR)/arm/debug.c \
	$(LOCAL_DIR)/arm/ops.S \
	$(LOCAL_DIR)/arm/faults.c \
	$(LOCAL_DIR)/arm/mmu.c \
	$(LOCAL_DIR)/arm/thread.c \
	$(LOCAL_DIR)/arm/uspace_entry.S

MODULE_FLOAT_SRCS += \
	$(LOCAL_DIR)/arm/exceptions.S \
	$(LOCAL_DIR)/arm/fpu.c \

MODULE_ARM_OVERRIDE_SRCS := \
	$(LOCAL_DIR)/arm/arch.c

GLOBAL_DEFINES += \
	ARCH_DEFAULT_STACK_SIZE=4096

ARCH_OPTFLAGS := -O2
WITH_LINKER_GC ?= 1

# use the numeric registers when disassembling code
ARCH_OBJDUMP_FLAGS := -Mreg-names-raw

# for arm, have the kernel occupy the entire top 3GB of virtual space,
# but put the kernel itself at 0x80000000.
# this leaves 0x40000000 - 0x80000000 open for kernel space to use.
GLOBAL_DEFINES += \
    KERNEL_ASPACE_BASE=0x40000000 \
    KERNEL_ASPACE_SIZE=0xc0000000

KERNEL_BASE ?= 0x80000000
KERNEL_LOAD_OFFSET ?= 0

GLOBAL_DEFINES += \
    KERNEL_BASE=$(KERNEL_BASE) \
    KERNEL_LOAD_OFFSET=$(KERNEL_LOAD_OFFSET)

# if its requested we build with SMP, arm generically supports 4 cpus
ifeq ($(WITH_SMP),1)
SMP_MAX_CPUS ?= 4
SMP_CPU_CLUSTER_SHIFT ?= 8
SMP_CPU_ID_BITS ?= 24

GLOBAL_DEFINES += \
    WITH_SMP=1 \
    SMP_MAX_CPUS=$(SMP_MAX_CPUS) \
    SMP_CPU_CLUSTER_SHIFT=$(SMP_CPU_CLUSTER_SHIFT) \
    SMP_CPU_ID_BITS=$(SMP_CPU_ID_BITS)

MODULE_SRCS += \
	$(LOCAL_DIR)/arm/mp.c
else
GLOBAL_DEFINES += \
    SMP_MAX_CPUS=1
endif

ifeq (true,$(call TOBOOL,$(WITH_NS_MAPPING)))
GLOBAL_DEFINES += \
    WITH_ARCH_MMU_PICK_SPOT=1
endif

endif

# try to find toolchain
include $(LOCAL_DIR)/toolchain.mk
TOOLCHAIN_PREFIX := $(ARCH_$(ARCH)_TOOLCHAIN_PREFIX)
$(info TOOLCHAIN_PREFIX = $(TOOLCHAIN_PREFIX))

ARCH_COMPILEFLAGS += $(ARCH_$(ARCH)_COMPILEFLAGS)
ARCH_COMPILEFLAGS_NOFLOAT :=
ARCH_COMPILEFLAGS_FLOAT := $(ARCH_$(ARCH)_COMPILEFLAGS_FLOAT)

GLOBAL_COMPILEFLAGS += $(THUMBINTERWORK)

# set the max page size to something more reasonable (defaults to 64K or above)
ARCH_LDFLAGS += -z max-page-size=4096

# userspace support
USER_LINKER_SCRIPT := $(LOCAL_DIR)/user.ld

# find the direct path to libgcc.a for our particular multilib variant
LIBGCC := $(shell $(TOOLCHAIN_PREFIX)gcc $(GLOBAL_COMPILEFLAGS) $(ARCH_COMPILEFLAGS) $(THUMBCFLAGS) -print-libgcc-file-name)
#$(info LIBGCC = $(LIBGCC))
#$(info LIBGCC COMPILEFLAGS = $(GLOBAL_COMPILEFLAGS) $(ARCH_COMPILEFLAGS) $(THUMBCFLAGS))

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

GLOBAL_DEFINES += \
	MEMBASE=$(MEMBASE) \
	MEMSIZE=$(MEMSIZE)

# potentially generated files that should be cleaned out with clean make rule
GENERATED += \
	$(BUILDDIR)/system-onesegment.ld \
	$(BUILDDIR)/system-twosegment.ld

# rules for generating the linker scripts
$(BUILDDIR)/system-onesegment.ld: $(LOCAL_DIR)/system-onesegment.ld $(wildcard arch/*.ld) linkerscript.phony
	@echo generating $@
	@$(MKDIR)
	$(NOECHO)sed "s/%MEMBASE%/$(MEMBASE)/;s/%MEMSIZE%/$(MEMSIZE)/;s/%KERNEL_BASE%/$(KERNEL_BASE)/;s/%KERNEL_LOAD_OFFSET%/$(KERNEL_LOAD_OFFSET)/" < $< > $@.tmp
	@$(call TESTANDREPLACEFILE,$@.tmp,$@)

$(BUILDDIR)/system-twosegment.ld: $(LOCAL_DIR)/system-twosegment.ld $(wildcard arch/*.ld) linkerscript.phony
	@echo generating $@
	@$(MKDIR)
	$(NOECHO)sed "s/%ROMBASE%/$(ROMBASE)/;s/%MEMBASE%/$(MEMBASE)/;s/%MEMSIZE%/$(MEMSIZE)/" < $< > $@.tmp
	@$(call TESTANDREPLACEFILE,$@.tmp,$@)

linkerscript.phony:
.PHONY: linkerscript.phony

# arm specific script to try to guess stack usage
$(OUTELF).stack: LOCAL_DIR:=$(LOCAL_DIR)
$(OUTELF).stack: $(OUTELF)
	$(NOECHO)echo generating stack usage $@
	$(NOECHO)$(OBJDUMP) $(ARCH_OBJDUMP_FLAGS) -d $< | $(LOCAL_DIR)/stackusage | $(CPPFILT) | sort -n -k 1 -r > $@

EXTRA_BUILDDEPS += $(OUTELF).stack
GENERATED += $(OUTELF).stack

include make/module.mk
