// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <assert.h>
#include <stdint.h>
#include <lib/sbl/ref_counted.h>
#include <lib/sbl/ref_ptr.h>

class VmAspace;
class VmObject;

class VmRegion : public sbl::RefCounted<VmRegion> {
public:
    static sbl::RefPtr<VmRegion> Create(VmAspace& aspace, vaddr_t base, size_t size,
                                          uint arch_mmu_flags, const char* name);
    ~VmRegion();

    // accessors
    vaddr_t base() const { return base_; }
    size_t size() const { return size_; }
    uint arch_mmu_flags() const { return arch_mmu_flags_; }

    // set base address
    void set_base(vaddr_t vaddr) { base_ = vaddr; }

    // list for aspace
    void list_set_prev(VmRegion* node) { prev_ = node; }
    void list_set_next(VmRegion* node) { next_ = node; }
    VmRegion* list_prev() { return prev_; }
    VmRegion* list_next() { return next_; }
    const VmRegion* list_prev() const { return prev_; }
    const VmRegion* list_next() const { return next_; }

    void Dump() const;

    // set the object that this region backs
    status_t SetObject(sbl::RefPtr<VmObject> o, uint64_t offset);

    // map in pages from the underlying vm object, optionally committing pages as it goes
    status_t MapRange(size_t offset, size_t len, bool commit);

    // map in a physical range of memory to this region
    status_t MapPhysicalRange(size_t offset, size_t len, paddr_t paddr, bool allow_remap);

    // unmap all pages and remove dependency on vm object it has a ref to
    status_t Destroy();

    // unmap the region of memory in the container address space
    int Unmap();

    // change mapping permissions
    status_t Protect(uint arch_mmu_flags);

    // page fault in an address into the region
    status_t PageFault(vaddr_t va, uint pf_flags);

private:
    // private constructor, use Create()
    VmRegion(VmAspace& aspace, vaddr_t base, size_t size, uint arch_mmu_flags, const char* name);

    // nocopy
    VmRegion(const VmRegion&) = delete;
    VmRegion& operator=(const VmRegion&) = delete;

    // magic value
    static const uint32_t MAGIC = 0x564d5247; // VMRG
    uint32_t magic_ = MAGIC;

    // address/size within the container address space
    vaddr_t base_;
    size_t size_;

    // cached mapping flags (read/write/user/etc)
    uint arch_mmu_flags_;

    // pointer back to our member address space
    sbl::RefPtr<VmAspace> aspace_;

    // pointer and region of the object we are mapping
    sbl::RefPtr<VmObject> object_;
    uint64_t object_offset_ = 0;

    char name_[32];

    // doubly link list stuff
    VmRegion* prev_ = nullptr;
    VmRegion* next_ = nullptr;
};
