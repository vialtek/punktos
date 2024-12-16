/*
 * Copyright (c) 2008 Travis Geiselbrecht
 * Copyright (c) 2015 Intel Corporation
 *
 * Use of this source code is governed by a MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT
 */
#pragma once

/* top level defines for the x86 mmu */
/* NOTE: the top part can be included from assembly */
#define KB                (1024UL)
#define MB                (1024UL*1024UL)
#define GB                (1024UL*1024UL*1024UL)

#define X86_MMU_PG_P        0x001           /* P    Valid                   */
#define X86_MMU_PG_RW       0x002           /* R/W  Read/Write              */
#define X86_MMU_PG_U        0x004           /* U/S  User/Supervisor         */
#define X86_MMU_PG_PS       0x080           /* PS   Page size (0=4k,1=4M)   */
#define X86_MMU_PG_PTE_PAT  0x080           /* PAT  PAT index               */
#define X86_MMU_PG_G        0x100           /* G    Global                  */
#define X86_MMU_CLEAR       0x0
#define X86_DIRTY_ACCESS_MASK   0xf9f
#define X86_MMU_CACHE_DISABLE   0x010       /* C Cache disable */

/* default flags for inner page directory entries */
#define X86_KERNEL_PD_FLAGS (X86_MMU_PG_RW | X86_MMU_PG_P)

/* default flags for 2MB/4MB/1GB page directory entries */
#define X86_KERNEL_PD_LP_FLAGS (X86_MMU_PG_G | X86_MMU_PG_PS | X86_MMU_PG_RW | X86_MMU_PG_P)

#define PAGE_SIZE       4096
#define PAGE_DIV_SHIFT      12
#define X86_PDPT_ADDR_MASK  (0x00000000ffffffe0ul)
#define X86_PG_FRAME        (0xfffffffffffff000ul)
#define X86_PHY_ADDR_MASK   (0x000ffffffffffffful)
#define X86_FLAGS_MASK      (0x8000000000000ffful)
#define X86_PTE_NOT_PRESENT (0xFFFFFFFFFFFFFFFEul)
#define X86_2MB_PAGE_FRAME  (0x000fffffffe00000ul)
#define PAGE_OFFSET_MASK_4KB    (0x0000000000000ffful)
#define PAGE_OFFSET_MASK_2MB    (0x00000000001ffffful)
#define X86_MMU_PG_NX       (1ULL << 63)
#define X86_PAGING_LEVELS   4
#define PML4_SHIFT      39
#define PDP_SHIFT       30
#define PD_SHIFT        21
#define PT_SHIFT        12
#define ADDR_OFFSET     9
#define PDPT_ADDR_OFFSET    2
#define NO_OF_PT_ENTRIES    512

/* on x86-64 physical memory is mapped at the base of the kernel address space */
#define X86_PHYS_TO_VIRT(x)     ((uintptr_t)(x) + KERNEL_ASPACE_BASE)
#define X86_VIRT_TO_PHYS(x)     ((uintptr_t)(x) - KERNEL_ASPACE_BASE)

/* C defines below */
#ifndef ASSEMBLY

#include <sys/types.h>
#include <lk/compiler.h>

__BEGIN_CDECLS

/* Different page table levels in the page table mgmt hirerachy */
enum page_table_levels {
    PF_L,
    PT_L,
    PD_L,
    PDP_L,
    PML4_L
};

struct map_range {
    vaddr_t start_vaddr;
    paddr_t start_paddr; /* Physical address in the PAE mode is 32 bits wide */
    uint32_t size;
};

typedef uint64_t pt_entry_t;
typedef uint64_t map_addr_t;
typedef uint64_t arch_flags_t;

void x86_mmu_early_init(void);
void x86_mmu_init(void);

__END_CDECLS

#endif // !ASSEMBLY
