// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2009 Corey Tabaka
// Copyright (c) 2015 Intel Corporation
// Copyright (c) 2016 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <lk/compiler.h>
#include <sys/types.h>
#include <stdlib.h>
#include <stdbool.h>

#include <arch/x86/registers.h>

__BEGIN_CDECLS

#define X86_8BYTE_MASK 0xFFFFFFFF

struct x86_32_iframe {
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;    // pushed by common handler using pusha
    uint32_t ds, es, fs, gs;                            // pushed by common handler
    uint32_t vector;                                    // pushed by stub
    uint32_t err_code;                                  // pushed by interrupt or stub
    uint32_t ip, cs, flags;                             // pushed by interrupt
    uint32_t user_sp, user_ss;                          // pushed by interrupt if priv change occurs
};

struct x86_64_iframe {
    uint64_t rdi, rsi, rbp, rbx, rdx, rcx, rax;         // pushed by common handler
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;      // pushed by common handler
    uint64_t vector;                                    // pushed by stub
    uint64_t err_code;                                  // pushed by interrupt or stub
    uint64_t ip, cs, flags;                             // pushed by interrupt
    uint64_t user_sp, user_ss;                          // pushed by interrupt
};

#if ARCH_X86_32
typedef struct x86_32_iframe x86_iframe_t;
#elif ARCH_X86_64
typedef struct x86_64_iframe x86_iframe_t;
#endif

struct x86_32_context_switch_frame {
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
    uint32_t eflags;
    uint32_t eip;
};

struct x86_64_context_switch_frame {
    uint64_t r15, r14, r13, r12;
    uint64_t rbp;
    uint64_t rbx;
    uint64_t rflags;
    uint64_t rip;
};

void x86_64_context_switch(vaddr_t *oldsp, vaddr_t newsp);
void x86_uspace_entry(
        void *thread_arg,
        vaddr_t ustack,
        uint64_t rflags,
        vaddr_t entry_point) __NO_RETURN;

/* @brief Bring all of the APs up and hand them over to the kernel
 *
 * Must be called on the BP
 *
 * @param apic_ids A list of all APIC IDs that correspond to cores that should
 *                 be running.  The BP should be in the list.
 * @param num_cpus The number of entries in the apic_ids list.
 */
void x86_bringup_smp(uint32_t *apic_ids, uint32_t num_cpus);

#define IO_BITMAP_BITS      65536
#define IO_BITMAP_BYTES     (IO_BITMAP_BITS/8)
#define IO_BITMAP_LONGS     (IO_BITMAP_BITS/sizeof(long))

/*
 * x86-32 TSS structure
 */
typedef struct {
    uint16_t    backlink, __blh;
    uint32_t    esp0;
    uint16_t    ss0, __ss0h;
    uint32_t    esp1;
    uint16_t    ss1, __ss1h;
    uint32_t    esp2;
    uint16_t    ss2, __ss2h;
    uint32_t    cr3;
    uint32_t    eip;
    uint32_t    eflags;
    uint32_t    eax, ecx, edx, ebx;
    uint32_t    esp, ebp, esi, edi;
    uint16_t    es, __esh;
    uint16_t    cs, __csh;
    uint16_t    ss, __ssh;
    uint16_t    ds, __dsh;
    uint16_t    fs, __fsh;
    uint16_t    gs, __gsh;
    uint16_t    ldt, __ldth;
    uint16_t    trace, bitmap;

    uint8_t tss_bitmap[IO_BITMAP_BYTES + 1];
} __PACKED tss_32_t;

/*
 * Assignment of Interrupt Stack Table entries
 */
#define NUM_ASSIGNED_IST_ENTRIES 3
#define NMI_IST_INDEX 1
#define MCE_IST_INDEX 2
#define DBF_IST_INDEX 3

/*
 * x86-64 TSS structure
 */
typedef struct {
    uint32_t rsvd0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint32_t rsvd1;
    uint32_t rsvd2;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint32_t rsvd3;
    uint32_t rsvd4;
    uint16_t rsvd5;
    uint16_t iomap_base;

    uint8_t tss_bitmap[IO_BITMAP_BYTES + 1];
} __PACKED tss_64_t;

#if ARCH_X86_32
typedef tss_32_t tss_t;
#elif ARCH_X86_64
typedef tss_64_t tss_t;
#endif

