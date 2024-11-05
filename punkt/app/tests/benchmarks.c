/*
 * Copyright (c) 2008-2012 Travis Geiselbrecht
 *
 * Use of this source code is governed by a MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT
 */
#include <sys/types.h>
#include <stdio.h>
#include <rand.h>
#include <lk/err.h>
#include <stdlib.h>
#include <string.h>
#include <app/tests.h>
#include <kernel/thread.h>
#include <kernel/mutex.h>
#include <kernel/semaphore.h>
#include <kernel/event.h>
#include <platform.h>

const size_t BUFSIZE = (1024*1024);
const uint ITER = 1024;

__NO_INLINE static void bench_set_overhead(void) {
    uint32_t *buf = malloc(BUFSIZE);
    if (!buf) {
        printf("failed to allocate buffer\n");
        return;
    }

    ulong count = arch_cycle_count();
    for (uint i = 0; i < ITER; i++) {
        __asm__ volatile("");
    }
    count = arch_cycle_count() - count;

    printf("took %lu cycles overhead to loop %u times\n", count, ITER);

    free(buf);
}

__NO_INLINE static void bench_memset(void) {
    void *buf = malloc(BUFSIZE);
    if (!buf) {
        printf("failed to allocate buffer\n");
        return;
    }

    ulong count = arch_cycle_count();
    for (uint i = 0; i < ITER; i++) {
        memset(buf, 0, BUFSIZE);
    }
    count = arch_cycle_count() - count;

    size_t total_bytes = BUFSIZE * ITER;
    double bytes_cycle = total_bytes / (double)count;
    printf("took %lu cycles to memset a buffer of size %zu %d times (%zu bytes), %f bytes/cycle\n",
           count, BUFSIZE, ITER, total_bytes, bytes_cycle);

    free(buf);
}

#define bench_cset(type) \
__NO_INLINE static void bench_cset_##type(void) \
{ \
    type *buf = malloc(BUFSIZE); \
    if (!buf) { \
        printf("failed to allocate buffer\n"); \
        return; \
    } \
 \
    ulong count = arch_cycle_count(); \
    for (uint i = 0; i < ITER; i++) { \
        for (uint j = 0; j < BUFSIZE / sizeof(*buf); j++) { \
            buf[j] = 0; \
        } \
    } \
    count = arch_cycle_count() - count; \
 \
    size_t total_bytes = BUFSIZE * ITER; \
    double bytes_cycle = total_bytes / (double)count; \
    printf("took %lu cycles to manually clear a buffer using wordsize %zu of size %zu %u times (%zu bytes), %f bytes/cycle\n", \
           count, sizeof(*buf), BUFSIZE, ITER, total_bytes, bytes_cycle); \
 \
    free(buf); \
}

bench_cset(uint8_t)
bench_cset(uint16_t)
bench_cset(uint32_t)
bench_cset(uint64_t)

__NO_INLINE static void bench_cset_wide(void) {
    uint32_t *buf = malloc(BUFSIZE);
    if (!buf) {
        printf("failed to allocate buffer\n");
        return;
    }

    ulong count = arch_cycle_count();
    for (uint i = 0; i < ITER; i++) {
        for (uint j = 0; j < BUFSIZE / sizeof(*buf) / 8; j++) {
            buf[j*8] = 0;
            buf[j*8+1] = 0;
            buf[j*8+2] = 0;
            buf[j*8+3] = 0;
            buf[j*8+4] = 0;
            buf[j*8+5] = 0;
            buf[j*8+6] = 0;
            buf[j*8+7] = 0;
        }
    }
    count = arch_cycle_count() - count;

    size_t total_bytes = BUFSIZE * ITER;
    double bytes_cycle = total_bytes / (double)count;
    printf("took %lu cycles to manually clear a buffer of size %zu %d times 8 words at a time (%zu bytes), %f bytes/cycle\n",
           count, BUFSIZE, ITER, total_bytes, bytes_cycle);

    free(buf);
}

__NO_INLINE static void bench_memcpy(void) {
    uint8_t *buf = malloc(BUFSIZE);
    if (!buf) {
        printf("failed to allocate buffer\n");
        return;
    }

    ulong count = arch_cycle_count();
    for (uint i = 0; i < ITER; i++) {
        memcpy(buf, buf + BUFSIZE / 2, BUFSIZE / 2);
    }
    count = arch_cycle_count() - count;

    size_t total_bytes = (BUFSIZE / 2) * ITER;
    double bytes_cycle = total_bytes / (double)count;
    printf("took %lu cycles to memcpy a buffer of size %zu %d times (%zu source bytes), %f source bytes/cycle\n",
           count, BUFSIZE / 2, ITER, total_bytes, bytes_cycle);

    free(buf);
}

#if ARCH_ARM
__NO_INLINE static void arm_bench_cset_stm(void) {
    uint32_t *buf = malloc(BUFSIZE);
    if (!buf) {
        printf("failed to allocate buffer\n");
        return;
    }

    ulong count = arch_cycle_count();
    for (uint i = 0; i < ITER; i++) {
        for (uint j = 0; j < BUFSIZE / sizeof(*buf) / 8; j++) {
            __asm__ volatile(
                "stm    %0, {r0-r7};"
                :: "r" (&buf[j*8])
            );
        }
    }
    count = arch_cycle_count() - count;

    size_t total_bytes = BUFSIZE * ITER;
    double bytes_cycle = total_bytes / (float)count;
    printf("took %lu cycles to manually clear a buffer of size %zu %d times 8 words at a time using stm (%zu bytes), %f bytes/cycle\n",
           count, BUFSIZE, ITER, total_bytes, bytes_cycle);

    free(buf);
}
#endif // ARCH_ARM

int benchmarks(int argc, const console_cmd_args *argv) {
    bench_set_overhead();
    bench_memset();
    bench_memcpy();

    bench_cset_uint8_t();
    bench_cset_uint16_t();
    bench_cset_uint32_t();
    bench_cset_uint64_t();
    bench_cset_wide();

#if ARCH_ARM
    arm_bench_cset_stm();
#endif

    return NO_ERROR;
}

