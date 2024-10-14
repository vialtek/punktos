/*
 * Copyright (c) 2015 Google, Inc. All rights reserved
 *
 * Use of this source code is governed by a MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT
 */
#pragma once

#include <stddef.h>
#include <sys/types.h>
#include <lk/compiler.h>
#include <vm/vm.h>

/* A simple page-aligned wrapper around the pmm implementation of
 * the underlying physical page allocator. Used by system heaps or any
 * other user that wants pages of memory but doesn't want to use LK
 * specific apis.
 */

__BEGIN_CDECLS

#define PAGE_ALLOC_ANY_ARENA (-1)

/* Pass PAGE_ALLOC_ANY_ARENA as the arena mask if you don't care which arena
 * the allocation comes from. The arena mask is only used on non-virtual memory
 * platforms.
 */
void *page_alloc(size_t pages, int arena_mask);
void page_free(void *ptr, size_t pages);

struct page_range {
    void *address;
    size_t size;
};

// You can call this once at the start, and it will either return a page or it
// will return some non-page-aligned memory that would otherwise go to waste.
void *page_first_alloc(size_t *size_return);

__END_CDECLS
