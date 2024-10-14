// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <kernel/vm/vm_aspace.h>

#include "vm_priv.h"
#include <assert.h>
#include <lk/err.h>
#include <kernel/auto_lock.h>
#include <kernel/thread.h>
#include <vm/vm.h>
#include <vm/vm_object.h>
#include <vm/vm_region.h>
#include <lk/init.h>
#include <stdlib.h>
#include <string.h>
#include <lk/trace.h>
#include <lib/sbl/auto_call.h>
#include <lib/sbl/intrusive_double_list.h>
#include <lib/sbl/list_utils.h>
#include <lib/sbl/type_support.h>

#define LOCAL_TRACE MAX(VM_GLOBAL_TRACE, 0)

// pointer to a singleton kernel address space
VmAspace* VmAspace::kernel_aspace_ = nullptr;

// list of all address spaces
static mutex_t aspace_list_lock = MUTEX_INITIAL_VALUE(aspace_list_lock);
static sbs::DoublyLinkedList<VmAspace> aspaces;

// called once at boot to initialize the singleton kernel address space
void VmAspace::KernelAspaceInit() {
    // the singleton kernel address space
    static VmAspace _kernel_aspace(KERNEL_ASPACE_BASE, KERNEL_ASPACE_SIZE, VmAspace::TYPE_KERNEL,
                                   "kernel");
    auto err = _kernel_aspace.Init();
    ASSERT(err >= 0);

#if LK_DEBUGLEVEL > 0
    _kernel_aspace.Adopt();
#endif

    aspaces.push_front(&_kernel_aspace);

    // save a pointer to the singleton kernel address space
    VmAspace::kernel_aspace_ = &_kernel_aspace;
}

// simple test routines
static inline bool is_inside(VmAspace& aspace, vaddr_t vaddr) {
    return (vaddr >= aspace.base() && vaddr <= aspace.base() + aspace.size() - 1);
}

static inline bool is_inside(VmAspace& aspace, VmRegion& r) {
    // is the starting address within the address space
    if (!is_inside(aspace, r.base()))
        return false;

    if (r.size() == 0)
        return true;

    // see if the size is enough to wrap the integer
    if (r.base() + r.size() - 1 < r.base())
        return false;

    // test to see if the end address is within the address space's
    if (r.base() + r.size() - 1 > aspace.base() + aspace.size() - 1)
        return false;

    return true;
}

static inline size_t trim_to_aspace(VmAspace& aspace, vaddr_t vaddr, size_t size) {
    DEBUG_ASSERT(is_inside(aspace, vaddr));

    if (size == 0)
        return size;

    size_t offset = vaddr - aspace.base();

    // LTRACEF("vaddr 0x%lx size 0x%zx offset 0x%zx aspace base 0x%lx aspace size 0x%zx\n",
    //        vaddr, size, offset, aspace.base(), aspace.size());

    if (offset + size < offset)
        size = ULONG_MAX - offset - 1;

    // LTRACEF("size now 0x%zx\n", size);

    if (offset + size >= aspace.size() - 1)
        size = aspace.size() - offset;

    // LTRACEF("size now 0x%zx\n", size);

    return size;
}

VmAspace::VmAspace(vaddr_t base, size_t size, uint32_t flags, const char* name)
    : base_(base), size_(size), flags_(flags) {

    DEBUG_ASSERT(size != 0);
    DEBUG_ASSERT(base + size - 1 >= base);

    Rename(name);

    LTRACEF("%p '%s'\n", this, name_);
}

status_t VmAspace::Init() {
    DEBUG_ASSERT(magic_ == MAGIC);

    LTRACEF("%p '%s'\n", this, name_);

    // intialize the architectually specific part
    bool is_high_kernel = (flags_ & TYPE_MASK) == TYPE_KERNEL;
    uint arch_aspace_flags = is_high_kernel ? ARCH_ASPACE_FLAG_KERNEL : 0;
    return arch_mmu_init_aspace(&arch_aspace_, base_, size_, arch_aspace_flags);
}

