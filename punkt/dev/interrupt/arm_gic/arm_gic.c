/*
 * Copyright (c) 2012-2015 Travis Geiselbrecht
 *
 * Use of this source code is governed by a MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT
 */
#include <assert.h>
#include <lk/bits.h>
#include <lk/err.h>
#include <sys/types.h>
#include <lk/debug.h>
#include <dev/interrupt/arm_gic.h>
#include <lk/reg.h>
#include <kernel/thread.h>
#include <kernel/debug.h>
#include <lk/init.h>
#include <platform/interrupts.h>
#include <arch/ops.h>
#include <platform/gic.h>
#include <lk/trace.h>

#define LOCAL_TRACE 0

#if ARCH_ARM
#include <arch/arm.h>
#define iframe arm_iframe
#define IFRAME_PC(frame) ((frame)->pc)
#endif
#if ARCH_ARM64
#include <arch/arm64.h>
#define iframe arm64_iframe_short
#define IFRAME_PC(frame) ((frame)->elr)
#endif

static status_t arm_gic_set_secure_locked(u_int irq, bool secure);

static spin_lock_t gicd_lock;

#define GICD_LOCK_FLAGS SPIN_LOCK_FLAG_INTERRUPTS
#define GIC_MAX_PER_CPU_INT 32

static bool arm_gic_interrupt_change_allowed(uint irq) {
    return true;
}

static void suspend_resume_fiq(bool resume_gicc, bool resume_gicd) {
}

struct int_handler_struct {
    int_handler handler;
    void *arg;
};

static struct int_handler_struct int_handler_table_per_cpu[GIC_MAX_PER_CPU_INT][SMP_MAX_CPUS];
static struct int_handler_struct int_handler_table_shared[MAX_INT-GIC_MAX_PER_CPU_INT];

static struct int_handler_struct *get_int_handler(unsigned int vector, uint cpu) {
    if (vector < GIC_MAX_PER_CPU_INT) {
        return &int_handler_table_per_cpu[vector][cpu];
    } else {
        return &int_handler_table_shared[vector - GIC_MAX_PER_CPU_INT];
    }
}

void register_int_handler(unsigned int vector, int_handler handler, void *arg) {
    struct int_handler_struct *h;
    uint cpu = arch_curr_cpu_num();

    spin_lock_saved_state_t state;

    if (vector >= MAX_INT)
        panic("register_int_handler: vector out of range %d\n", vector);

    spin_lock_save(&gicd_lock, &state, GICD_LOCK_FLAGS);

    if (arm_gic_interrupt_change_allowed(vector)) {
        h = get_int_handler(vector, cpu);
        h->handler = handler;
        h->arg = arg;
    }

    spin_unlock_restore(&gicd_lock, state, GICD_LOCK_FLAGS);
}

void register_int_handler_msi(unsigned int vector, int_handler handler, void *arg, bool edge) {
    // only can deal with edge triggered at the moment
    DEBUG_ASSERT(edge);

    register_int_handler(vector, handler, arg);
}

/* main cpu regs */
#define GICC_CTLR               (GICC_OFFSET + 0x0000)
#define GICC_PMR                (GICC_OFFSET + 0x0004)
#define GICC_BPR                (GICC_OFFSET + 0x0008)
#define GICC_IAR                (GICC_OFFSET + 0x000c)
#define GICC_EOIR               (GICC_OFFSET + 0x0010)
#define GICC_RPR                (GICC_OFFSET + 0x0014)
#define GICC_HPPIR              (GICC_OFFSET + 0x0018)
#define GICC_APBR               (GICC_OFFSET + 0x001c)
#define GICC_AIAR               (GICC_OFFSET + 0x0020)
#define GICC_AEOIR              (GICC_OFFSET + 0x0024)
#define GICC_AHPPIR             (GICC_OFFSET + 0x0028)
#define GICC_APR(n)             (GICC_OFFSET + 0x00d0 + (n) * 4)
#define GICC_NSAPR(n)           (GICC_OFFSET + 0x00e0 + (n) * 4)
#define GICC_IIDR               (GICC_OFFSET + 0x00fc)
#define GICC_DIR                (GICC_OFFSET + 0x1000)

