/*
 * Copyright (c) 2008-2015 Travis Geiselbrecht
 *
 * Use of this source code is governed by a MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT
 */
#include <lib/heap.h>

#include <lk/trace.h>
#include <lk/debug.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <lk/err.h>
#include <lk/list.h>
#include <kernel/spinlock.h>
#include <lk/console_cmd.h>
#include <lib/page_alloc.h>
#include <lib/cmpctmalloc.h>

#define LOCAL_TRACE 0

/* heap tracing */
#if LK_DEBUGLEVEL > 0
static bool heap_trace = false;
#else
#define heap_trace (false)
#endif

/* delayed free list */
struct list_node delayed_free_list = LIST_INITIAL_VALUE(delayed_free_list);
spin_lock_t delayed_free_lock = SPIN_LOCK_INITIAL_VALUE;

#define HEAP_MEMALIGN(boundary, s) cmpct_memalign(s, boundary)
#define HEAP_MALLOC cmpct_alloc
#define HEAP_REALLOC cmpct_realloc
#define HEAP_FREE cmpct_free
#define HEAP_INIT cmpct_init
#define HEAP_DUMP cmpct_dump
#define HEAP_TRIM cmpct_trim
static inline void *HEAP_CALLOC(size_t n, size_t s) {
    size_t realsize = n * s;

    void *ptr = cmpct_alloc(realsize);
    if (likely(ptr))
        memset(ptr, 0, realsize);
    return ptr;
}

static void heap_free_delayed_list(void) {
    struct list_node list;

    list_initialize(&list);

    spin_lock_saved_state_t state;
    spin_lock_irqsave(&delayed_free_lock, state);

    struct list_node *node;
    while ((node = list_remove_head(&delayed_free_list))) {
        list_add_head(&list, node);
    }
    spin_unlock_irqrestore(&delayed_free_lock, state);

    while ((node = list_remove_head(&list))) {
        LTRACEF("freeing node %p\n", node);
        HEAP_FREE(node);
    }
}

void heap_init(void) {
    HEAP_INIT();
}

void heap_trim(void) {
    // deal with the pending free list
    if (unlikely(!list_is_empty(&delayed_free_list))) {
        heap_free_delayed_list();
    }

    HEAP_TRIM();
}

void *malloc(size_t size) {
    LTRACEF("size %zd\n", size);

    // deal with the pending free list
    if (unlikely(!list_is_empty(&delayed_free_list))) {
        heap_free_delayed_list();
    }

    void *ptr = HEAP_MALLOC(size);
    if (heap_trace)
        printf("caller %p malloc %zu -> %p\n", __GET_CALLER(), size, ptr);
    return ptr;
}

void *memalign(size_t boundary, size_t size) {
    LTRACEF("boundary %zu, size %zd\n", boundary, size);

    // deal with the pending free list
    if (unlikely(!list_is_empty(&delayed_free_list))) {
        heap_free_delayed_list();
    }

    void *ptr = HEAP_MEMALIGN(boundary, size);
    if (heap_trace)
        printf("caller %p memalign %zu, %zu -> %p\n", __GET_CALLER(), boundary, size, ptr);
    return ptr;
}

void *calloc(size_t count, size_t size) {
    LTRACEF("count %zu, size %zd\n", count, size);

    // deal with the pending free list
    if (unlikely(!list_is_empty(&delayed_free_list))) {
        heap_free_delayed_list();
    }

    void *ptr = HEAP_CALLOC(count, size);
    if (heap_trace)
        printf("caller %p calloc %zu, %zu -> %p\n", __GET_CALLER(), count, size, ptr);
    return ptr;
}

void *realloc(void *ptr, size_t size) {
    LTRACEF("ptr %p, size %zd\n", ptr, size);

    // deal with the pending free list
    if (unlikely(!list_is_empty(&delayed_free_list))) {
        heap_free_delayed_list();
    }

    void *ptr2 = HEAP_REALLOC(ptr, size);
    if (heap_trace)
        printf("caller %p realloc %p, %zu -> %p\n", __GET_CALLER(), ptr, size, ptr2);
    return ptr2;
}

void free(void *ptr) {
    LTRACEF("ptr %p\n", ptr);
    if (heap_trace)
        printf("caller %p free %p\n", __GET_CALLER(), ptr);

    HEAP_FREE(ptr);
}

/* critical section time delayed free */
void heap_delayed_free(void *ptr) {
    LTRACEF("ptr %p\n", ptr);

    /* throw down a structure on the free block */
    /* XXX assumes the free block is large enough to hold a list node */
    struct list_node *node = (struct list_node *)ptr;

    spin_lock_saved_state_t state;
    spin_lock_irqsave(&delayed_free_lock, state);
    list_add_head(&delayed_free_list, node);
    spin_unlock_irqrestore(&delayed_free_lock, state);
}

static void heap_dump(void) {
    HEAP_DUMP();

    printf("\tdelayed free list:\n");
    spin_lock_saved_state_t state;
    spin_lock_irqsave(&delayed_free_lock, state);
    struct list_node *node;
    list_for_every(&delayed_free_list, node) {
        printf("\t\tnode %p\n", node);
    }
    spin_unlock_irqrestore(&delayed_free_lock, state);
}

static void heap_test(void) { cmpct_test(); }

#if LK_DEBUGLEVEL > 1

static int cmd_heap(int argc, const console_cmd_args *argv);

STATIC_COMMAND_START
STATIC_COMMAND("heap", "heap debug commands", &cmd_heap)
STATIC_COMMAND_END(heap);

static int cmd_heap(int argc, const console_cmd_args *argv) {
    if (argc < 2) {
notenoughargs:
        printf("not enough arguments\n");
usage:
        printf("usage:\n");
        printf("\t%s info\n", argv[0].str);
        printf("\t%s trace\n", argv[0].str);
        printf("\t%s trim\n", argv[0].str);
        printf("\t%s alloc <size> [alignment]\n", argv[0].str);
        printf("\t%s realloc <ptr> <size>\n", argv[0].str);
        printf("\t%s free <address>\n", argv[0].str);
        return -1;
    }

    if (strcmp(argv[1].str, "info") == 0) {
        heap_dump();
    } else if (strcmp(argv[1].str, "test") == 0) {
        heap_test();
    } else if (strcmp(argv[1].str, "trace") == 0) {
        heap_trace = !heap_trace;
        printf("heap trace is now %s\n", heap_trace ? "on" : "off");
    } else if (strcmp(argv[1].str, "trim") == 0) {
        heap_trim();
    } else if (strcmp(argv[1].str, "alloc") == 0) {
        if (argc < 3) goto notenoughargs;

        void *ptr = memalign((argc >= 4) ? argv[3].u : 0, argv[2].u);
        printf("memalign returns %p\n", ptr);
    } else if (strcmp(argv[1].str, "realloc") == 0) {
        if (argc < 4) goto notenoughargs;

        void *ptr = realloc(argv[2].p, argv[3].u);
        printf("realloc returns %p\n", ptr);
    } else if (strcmp(argv[1].str, "free") == 0) {
        if (argc < 3) goto notenoughargs;

        free(argv[2].p);
    } else {
        printf("unrecognized command\n");
        goto usage;
    }

    return 0;
}

#endif


