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
#include <kernel/vm.h>

/* A simple page-aligned wrapper around the pmm or novm implementation of
 * the underlying physical page allocator. Used by system heaps or any
 * other user that wants pages of memory but doesn't want to use LK
 * specific apis.
 */
#define LOCAL_TRACE 0

#if WITH_STATIC_HEAP

#error "fix static heap post page allocator and novm stuff"

#if !defined(HEAP_START) || !defined(HEAP_LEN)
#error WITH_STATIC_HEAP set but no HEAP_START or HEAP_LEN defined
#endif

#endif

void *page_alloc(size_t pages, int arena) {
    void *result = pmm_alloc_kpages(pages, NULL);
    return result;
}

void page_free(void *ptr, size_t pages) {
    DEBUG_ASSERT(IS_PAGE_ALIGNED((uintptr_t)ptr));

    pmm_free_kpages(ptr, pages);
}

int page_get_arenas(struct page_range *ranges, int number_of_ranges) {
    ranges[0].address = kvaddr_get_range(&ranges[0].size);
    return 1;
}

void *page_first_alloc(size_t *size_return) {
    *size_return = PAGE_SIZE;
    return page_alloc(1, PAGE_ALLOC_ANY_ARENA);
}

#if LK_DEBUGLEVEL > 1

static int cmd_page_alloc(int argc, const console_cmd_args *argv);
static void page_alloc_dump(void);

STATIC_COMMAND_START
STATIC_COMMAND("page_alloc", "page allocator debug commands", &cmd_page_alloc)
STATIC_COMMAND_END(page_alloc);

static int cmd_page_alloc(int argc, const console_cmd_args *argv) {
    if (argc != 2) {
notenoughargs:
        printf("not enough arguments\n");
usage:
        printf("usage:\n");
        printf("\t%s info\n", argv[0].str);
        return -1;
    }

    if (strcmp(argv[1].str, "info") == 0) {
        page_alloc_dump();
    } else {
        printf("unrecognized command\n");
        goto usage;
    }

    return 0;
}

static void page_alloc_dump(void) {
    dprintf(INFO, "Page allocator is based on pmm\n");
}

#endif
