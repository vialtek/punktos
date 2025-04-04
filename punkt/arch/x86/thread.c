/*
 * Copyright (c) 2009 Corey Tabaka
 * Copyright (c) 2014 Travis Geiselbrecht
 * Copyright (c) 2015 Intel Corporation
 *
 * Use of this source code is governed by a MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT
 */
#include <sys/types.h>
#include <string.h>
#include <stdlib.h>
#include <lk/debug.h>
#include <kernel/thread.h>
#include <kernel/spinlock.h>
#include <arch/x86.h>
#include <arch/x86/descriptor.h>
#include <arch/fpu.h>

/* we're uniprocessor at this point for x86, so store a global pointer to the current thread */
struct thread *_current_thread;

void arch_thread_initialize(thread_t *t, vaddr_t entry_point) {
    // create a default stack frame on the stack
    vaddr_t stack_top = (vaddr_t)t->stack + t->stack_size;

    // make sure the top of the stack is 16 byte aligned for ABI compliance
    stack_top = ROUNDDOWN(stack_top, 16);

    // make sure we start the frame 8 byte unaligned (relative to the 16 byte alignment) because
    // of the way the context switch will pop the return address off the stack. After the first
    // context switch, this leaves the stack in unaligned relative to how a called function expects it.
    stack_top -= 8;
    struct x86_64_context_switch_frame *frame = (struct x86_64_context_switch_frame *)(stack_top);

    // move down a frame size and zero it out
    frame--;
    memset(frame, 0, sizeof(*frame));

    frame->rip = entry_point;
    frame->rflags = 0x3002; /* IF = 0, NT = 0, IOPL = 3 */

    // initialize the saved fpu state
    fpu_init_thread_states(t);

    // set the stack pointer
    t->arch.sp = (vaddr_t)frame;
    t->arch.fs_base = 0;
    t->arch.gs_base = 0;
}

void arch_dump_thread(thread_t *t) {
    if (t->state != THREAD_RUNNING) {
        dprintf(INFO, "\tarch: ");
        dprintf(INFO, "sp 0x%lx\n", t->arch.sp);
    }
}

void arch_context_switch(thread_t *oldthread, thread_t *newthread) {
    fpu_context_switch(oldthread, newthread);

    /* user and kernel gs have been swapped, so unswap them when loading
     * from the msrs
     */
    oldthread->arch.fs_base = read_msr(X86_MSR_IA32_FS_BASE);
    oldthread->arch.gs_base = read_msr(X86_MSR_IA32_KERNEL_GS_BASE);

    write_msr(X86_MSR_IA32_FS_BASE, newthread->arch.fs_base);
    write_msr(X86_MSR_IA32_KERNEL_GS_BASE, newthread->arch.gs_base);

    x86_64_context_switch(&oldthread->arch.sp, newthread->arch.sp);
}

