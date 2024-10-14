// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2009 Corey Tabaka
// Copyright (c) 2015 Intel Corporation
// Copyright (c) 2016 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT


#include <assert.h>
#include <lk/compiler.h>
#include <lk/debug.h>
#include <lk/err.h>
#include <lk/trace.h>
#include <arch.h>
#include <arch/ops.h>
#include <arch/mp.h>
#include <arch/x86.h>
#include <arch/x86/descriptor.h>
#include <arch/x86/feature.h>
#include <arch/x86/mmu.h>
#include <arch/x86/mp.h>
#include <arch/mmu.h>
#include <vm/vm.h>
#include <lib/console.h>
#include <lk/init.h>
#include <lk/main.h>
#include <platform.h>
#include <sys/types.h>
#include <string.h>

#define LOCAL_TRACE 0

/* early stack */
uint8_t _kstack[PAGE_SIZE] __ALIGNED(16);

/* save a pointer to the multiboot information coming in from whoever called us */
/* make sure it lives in .data to avoid it being wiped out by bss clearing */
__SECTION(".data") void *_multiboot_info;

/* also save a pointer to the boot_params structure */
__SECTION(".data") void *_zero_page_boot_params;

void arch_early_init(void)
{
    x86_mmu_early_init();
}

void arch_init(void)
{
    x86_mmu_init();
}

void arch_chain_load(void *entry, ulong arg0, ulong arg1, ulong arg2, ulong arg3)
{
    PANIC_UNIMPLEMENTED;
}

void arch_enter_uspace(vaddr_t entry_point, vaddr_t user_stack_top, void* thread_arg)
{
    LTRACEF("entry 0x%lx user stack 0x%lx\n", entry_point, user_stack_top);

    DEBUG_ASSERT(IS_ALIGNED(user_stack_top, 16));
    user_stack_top -= 8; /* start the user stack 8 byte unaligned, which is how the abi expects it */

    arch_disable_ints();

    /* default user space flags:
     * IOPL 0
     * Interrupts enabled
     */
    ulong flags = (0 << X86_FLAGS_IOPL_SHIFT) | X86_FLAGS_IF;

    /* check that we're probably still pointed at the kernel gs */
    DEBUG_ASSERT(is_kernel_address(read_msr(X86_MSR_IA32_GS_BASE)));

    /* set up user's fs: gs: base */
    write_msr(X86_MSR_IA32_FS_BASE, 0);

    /* set the KERNEL_GS_BASE msr here, because we're going to swapgs below */
    write_msr(X86_MSR_IA32_KERNEL_GS_BASE, 0);

    /* TODO: clear all of the other registers to avoid leading kernel state */

    x86_uspace_entry(thread_arg, user_stack_top, flags, entry_point);
    __UNREACHABLE;
}

#if WITH_SMP
#include <arch/x86/apic.h>
void x86_secondary_entry(uint8_t asm_cpu_num, volatile int *aps_still_booting)
{
    x86_init_percpu(asm_cpu_num);
    // Would prefer this to be in init_percpu, but there is a dependency on a
    // page mapping existing, and the BP calls thta before the VM subsystem is
    // initialized.
    apic_local_init();

    // Signal that this CPU is initialized
    atomic_add(aps_still_booting, -1);

    /* run early secondary cpu init routines up to the threading level */
    lk_init_level(LK_INIT_FLAG_SECONDARY_CPUS, LK_INIT_LEVEL_EARLIEST, LK_INIT_LEVEL_THREADING - 1);

    lk_secondary_cpu_entry();

    // lk_secondary_cpu_entry only returns on an error, halt the core in this
    // case.
    arch_disable_ints();
    while (1) {
      x86_hlt();
    }
}
#endif

static int cmd_cpu(int argc, const cmd_args *argv)
{
    if (argc < 2) {
notenoughargs:
        printf("not enough arguments\n");
usage:
        printf("usage:\n");
        printf("%s features\n", argv[0].str);
        return ERR_GENERIC;
    }

    if (!strcmp(argv[1].str, "features")) {
        x86_feature_debug();
    } else {
        printf("unknown command\n");
        goto usage;
    }

    return NO_ERROR;
}

STATIC_COMMAND_START
#if LK_DEBUGLEVEL > 0
STATIC_COMMAND("cpu", "cpu info commands", &cmd_cpu)
#endif
STATIC_COMMAND_END(cpu);
