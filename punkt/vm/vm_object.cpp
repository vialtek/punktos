// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "kernel/vm/vm_object.h"

#include "vm_priv.h"
#include <assert.h>
#include <lk/err.h>
#include <kernel/auto_lock.h>
#include <vm/vm.h>
#include <lib/user_copy.h>
#include <stdlib.h>
#include <string.h>
#include <lk/trace.h>

#define LOCAL_TRACE MAX(VM_GLOBAL_TRACE, 0)

static void ZeroPage(paddr_t pa) {
    void* ptr = paddr_to_kvaddr(pa);
    DEBUG_ASSERT(ptr);

    memset(ptr, 0, PAGE_SIZE);
}

static void ZeroPage(vm_page_t* p) {
    paddr_t pa = vm_page_to_paddr(p);
    ZeroPage(pa);
}

static size_t OffsetToIndex(uint64_t offset) {
    uint64_t index64 = offset / PAGE_SIZE;

    DEBUG_ASSERT(index64 <= SIZE_MAX);

    return static_cast<size_t>(index64);
}

VmObject::VmObject(uint32_t pmm_alloc_flags)
    : pmm_alloc_flags_(pmm_alloc_flags) {
    LTRACEF("%p\n", this);
}

VmObject::~VmObject() {
    DEBUG_ASSERT(magic_ == MAGIC);
    LTRACEF("%p\n", this);

    list_node list;
    list_initialize(&list);

    // free all of the pages attached to us
    size_t count = 0;
    for (size_t i = 0; i < page_array_.size(); i++) {
        auto p = page_array_[i];
        if (p) {
            LTRACEF("freeing page %p (0x%lx)\n", p, vm_page_to_paddr(p));

            // remove it from the object list of pages
            DEBUG_ASSERT(list_in_list(&p->node));
            list_delete(&p->node);

            // add to the temporary free list
            list_add_tail(&list, &p->node);
            count++;
        }
    }

    DEBUG_ASSERT(list_length(&page_list_) == 0);

    __UNUSED auto freed = pmm_free(&list);
    DEBUG_ASSERT(freed == count);

    // clear our magic value
    magic_ = 0;
}

utils::RefPtr<VmObject> VmObject::Create(uint32_t pmm_alloc_flags, uint64_t size) {
    // there's a max size to keep indexes within range
    if (size >= MAX_SIZE)
        return nullptr;

    auto vmo = utils::AdoptRef(new VmObject(pmm_alloc_flags));
    if (!vmo)
        return nullptr;

    auto err = vmo->Resize(size);
    DEBUG_ASSERT(err == NO_ERROR);
    if (err != NO_ERROR)
        return nullptr;

    return vmo;
}

void VmObject::Dump() {
    DEBUG_ASSERT(magic_ == MAGIC);

    size_t count = 0;
    {
        AutoLock a(lock_);
        for (size_t i = 0; i < page_array_.size(); i++) {
            if (page_array_[i])
                count++;
        }
    }
    printf("\t\tobject %p: ref %u size 0x%llx, %zu allocated pages\n", this, ref_count_debug(),
           size_, count);
}

status_t VmObject::Resize(uint64_t s) {
    DEBUG_ASSERT(magic_ == MAGIC);
    LTRACEF("vmo %p, size %llu\n", this, s);

    // there's a max size to keep indexes within range
    if (s >= MAX_SIZE)
        return ERR_TOO_BIG;

    AutoLock a(lock_);

    if (size_ != 0) {
        return ERR_NOT_IMPLEMENTED; // TODO: support resizing an existing object
    }

    // compute the number of pages we cover
    size_t page_count = OffsetToIndex(ROUNDUP_PAGE_SIZE(s));

    // save bytewise size
    size_ = s;

    // allocate a new array
    DEBUG_ASSERT(!page_array_); // no resizing
    vm_page_t** pa = new vm_page_t* [page_count] {};
    if (!pa)
        return ERR_NO_MEMORY;

    page_array_.reset(pa, page_count);

    return NO_ERROR;
}

void VmObject::AddPageToArray(size_t index, vm_page_t* p) {
    DEBUG_ASSERT(magic_ == MAGIC);
    DEBUG_ASSERT(is_mutex_held(&lock_));

    DEBUG_ASSERT(page_array_);
    DEBUG_ASSERT(!page_array_[index]);
    DEBUG_ASSERT(index < page_array_.size());
    page_array_[index] = p;

    DEBUG_ASSERT(!list_in_list(&p->node));
    list_add_tail(&page_list_, &p->node);
}

status_t VmObject::AddPage(vm_page_t* p, uint64_t offset) {
    DEBUG_ASSERT(magic_ == MAGIC);
    LTRACEF("vmo %p, offset 0x%llx, page %p (0x%lx)\n", this, offset, p, vm_page_to_paddr(p));

    DEBUG_ASSERT(p);

    AutoLock a(lock_);

    if (offset >= size_)
        return ERR_OUT_OF_RANGE;

    size_t index = OffsetToIndex(offset);

    AddPageToArray(index, p);

    return NO_ERROR;
}