/* shared accessors between 32 and 64bit */
static inline void x86_clts(void) {__asm__ __volatile__ ("clts"); }
static inline void x86_hlt(void) {__asm__ __volatile__ ("hlt"); }
static inline void x86_sti(void) {__asm__ __volatile__ ("sti"); }
static inline void x86_cli(void) {__asm__ __volatile__ ("cli"); }
static inline void x86_ltr(uint16_t sel)
{
    __asm__ __volatile__ ("ltr %%ax" :: "a" (sel));
}
static inline void x86_lidt(uintptr_t base)
{
    __asm volatile("lidt (%0)" :: "r"(base) : "memory");
}

static inline uint8_t inp(uint16_t _port)
{
    uint8_t rv;
    __asm__ __volatile__ ("inb %1, %0"
                          : "=a" (rv)
                          : "d" (_port));
    return (rv);
}

static inline uint16_t inpw (uint16_t _port)
{
    uint16_t rv;
    __asm__ __volatile__ ("inw %1, %0"
                          : "=a" (rv)
                          : "d" (_port));
    return (rv);
}

static inline uint32_t inpd(uint16_t _port)
{
    uint32_t rv;
    __asm__ __volatile__ ("inl %1, %0"
                          : "=a" (rv)
                          : "d" (_port));
    return (rv);
}

static inline void outp(uint16_t _port, uint8_t _data)
{
    __asm__ __volatile__ ("outb %1, %0"
                          :
                          : "d" (_port),
                          "a" (_data));
}

static inline void outpw(uint16_t _port, uint16_t _data)
{
    __asm__ __volatile__ ("outw %1, %0"
                          :
                          : "d" (_port),
                          "a" (_data));
}

static inline void outpd(uint16_t _port, uint32_t _data)
{
    __asm__ __volatile__ ("outl %1, %0"
                          :
                          : "d" (_port),
                          "a" (_data));
}

static inline uint64_t rdtsc(void)
{
    uint64_t tsc;

#if ARCH_X86_64
    uint32_t tsc_low;
    uint32_t tsc_hi;

    __asm__ __volatile__("rdtsc" : "=a" (tsc_low), "=d" (tsc_hi));

    tsc = ((uint64_t)tsc_hi << 32) | tsc_low;
#else
    __asm__ __volatile__("rdtsc" : "=A" (tsc));
#endif

     return tsc;
}

static inline void cpuid(uint32_t sel, uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d)
{
    *a = sel;

    __asm__ __volatile__("cpuid"
                         : "+a" (*a), "=c" (*c), "=d" (*d), "=b"(*b)
                        );
}

/* cpuid wrapper with ecx set to a second argument */
static inline void cpuid_c(uint32_t sel, uint32_t sel_c, uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d)
{
    *a = sel;
    *c = sel_c;

    __asm__ __volatile__("cpuid"
                         : "+a" (*a), "+c" (*c), "=d" (*d), "=b"(*b)
                        );
}

static inline void set_in_cr0(ulong mask)
{
    ulong temp;

    __asm__ __volatile__ (
        "mov %%cr0, %0  \n\t"
        "or %1, %0      \n\t"
        "mov %0, %%cr0   \n\t"
        : "=r" (temp) : "irg" (mask)
        :);
}

static inline void clear_in_cr0(ulong mask)
{
    ulong temp;

    __asm__ __volatile__ (
        "mov %%cr0, %0  \n\t"
        "and %1, %0     \n\t"
        "mov %0, %%cr0  \n\t"
        : "=r" (temp) : "irg" (~mask)
        :);
}

static inline ulong x86_get_cr2(void)
{
    ulong rv;

    __asm__ __volatile__ (
        "mov %%cr2, %0"
        : "=r" (rv)
    );

    return rv;
}

static inline ulong x86_get_cr3(void)
{
    ulong rv;

    __asm__ __volatile__ (
        "mov %%cr3, %0"
        : "=r" (rv));
    return rv;
}

static inline void x86_set_cr3(ulong in_val)
{
    __asm__ __volatile__ (
        "mov %0,%%cr3 \n\t"
        :
        :"r" (in_val));
}

static inline ulong x86_get_cr0(void)
{
    ulong rv;

    __asm__ __volatile__ (
        "mov %%cr0, %0 \n\t"
        : "=r" (rv));
    return rv;
}