/* distribution regs */
#define GICD_CTLR               (GICD_OFFSET + 0x000)
#define GICD_TYPER              (GICD_OFFSET + 0x004)
#define GICD_IIDR               (GICD_OFFSET + 0x008)
#define GICD_IGROUPR(n)         (GICD_OFFSET + 0x080 + (n) * 4)
#define GICD_ISENABLER(n)       (GICD_OFFSET + 0x100 + (n) * 4)
#define GICD_ICENABLER(n)       (GICD_OFFSET + 0x180 + (n) * 4)
#define GICD_ISPENDR(n)         (GICD_OFFSET + 0x200 + (n) * 4)
#define GICD_ICPENDR(n)         (GICD_OFFSET + 0x280 + (n) * 4)
#define GICD_ISACTIVER(n)       (GICD_OFFSET + 0x300 + (n) * 4)
#define GICD_ICACTIVER(n)       (GICD_OFFSET + 0x380 + (n) * 4)
#define GICD_IPRIORITYR(n)      (GICD_OFFSET + 0x400 + (n) * 4)
#define GICD_ITARGETSR(n)       (GICD_OFFSET + 0x800 + (n) * 4)
#define GICD_ICFGR(n)           (GICD_OFFSET + 0xc00 + (n) * 4)
#define GICD_NSACR(n)           (GICD_OFFSET + 0xe00 + (n) * 4)
#define GICD_SGIR               (GICD_OFFSET + 0xf00)
#define GICD_CPENDSGIR(n)       (GICD_OFFSET + 0xf10 + (n) * 4)
#define GICD_SPENDSGIR(n)       (GICD_OFFSET + 0xf20 + (n) * 4)

#define DIV_ROUND_UP(n,d) (((n) + (d) - 1) / (d))
#define GIC_REG_COUNT(bit_per_reg) DIV_ROUND_UP(MAX_INT, (bit_per_reg))
#define DEFINE_GIC_SHADOW_REG(name, bit_per_reg, init_val, init_from) \
    uint32_t (name)[GIC_REG_COUNT(bit_per_reg)] = { \
        [((init_from) / (bit_per_reg)) ... \
         (GIC_REG_COUNT(bit_per_reg) - 1)] = (init_val) \
    }

static DEFINE_GIC_SHADOW_REG(gicd_itargetsr, 4, 0x01010101, 32);

// accessor routines for GIC registers that go through the mmio interface
static inline uint32_t gicreg_read32(uint32_t gic, uint32_t register_offset) {
    return mmio_read32((volatile uint32_t *)(GICBASE(gic) + register_offset));
}

static inline void gicreg_write32(uint32_t gic, uint32_t register_offset, uint32_t value) {
    mmio_write32((volatile uint32_t *)(GICBASE(gic) + register_offset), value);
}

static void gic_set_enable(uint vector, bool enable) {
    uint reg = vector / 32;
    uint32_t mask = 1ULL << (vector % 32);

    if (enable) {
        gicreg_write32(0, GICD_ISENABLER(reg), mask);
    } else {
        gicreg_write32(0, GICD_ICENABLER(reg), mask);
    }
}

static void arm_gic_init_percpu(uint level) {
    gicreg_write32(0, GICC_CTLR, 1); // enable GIC0
    gicreg_write32(0, GICC_PMR, 0xFF); // unmask interrupts at all priority levels
}

LK_INIT_HOOK_FLAGS(arm_gic_init_percpu,
                   arm_gic_init_percpu,
                   LK_INIT_LEVEL_PLATFORM_EARLY, LK_INIT_FLAG_SECONDARY_CPUS);

static void arm_gic_suspend_cpu(uint level) {
    suspend_resume_fiq(false, false);
}

