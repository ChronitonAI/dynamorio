/* **********************************************************
 * Copyright (c) 2026 Chroniton PBC.  All rights reserved.
 * **********************************************************/

/*
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * * Neither the name of the copyright holder nor the names of its contributors
 *   may be used to endorse or promote products derived from this software
 *   without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/* Tests that the flushes of exact fragments and regions, and dr_app_memory_changed(),
 * remove the fragments of every fragment variant (-num_fragment_variants): the app runs
 * two neighboring blocks, A and B, in both variants, has the client flush A in one of
 * several ways from variant 0, and then checks in each variant which of the blocks the
 * flush removed.  See fragment_variants_flush.dll.c.
 */

#include "tools.h"
#include "fragment_variants_flush.h"

#include <stdint.h>

/* The app calls client_request(op, arg1, arg2), which the client intercepts. */
long
client_request(long op, long arg1, long arg2);
long
block_a(void);
long
block_b(void);

/* clang-format off */
__asm__(".text\n"
        ".globl client_request\n"
        ".type client_request, @function\n"
        "client_request:\n"
        "  mov $-1, %rax\n"
        "  ret\n"
        /* Blocks A and B are in the same area of code, 64 bytes apart. */
        ".p2align 6\n"
        ".globl block_a\n"
        ".type block_a, @function\n"
        "block_a:\n"
        "  mov $1, %eax\n"
        "  ret\n"
        ".p2align 6\n"
        ".globl block_b\n"
        ".type block_b, @function\n"
        "block_b:\n"
        "  mov $2, %eax\n"
        "  ret\n");
/* clang-format on */

#define NUM_VARIANTS 2

static int errors;

static void
check(long value, long expected, const char *what)
{
    if (value != expected) {
        errors++;
        print("ERROR: %s: %ld instead of %ld\n", what, value, expected);
    }
}

static void
select_variant(int v)
{
    check(client_request(REQ_SELECT, v, 0), 1, "select");
    check(client_request(REQ_CURRENT_VARIANT, 0, 0), v, "current variant");
}

/* Builds A and B in every variant, has the client flush A with op from variant 0, and
 * prints in which variants A and B are gone.
 */
static void
test_flush(const char *name, long op, long arg)
{
    int v;
    long a_gone = 0, b_gone = 0, builds[NUM_VARIANTS];
    for (v = 0; v < NUM_VARIANTS; v++) {
        select_variant(v);
        check(block_a(), 1, "block_a");
        check(block_b(), 2, "block_b");
        check(client_request(REQ_EXISTS, (long)block_a, 0), 1, "A before the flush");
        check(client_request(REQ_EXISTS, (long)block_b, 0), 1, "B before the flush");
        builds[v] = client_request(REQ_BUILDS, (long)block_a, v);
    }
    select_variant(0);
    check(client_request(op, (long)block_a, arg), 1, name);
    for (v = 0; v < NUM_VARIANTS; v++) {
        select_variant(v);
        if (!client_request(REQ_EXISTS, (long)block_a, 0))
            a_gone |= 1 << v;
        if (!client_request(REQ_EXISTS, (long)block_b, 0))
            b_gone |= 1 << v;
        /* A is built again where it is gone. */
        check(block_a(), 1, "block_a");
        check(client_request(REQ_BUILDS, (long)block_a, v),
              builds[v] + ((a_gone & (1 << v)) != 0 ? 1 : 0), "builds of A");
    }
    print("%s: A gone in variants %s%s, B gone in variants %s%s\n", name,
          (a_gone & 1) != 0 ? "0" : "-", (a_gone & 2) != 0 ? "1" : "-",
          (b_gone & 1) != 0 ? "0" : "-", (b_gone & 2) != 0 ? "1" : "-");
}

int
main(int argc, char **argv)
{
    check(client_request(REQ_NUM_VARIANTS, 0, 0), NUM_VARIANTS, "number of variants");
    test_flush("dr_unlink_flush_fragment", REQ_FLUSH_TAG, 0);
    test_flush("dr_unlink_flush_region_ex exact", REQ_FLUSH_EXACT, 0);
    test_flush("dr_delay_flush_region_ex exact", REQ_DELAY_EXACT, 0);
    test_flush("dr_app_memory_changed contents", REQ_MEMORY_CHANGED, 0);
    test_flush("dr_app_memory_changed protection", REQ_MEMORY_CHANGED, 1);
    test_flush("dr_unlink_flush_region", REQ_FLUSH_REGION, 0);
    test_flush("dr_flush_region", REQ_FLUSH_SYNCHALL, 0);
    print("%d errors\n", errors);
    return 0;
}