static inline ulong x86_get_cr4(void)
{
    ulong rv;

    __asm__ __volatile__ (
        "mov %%cr4, %0 \n\t"
        : "=r" (rv));
    return rv;
}


static inline void x86_set_cr0(ulong in_val)
{
    __asm__ __volatile__ (
        "mov %0,%%cr0 \n\t"
        :
        :"r" (in_val));
}

static inline void x86_set_cr4(ulong in_val)
{
    __asm__ __volatile__ (
        "mov %0,%%cr4 \n\t"
        :
        :"r" (in_val));
}

static inline uint64_t read_msr (uint32_t msr_id)
{
#if ARCH_X86_64
    uint32_t msr_read_val_lo;
    uint32_t msr_read_val_hi;

    __asm__ __volatile__ (
        "rdmsr \n\t"
        : "=a" (msr_read_val_lo), "=d" (msr_read_val_hi)
        : "c" (msr_id));

    return ((uint64_t)msr_read_val_hi << 32) | msr_read_val_lo;
#else
    /* =A doesn't seem to work properly on x86-64 */
    uint64_t msr_read_val;
    __asm__ __volatile__ (
        "rdmsr \n\t"
        : "=A" (msr_read_val)
        : "c" (msr_id));

    return msr_read_val;
#endif
}

static inline void write_msr (uint32_t msr_id, uint64_t msr_write_val)
{
#if ARCH_X86_64
    __asm__ __volatile__ (
        "wrmsr \n\t"
        : : "c" (msr_id), "a" (msr_write_val & 0xffffffff), "d" (msr_write_val >> 32));
#else
    __asm__ __volatile__ (
        "wrmsr \n\t"
        : : "c" (msr_id), "A" (msr_write_val));
#endif
}


static inline bool x86_is_paging_enabled(void)
{
    if (x86_get_cr0() & X86_CR0_PG)
        return true;

    return false;
}

static inline bool x86_is_PAE_enabled(void)
{
    if (x86_is_paging_enabled() == false)
        return false;

    if (!(x86_get_cr4() & X86_CR4_PAE))
        return false;

    return true;
}

static inline ulong x86_read_gs_offset(uintptr_t offset)
{
    ulong ret;

    __asm__(
        "mov   %%gs:%1, %0"
        : "=r" (ret)
        : "m" (*(ulong *)(offset))
        :
    );

    return ret;
}

static inline void x86_write_gs_offset(uintptr_t offset, ulong val)
{
    __asm__(
        "mov   %0, %%gs:%1"
        :
        : "r" (val), "m" (*(ulong *)(offset))
        : "memory"
    );
}


#if ARCH_X86_32

typedef uint32_t x86_flags_t;

static inline uint32_t x86_save_flags(void)
{
    unsigned int state;

    __asm__ volatile(
        "pushfl;"
        "popl %0"
        : "=rm" (state)
        :: "memory");

    return state;
}

static inline void x86_restore_flags(uint32_t flags)
{
    __asm__ volatile(
        "pushl %0;"
        "popfl"
        :: "g" (flags)
        : "memory", "cc");
}

static inline void inprep(uint16_t _port, uint8_t *_buffer, uint32_t _reads)
{
    __asm__ __volatile__ ("pushal \n\t"
                          "pushfl \n\t"
                          "cli \n\t"
                          "cld \n\t"
                          "rep insb \n\t"
                          "popfl \n\t"
                          "popal"
                          :
                          : "d" (_port),
                          "D" (_buffer),
                          "c" (_reads));
}

static inline void outprep(uint16_t _port, uint8_t *_buffer, uint32_t _writes)
{
    __asm__ __volatile__ ("pushal \n\t"
                          "pushfl \n\t"
                          "cli \n\t"
                          "cld \n\t"
                          "rep outsb \n\t"
                          "popfl \n\t"
                          "popal"
                          :
                          : "d" (_port),
                          "S" (_buffer),
                          "c" (_writes));
}

static inline void inpwrep(uint16_t _port, uint16_t *_buffer, uint32_t _reads)
{
    __asm__ __volatile__ ("pushal \n\t"
                          "pushfl \n\t"
                          "cli \n\t"
                          "cld \n\t"
                          "rep insw \n\t"
                          "popfl \n\t"
                          "popal"
                          :
                          : "d" (_port),
                          "D" (_buffer),
                          "c" (_reads));
}