LK_INIT_HOOK_FLAGS(arm_gic_suspend_cpu, arm_gic_suspend_cpu,
                   LK_INIT_LEVEL_PLATFORM, LK_INIT_FLAG_CPU_SUSPEND);

static void arm_gic_resume_cpu(uint level) {
    spin_lock_saved_state_t state;
    bool resume_gicd = false;

    spin_lock_save(&gicd_lock, &state, GICD_LOCK_FLAGS);
    if (!(gicreg_read32(0, GICD_CTLR) & 1)) {
        dprintf(SPEW, "%s: distibutor is off, calling arm_gic_init instead\n", __func__);
        arm_gic_init();
        resume_gicd = true;
    } else {
        arm_gic_init_percpu(0);
    }
    spin_unlock_restore(&gicd_lock, state, GICD_LOCK_FLAGS);
    suspend_resume_fiq(true, resume_gicd);
}

LK_INIT_HOOK_FLAGS(arm_gic_resume_cpu, arm_gic_resume_cpu,
                   LK_INIT_LEVEL_PLATFORM, LK_INIT_FLAG_CPU_RESUME);

static uint arm_gic_max_cpu(void) {
    return (gicreg_read32(0, GICD_TYPER) >> 5) & 0x7;
}

static status_t gic_configure_interrupt(unsigned int vector,
                                        enum interrupt_trigger_mode tm,
                                        enum interrupt_polarity pol) {
    //Only configurable for SPI interrupts
    if ((vector >= MAX_INT) || (vector < GIC_BASE_SPI)) {
        return ERR_INVALID_ARGS;
    }

    if (pol != IRQ_POLARITY_ACTIVE_HIGH) {
        // TODO: polarity should actually be configure through a GPIO controller
        return ERR_NOT_SUPPORTED;
    }

    // type is encoded with two bits, MSB of the two determine type
    // 16 irqs encoded per ICFGR register
    uint32_t reg_ndx = vector >> 4;
    uint32_t bit_shift = ((vector & 0xf) << 1) + 1;
    uint32_t reg_val   = gicreg_read32(0, GICD_ICFGR(reg_ndx));
    if (tm == IRQ_TRIGGER_MODE_EDGE) {
        reg_val |= (1 << bit_shift);
    } else {
        reg_val &= ~(1 << bit_shift);
    }
    gicreg_write32(0, GICD_ICFGR(reg_ndx), reg_val);

    return NO_ERROR;
}

void arm_gic_init(void) {
    int i;

    for (i = 0; i < MAX_INT; i+= 32) {
        gicreg_write32(0, GICD_ICENABLER(i / 32), ~0);
        gicreg_write32(0, GICD_ICPENDR(i / 32), ~0);
    }

    if (arm_gic_max_cpu() > 0) {
        /* Set external interrupts to target cpu 0 */
        for (i = 32; i < MAX_INT; i += 4) {
            gicreg_write32(0, GICD_ITARGETSR(i / 4), gicd_itargetsr[i / 4]);
        }
    }

    // Initialize all the SPIs to edge triggered
    for (i = 32; i < MAX_INT; i++) {
        gic_configure_interrupt(i, IRQ_TRIGGER_MODE_EDGE, IRQ_POLARITY_ACTIVE_HIGH);
    }


    gicreg_write32(0, GICD_CTLR, 1); // enable GIC0
    arm_gic_init_percpu(0);
}

static status_t arm_gic_set_secure_locked(u_int irq, bool secure) {
    return NO_ERROR;
}

static status_t arm_gic_set_target_locked(u_int irq, u_int cpu_mask, u_int enable_mask) {
    u_int reg = irq / 4;
    u_int shift = 8 * (irq % 4);
    u_int old_val;
    u_int new_val;

    cpu_mask = (cpu_mask & 0xff) << shift;
    enable_mask = (enable_mask << shift) & cpu_mask;

    old_val = gicreg_read32(0, GICD_ITARGETSR(reg));
    new_val = (gicd_itargetsr[reg] & ~cpu_mask) | enable_mask;
    gicreg_write32(0, GICD_ITARGETSR(reg), (gicd_itargetsr[reg] = new_val));
    LTRACEF("irq %i, GICD_ITARGETSR%d %x => %x (got %x)\n",
            irq, reg, old_val, new_val, gicreg_read32(0, GICD_ITARGETSR(reg)));

    return NO_ERROR;
}