sbs::RefPtr<VmAspace> VmAspace::Create(uint32_t flags, const char* name) {
    LTRACEF("flags 0x%x, name '%s'\n", flags, name);

    vaddr_t base;
    size_t size;
    switch (flags & TYPE_MASK) {
    case TYPE_USER:
        base = USER_ASPACE_BASE;
        size = USER_ASPACE_SIZE;
        break;
    case TYPE_KERNEL:
        base = KERNEL_ASPACE_BASE;
        size = KERNEL_ASPACE_SIZE;
        break;
    case TYPE_LOW_KERNEL:
        base = 0;
        size = USER_ASPACE_BASE + USER_ASPACE_SIZE;
        break;
    default:
        panic("Invalid aspace type");
    }
    auto aspace = sbs::AdoptRef(new VmAspace(base, size, flags, name));
    if (!aspace)
        return nullptr;

    // initialize the arch specific component to our address space
    auto err = aspace->Init();
    if (err < 0) {
        return nullptr;
    }

    // add it to the global list
    {
        AutoLock a(aspace_list_lock);
        aspaces.push_back(aspace.get());
    }

    // return a ref pointer to the aspace
    return sbs::move(aspace);
}

void VmAspace::Rename(const char* name) {
    DEBUG_ASSERT(magic_ == MAGIC);
    strlcpy(name_, name ? name : "unnamed", sizeof(name_));
}

VmAspace::~VmAspace() {
    DEBUG_ASSERT(magic_ == MAGIC);
    LTRACEF("%p '%s'\n", this, name_);

    // we have to have already been destroyed before freeing
    DEBUG_ASSERT(regions_.is_empty());

    // pop it out of the global aspace list
    {
        AutoLock a(aspace_list_lock);
        aspaces.remove(this);
    }

    // destroy the arch portion of the aspace
    arch_mmu_destroy_aspace(&arch_aspace_);

    // clear the magic
    magic_ = 0;
}

status_t VmAspace::Destroy() {
    DEBUG_ASSERT(magic_ == MAGIC);
    LTRACEF("%p '%s'\n", this, name_);

    // tear down and free all of the regions in our address space
    mutex_acquire(&lock_);
    VmRegion* r;
    while ((r = regions_.pop_front())) {
        r->Unmap();

        mutex_release(&lock_);

        // free any resources the region holds
        r->Destroy();

        // free it if we have the last ref
        if (r->Release())
            delete r;

        mutex_acquire(&lock_);
    }

    mutex_release(&lock_);

    return NO_ERROR;
}

// add a region to the appropriate spot in the address space list,
// testing to see if there's a space
status_t VmAspace::AddRegion(sbs::RefPtr<VmRegion> r) {
    DEBUG_ASSERT(magic_ == MAGIC);
    DEBUG_ASSERT(r);

    LTRACEF_LEVEL(2, "aspace %p base 0x%lx size 0x%zx r %p base 0x%lx size 0x%zx\n", this, base_,
                  size_, r.get(), r->base(), r->size());

    // only try if the region will at least fit in the address space
    if (r->size() == 0 || !is_inside(*this, *r)) {
        LTRACEF_LEVEL(2, "region was out of range\n");
        return ERR_OUT_OF_RANGE;
    }

    vaddr_t r_end = r->base() + r->size() - 1;

    // does it fit in front
    auto first = regions_.first();
    if (!first || r_end < first->base()) {
        // empty list or not empty and fits before the first element
        regions_.push_front(r.get());
        r->AddRef();
        return NO_ERROR;
    }

    for (auto last = regions_.first(); last; last = regions_.next(last)) {
        // does it go after last?
        if (r->base() > last->base() + last->size() - 1) {
            // get the next element in the list
            auto next = regions_.next(last);
            if (!next || (r_end < next->base())) {
                // end of the list or next exists and it goes between them
                regions_.add_after(last, r.get());
                r->AddRef();
                return NO_ERROR;
            }
        }
    }

    LTRACEF_LEVEL(2, "couldn't find spot\n");
    return ERR_NO_MEMORY;
}

//
//  Try to pick the spot within specified gap
//
//  Arch can override this to impose it's own restrictions.

