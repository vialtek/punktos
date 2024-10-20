// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <stdio.h>
#include <lk/debug.h>
#include <lk/bits.h>
#include <lk/trace.h>
#include <arch/arch_ops.h>
#include <arch/arm64.h>
#include <kernel/thread.h>
#include <vm/vm.h>

#if WITH_LIB_MAGENTA
#include <lib/user_copy.h>
#include <magenta/exception.h>

struct arch_exception_context {
    struct arm64_iframe_long *frame;
    uint64_t far;
    uint32_t esr;
};
#endif

#define LOCAL_TRACE 0

struct fault_handler_table_entry {
    uint64_t pc;
    uint64_t fault_handler;
};

extern struct fault_handler_table_entry __fault_handler_table_start[];
extern struct fault_handler_table_entry __fault_handler_table_end[];

extern enum handler_return platform_irq(struct arm64_iframe_long *frame);

static void dump_iframe(const struct arm64_iframe_long *iframe)
{
    printf("iframe %p:\n", iframe);
    printf("x0  0x%16llx x1  0x%16llx x2  0x%16llx x3  0x%16llx\n", iframe->r[0], iframe->r[1], iframe->r[2], iframe->r[3]);
    printf("x4  0x%16llx x5  0x%16llx x6  0x%16llx x7  0x%16llx\n", iframe->r[4], iframe->r[5], iframe->r[6], iframe->r[7]);
    printf("x8  0x%16llx x9  0x%16llx x10 0x%16llx x11 0x%16llx\n", iframe->r[8], iframe->r[9], iframe->r[10], iframe->r[11]);
    printf("x12 0x%16llx x13 0x%16llx x14 0x%16llx x15 0x%16llx\n", iframe->r[12], iframe->r[13], iframe->r[14], iframe->r[15]);
    printf("x16 0x%16llx x17 0x%16llx x18 0x%16llx x19 0x%16llx\n", iframe->r[16], iframe->r[17], iframe->r[18], iframe->r[19]);
    printf("x20 0x%16llx x21 0x%16llx x22 0x%16llx x23 0x%16llx\n", iframe->r[20], iframe->r[21], iframe->r[22], iframe->r[23]);
    printf("x24 0x%16llx x25 0x%16llx x26 0x%16llx x27 0x%16llx\n", iframe->r[24], iframe->r[25], iframe->r[26], iframe->r[27]);
    printf("x28 0x%16llx x29 0x%16llx lr  0x%16llx usp 0x%16llx\n", iframe->r[28], iframe->r[29], iframe->lr, iframe->usp);
    printf("elr 0x%16llx\n", iframe->elr);
    printf("spsr 0x%16llx\n", iframe->spsr);
}

__WEAK void arm64_syscall(struct arm64_iframe_long *iframe, bool is_64bit, uint32_t syscall_imm, uint64_t pc)
{
    panic("unhandled syscall vector\n");
}