static uint8_t arm_gic_get_priority(u_int irq) {
    u_int reg = irq / 4;
    u_int shift = 8 * (irq % 4);
    return (gicreg_read32(0, GICD_IPRIORITYR(reg)) >> shift) & 0xff;
}

static status_t arm_gic_set_priority_locked(u_int irq, uint8_t priority) {
    u_int reg = irq / 4;
    u_int shift = 8 * (irq % 4);
    u_int mask = 0xff << shift;
    uint32_t regval;

    regval = gicreg_read32(0, GICD_IPRIORITYR(reg));
    LTRACEF("irq %i, old GICD_IPRIORITYR%d = %x\n", irq, reg, regval);
    regval = (regval & ~mask) | ((uint32_t)priority << shift);
    gicreg_write32(0, GICD_IPRIORITYR(reg), regval);
    LTRACEF("irq %i, new GICD_IPRIORITYR%d = %x, req %x\n",
            irq, reg, gicreg_read32(0, GICD_IPRIORITYR(reg)), regval);

    return 0;
}

status_t arm_gic_sgi(u_int irq, u_int flags, u_int cpu_mask) {
    u_int val =
        ((flags & ARM_GIC_SGI_FLAG_TARGET_FILTER_MASK) << 24) |
        ((cpu_mask & 0xff) << 16) |
        ((flags & ARM_GIC_SGI_FLAG_NS) ? (1U << 15) : 0) |
        (irq & 0xf);

    if (irq >= 16)
        return ERR_INVALID_ARGS;

    LTRACEF("GICD_SGIR: %x\n", val);

    gicreg_write32(0, GICD_SGIR, val);

    return NO_ERROR;
}

status_t mask_interrupt(unsigned int vector) {
    if (vector >= MAX_INT)
        return ERR_INVALID_ARGS;

    if (arm_gic_interrupt_change_allowed(vector))
        gic_set_enable(vector, false);

    return NO_ERROR;
}

status_t unmask_interrupt(unsigned int vector) {
    if (vector >= MAX_INT)
        return ERR_INVALID_ARGS;

    if (arm_gic_interrupt_change_allowed(vector))
        gic_set_enable(vector, true);

    return NO_ERROR;
}

static
enum handler_return __platform_irq(struct iframe *frame) {
    // get the current vector
    uint32_t iar = gicreg_read32(0, GICC_IAR);
    unsigned int vector = iar & 0x3ff;

    if (vector >= 0x3fe) {
        // spurious
        return INT_NO_RESCHEDULE;
    }

    THREAD_STATS_INC(interrupts);
    KEVLOG_IRQ_ENTER(vector);

    uint cpu = arch_curr_cpu_num();

    LTRACEF_LEVEL(2, "iar 0x%x cpu %u currthread %p vector %d pc 0x%lx\n", iar, cpu,
                  get_current_thread(), vector, (uintptr_t)IFRAME_PC(frame));

    // deliver the interrupt
    enum handler_return ret;

    ret = INT_NO_RESCHEDULE;
    struct int_handler_struct *handler = get_int_handler(vector, cpu);
    if (handler->handler)
        ret = handler->handler(handler->arg);

    gicreg_write32(0, GICC_EOIR, iar);

    LTRACEF_LEVEL(2, "cpu %u exit %d\n", cpu, ret);

    KEVLOG_IRQ_EXIT(vector);

    return ret;
}

enum handler_return platform_irq(struct iframe *frame);
enum handler_return platform_irq(struct iframe *frame) {
    return __platform_irq(frame);
}

// TODO: check whether we need this. Remove if not
void platform_fiq(struct iframe *frame);
void platform_fiq(struct iframe *frame) {
    PANIC_UNIMPLEMENTED;
}