vm_page_t* VmObject::GetPage(uint64_t offset) {
    DEBUG_ASSERT(magic_ == MAGIC);
    AutoLock a(lock_);

    if (offset >= size_)
        return nullptr;

    size_t index = OffsetToIndex(offset);

    return page_array_[index];
}

vm_page_t* VmObject::FaultPageLocked(uint64_t offset, uint pf_flags) {
    DEBUG_ASSERT(magic_ == MAGIC);
    DEBUG_ASSERT(is_mutex_held(&lock_));

    LTRACEF("vmo %p, offset 0x%llx, pf_flags 0x%x\n", this, offset, pf_flags);

    if (offset >= size_)
        return nullptr;

    size_t index = OffsetToIndex(offset);

    vm_page_t* p = page_array_[index];
    if (p)
        return p;

    // allocate a page
    paddr_t pa;
    p = pmm_alloc_page(pmm_alloc_flags_, &pa);
    if (!p)
        return nullptr;

    // TODO: remove once pmm returns zeroed pages
    ZeroPage(pa);

    AddPageToArray(index, p);

    LTRACEF("faulted in page %p, pa 0x%lx\n", p, pa);

    return p;
}

vm_page_t* VmObject::FaultPage(uint64_t offset, uint pf_flags) {
    DEBUG_ASSERT(magic_ == MAGIC);
    AutoLock a(lock_);

    return FaultPageLocked(offset, pf_flags);
}

int64_t VmObject::CommitRange(uint64_t offset, uint64_t len) {
    DEBUG_ASSERT(magic_ == MAGIC);
    LTRACEF("offset 0x%llx, len 0x%llx\n", offset, len);

    AutoLock a(lock_);

    // trim the size
    if (!TrimRange(offset, len, size_))
        return ERR_OUT_OF_RANGE;

    // was in range, just zero length
    if (len == 0)
        return 0;

    // compute a page aligned end to do our searches in to make sure we cover all the pages
    uint64_t end = ROUNDUP_PAGE_SIZE(offset + len);
    DEBUG_ASSERT(end > offset);

    // make a pass through the list, counting the number of pages we need to allocate
    size_t count = 0;
    for (uint64_t o = offset; o < end; o += PAGE_SIZE) {
        size_t index = OffsetToIndex(o);

        if (!page_array_[index])
            count++;
    }
    if (count == 0)
        return 0;

    // allocate count number of pages
    list_node page_list;
    list_initialize(&page_list);

    size_t allocated = pmm_alloc_pages(count, pmm_alloc_flags_, &page_list);
    if (allocated < count) {
        LTRACEF("failed to allocate enough pages (asked for %zu, got %zu)\n", count, allocated);
        pmm_free(&page_list);
        return ERR_NO_MEMORY;
    }

    // add them to the appropriate range of the object
    for (uint64_t o = offset; o < end; o += PAGE_SIZE) {
        size_t index = OffsetToIndex(o);

        vm_page_t* p = list_remove_head_type(&page_list, vm_page_t, node);
        DEBUG_ASSERT(p);

        // TODO: remove once pmm returns zeroed pages
        ZeroPage(p);

        AddPageToArray(index, p);
    }

    DEBUG_ASSERT(list_is_empty(&page_list));

    return len;
}

int64_t VmObject::CommitRangeContiguous(uint64_t offset, uint64_t len, uint8_t alignment_log2) {
    DEBUG_ASSERT(magic_ == MAGIC);
    LTRACEF("offset 0x%llx, len 0x%llx, alignment %hhu\n", offset, len, alignment_log2);

    AutoLock a(lock_);

    // trim the size
    if (!TrimRange(offset, len, size_))
        return ERR_OUT_OF_RANGE;

    // was in range, just zero length
    if (len == 0)
        return 0;

    // compute a page aligned end to do our searches in to make sure we cover all the pages
    uint64_t end = ROUNDUP_PAGE_SIZE(offset + len);
    DEBUG_ASSERT(end > offset);

    // make a pass through the list, making sure we have an empty run on the object
    size_t count = 0;
    for (uint64_t o = offset; o < end; o += PAGE_SIZE) {
        size_t index = OffsetToIndex(o);

        if (page_array_[index])
            return ERR_NO_MEMORY;

        count++;
    }

    DEBUG_ASSERT(count == len / PAGE_SIZE);

    // allocate count number of pages
    list_node page_list;
    list_initialize(&page_list);

    size_t allocated =
        pmm_alloc_contiguous(count, pmm_alloc_flags_, alignment_log2, nullptr, &page_list);
    if (allocated < count) {
        LTRACEF("failed to allocate enough pages (asked for %zu, got %zu)\n", count, allocated);
        pmm_free(&page_list);
        return ERR_NO_MEMORY;
    }

    DEBUG_ASSERT(list_length(&page_list) == allocated);

    // add them to the appropriate range of the object
    for (uint64_t o = offset; o < end; o += PAGE_SIZE) {
        size_t index = OffsetToIndex(o);

        vm_page_t* p = list_remove_head_type(&page_list, vm_page_t, node);
        DEBUG_ASSERT(p);

        // TODO: remove once pmm returns zeroed pages
        ZeroPage(p);

        AddPageToArray(index, p);
    }

    return count * PAGE_SIZE;
}

