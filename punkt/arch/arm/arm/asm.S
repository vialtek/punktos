/*
 * Copyright (c) 2008 Travis Geiselbrecht
 *
 * Use of this source code is governed by a MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT
 */
#include <lk/asm.h>
#include <arch/arm/cores.h>

    /* context switch frame is as follows:
     * lr
     * r11
     * r10
     * r9
     * r8
     * r7
     * r6
     * r5
     * r4
     */
/* arm_context_switch(addr_t *old_sp, addr_t new_sp) */
FUNCTION(arm_context_switch)
    /* save non callee trashed supervisor registers */
    /* spsr and user mode registers are saved and restored in the iframe by exceptions.S */
    push    { r4-r11, lr }

    /* save old sp */
    str     sp, [r0]

    /* clear any exlusive locks that the old thread holds */
#if ARM_ARCH_LEVEL >= 7
    /* can clear it directly */
    clrex
#elif ARM_ARCH_LEVEL == 6
    /* have to do a fake strex to clear it */
    ldr     r0, =strex_spot
    strex   r3, r2, [r0]
#endif

    /* load new regs */
    mov     sp, r1
    pop     { r4-r11, lr }
    bx      lr

.ltorg

#if ARM_ARCH_LEVEL == 6
.data
strex_spot:
    .word   0
#endif

.text

FUNCTION(arm_save_mode_regs)
    mrs     r1, cpsr

    stmia   r0, { r13, r14 }^ /* usr */
    add     r0, #8

    cps     #0x11   /* fiq */
    str     r13, [r0], #4
    str     r14, [r0], #4

    cps     #0x12   /* irq */
    str     r13, [r0], #4
    str     r14, [r0], #4

    cps     #0x13   /* svc */
    str     r13, [r0], #4
    str     r14, [r0], #4

    cps     #0x17   /* abt */
    str     r13, [r0], #4
    str     r14, [r0], #4

    cps     #0x1b   /* und */
    str     r13, [r0], #4
    str     r14, [r0], #4

    cps     #0x1f   /* sys */
    str     r13, [r0], #4
    str     r14, [r0], #4

    msr     cpsr_c, r1

    bx      lr

.text

/* void arm_chain_load(paddr_t entry, ulong arg0, ulong arg1, ulong arg2, ulong arg3) __NO_RETURN; */
/* shut down the system, branching into the secondary system */
FUNCTION(arm_chain_load)
    /* shuffle the args around */
    mov     r4, r0      /* r4 = entry point */
    mov     r0, r1
    mov     r1, r2
    mov     r2, r3
    ldr     r3, [sp]

/* The MMU is initialized and running at this point, so we'll need to
 * make sure we can disable it and continue to run. The caller should
 * have built a identity map for us and branched to our identity mapping,
 * so it will be safe to just disable the mmu and branch to the entry
 * point in physical space.
 */
    /* Read SCTLR */
    mrc     p15, 0, r12, c1, c0, 0

    /* Turn off the MMU */
    bic     r12, #0x1

    /* Write back SCTLR */
    mcr     p15, 0, r12, c1, c0, 0
    isb

    /* call the entry point */
    bx      r4