void arm64_sync_exception(struct arm64_iframe_long *iframe, uint exception_flags)
{
    struct fault_handler_table_entry *fault_handler;
    uint32_t esr = ARM64_READ_SYSREG(esr_el1);
    uint32_t ec = BITS_SHIFT(esr, 31, 26);
    uint32_t il = BIT(esr, 25);
    uint32_t iss = BITS(esr, 24, 0);

    switch (ec) {
        case 0b000000: /* unknown reason */
            /* this is for a lot of reasons, but most of them are undefined instructions */
        case 0b111000: /* BRK from arm32 */
        case 0b111100: { /* BRK from arm64 */
#if WITH_LIB_MAGENTA
            /* let magenta get a shot at it */
            arch_exception_context_t context = { .frame = iframe, .esr = esr };
            arch_enable_ints();
            status_t erc = magenta_exception_handler(EXC_UNDEFINED_INSTRUCTION, &context, iframe->elr);
            arch_disable_ints();
            if (erc == NO_ERROR)
                return;
#endif
            return;
        }
        case 0b000111: /* floating point */
            if (unlikely((exception_flags & ARM64_EXCEPTION_FLAG_LOWER_EL) == 0)) {
                /* we trapped a floating point instruction inside our own EL, this is bad */
                printf("invalid fpu use in kernel: PC at 0x%llx\n", iframe->elr);
                break;
            }
            arm64_fpu_exception(iframe, exception_flags);
            return;
        case 0b010001: /* syscall from arm32 */
        case 0b010101: /* syscall from arm64 */
#ifdef WITH_LIB_SYSCALL
            void arm64_syscall(struct arm64_iframe_long *iframe);
            arch_enable_fiqs();
            arm64_syscall(iframe);
            arch_disable_fiqs();
            return;
#else
            arm64_syscall(iframe, (ec == 0x15) ? true : false, iss & 0xffff, iframe->elr);
            return;
#endif
        case 0b100000: /* instruction abort from lower level */
        case 0b100001: { /* instruction abort from same level */
            /* read the FAR register */
            uint64_t far = ARM64_READ_SYSREG(far_el1);

            uint pf_flags = VMM_PF_FLAG_INSTRUCTION;
            pf_flags |= BIT(ec, 0) ? 0 : VMM_PF_FLAG_USER;

            LTRACEF("instruction abort: PC at 0x%llx, FAR 0x%llx, esr 0x%x, iss 0x%x\n",
                    iframe->elr, far, esr, iss);

            arch_enable_ints();
            status_t err = vmm_page_fault_handler(far, pf_flags);
            arch_disable_ints();
            if (err >= 0)
                return;

#if WITH_LIB_MAGENTA
            /* let magenta get a shot at it */
            arch_exception_context_t context = { .frame = iframe, .esr = esr, .far = far };
            arch_enable_ints();
            status_t erc = magenta_exception_handler(EXC_FATAL_PAGE_FAULT, &context, iframe->elr);
            arch_disable_ints();
            if (erc == NO_ERROR)
                return;
#endif

            printf("instruction abort: PC at 0x%llx\n", iframe->elr);
            break;
        }
        case 0b100100: /* data abort from lower level */
        case 0b100101: { /* data abort from same level */
            /* read the FAR register */
            uint64_t far = ARM64_READ_SYSREG(far_el1);

            uint pf_flags = 0;
            pf_flags |= BIT(iss, 6) ? VMM_PF_FLAG_WRITE : 0;
            pf_flags |= BIT(ec, 0) ? 0 : VMM_PF_FLAG_USER;

            LTRACEF("data fault: PC at 0x%llx, FAR 0x%llx, esr 0x%x, iss 0x%x\n",
                    iframe->elr, far, esr, iss);


            // TODO: Make it work with vmm
            // arch_enable_ints();
            // status_t err = vmm_page_fault_handler(far, pf_flags);
            // arch_disable_ints();
            // if (err >= 0)
            //     return;

            // Check if the current thread was expecting a data fault and
            // we should return to its handler.
            thread_t *thr = get_current_thread();
            if (thr->arch.data_fault_resume != NULL) {
                iframe->elr = (uintptr_t)thr->arch.data_fault_resume;
                return;
            }

            for (fault_handler = __fault_handler_table_start;
                    fault_handler < __fault_handler_table_end;
                    fault_handler++) {
                if (fault_handler->pc == iframe->elr) {
                    iframe->elr = fault_handler->fault_handler;
                    return;
                }
            }

#if WITH_LIB_MAGENTA
            /* let magenta get a shot at it */
            arch_exception_context_t context = { .frame = iframe, .esr = esr, .far = far };
            arch_enable_ints();
            status_t erc = magenta_exception_handler(EXC_FATAL_PAGE_FAULT, &context, iframe->elr);
            arch_disable_ints();
            if (erc == NO_ERROR)
                return;
#endif

            /* decode the iss */
            if (BIT(iss, 24)) { /* ISV bit */
                printf("data fault: PC at 0x%llx, FAR 0x%llx, iss 0x%x (DFSC 0x%lx)\n",
                       iframe->elr, far, iss, BITS(iss, 5, 0));
            } else {
                printf("data fault: PC at 0x%llx, FAR 0x%llx, iss 0x%x\n", iframe->elr, far, iss);
            }

            break;
        }
        default: {
#if WITH_LIB_MAGENTA
            /* let magenta get a shot at it */
            arch_exception_context_t context = { .frame = iframe, .esr = esr };
            arch_enable_ints();
            status_t erc = magenta_exception_handler(EXC_GENERAL, &context, iframe->elr);
            arch_disable_ints();
            if (erc == NO_ERROR)
                return;
#endif
            printf("unhandled synchronous exception\n");
        }
    }

    /* fatal exception, die here */
    printf("ESR 0x%x: ec 0x%x, il 0x%x, iss 0x%x\n", esr, ec, il, iss);
    dump_iframe(iframe);

    panic("die\n");
}

void arm64_irq(struct arm64_iframe_long *iframe, uint exception_flags)
{
    LTRACEF("iframe %p, flags 0x%x\n", iframe, exception_flags);

    enum handler_return ret = platform_irq(iframe);
    if (ret != INT_NO_RESCHEDULE)
        thread_preempt();
}

void arm64_invalid_exception(struct arm64_iframe_long *iframe, unsigned int which)
{
    printf("invalid exception, which 0x%x\n", which);
    dump_iframe(iframe);

    panic("die\n");
}

#if WITH_LIB_MAGENTA
void arch_dump_exception_context(arch_exception_context_t *context)
{
    uint32_t ec = BITS_SHIFT(context->esr, 31, 26);
    uint32_t iss = BITS(context->esr, 24, 0);

    switch (ec) {
        case 0b100000: /* instruction abort from lower level */
        case 0b100001: /* instruction abort from same level */
            printf("instruction abort: PC at 0x%llx, address 0x%llx IFSC 0x%lx %s\n",
                    context->frame->elr, context->far,
                    BITS(context->esr, 5, 0),
                    BIT(ec, 0) ? "" : "user ");

            break;
        case 0b100100: /* data abort from lower level */
        case 0b100101: /* data abort from same level */
            printf("data abort: PC at 0x%llx, address 0x%llx %s%s\n",
                    context->frame->elr, context->far,
                    BIT(ec, 0) ? "" : "user ",
                    BIT(iss, 6) ? "write" : "read");
    }

    dump_iframe(context->frame);

    // try to dump the user stack
    if (is_user_address(context->frame->usp)) {
        uint8_t buf[256];
        if (copy_from_user(buf, (void *)context->frame->usp, sizeof(buf)) == NO_ERROR) {
            printf("bottom of user stack at 0x%lx:\n", (vaddr_t)context->frame->usp);
            hexdump_ex(buf, sizeof(buf), context->frame->usp);
        }
    }
}
#endif

