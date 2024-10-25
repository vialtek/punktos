/*
 * Copyright (c) 2015 Google, Inc. All rights reserved
 *
 * Use of this source code is governed by a MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT
 */

#include <lib/page_alloc.h>

#include <lk/debug.h>
#include <assert.h>
#include <string.h>
#include <lk/trace.h>
#include <lk/console_cmd.h>
#include <vm/vm.h>

/* A simple page-aligned wrapper around the pmm implementation of
 * the underlying physical page allocator. Used by system heaps or any
 * other user that wants pages of memory but doesn't want to use LK
 * specific apis.
 */
#define LOCAL_TRACE 0

void *page_alloc(size_t pages, int arena) {
    void *result = pmm_alloc_kpages(pages, NULL, NULL);
    return result;
}

void page_free(void *ptr, size_t pages) {
    DEBUG_ASSERT(IS_PAGE_ALIGNED((uintptr_t)ptr));

    pmm_free_kpages(ptr, pages);
}

void *page_first_alloc(size_t *size_return) {
    *size_return = PAGE_SIZE;
    return page_alloc(1, PAGE_ALLOC_ANY_ARENA);
}

