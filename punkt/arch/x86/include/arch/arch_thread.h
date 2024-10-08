/*
 * Copyright (c) 2009 Corey Tabaka
 * Copyright (c) 2015 Intel Corporation
 *
 * Use of this source code is governed by a MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT
 */
#pragma once

#include <sys/types.h>

struct arch_thread {
    vaddr_t sp;
    vaddr_t fs_base;
    vaddr_t gs_base;

    vaddr_t *fpu_states;
    uint8_t fpu_buffer[512 + 16];

    /* if non-NULL, address to return to on page fault */
    void *page_fault_resume;
};