__WEAK vaddr_t arch_mmu_pick_spot(arch_aspace_t* aspace, vaddr_t base,
                                  uint prev_region_arch_mmu_flags, vaddr_t end,
                                  uint next_region_arch_mmu_flags, vaddr_t align, size_t size,
                                  uint arch_mmu_flags) {
    // just align it by default
    return ALIGN(base, align);
}

//
//  Returns true if the caller has to stop search

static inline bool check_gap(VmAspace* aspace, VmRegion* prev, VmRegion* next, vaddr_t* pva,
                             vaddr_t align, size_t size, uint arch_mmu_flags) {
    vaddr_t gap_beg; // first byte of a gap
    vaddr_t gap_end; // last byte of a gap

    DEBUG_ASSERT(pva);

    if (prev)
        gap_beg = prev->base() + prev->size();
    else
        gap_beg = aspace->base();

    if (next) {
        if (gap_beg == next->base())
            goto next_gap; // no gap between regions
        gap_end = next->base() - 1;
    } else {
        if (gap_beg == (aspace->base() + aspace->size()))
            goto not_found; // no gap at the end of address space. Stop search
        gap_end = aspace->base() + aspace->size() - 1;
    }

    *pva = arch_mmu_pick_spot(&aspace->arch_aspace(), gap_beg,
                              prev ? prev->arch_mmu_flags() : ARCH_MMU_FLAG_INVALID, gap_end,
                              next ? next->arch_mmu_flags() : ARCH_MMU_FLAG_INVALID, align, size,
                              arch_mmu_flags);
    if (*pva < gap_beg)
        goto not_found; // address wrapped around

    if (*pva < gap_end && ((gap_end - *pva + 1) >= size)) {
        // we have enough room
        return true; // found spot, stop search
    }

next_gap:
    return false; // continue search

not_found:
    *pva = -1;
    return true; // not_found: stop search
}

// search for a spot to allocate for a region of a given size, returning the pointer to the region
// before in the list
vaddr_t VmAspace::AllocSpot(size_t size, uint8_t align_pow2, uint arch_mmu_flags,
                            VmRegion** before) {
    DEBUG_ASSERT(magic_ == MAGIC);
    DEBUG_ASSERT(size > 0 && IS_PAGE_ALIGNED(size));

    LTRACEF_LEVEL(2, "aspace %p size 0x%zx align %hhu\n", this, size, align_pow2);

    if (align_pow2 < PAGE_SIZE_SHIFT)
        align_pow2 = PAGE_SIZE_SHIFT;
    vaddr_t align = 1UL << align_pow2;

    vaddr_t spot;

    // try to pick spot at the beginning of address space
    VmRegion* r = nullptr;
    if (check_gap(this, nullptr, regions_.first(), &spot, align, size, arch_mmu_flags))
        goto done;

    // search the middle of the list
    for (r = regions_.first(); r; r = regions_.next(r)) {
        if (check_gap(this, r, regions_.next(r), &spot, align, size, arch_mmu_flags))
            goto done;
    }

    // couldn't find anything
    return -1;

done:
    if (before)
        *before = r;
    return spot;
}

// allocate a region and insert it into the list
sbs::RefPtr<VmRegion> VmAspace::AllocRegion(const char* name, size_t size, vaddr_t vaddr,
                                              uint8_t align_pow2, uint vmm_flags,
                                              uint arch_mmu_flags) {
    DEBUG_ASSERT(magic_ == MAGIC);
    DEBUG_ASSERT(is_mutex_held(&lock_));
    LTRACEF_LEVEL(2, "aspace %p name '%s' size 0x%zx vaddr 0x%lx\n", this, name, size, vaddr);

    // make a region struct for it and stick it in the list
    sbs::RefPtr<VmRegion> r = VmRegion::Create(*this, vaddr, size, arch_mmu_flags, name);
    if (!r)
        return nullptr;

    // if they ask us for a specific spot, put it there
    if (vmm_flags & VMM_FLAG_VALLOC_SPECIFIC) {
        DEBUG_ASSERT(IS_PAGE_ALIGNED(vaddr));

        // stick it in the list, checking to see if it fits
        if (AddRegion(r) < 0) {
            // didn't fit
            return nullptr;
        }
    } else {
        // allocate a virtual slot for it
        VmRegion* before;

        vaddr = AllocSpot(size, align_pow2, arch_mmu_flags, &before);
        LTRACEF_LEVEL(2, "alloc_spot returns 0x%lx, before %p\n", vaddr, before);

        if (vaddr == (vaddr_t)-1) {
            LTRACEF_LEVEL(2, "failed to find spot\n");
            return nullptr;
        }

        r->set_base(vaddr);

        // add it to the region list
        r->AddRef();
        if (before)
            regions_.add_after(before, r.get());
        else
            regions_.push_front(r.get());
    }

    return sbs::move(r);
}

