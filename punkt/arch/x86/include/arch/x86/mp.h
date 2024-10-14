// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2016 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

/* describes the per cpu structure pointed to by gs: in the kernel */

/* offsets into this structure, used by assembly */
#define PERCPU_DIRECT_OFFSET           0x0
#define PERCPU_CURRENT_THREAD_OFFSET   0x8
#define PERCPU_KERNEL_SP_OFFSET        0x10
#define PERCPU_SAVED_USER_SP_OFFSET    0x18
#define PERCPU_DEFAULT_TSS_OFFSET      0x30

#ifndef ASSEMBLY

#include <lk/compiler.h>
#include <stdint.h>
#include <arch/x86.h>
#include <arch/x86/idt.h>

__BEGIN_CDECLS

struct thread;

struct x86_percpu {
    /* a direct pointer to ourselves */
    struct x86_percpu *direct;

    /* the current thread */
    struct thread *current_thread;

    /* our current kernel sp, to be loaded by syscall */
    // TODO: Remove this and replace with a fetch from
    // the current tss?
    uintptr_t kernel_sp;

    /* temporarily saved during a syscall */
    uintptr_t saved_user_sp;

    /* local APIC id */
    uint32_t apic_id;

    /* CPU number */
    uint8_t cpu_num;

    /* This CPU's default TSS */
    tss_t __ALIGNED(16) default_tss;

    /* The IDT for this CPU */
    struct idt idt;
    /* Reserved space for interrupt stacks */
    uint8_t interrupt_stacks[NUM_ASSIGNED_IST_ENTRIES][PAGE_SIZE];
};

STATIC_ASSERT(__offsetof(struct x86_percpu, direct) == PERCPU_DIRECT_OFFSET);
STATIC_ASSERT(__offsetof(struct x86_percpu, current_thread) == PERCPU_CURRENT_THREAD_OFFSET);
STATIC_ASSERT(__offsetof(struct x86_percpu, kernel_sp) == PERCPU_KERNEL_SP_OFFSET);
STATIC_ASSERT(__offsetof(struct x86_percpu, saved_user_sp) == PERCPU_SAVED_USER_SP_OFFSET);
STATIC_ASSERT(__offsetof(struct x86_percpu, default_tss) == PERCPU_DEFAULT_TSS_OFFSET);

/* needs to be run very early in the boot process from start.S and as each cpu is brought up */
void x86_init_percpu(uint8_t cpu_num);

void x86_set_local_apic_id(uint32_t apic_id);

// Allocate all of the necessary structures for all of the APs to run.
status_t x86_allocate_ap_structures(uint8_t cpu_count);

static inline struct x86_percpu *x86_get_percpu(void)
{
#if ARCH_X86_64
    return (struct x86_percpu *)x86_read_gs_offset(PERCPU_DIRECT_OFFSET);
#else
    /* x86-32 does not yet support SMP and thus does not need a gs: pointer to point
     * at the percpu structure
     */
    STATIC_ASSERT(SMP_MAX_CPUS == 1);
    extern struct x86_percpu bp_percpu;
    return &bp_percpu;
#endif
}

static inline struct thread *arch_get_current_thread(void)
{
    return x86_get_percpu()->current_thread;
}

static inline void arch_set_current_thread(struct thread *t)
{
    x86_get_percpu()->current_thread = t;
}

static inline uint arch_curr_cpu_num(void)
{
    return x86_get_percpu()->cpu_num;
}

extern uint8_t x86_num_cpus;
static uint arch_max_num_cpus(void)
{
    return x86_num_cpus;
}

/* set on every context switch and before entering user space */
static inline void x86_set_percpu_kernel_sp(uintptr_t sp)
{
    x86_get_percpu()->kernel_sp = sp;
}

enum handler_return x86_ipi_generic_handler(void);
enum handler_return x86_ipi_reschedule_handler(void);

__END_CDECLS

#endif

