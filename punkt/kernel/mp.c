// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT


#include <kernel/mp.h>

#include <assert.h>
#include <lk/debug.h>
#include <lk/err.h>
#include <stdlib.h>
#include <lk/trace.h>
#include <arch/mp.h>
#include <arch/ops.h>
#include <kernel/spinlock.h>

#define LOCAL_TRACE 0

#if WITH_SMP
/* a global state structure, aligned on cpu cache line to minimize aliasing */
struct mp_state mp __CPU_ALIGN;

/* Helpers used for implementing mp_sync */
struct mp_sync_context;
static void mp_sync_task(void *context);

void mp_init(void)
{
    mp.ipi_task_lock = SPIN_LOCK_INITIAL_VALUE;
    for (uint i = 0; i < countof(mp.ipi_task_list); ++i) {
        list_initialize(&mp.ipi_task_list[i]);
    }
}

void mp_reschedule(mp_cpu_mask_t target, uint flags)
{
    uint local_cpu = arch_curr_cpu_num();

    LTRACEF("local %d, target 0x%x\n", local_cpu, target);

    if (target == MP_CPU_ALL) {
        target = mp.active_cpus;
    }

    /* mask out cpus that are not active and the local cpu */
    target &= mp.active_cpus;

    /* mask out cpus that are currently running realtime code */
    if ((flags & MP_RESCHEDULE_FLAG_REALTIME) == 0) {
        target &= ~mp.realtime_cpus;
    }
    target &= ~(1U << local_cpu);

    LTRACEF("local %d, post mask target now 0x%x\n", local_cpu, target);

    arch_mp_send_ipi(target, MP_IPI_RESCHEDULE);
}

struct mp_sync_context {
    mp_sync_task_t task;
    void *task_context;
    volatile int tasks_running;
};

static void mp_sync_task(void *raw_context)
{
    struct mp_sync_context *context = raw_context;
    context->task(context->task_context);
    /* use seq-cst atomic to ensure this update is not seen before the
     * side-effects of context->task */
    atomic_add(&context->tasks_running, -1);
    arch_spinloop_signal();
}

/* @brief Execute a task on the specified CPUs, and block on the calling
 *        CPU until all CPUs have finished the task.
 *
 *  If MP_CPU_ALL or MP_CPU_ALL_BUT_LOCAL is the target, the online CPU
 *  mask will be used to determine actual targets.
 *
 * Interrupts must be disabled if calling with MP_CPU_ALL_BUT_LOCAL as target
 */
void mp_sync_exec(mp_cpu_mask_t target, mp_sync_task_t task, void *context)
{
    uint num_cpus = arch_max_num_cpus();
    uint num_targets = 0;

    if (target == MP_CPU_ALL) {
        target = mp.online_cpus;
    } else if (target == MP_CPU_ALL_BUT_LOCAL) {
        /* targeting all other CPUs but the current one is hazardous
         * if the local CPU may be changed underneath us */
        DEBUG_ASSERT(arch_ints_disabled());
        target = mp.online_cpus & ~(1U << arch_curr_cpu_num());
    }

    /* initialize num_targets (may include self) */
    mp_cpu_mask_t remaining = target;
    uint cpu_id = 0;
    while (remaining && cpu_id < num_cpus) {
        if (remaining & 1) {
            num_targets++;
        }
        remaining >>= 1;
        cpu_id++;
    }

    /* disable interrupts so our current CPU doesn't change */
    spin_lock_saved_state_t irqstate;
    arch_interrupt_save(&irqstate, SPIN_LOCK_FLAG_INTERRUPTS);
    smp_rmb();

    uint local_cpu = arch_curr_cpu_num();
    /* remove self from target lists, since no need to IPI ourselves */
    bool targetting_self =
            (target != MP_CPU_ALL_BUT_LOCAL && (target & (1U << local_cpu)));
    target &= ~(1U << local_cpu);

    /* create tasks to enqueue (we need one per target due to each containing
     * a linked list node */
    struct mp_sync_context sync_context = {
        .task = task,
        .task_context = context,
        .tasks_running = num_targets,
    };

    struct mp_ipi_task sync_tasks[SMP_MAX_CPUS] = { 0 };
    for (uint i = 0; i < num_cpus; ++i) {
        sync_tasks[i].func = mp_sync_task;
        sync_tasks[i].context = &sync_context;
    }

    /* enqueue tasks */
    spin_lock(&mp.ipi_task_lock);
    remaining = target;
    cpu_id = 0;
    while (remaining && cpu_id < num_cpus) {
        if (remaining & 1) {
            list_add_tail(&mp.ipi_task_list[cpu_id], &sync_tasks[cpu_id].node);
        }
        remaining >>= 1;
        cpu_id++;
    }
    spin_unlock(&mp.ipi_task_lock);

    /* let CPUs know to begin executing */
    __UNUSED status_t status = arch_mp_send_ipi(target, MP_IPI_GENERIC);
    DEBUG_ASSERT(status == NO_ERROR);

    if (targetting_self) {
        mp_sync_task(&sync_context);
    }
    smp_mb();

    /* we can take interrupts again once we've executed our task */
    arch_interrupt_restore(irqstate, SPIN_LOCK_FLAG_INTERRUPTS);

    /* wait for all other CPUs to be done with the context */
    while (sync_context.tasks_running != 0) {
        arch_spinloop_pause();
    }
    smp_mb();

    /* make sure the sync_tasks aren't in lists anymore, since they're
     * stack allocated */
    for (uint i = 0; i < num_cpus; ++i) {
        ASSERT(!list_in_list(&sync_tasks[i].node));
    }
}

void mp_set_curr_cpu_online(bool online)
{
    if (online) {
        atomic_or((volatile int *)&mp.online_cpus, 1U << arch_curr_cpu_num());
    } else {
        atomic_and((volatile int *)&mp.online_cpus, ~(1U << arch_curr_cpu_num()));
    }
}

void mp_set_curr_cpu_active(bool active)
{
    if (active) {
        atomic_or((volatile int *)&mp.active_cpus, 1U << arch_curr_cpu_num());
    } else {
        atomic_and((volatile int *)&mp.active_cpus, ~(1U << arch_curr_cpu_num()));
    }
}

enum handler_return mp_mbx_generic_irq(void)
{
    DEBUG_ASSERT(arch_ints_disabled());
    uint local_cpu = arch_curr_cpu_num();

    while (1) {
        struct mp_ipi_task *task;
        spin_lock(&mp.ipi_task_lock);
        task = list_remove_head_type(&mp.ipi_task_list[local_cpu], struct mp_ipi_task, node);
        spin_unlock(&mp.ipi_task_lock);
        if (task == NULL) {
            break;
        }

        task->func(task->context);
    }
    return INT_NO_RESCHEDULE;
}

enum handler_return mp_mbx_reschedule_irq(void)
{
    uint cpu = arch_curr_cpu_num();

    LTRACEF("cpu %u\n", cpu);

    THREAD_STATS_INC(reschedule_ipis);

    return (mp.active_cpus & (1U << cpu)) ? INT_RESCHEDULE : INT_NO_RESCHEDULE;
}

#endif