// internal find region search routine
VmRegion* VmAspace::FindRegionLocked(vaddr_t vaddr) {
    DEBUG_ASSERT(magic_ == MAGIC);
    DEBUG_ASSERT(is_mutex_held(&lock_));

    // search the region list
    for (auto r = regions_.first(); r; r = regions_.next(r)) {
        if ((vaddr >= r->base()) && (vaddr <= r->base() + r->size() - 1))
            return r;
    }

    return nullptr;
}

// return a ref pointer to a region
sbs::RefPtr<VmRegion> VmAspace::FindRegion(vaddr_t vaddr) {
    AutoLock a(lock_);

    auto r = FindRegionLocked(vaddr);

    return sbs::RefPtr<VmRegion>(r);
}

status_t VmAspace::MapObject(sbs::RefPtr<VmObject> vmo, const char* name, uint64_t offset,
                             size_t size, void** ptr, uint8_t align_pow2, uint vmm_flags,
                             uint arch_mmu_flags) {

    DEBUG_ASSERT(magic_ == MAGIC);
    LTRACEF(
        "aspace %p name '%s' vmo %p, offset 0x%llx size 0x%zx ptr %p align %hhu vmm_flags 0x%x "
        "arch_mmu_flags 0x%x\n",
        this, name, vmo.get(), offset, size, ptr ? *ptr : 0, align_pow2, vmm_flags, arch_mmu_flags);

    size = ROUNDUP(size, PAGE_SIZE);
    if (size == 0)
        return ERR_INVALID_ARGS;
    if (!vmo)
        return ERR_INVALID_ARGS;
    if (!IS_PAGE_ALIGNED(offset))
        return ERR_INVALID_ARGS;

    vaddr_t vaddr = 0;
    // if they're asking for a specific spot, copy the address
    if (vmm_flags & VMM_FLAG_VALLOC_SPECIFIC) {
        // can't ask for a specific spot and then not provide one
        if (!ptr) {
            return ERR_INVALID_ARGS;
        }
        vaddr = (vaddr_t)*ptr;

        // check that it's page aligned
        if (!IS_PAGE_ALIGNED(vaddr))
            return ERR_INVALID_ARGS;
    }

    // hold the vmm lock for the rest of the function
    AutoLock a(lock_);

    // allocate a region and put it in the aspace list
    auto r = AllocRegion(name, size, vaddr, align_pow2, vmm_flags, arch_mmu_flags);
    if (!r) {
        return ERR_NO_MEMORY;
    }

    // associate the vm object with it
    r->SetObject(sbs::move(vmo), offset);

    // if we're committing it, map the region now
    if (vmm_flags & VMM_FLAG_COMMIT) {
        auto err = r->MapRange(0, size, true);
        if (err < 0)
            return err;
    }

    // return the vaddr if requested
    if (ptr)
        *ptr = (void*)r->base();

    return NO_ERROR;
}

