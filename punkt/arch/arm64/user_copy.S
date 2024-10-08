// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lk/asm.h>
#include <lk/err.h>

# We do not need to check whether the addresses are in the
# kernel or virtual address spaces, sine we only access them
# using EL0 privileges.

# status_t _arm64_copy_from_user(void *dst, const void *src, size_t len, void **fault_return)
FUNCTION(_arm64_copy_from_user)
    # Setup data fault return
    adr x4, .Lfault_from_user
    str x4, [x3]

    # Perform the memcpy
    cbz x2, 0f
.Lcopy_byte_from_user:
    ldtrb w4, [x1]
    strb w4, [x0]
    add x0, x0, #1
    add x1, x1, #1
    sub x2, x2, #1
    cbnz x2, .Lcopy_byte_from_user
0:

    mov x0, #NO_ERROR
    b .Lcleanup_from_user
.Lfault_from_user:
    mov x0, #ERR_INVALID_ARGS
.Lcleanup_from_user:
    # Reset data fault return
    str xzr, [x3]
    ret

# status_t _arm64_copy_to_user(void *dst, const void *src, size_t len, void **fault_return)
FUNCTION(_arm64_copy_to_user)
    # Setup data fault return
    adr x4, .Lfault_to_user
    str x4, [x3]

    # Perform the memcpy
    cbz x2, 0f
.Lcopy_byte_to_user:
    ldrb w4, [x1]
    sttrb w4, [x0]
    add x0, x0, #1
    add x1, x1, #1
    sub x2, x2, #1
    cbnz x2, .Lcopy_byte_to_user
0:

    mov x0, #NO_ERROR
    b .Lcleanup_to_user
.Lfault_to_user:
    mov x0, #ERR_INVALID_ARGS
.Lcleanup_to_user:
    # Reset data fault return
    str xzr, [x3]
    ret