// perform some sort of copy in/out on a range of the object using a passed in lambda
// for the copy routine
template <typename T>
status_t VmObject::ReadWriteInternal(uint64_t offset, size_t len, size_t* bytes_copied,
                                     bool write, T copyfunc) {
    DEBUG_ASSERT(magic_ == MAGIC);
    if (bytes_copied)
        *bytes_copied = 0;

    AutoLock a(lock_);

    // trim the size
    if (!TrimRange(offset, len, size_))
        return ERR_OUT_OF_RANGE;

    // was in range, just zero length
    if (len == 0)
        return 0;

    // walk the list of pages and do the write
    size_t dest_offset = 0;
    while (len > 0) {
        size_t page_offset = offset % PAGE_SIZE;
        size_t tocopy = MIN(PAGE_SIZE - page_offset, len);

        // fault in the page
        vm_page_t* p = FaultPageLocked(offset, write ? VMM_PF_FLAG_WRITE : 0);
        if (!p)
            return ERR_NO_MEMORY;

        // compute the kernel mapping of this page
        paddr_t pa = vm_page_to_paddr(p);
        uint8_t* page_ptr = reinterpret_cast<uint8_t*>(paddr_to_kvaddr(pa));

        // call the copy routine
        auto err = copyfunc(page_ptr + page_offset, dest_offset, tocopy);
        if (err < 0)
            return err;

        offset += tocopy;
        if (bytes_copied)
            *bytes_copied += tocopy;
        dest_offset += tocopy;
        len -= tocopy;
    }

    return NO_ERROR;
}

status_t VmObject::Read(void* _ptr, uint64_t offset, size_t len, size_t* bytes_read) {
    DEBUG_ASSERT(magic_ == MAGIC);
    // test to make sure this is a kernel pointer
    if (!is_kernel_address(reinterpret_cast<vaddr_t>(_ptr))) {
        DEBUG_ASSERT_MSG(0, "non kernel pointer passed\n");
        return ERR_INVALID_ARGS;
    }

    // read routine that just uses a memcpy
    uint8_t* ptr = reinterpret_cast<uint8_t*>(_ptr);
    auto read_routine = [ptr](const void* src, size_t offset, size_t len) -> status_t {
        memcpy(ptr + offset, src, len);
        return NO_ERROR;
    };

    return ReadWriteInternal(offset, len, bytes_read, false, read_routine);
}

status_t VmObject::Write(const void* _ptr, uint64_t offset, size_t len, size_t* bytes_written) {
    DEBUG_ASSERT(magic_ == MAGIC);
    // test to make sure this is a kernel pointer
    if (!is_kernel_address(reinterpret_cast<vaddr_t>(_ptr))) {
        DEBUG_ASSERT_MSG(0, "non kernel pointer passed\n");
        return ERR_INVALID_ARGS;
    }

    // write routine that just uses a memcpy
    const uint8_t* ptr = reinterpret_cast<const uint8_t*>(_ptr);
    auto write_routine = [ptr](void* dst, size_t offset, size_t len) -> status_t {
        memcpy(dst, ptr + offset, len);
        return NO_ERROR;
    };

    return ReadWriteInternal(offset, len, bytes_written, true, write_routine);
}

status_t VmObject::ReadUser(void* _ptr, uint64_t offset, size_t len, size_t* bytes_read) {
    DEBUG_ASSERT(magic_ == MAGIC);
    // test to make sure this is a iuser pointer
    if (!is_user_address(reinterpret_cast<vaddr_t>(_ptr))) {
        DEBUG_ASSERT_MSG(0, "non user pointer passed\n");
        return ERR_INVALID_ARGS;
    }

    // read routine that uses copy_to_user
    uint8_t* ptr = reinterpret_cast<uint8_t*>(_ptr);
    auto read_routine = [ptr](const void* src, size_t offset, size_t len) -> status_t {
        return copy_to_user(ptr + offset, src, len);
    };

    return ReadWriteInternal(offset, len, bytes_read, false, read_routine);
}

status_t VmObject::WriteUser(const void* _ptr, uint64_t offset, size_t len, size_t* bytes_written) {
    DEBUG_ASSERT(magic_ == MAGIC);
    // test to make sure this is a iuser pointer
    if (!is_user_address(reinterpret_cast<vaddr_t>(_ptr))) {
        DEBUG_ASSERT_MSG(0, "non user pointer passed\n");
        return ERR_INVALID_ARGS;
    }

    // write routine that uses copy_from_user
    const uint8_t* ptr = reinterpret_cast<const uint8_t*>(_ptr);
    auto write_routine = [ptr](void* dst, size_t offset, size_t len) -> status_t {
        return copy_from_user(dst, ptr + offset, len);
    };

    return ReadWriteInternal(offset, len, bytes_written, true, write_routine);
}