status_t VmAspace::ReserveSpace(const char* name, size_t size, vaddr_t vaddr) {
    DEBUG_ASSERT(magic_ == MAGIC);
    LTRACEF("aspace %p name '%s' size 0x%zx vaddr 0x%lx\n", this, name, size, vaddr);

    DEBUG_ASSERT(IS_PAGE_ALIGNED(vaddr));
    DEBUG_ASSERT(IS_PAGE_ALIGNED(size));

    size = ROUNDUP_PAGE_SIZE(size);
    if (size == 0)
        return NO_ERROR;
    if (!IS_PAGE_ALIGNED(vaddr))
        return ERR_INVALID_ARGS;
    if (!is_inside(*this, vaddr))
        return ERR_OUT_OF_RANGE;

    // trim the size
    size = trim_to_aspace(*this, vaddr, size);

    AutoLock a(lock_);

    // lookup how it's already mapped
    uint arch_mmu_flags = 0;
    auto err = arch_mmu_query(&arch_aspace_, vaddr, nullptr, &arch_mmu_flags);
    if (err) {
        // if it wasn't already mapped, use some sort of strict default
        arch_mmu_flags =
            ARCH_MMU_FLAG_CACHED | ARCH_MMU_FLAG_PERM_RO | ARCH_MMU_FLAG_PERM_NO_EXECUTE;
    }

    // build a new region structure without any backing vm object
    auto r = AllocRegion(name, size, vaddr, 0, VMM_FLAG_VALLOC_SPECIFIC, arch_mmu_flags);

    return r ? NO_ERROR : ERR_NO_MEMORY;
}

status_t VmAspace::AllocPhysical(const char* name, size_t size, void** ptr, uint8_t align_log2,
                                 paddr_t paddr, uint vmm_flags, uint arch_mmu_flags) {
    DEBUG_ASSERT(magic_ == MAGIC);
    LTRACEF(
        "aspace %p name '%s' size 0x%zx ptr %p paddr 0x%lx vmm_flags 0x%x arch_mmu_flags 0x%x\n",
        this, name, size, ptr ? *ptr : 0, paddr, vmm_flags, arch_mmu_flags);

    DEBUG_ASSERT(IS_PAGE_ALIGNED(paddr));

    if (size == 0)
        return NO_ERROR;
    if (!IS_PAGE_ALIGNED(paddr))
        return ERR_INVALID_ARGS;

    size = ROUNDUP_PAGE_SIZE(size);

    // test for invalid flags
    if (vmm_flags & VMM_FLAG_COMMIT)
        return ERR_INVALID_ARGS;

    vaddr_t vaddr = 0;

    // if they're asking for a specific spot, copy the address
    if (vmm_flags & VMM_FLAG_VALLOC_SPECIFIC) {
        // can't ask for a specific spot and then not provide one
        if (!ptr) {
            return ERR_INVALID_ARGS;
        }
        vaddr = reinterpret_cast<vaddr_t>(*ptr);

        // check that it's page aligned
        if (!IS_PAGE_ALIGNED(vaddr))
            return ERR_INVALID_ARGS;
    }

    AutoLock a(lock_);

    // allocate a region and put it in the aspace list
    auto r = AllocRegion(name, size, vaddr, align_log2, vmm_flags, arch_mmu_flags);
    if (!r) {
        return ERR_NO_MEMORY;
    }

    // map memory physically
    auto err = r->MapPhysicalRange(0, size, paddr, false);
    if (err < 0) {
        // TODO: remove the region from the aspace
        return err;
    }

    // return the vaddr if requested
    if (ptr)
        *ptr = reinterpret_cast<void*>(r->base());

    return NO_ERROR;
}

status_t VmAspace::AllocContiguous(const char* name, size_t size, void** ptr, uint8_t align_pow2,
                                   uint vmm_flags, uint arch_mmu_flags) {
    DEBUG_ASSERT(magic_ == MAGIC);
    LTRACEF("aspace %p name '%s' size 0x%zx ptr %p align %hhu vmm_flags 0x%x arch_mmu_flags 0x%x\n",
            this, name, size, ptr ? *ptr : 0, align_pow2, vmm_flags, arch_mmu_flags);

    size = ROUNDUP(size, PAGE_SIZE);
    if (size == 0)
        return ERR_INVALID_ARGS;

    // test for invalid flags
    if (!(vmm_flags & VMM_FLAG_COMMIT))
        return ERR_INVALID_ARGS;

    // create a vm object to back it
    auto vmo = VmObject::Create(PMM_ALLOC_FLAG_ANY, size);
    if (!vmo)
        return ERR_NO_MEMORY;

    // always immediately commit memory to the object
    int64_t committed = vmo->CommitRangeContiguous(0, size, align_pow2);
    if (committed < 0 || (size_t)committed < size) {
        LTRACEF("failed to allocate enough pages (asked for %zu, got %zu)\n", size / PAGE_SIZE,
                (size_t)committed / PAGE_SIZE);
        return ERR_NO_MEMORY;
    }

    return MapObject(sbs::move(vmo), name, 0, size, ptr, align_pow2, vmm_flags, arch_mmu_flags);
}