static inline void outpwrep(uint16_t _port, uint16_t *_buffer,
                            uint32_t _writes)
{
    __asm__ __volatile__ ("pushal \n\t"
                          "pushfl \n\t"
                          "cli \n\t"
                          "cld \n\t"
                          "rep outsw \n\t"
                          "popfl \n\t"
                          "popal"
                          :
                          : "d" (_port),
                          "S" (_buffer),
                          "c" (_writes));
}

static inline void inpdrep(uint16_t _port, uint32_t *_buffer,
                           uint32_t _reads)
{
    __asm__ __volatile__ ("pushal \n\t"
                          "pushfl \n\t"
                          "cli \n\t"
                          "cld \n\t"
                          "rep insl \n\t"
                          "popfl \n\t"
                          "popal"
                          :
                          : "d" (_port),
                          "D" (_buffer),
                          "c" (_reads));
}

static inline void outpdrep(uint16_t _port, uint32_t *_buffer,
                            uint32_t _writes)
{
    __asm__ __volatile__ ("pushal \n\t"
                          "pushfl \n\t"
                          "cli \n\t"
                          "cld \n\t"
                          "rep outsl \n\t"
                          "popfl \n\t"
                          "popal"
                          :
                          : "d" (_port),
                          "S" (_buffer),
                          "c" (_writes));
}

#endif // ARCH_X86_32

#if ARCH_X86_64

typedef uint64_t x86_flags_t;

static inline uint64_t x86_save_flags(void)
{
    uint64_t state;

    __asm__ volatile(
        "pushfq;"
        "popq %0"
        : "=rm" (state)
        :: "memory");

    return state;
}

static inline void x86_restore_flags(uint64_t flags)
{
    __asm__ volatile(
        "pushq %0;"
        "popfq"
        :: "g" (flags)
        : "memory", "cc");
}

static inline void inprep(uint16_t _port, uint8_t *_buffer, uint32_t _reads)
{
    __asm__ __volatile__ ("pushfq \n\t"
                          "cli \n\t"
                          "cld \n\t"
                          "rep insb \n\t"
                          "popfq \n\t"
                          :
                          : "d" (_port),
                          "D" (_buffer),
                          "c" (_reads));
}

static inline void outprep(uint16_t _port, uint8_t *_buffer, uint32_t _writes)
{
    __asm__ __volatile__ ("pushfq \n\t"
                          "cli \n\t"
                          "cld \n\t"
                          "rep outsb \n\t"
                          "popfq \n\t"
                          :
                          : "d" (_port),
                          "S" (_buffer),
                          "c" (_writes));
}

static inline void inpwrep(uint16_t _port, uint16_t *_buffer, uint32_t _reads)
{
    __asm__ __volatile__ ("pushfq \n\t"
                          "cli \n\t"
                          "cld \n\t"
                          "rep insw \n\t"
                          "popfq \n\t"
                          :
                          : "d" (_port),
                          "D" (_buffer),
                          "c" (_reads));
}

static inline void outpwrep(uint16_t _port, uint16_t *_buffer,
                            uint32_t _writes)
{
    __asm__ __volatile__ ("pushfq \n\t"
                          "cli \n\t"
                          "cld \n\t"
                          "rep outsw \n\t"
                          "popfq \n\t"
                          :
                          : "d" (_port),
                          "S" (_buffer),
                          "c" (_writes));
}

static inline void inpdrep(uint16_t _port, uint32_t *_buffer,
                           uint32_t _reads)
{
    __asm__ __volatile__ ("pushfq \n\t"
                          "cli \n\t"
                          "cld \n\t"
                          "rep insl \n\t"
                          "popfq \n\t"
                          :
                          : "d" (_port),
                          "D" (_buffer),
                          "c" (_reads));
}

static inline void outpdrep(uint16_t _port, uint32_t *_buffer,
                            uint32_t _writes)
{
    __asm__ __volatile__ ("pushfq \n\t"
                          "cli \n\t"
                          "cld \n\t"
                          "rep outsl \n\t"
                          "popfq \n\t"
                          :
                          : "d" (_port),
                          "S" (_buffer),
                          "c" (_writes));
}

#endif // ARCH_X86_64

__END_CDECLS
