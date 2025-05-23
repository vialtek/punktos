/*
 * Copyright (c) 2009 Corey Tabaka
 * Copyright (c) 2015 Intel Corporation
 *
 * Use of this source code is governed by a MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT
 */

#include <lk/debug.h>
#include <arch.h>
#include <arch/ops.h>
#include <arch/x86.h>
#include <arch/x86/mmu.h>
#include <arch/x86/descriptor.h>
#include <arch/x86/feature.h>
#include <arch/fpu.h>
#include <arch/mmu.h>
#include <vm/vm.h>
#include <platform.h>
#include <sys/types.h>

/* Describe how start.S sets up the MMU.
 * These data structures are later used by vm routines to lookup pointers
 * to physical pages based on physical addresses.
 */
struct mmu_initial_mapping mmu_initial_mappings[] = {
    /* 64GB of memory mapped where the kernel lives */
    {
        .phys = MEMBASE,
        .virt = KERNEL_ASPACE_BASE,
        .size = PHYSMAP_SIZE, /* x86-64 maps first 64GB by default, 1GB on x86-32 */
        .flags = 0,
        .name = "physmap"
    },

    /* 1GB of memory mapped where the kernel lives */
    {
        .phys = MEMBASE,
        .virt = KERNEL_BASE,
        .size = 1*GB, /* x86 maps first 1GB by default */
        .flags = 0,
        .name = "kernel"
    },

    /* null entry to terminate the list */
    { 0 }
};

/* early stack */
uint8_t _kstack[PAGE_SIZE] __ALIGNED(sizeof(unsigned long));

/* save a pointer to the multiboot information coming in from whoever called us */
/* make sure it lives in .data to avoid it being wiped out by bss clearing */
__SECTION(".data") uint32_t _multiboot_info;

/* main tss */
static tss_t system_tss __ALIGNED(16);

void x86_early_init_percpu(void) {
    // enable caches
    clear_in_cr0(X86_CR0_NW | X86_CR0_CD);

    // configure the system TSS
    // XXX move to a per cpu TSS in the percpu structure
    const uint selector = TSS_SELECTOR_BASE + 8 * arch_curr_cpu_num();
    x86_set_gdt_descriptor(selector, &system_tss, sizeof(system_tss), 1, 0, 0, SEG_TYPE_TSS, 0, 0);
    x86_ltr(selector);

    /* load the kernel's IDT */
    asm("lidt _idtr");

    x86_mmu_early_init_percpu();
    x86_fpu_early_init_percpu();
}

/* early initialization of the system, on the boot cpu, usually before any sort of
 * printf output is available.
 */
void arch_early_init(void) {
    x86_feature_early_init();
    x86_mmu_early_init();
    x86_fpu_early_init();
    x86_early_init_percpu();
}

/* later initialization pass, once the main kernel is initialized and scheduling has begun */
void arch_init(void) {
    x86_feature_init();
    x86_mmu_init();
    x86_fpu_init();
}

void arch_chain_load(void *entry, ulong arg0, ulong arg1, ulong arg2, ulong arg3) {
    PANIC_UNIMPLEMENTED;
}

void arch_enter_uspace(vaddr_t entry_point, vaddr_t user_stack_top, void* thread_arg) {
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
