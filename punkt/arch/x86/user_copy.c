// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <string.h>
#include <lk/trace.h>

#include <arch/user_copy.h>
#include <arch/x86.h>
#include <arch/x86/user_copy.h>
#include <kernel/thread.h>
#include <vm/vm.h>

#define LOCAL_TRACE 0

static inline bool ac_flag(void)
{
    return x86_save_flags() & X86_FLAGS_AC;
}

status_t arch_copy_from_user(void *dst, const void *src, size_t len)
{
    DEBUG_ASSERT(!ac_flag());

    thread_t *thr = get_current_thread();
    status_t status =
            _x86_copy_from_user(dst, src, len, &thr->arch.page_fault_resume);

    DEBUG_ASSERT(!ac_flag());
    return status;
}

status_t arch_copy_to_user(void *dst, const void *src, size_t len)
{
    DEBUG_ASSERT(!ac_flag());

    thread_t *thr = get_current_thread();
    status_t status;
    status = _x86_copy_to_user(dst, src, len, &thr->arch.page_fault_resume);

    DEBUG_ASSERT(!ac_flag());
    return status;
}

static bool can_access(const void *base, size_t len, bool for_write)
{
    LTRACEF("can_access: base %p, len %lu\n", base, len);

    // If the target wraps around, it would be possible for the start
    // and end to be user addresses but intermediate address to not be.
    // Since the user address space is a contiguous range, we can
    // get by with checking the start and end if there was no wrap-around.
    vaddr_t base_vaddr = (vaddr_t)base;
    if (base_vaddr + len < base_vaddr) {
        return false;
    }
    bool in_range = is_user_address(base_vaddr) &&
            is_user_address(base_vaddr + len - 1);
    if (!in_range) {
        return false;
    }

    // We don't care about whether pages are actually mapped or what their
    // permissions are, as long as they are in the user address space.  We
    // rely on a page fault occurring if an actual permissions error occurs.
    DEBUG_ASSERT(x86_get_cr0() & X86_CR0_WP);
    return true;
}

bool _x86_usercopy_can_read(const void *base, size_t len)
{
    return can_access(base, len, false);
}

bool _x86_usercopy_can_write(const void *base, size_t len)
{
    return can_access(base, len, true);
}
