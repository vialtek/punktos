// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <arch/x86/mmu.h>

#define BCD_PHYS_BOOTSTRAP_PML4_OFFSET 0
#define BCD_PHYS_KERNEL_PML4_OFFSET 4
#define BCD_PHYS_GDTR_OFFSET 8
#define BCD_PHYS_LM_ENTRY_OFFSET 20
#define BCD_LM_CS_OFFSET 24
#define BCD_CPU_COUNTER_OFFSET 28
#define BCD_CPU_WAITING_OFFSET 32
#define BCD_KSTACK_BASE_OFFSET 40

#ifndef ASSEMBLY
#include <lk/compiler.h>
#include <vm/vm.h>

__BEGIN_CDECLS

#define PHYS_BOOTSTRAP_PAGE 0x9e000

// Markers for the application processor bootstrap code region
extern void x86_bootstrap16_start(void);
extern void x86_bootstrap16_end(void);

// 64-bit entry points that bootstrap might transition to
extern void _x86_secondary_cpu_long_mode_entry(void);

struct __PACKED x86_bootstrap16_data {
    // Physical address of identity PML4
    uint32_t phys_bootstrap_pml4;
    // Physical address of the kernel PML4
    uint32_t phys_kernel_pml4;
    // Physical address of GDTR
    uint8_t phys_gdtr[10];
    uint16_t __pad;

    // Ordering of these two matter; they should be usable by retfl
    // Physical address of long mode entry point
    uint32_t phys_long_mode_entry;
    // 64-bit code segment to use
    uint32_t long_mode_cs;
};

struct __PACKED x86_ap_bootstrap_data {
    struct x86_bootstrap16_data hdr;

    // Counter for APs to use to determine CPU IDs
    uint32_t cpu_id_counter;
    // Pointer to value to use to determine when APs are done with boot
    volatile int  *cpu_waiting_counter;
    // Virtual address of initial kstacks, one kstack per secondary cpu
    uint64_t kstack_base[SMP_MAX_CPUS - 1];
};

// Upon success, returns a pointer to the bootstrap aspace and to the
// virtual address of the bootstrap data.  It is the caller's
// responsibility to free the aspace and unmap the aperature.
status_t x86_bootstrap16_prep(
        paddr_t bootstrap_phys_addr,
        uintptr_t entry64,
        vmm_aspace_t **temp_aspace,
        void **bootstrap_aperature);

STATIC_ASSERT(sizeof(struct x86_ap_bootstrap_data) <= PAGE_SIZE);
STATIC_ASSERT(__offsetof(struct x86_bootstrap16_data, phys_bootstrap_pml4) == BCD_PHYS_BOOTSTRAP_PML4_OFFSET);
STATIC_ASSERT(__offsetof(struct x86_bootstrap16_data, phys_kernel_pml4) == BCD_PHYS_KERNEL_PML4_OFFSET);
STATIC_ASSERT(__offsetof(struct x86_bootstrap16_data, phys_gdtr) == BCD_PHYS_GDTR_OFFSET);
STATIC_ASSERT(__offsetof(struct x86_bootstrap16_data, phys_long_mode_entry) == BCD_PHYS_LM_ENTRY_OFFSET);
STATIC_ASSERT(__offsetof(struct x86_bootstrap16_data, long_mode_cs) == BCD_LM_CS_OFFSET);

STATIC_ASSERT(__offsetof(struct x86_ap_bootstrap_data, hdr) == 0);
STATIC_ASSERT(__offsetof(struct x86_ap_bootstrap_data, cpu_id_counter) == BCD_CPU_COUNTER_OFFSET);
STATIC_ASSERT(__offsetof(struct x86_ap_bootstrap_data, cpu_waiting_counter) == BCD_CPU_WAITING_OFFSET);
STATIC_ASSERT(__offsetof(struct x86_ap_bootstrap_data, kstack_base) == BCD_KSTACK_BASE_OFFSET);

__END_CDECLS

#endif // ASSEMBLY
