// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <lk/compiler.h>
#include <stdint.h>
#include <arch/x86.h>

__BEGIN_CDECLS

struct cpuid_leaf {
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
};

enum x86_cpuid_leaf_num {
    X86_CPUID_BASE = 0,
    X86_CPUID_TOPOLOGY = 0xb,

    X86_CPUID_EXT_BASE = 0x80000000,
    X86_CPUID_ADDR_WIDTH = 0x80000008,
};

struct x86_cpuid_bit {
    enum x86_cpuid_leaf_num leaf_num;
    uint8_t word;
    uint8_t bit;
};

#define X86_CPUID_BIT(leaf, word, bit) \
        (struct x86_cpuid_bit){(enum x86_cpuid_leaf_num)(leaf), (word), (bit)}

void x86_feature_init(void);
const struct cpuid_leaf *x86_get_cpuid_leaf(enum x86_cpuid_leaf_num);
/* Retrieve the specified subleaf.  This function is not cached.
 * Returns false if leaf num is invalid */
bool x86_get_cpuid_subleaf(
        enum x86_cpuid_leaf_num, uint32_t, struct cpuid_leaf *);
bool x86_feature_test(struct x86_cpuid_bit);

void x86_feature_debug(void);

/* feature bits for x86_feature_test */
/* add feature bits to test here */
/* format: X86_CPUID_BIT(cpuid leaf, register (eax-edx:0-3), bit) */
#define X86_FEATURE_SSE3                X86_CPUID_BIT(0x1, 2, 0)
#define X86_FEATURE_MON                 X86_CPUID_BIT(0x1, 2, 3)
#define X86_FEATURE_VMX                 X86_CPUID_BIT(0x1, 2, 5)
#define X86_FEATURE_TM2                 X86_CPUID_BIT(0x1, 2, 8)
#define X86_FEATURE_SSSE3               X86_CPUID_BIT(0x1, 2, 9)
#define X86_FEATURE_PDCM                X86_CPUID_BIT(0x1, 2, 15)
#define X86_FEATURE_PCID                X86_CPUID_BIT(0x1, 2, 17)
#define X86_FEATURE_SSE4_1              X86_CPUID_BIT(0x1, 2, 19)
#define X86_FEATURE_SSE4_2              X86_CPUID_BIT(0x1, 2, 20)
#define X86_FEATURE_X2APIC              X86_CPUID_BIT(0x1, 2, 21)
#define X86_FEATURE_TSC_DEADLINE        X86_CPUID_BIT(0x1, 2, 24)
#define X86_FEATURE_AESNI               X86_CPUID_BIT(0x1, 2, 25)
#define X86_FEATURE_XSAVE               X86_CPUID_BIT(0x1, 2, 26)
#define X86_FEATURE_AVX                 X86_CPUID_BIT(0x1, 2, 28)
#define X86_FEATURE_RDRAND              X86_CPUID_BIT(0x1, 2, 30)
#define X86_FEATURE_HYPERVISOR          X86_CPUID_BIT(0x1, 2, 31)
#define X86_FEATURE_FPU                 X86_CPUID_BIT(0x1, 3, 0)
#define X86_FEATURE_PSE                 X86_CPUID_BIT(0x1, 3, 3)
#define X86_FEATURE_PAE                 X86_CPUID_BIT(0x1, 3, 6)
#define X86_FEATURE_APIC                X86_CPUID_BIT(0x1, 3, 9)
#define X86_FEATURE_SEP                 X86_CPUID_BIT(0x1, 3, 11)
#define X86_FEATURE_PGE                 X86_CPUID_BIT(0x1, 3, 13)
#define X86_FEATURE_PAT                 X86_CPUID_BIT(0x1, 3, 16)
#define X86_FEATURE_PSE36               X86_CPUID_BIT(0x1, 3, 17)
#define X86_FEATURE_CLFLUSH             X86_CPUID_BIT(0x1, 3, 19)
#define X86_FEATURE_ACPI                X86_CPUID_BIT(0x1, 3, 22)
#define X86_FEATURE_MMX                 X86_CPUID_BIT(0x1, 3, 23)
#define X86_FEATURE_FXSR                X86_CPUID_BIT(0x1, 3, 24)
#define X86_FEATURE_SSE                 X86_CPUID_BIT(0x1, 3, 25)
#define X86_FEATURE_SSE2                X86_CPUID_BIT(0x1, 3, 26)
#define X86_FEATURE_TM                  X86_CPUID_BIT(0x1, 3, 29)
#define X86_FEATURE_DTS                 X86_CPUID_BIT(0x6, 0, 0)
#define X86_FEATURE_TURBO               X86_CPUID_BIT(0x6, 0, 1)
#define X86_FEATURE_PLN                 X86_CPUID_BIT(0x6, 0, 4)
#define X86_FEATURE_PTM                 X86_CPUID_BIT(0x6, 0, 6)
#define X86_FEATURE_HWP                 X86_CPUID_BIT(0x6, 0, 7)
#define X86_FEATURE_HWP_NOT             X86_CPUID_BIT(0x6, 0, 8)
#define X86_FEATURE_HWP_ACT             X86_CPUID_BIT(0x6, 0, 9)
#define X86_FEATURE_HWP_PREF            X86_CPUID_BIT(0x6, 0, 10)
#define X86_FEATURE_TURBO_MAX           X86_CPUID_BIT(0x6, 0, 14)
#define X86_FEATURE_HW_FEEDBACK         X86_CPUID_BIT(0x6, 2, 0)
#define X86_FEATURE_PERF_BIAS           X86_CPUID_BIT(0x6, 2, 3)
#define X86_FEATURE_FSGSBASE            X86_CPUID_BIT(0x7, 1, 0)
#define X86_FEATURE_TSC_ADJUST          X86_CPUID_BIT(0x7, 1, 1)
#define X86_FEATURE_AVX2                X86_CPUID_BIT(0x7, 1, 5)
#define X86_FEATURE_SMEP                X86_CPUID_BIT(0x7, 1, 7)
#define X86_FEATURE_ERMS                X86_CPUID_BIT(0x7, 1, 9)
#define X86_FEATURE_INVPCID             X86_CPUID_BIT(0x7, 1, 10)
#define X86_FEATURE_RDSEED              X86_CPUID_BIT(0x7, 1, 18)
#define X86_FEATURE_SMAP                X86_CPUID_BIT(0x7, 1, 20)
#define X86_FEATURE_CLFLUSHOPT          X86_CPUID_BIT(0x7, 1, 23)
#define X86_FEATURE_CLWB                X86_CPUID_BIT(0x7, 1, 24)
#define X86_FEATURE_PT                  X86_CPUID_BIT(0x7, 1, 25)
#define X86_FEATURE_UMIP                X86_CPUID_BIT(0x7, 2, 2)
#define X86_FEATURE_PKU                 X86_CPUID_BIT(0x7, 2, 3)
#define X86_FEATURE_MD_CLEAR            X86_CPUID_BIT(0x7, 3, 10)
#define X86_FEATURE_IBRS_IBPB           X86_CPUID_BIT(0x7, 3, 26)
#define X86_FEATURE_STIBP               X86_CPUID_BIT(0x7, 3, 27)
#define X86_FEATURE_L1D_FLUSH           X86_CPUID_BIT(0x7, 3, 28)
#define X86_FEATURE_ARCH_CAPABILITIES   X86_CPUID_BIT(0x7, 3, 29)
#define X86_FEATURE_SSBD                X86_CPUID_BIT(0x7, 3, 31)

