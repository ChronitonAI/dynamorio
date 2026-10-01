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

/* Tests mremap of writable memory with code that ran, which DR made read-only to
 * see changes to the code: the memory must stay writable for the application at
 * its new place, and changes to the code there must be seen.
 * 1) Moved (MREMAP_FIXED).
 * 2) Grown in place.
 * 3) Shrunk in place.
 * 4) A shared mapping duplicated with an old size of 0.
 * The code is run, the memory remapped, and the code at the new place is run,
 * modified in place and run again; in 2) also code in the new part.
 */

#define _GNU_SOURCE 1 /* for mremap */
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>

#include "tools.h"

#define RWX (PROT_READ | PROT_WRITE | PROT_EXEC)

typedef int (*func_t)(void);

/* mov eax, imm32; ret */
static const unsigned char code_template[] = { 0xb8, 0, 0, 0, 0, 0xc3 };

static int wrong;

static void
set_code(unsigned char *pc, int32_t value)
{
    memcpy(pc, code_template, sizeof(code_template));
    memcpy(pc + 1, &value, sizeof(value));
}

static void
check(unsigned char *pc, int32_t value)
{
    if (((func_t)pc)() != value)
        wrong++;
}

/* Runs the code at pc (value), then writes and runs new code there. */
static void
modify(unsigned char *pc, int32_t value)
{
    check(pc, value);
    set_code(pc, value + 1);
    check(pc, value + 1);
}

/* An inaccessible mapping of n pages, to place mappings in. */
static unsigned char *
reserve(int n)
{
    unsigned char *res =
        mmap(NULL, n * PAGE_SIZE, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (res == MAP_FAILED) {
        print("mmap failed\n");
        exit(1);
    }
    return res;
}

static unsigned char *
map_code(unsigned char *at, int n, int flags)
{
    unsigned char *res =
        mmap(at, n * PAGE_SIZE, RWX, flags | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (res != at) {
        print("mmap failed\n");
        exit(1);
    }
    return res;
}

static void
check_remap(unsigned char *res, unsigned char *expect)
{
    if (res != expect) {
        print("mremap failed\n");
        exit(1);
    }
}

int
main(int argc, char **argv)
{
    unsigned char *space, *code, *res;

    /* 1) Moved. */
    space = reserve(3);
    code = map_code(space, 1, MAP_PRIVATE);
    set_code(code, 10);
    check(code, 10);
    res = mremap(code, PAGE_SIZE, PAGE_SIZE, MREMAP_MAYMOVE | MREMAP_FIXED,
                 space + 2 * PAGE_SIZE);
    check_remap(res, space + 2 * PAGE_SIZE);
    modify(res, 10);
    print("moved: %d wrong results\n", wrong);

    /* 2) Grown in place. */
    wrong = 0;
    space = reserve(2);
    code = map_code(space, 1, MAP_PRIVATE);
    munmap(space + PAGE_SIZE, PAGE_SIZE);
    set_code(code, 20);
    check(code, 20);
    res = mremap(code, PAGE_SIZE, 2 * PAGE_SIZE, 0);
    check_remap(res, code);
    modify(res, 20);
    set_code(res + PAGE_SIZE, 30);
    modify(res + PAGE_SIZE, 30);
    print("grown: %d wrong results\n", wrong);

    /* 3) Shrunk in place. */
    wrong = 0;
    space = reserve(2);
    code = map_code(space, 2, MAP_PRIVATE);
    set_code(code, 40);
    check(code, 40);
    set_code(code + PAGE_SIZE, 50);
    check(code + PAGE_SIZE, 50);
    res = mremap(code, 2 * PAGE_SIZE, PAGE_SIZE, 0);
    check_remap(res, code);
    modify(res, 40);
    print("shrunk: %d wrong results\n", wrong);

    /* 4) A shared mapping duplicated. */
    wrong = 0;
    space = reserve(3);
    code = map_code(space, 1, MAP_SHARED);
    set_code(code, 60);
    check(code, 60);
    res =
        mremap(code, 0, PAGE_SIZE, MREMAP_MAYMOVE | MREMAP_FIXED, space + 2 * PAGE_SIZE);
    check_remap(res, space + 2 * PAGE_SIZE);
    modify(res, 60);
    print("duplicated: %d wrong results\n", wrong);

    print("all done\n");
    return 0;
}
