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

/* add feature bits to test here */
#define X86_FEATURE_SSE3        X86_CPUID_BIT(0x1, 2, 0)
#define X86_FEATURE_SSSE3       X86_CPUID_BIT(0x1, 2, 9)
#define X86_FEATURE_SSE4_1      X86_CPUID_BIT(0x1, 2, 19)
#define X86_FEATURE_SSE4_2      X86_CPUID_BIT(0x1, 2, 20)
#define X86_FEATURE_AESNI       X86_CPUID_BIT(0x1, 2, 25)
#define X86_FEATURE_XSAVE       X86_CPUID_BIT(0x1, 2, 26)
#define X86_FEATURE_AVX         X86_CPUID_BIT(0x1, 2, 28)
#define X86_FEATURE_RDRAND      X86_CPUID_BIT(0x1, 2, 30)
#define X86_FEATURE_FPU         X86_CPUID_BIT(0x1, 3, 0)
#define X86_FEATURE_MMX         X86_CPUID_BIT(0x1, 3, 23)
#define X86_FEATURE_FXSR        X86_CPUID_BIT(0x1, 3, 24)
#define X86_FEATURE_SSE         X86_CPUID_BIT(0x1, 3, 25)
#define X86_FEATURE_SSE2        X86_CPUID_BIT(0x1, 3, 26)
#define X86_FEATURE_TSC_ADJUST  X86_CPUID_BIT(0x7, 1, 1)
#define X86_FEATURE_AVX2        X86_CPUID_BIT(0x7, 1, 5)
#define X86_FEATURE_SMEP        X86_CPUID_BIT(0x7, 1, 7)
#define X86_FEATURE_RDSEED      X86_CPUID_BIT(0x7, 1, 18)
#define X86_FEATURE_SMAP        X86_CPUID_BIT(0x7, 1, 20)
#define X86_FEATURE_PKU         X86_CPUID_BIT(0x7, 2, 3)
#define X86_FEATURE_SYSCALL     X86_CPUID_BIT(0x80000001, 3, 11)
#define X86_FEATURE_NX          X86_CPUID_BIT(0x80000001, 3, 20)
#define X86_FEATURE_HUGE_PAGE   X86_CPUID_BIT(0x80000001, 3, 26)
#define X86_FEATURE_RDTSCP      X86_CPUID_BIT(0x80000001, 3, 27)
#define X86_FEATURE_INVAR_TSC   X86_CPUID_BIT(0x80000007, 3, 8)

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