#define X86_FEATURE_KVM_PV_CLOCK        X86_CPUID_BIT(0x40000001, 0, 3)
#define X86_FEATURE_KVM_PV_EOI          X86_CPUID_BIT(0x40000001, 0, 6)
#define X86_FEATURE_KVM_PV_IPI          X86_CPUID_BIT(0x40000001, 0, 11)
#define X86_FEATURE_KVM_PV_CLOCK_STABLE X86_CPUID_BIT(0x40000001, 0, 24)

#define X86_FEATURE_AMD_TOPO            X86_CPUID_BIT(0x80000001, 2, 22)
#define X86_FEATURE_SSE4A               X86_CPUID_BIT(0x80000001, 3, 6)
#define X86_FEATURE_SYSCALL             X86_CPUID_BIT(0x80000001, 3, 11)
#define X86_FEATURE_NX                  X86_CPUID_BIT(0x80000001, 3, 20)
#define X86_FEATURE_HUGE_PAGE           X86_CPUID_BIT(0x80000001, 3, 26)
#define X86_FEATURE_RDTSCP              X86_CPUID_BIT(0x80000001, 3, 27)
#define X86_FEATURE_INVAR_TSC           X86_CPUID_BIT(0x80000007, 3, 8)

/* legacy accessors */
static inline uint8_t x86_linear_address_width(void)
{
    const struct cpuid_leaf *leaf = x86_get_cpuid_leaf(X86_CPUID_ADDR_WIDTH);
    if (!leaf)
        return 0;

    /*
     Extracting bit 15:8 from eax register
     Bits 15-08: #Linear Address Bits
    */
    return (leaf->a >> 8) & 0xff;
}

static inline uint8_t x86_physical_address_width(void)
{
    const struct cpuid_leaf *leaf = x86_get_cpuid_leaf(X86_CPUID_ADDR_WIDTH);
    if (!leaf)
        return 0;

    /*
     Extracting bit 7:0 from eax register
     Bits 07-00: #Physical Address Bits
    */
    return leaf->a & 0xff;
}

#define X86_TOPOLOGY_INVALID 0
#define X86_TOPOLOGY_SMT 1
#define X86_TOPOLOGY_CORE 2

struct x86_topology_level {
    /* The number of bits of the APIC ID that encode this level */
    uint8_t num_bits;
    /* The type of relationship this level describes (hyperthread/core/etc) */
    uint8_t type;
};

/**
 * @brief Fetch the topology information for the given level.
 *
 * This interface is uncached.
 *
 * @param level The level to retrieve info for.  Should initially be 0 and
 * incremented with each call.
 * @param info The structure to populate with the discovered information
 *
 * @return true if the requested level existed (and there may be higher levels).
 * @return false if the requested level does not exist (and no higher ones do).
 */
bool x86_topology_enumerate(uint8_t level, struct x86_topology_level *info);

__END_CDECLS
