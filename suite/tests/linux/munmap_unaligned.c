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

/* Tests munmap with unaligned arguments.
 * 1) A length that is not a multiple of the page size: the kernel unmaps the whole
 *    last page, and DR must forget all of it.  Code runs in a writable mapping,
 *    which is then unmapped that way; a new writable mapping right after it gets
 *    code that is run, modified in place and run again.  If DR kept the rest of the
 *    last page, it would take it and the new mapping for one region, and fail to
 *    see the modification.
 * 2) An address that is not page-aligned: the munmap fails and unmaps nothing, also
 *    where the range reaches into the next mapping, which has code that must keep
 *    running, also after it is modified.
 */

#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>

#include "tools.h"

#define ROUNDS 100

typedef int (*func_t)(void);

/* mov eax, imm32; ret */
static const unsigned char code_template[] = { 0xb8, 0, 0, 0, 0, 0xc3 };

static void
set_code(unsigned char *pc, int32_t value)
{
    memcpy(pc, code_template, sizeof(code_template));
    memcpy(pc + 1, &value, sizeof(value));
}

int
main(int argc, char **argv)
{
    int wrong = 0;
    for (int i = 0; i < ROUNDS; i++) {
        /* Three pages: two for the first mapping, one for the second. */
        unsigned char *base =
            mmap(NULL, 3 * PAGE_SIZE, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (base == MAP_FAILED) {
            print("mmap failed\n");
            return 1;
        }
        unsigned char *first =
            mmap(base, 2 * PAGE_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC,
                 MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
        if (first != base) {
            print("mmap failed\n");
            return 1;
        }
        set_code(first, i);
        if (((func_t)first)() != i)
            wrong++;
        /* Unmaps both pages. */
        if (munmap(first, PAGE_SIZE + 100) != 0) {
            print("munmap failed\n");
            return 1;
        }
        unsigned char *second =
            mmap(base + 2 * PAGE_SIZE, PAGE_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC,
                 MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
        if (second != base + 2 * PAGE_SIZE) {
            print("mmap failed\n");
            return 1;
        }
        set_code(second, 1000 + i);
        if (((func_t)second)() != 1000 + i)
            wrong++;
        /* In place. */
        set_code(second, 2000 + i);
        if (((func_t)second)() != 2000 + i)
            wrong++;
        munmap(second, PAGE_SIZE);
    }
    print("unaligned length: %d wrong results\n", wrong);

    wrong = 0;
    for (int i = 0; i < ROUNDS; i++) {
        /* An inaccessible page, then a page with code. */
        unsigned char *base =
            mmap(NULL, 2 * PAGE_SIZE, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (base == MAP_FAILED) {
            print("mmap failed\n");
            return 1;
        }
        unsigned char *code = base + PAGE_SIZE;
        if (mprotect(code, PAGE_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
            print("mprotect failed\n");
            return 1;
        }
        set_code(code, i);
        if (((func_t)code)() != i)
            wrong++;
        /* Fails: the address is not page-aligned. */
        if (munmap(base + PAGE_SIZE / 2, PAGE_SIZE) == 0 || errno != EINVAL)
            print("munmap did not fail\n");
        if (((func_t)code)() != i)
            wrong++;
        set_code(code, 1000 + i);
        if (((func_t)code)() != 1000 + i)
            wrong++;
        munmap(base, 2 * PAGE_SIZE);
    }
    print("unaligned address: %d wrong results\n", wrong);
    print("all done\n");
    return 0;
}
