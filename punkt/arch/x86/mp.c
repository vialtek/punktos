// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2016 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT


#include <assert.h>
#include <lk/compiler.h>
#include <lk/debug.h>
#include <lk/err.h>
#include <stdio.h>
#include <lk/trace.h>

#include <arch/fpu.h>
#include <arch/mp.h>
#include <arch/ops.h>
#include <arch/x86.h>
#include <arch/x86/apic.h>
#include <arch/x86/descriptor.h>
#include <arch/x86/feature.h>
#include <arch/x86/interrupts.h>
#include <arch/x86/mmu.h>
#include <arch/x86/mp.h>

// TODO: use dev/interrupts
//#include <dev/interrupt.h>
#include <platform/interrupts.h>

struct x86_percpu bp_percpu;
static struct x86_percpu *ap_percpus;
uint8_t x86_num_cpus = 1;

status_t x86_allocate_ap_structures(uint8_t cpu_count)
{
    ASSERT(ap_percpus == NULL);
    ap_percpus = calloc(sizeof(*ap_percpus), cpu_count - 1);
    if (ap_percpus == NULL) {
        return ERR_NO_MEMORY;
    }
    x86_num_cpus = cpu_count;
    return NO_ERROR;
}

void x86_init_percpu(uint8_t cpu_num)
{
    struct x86_percpu *percpu;
    if (cpu_num == 0) {
        percpu = &bp_percpu;
    } else {
        percpu = &ap_percpus[cpu_num - 1];
    }
    /* set up the percpu structure */
    percpu->direct = percpu;
    percpu->cpu_num = cpu_num;
    // Start with an invalid id until we know the local APIC is setup
    percpu->apic_id = INVALID_APIC_ID;

    /* point gs at the per cpu structure */
    write_msr(X86_MSR_IA32_GS_BASE, (uintptr_t)percpu);

    /* set the KERNEL_GS_BASE MSR to 0 */
    /* when we enter user space, this will be populated via a swapgs */
    write_msr(X86_MSR_IA32_KERNEL_GS_BASE, 0);

    x86_feature_init();
    fpu_init();

    idt_setup(&percpu->idt);
    idt_load(&percpu->idt);

    x86_initialize_percpu_tss();

    x86_mmu_percpu_init();

    /* load the syscall entry point */
    extern void x86_syscall(void);

    write_msr(X86_MSR_IA32_LSTAR, (uint64_t)&x86_syscall);

    /* set the STAR MSR to load the appropriate kernel code selector on syscall
     * and the appropriate user code selector on return.
     * on syscall entry the following are loaded into segment registers:
     *   CS = CODE_64_SELECTOR      (STAR[47:32])
     *   SS = DATA_SELECTOR         (STAR[47:32] + 0x8)
     * on syscall exit:
     *   CS = USER_CODE_64_SELECTOR (STAR[63:48] + 0x16)
     *   SS = USER_DATA_SELECTOR    (STAR[63:48] + 0x8)
     */
    write_msr(X86_MSR_IA32_STAR, (uint64_t)USER_CODE_SELECTOR << 48 | (uint64_t)CODE_64_SELECTOR << 32);

    /* set the FMASK register to mask off certain bits in RFLAGS on syscall entry */
    uint64_t mask =
        X86_FLAGS_AC |         /* disable alignment check/access control (this
                                * prevents ring 0 from performing data access
                                * to ring 3 if SMAP is available) */
        X86_FLAGS_NT |         /* clear nested task */
        X86_FLAGS_IOPL_MASK |  /* set iopl to 0 */
        X86_FLAGS_STATUS_MASK; /* clear all status flags, interrupt disabled, trap flag */
    write_msr(X86_MSR_IA32_FMASK, mask);

    /* enable syscall instruction */
    uint64_t efer_msr = read_msr(X86_MSR_EFER);
    efer_msr |= X86_EFER_SCE;
    write_msr(X86_MSR_EFER, efer_msr);

#if WITH_SMP
    mp_set_curr_cpu_online(true);
#endif
}

void x86_set_local_apic_id(uint32_t apic_id)
{
  x86_get_percpu()->apic_id = apic_id;
}

#if WITH_SMP
status_t arch_mp_send_ipi(mp_cpu_mask_t target, mp_ipi_t ipi)
{
    uint8_t vector = 0;
    switch (ipi) {
        case MP_IPI_GENERIC:
            vector = X86_INT_IPI_GENERIC;
            break;
        case MP_IPI_RESCHEDULE:
            vector = X86_INT_IPI_RESCHEDULE;
            break;
        default:
            panic("Unexpected MP IPI value: %d", ipi);
    }

    if (target == MP_CPU_ALL_BUT_LOCAL) {
        apic_send_broadcast_ipi(vector, DELIVERY_MODE_FIXED);
        return NO_ERROR;
    } else if (target == MP_CPU_ALL) {
        apic_send_broadcast_self_ipi(vector, DELIVERY_MODE_FIXED);
        return NO_ERROR;
    }

    ASSERT(x86_num_cpus <= sizeof(target) * 8);

    mp_cpu_mask_t remaining = target;
    uint cpu_id = 0;
    while (remaining && cpu_id < x86_num_cpus) {
        if (remaining & 1) {
            struct x86_percpu *percpu;
            if (cpu_id == 0) {
                percpu = &bp_percpu;
            } else {
                percpu = &ap_percpus[cpu_id - 1];
            }
            /* Reschedule IPIs may occur before all CPUs are fully up.  Just
             * ignore attempts to send them to down CPUs. */
            if (ipi != MP_IPI_RESCHEDULE) {
                DEBUG_ASSERT(percpu->apic_id != INVALID_APIC_ID);
            }
            /* Make sure the CPU is actually up before sending the IPI */
            if (percpu->apic_id != INVALID_APIC_ID) {
                apic_send_ipi(vector, percpu->apic_id, DELIVERY_MODE_FIXED);
            }
        }
        remaining >>= 1;
        cpu_id++;
    }

    return NO_ERROR;
}

enum handler_return x86_ipi_generic_handler(void)
{
    //LTRACEF("cpu %u, arg %p\n", arch_curr_cpu_num(), arg);
    return mp_mbx_generic_irq();
}

enum handler_return x86_ipi_reschedule_handler(void)
{
    //TRACEF("rescheduling on cpu %u, arg %p\n", arch_curr_cpu_num(), arg);
    return mp_mbx_reschedule_irq();
}
#endif
