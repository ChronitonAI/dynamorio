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

/* Tests dr_sandbox_app_memory(): code in memory that can change without a write
 * that DR sees, which the client (sandbox_app_memory.dll.c) sandboxes when we ask it to.
 * The code is in a memfd mapped twice, read+write and read+exec, as a JIT with a
 * "dual-mapped" code cache does: we write code through the first view, run it through
 * the second, and change it through the first.
 * 1) Both pages of the read+exec view sandboxed.
 * 2) Only its second page: a block in the first page jumps to code in the second.
 * 3) The read+exec view made writable, written through itself, and read-only again.
 * 4) The read+exec view unmapped and mapped again: the request persists.
 * 5) Sandboxing stopped for a private writable and executable page, whose changes DR
 *    sees through page protection again.
 * Without the requests, most calls in parts 1, 2 and 4 return a result of an older
 * version of the code.
 */

#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "tools.h"
#include "sandbox_app_memory.h"

/* Executions of each version of the code: more than -sandbox2ro_threshold. */
#define CALLS_PER_VERSION 50
#define VERSIONS 20

typedef int (*func_t)(void);

static unsigned char *rw, *rx;

static void
request(unsigned char *start, size_t size, int sandbox)
{
    if (syscall(SANDBOX_SYSCALL, start, size, sandbox) != 0)
        print("request failed\n");
}

/* Writes "mov eax, value; ret" at code. */
static void
write_code(unsigned char *code, int32_t value)
{
    code[0] = 0xb8;
    memcpy(code + 1, &value, sizeof(value));
    code[5] = 0xc3;
}

/* Writes "jmp target" at code, which runs at pc. */
static void
write_jmp(unsigned char *code, unsigned char *pc, unsigned char *target)
{
    int32_t rel = (int32_t)(target - (pc + 5));
    code[0] = 0xe9;
    memcpy(code + 1, &rel, sizeof(rel));
}

/* Calls entry CALLS_PER_VERSION times for each of VERSIONS versions of the code at
 * write_at, and returns the number of wrong results.
 */
static int
run(func_t entry, unsigned char *write_at, int32_t base)
{
    int wrong = 0;
    for (int32_t v = base; v < base + VERSIONS; v++) {
        write_code(write_at, v);
        for (int i = 0; i < CALLS_PER_VERSION; i++) {
            if (entry() != v)
                wrong++;
        }
    }
    return wrong;
}

static int
map_views(int fd)
{
    rw = mmap(NULL, 2 * PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    rx = mmap(NULL, 2 * PAGE_SIZE, PROT_READ | PROT_EXEC, MAP_SHARED, fd, 0);
    if (rw == MAP_FAILED || rx == MAP_FAILED) {
        print("mmap failed\n");
        return -1;
    }
    return 0;
}

int
main(int argc, char **argv)
{
    int fd = (int)syscall(SYS_memfd_create, "code", 0);
    if (fd < 0 || ftruncate(fd, 2 * PAGE_SIZE) != 0) {
        print("memfd failed\n");
        return 1;
    }

    /* 1) Both pages sandboxed. */
    if (map_views(fd) != 0)
        return 1;
    request(rx, 2 * PAGE_SIZE, 1);
    print("dual-mapped: %d wrong results\n", run((func_t)rx, rw, 1));

    /* 2) Only the second page: the jmp in the first may not take the second's code
     * into its block.
     */
    request(rx, 2 * PAGE_SIZE, 0);
    request(rx + PAGE_SIZE, PAGE_SIZE, 1);
    write_jmp(rw, rx, rx + PAGE_SIZE);
    print("partly sandboxed: %d wrong results\n", run((func_t)rx, rw + PAGE_SIZE, 100));
    request(rx + PAGE_SIZE, PAGE_SIZE, 0);

    /* 3) Protection changes of the read+exec view. */
    request(rx, 2 * PAGE_SIZE, 1);
    int wrong = run((func_t)rx, rw, 200);
    if (mprotect(rx, 2 * PAGE_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        print("mprotect failed\n");
        return 1;
    }
    wrong += run((func_t)rx, rx, 300);
    wrong += run((func_t)rx, rw, 400);
    if (mprotect(rx, 2 * PAGE_SIZE, PROT_READ | PROT_EXEC) != 0) {
        print("mprotect failed\n");
        return 1;
    }
    wrong += run((func_t)rx, rw, 500);
    print("protection changes: %d wrong results\n", wrong);

    /* 4) Mapped again at the same address. */
    if (munmap(rx, 2 * PAGE_SIZE) != 0 ||
        mmap(rx, 2 * PAGE_SIZE, PROT_READ | PROT_EXEC, MAP_SHARED | MAP_FIXED, fd, 0) !=
            rx) {
        print("mapping again failed\n");
        return 1;
    }
    print("mapped again: %d wrong results\n", run((func_t)rx, rw, 600));
    request(rx, 2 * PAGE_SIZE, 0);

    /* 5) Sandboxing stopped for a private page. */
    unsigned char *page = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC,
                               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (page == MAP_FAILED) {
        print("mmap failed\n");
        return 1;
    }
    request(page, PAGE_SIZE, 1);
    wrong = run((func_t)page, page, 700);
    request(page, PAGE_SIZE, 0);
    wrong += run((func_t)page, page, 800);
    print("sandboxing stopped: %d wrong results\n", wrong);

    print("all done\n");
    return 0;
}