status_t VmAspace::Alloc(const char* name, size_t size, void** ptr, uint8_t align_pow2,
                         uint vmm_flags, uint arch_mmu_flags) {
    DEBUG_ASSERT(magic_ == MAGIC);
    LTRACEF("aspace %p name '%s' size 0x%zx ptr %p align %hhu vmm_flags 0x%x arch_mmu_flags 0x%x\n",
            this, name, size, ptr ? *ptr : 0, align_pow2, vmm_flags, arch_mmu_flags);

    size = ROUNDUP(size, PAGE_SIZE);
    if (size == 0)
        return ERR_INVALID_ARGS;

    // allocate a vm object to back it
    auto vmo = VmObject::Create(PMM_ALLOC_FLAG_ANY, size);
    if (!vmo)
        return ERR_NO_MEMORY;

    // commit memory up front if requested
    if (vmm_flags & VMM_FLAG_COMMIT) {
        // commit memory to the object
        int64_t committed = vmo->CommitRange(0, size);
        if (committed < 0 || (size_t)committed < size) {
            LTRACEF("failed to allocate enough pages (asked for %zu, got %zu)\n", size / PAGE_SIZE,
                    (size_t)committed / PAGE_SIZE);
            return ERR_NO_MEMORY;
        }
    }

    // map it, creating a new region
    return MapObject(sbs::move(vmo), name, 0, size, ptr, align_pow2, vmm_flags, arch_mmu_flags);
}

status_t VmAspace::FreeRegion(vaddr_t vaddr) {
    DEBUG_ASSERT(magic_ == MAGIC);
    LTRACEF("vaddr 0x%lx\n", vaddr);

    VmRegion* r;
    {
        AutoLock a(lock_);

        r = FindRegionLocked(vaddr);
        if (!r) {
            return ERR_NOT_FOUND;
        }

        // remove it from the address space list
        regions_.remove(r);

        // unmap it
        r->Unmap();
    }

    // destroy the region
    r->Destroy();

    // drop a ref and potentially free it
    if (r->Release())
        delete r;

    return NO_ERROR;
}

void VmAspace::AttachToThread(thread_t* t) {
    DEBUG_ASSERT(magic_ == MAGIC);
    DEBUG_ASSERT(t);

    // point the lk thread at our object via the dummy C vmm_aspace_t struct
    THREAD_LOCK(state);

    // not prepared to handle setting a new address space or one on a running thread
    DEBUG_ASSERT(!t->aspace);
    DEBUG_ASSERT(t->state != THREAD_RUNNING);

    t->aspace = reinterpret_cast<vmm_aspace_t*>(this);
    THREAD_UNLOCK(state);
}

status_t VmAspace::PageFault(vaddr_t va, uint flags) {
    DEBUG_ASSERT(magic_ == MAGIC);
    LTRACEF("va 0x%lx, flags 0x%x\n", va, flags);

    // for now, hold the aspace lock across the page fault operation,
    // which stops any other operations on the address space from moving
    // the region out from underneath it
    AutoLock a(lock_);

    auto r = FindRegionLocked(va);
    if (unlikely(!r))
        return ERR_NOT_FOUND;

    return r->PageFault(va, flags);
}

void VmAspace::Dump() {
    DEBUG_ASSERT(magic_ == MAGIC);
    printf("aspace %p: ref %u name '%s' range 0x%lx - 0x%lx size 0x%zx flags 0x%x\n", this,
           ref_count_debug(), name_, base_, base_ + size_ - 1, size_, flags_);

    printf("regions:\n");
    AutoLock a(lock_);
    for (auto r = regions_.first(); r; r = regions_.next(r)) {
        r->Dump();
    }
}

void DumpAllAspaces() {
    AutoLock a(aspace_list_lock);

    sbs::for_each(&aspaces, [](VmAspace* a) { a->Dump(); });
}
