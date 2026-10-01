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

/* Tests code in writable memory that DR cannot make read-only, because the
 * memory is sealed (mseal): DR must sandbox the code to see it change.
 * 1) Code in a sealed page, modified in place.
 * 2) Code in a page that DR sandboxes after repeated writes (-ro2sandbox_threshold)
 *    and that is sealed then: DR must keep sandboxing it when it tries to switch
 *    it back to page protection (-sandbox2ro_threshold).
 * Without mseal (Linux before 6.10) both parts run with unsealed pages.
 */

#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "tools.h"

#ifndef SYS_mseal
#    define SYS_mseal 462
#endif

/* Executions of each version of the code: more than -sandbox2ro_threshold. */
#define CALLS_PER_VERSION 50
/* Versions written in a row in part 2: more than -ro2sandbox_threshold. */
#define WRITES_TO_SANDBOX 30

typedef int (*func_t)(void);

/* mov eax, imm32; ret */
static const unsigned char code_template[] = { 0xb8, 0, 0, 0, 0, 0xc3 };

/* A writable and executable page between two inaccessible ones, so that DR does not
 * take it and a neighboring mapping for one region.
 */
static unsigned char *
map_code_page(void)
{
    unsigned char *guarded =
        mmap(NULL, 3 * PAGE_SIZE, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (guarded == MAP_FAILED) {
        print("mmap failed\n");
        return NULL;
    }
    unsigned char *page = guarded + PAGE_SIZE;
    if (mprotect(page, PAGE_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        print("mprotect failed\n");
        return NULL;
    }
    memcpy(page, code_template, sizeof(code_template));
    return page;
}

static void
seal(unsigned char *page)
{
    /* Older kernels have no mseal (ENOSYS): the page stays unsealed. */
    if (syscall(SYS_mseal, page, PAGE_SIZE, 0) != 0 && errno != ENOSYS)
        print("mseal failed: %d\n", errno);
}

static void
set_value(unsigned char *page, int32_t value)
{
    memcpy(page + 1, &value, sizeof(value));
}

/* Runs the code CALLS_PER_VERSION times and returns the number of wrong results. */
static int
run(unsigned char *page, int32_t expect)
{
    int wrong = 0;
    for (int i = 0; i < CALLS_PER_VERSION; i++) {
        if (((func_t)page)() != expect)
            wrong++;
    }
    return wrong;
}

int
main(int argc, char **argv)
{
    unsigned char *page;
    int wrong = 0;
    int32_t value;

    /* 1) Sealed before any code in it runs. */
    page = map_code_page();
    if (page == NULL)
        return 1;
    seal(page);
    for (value = 1; value <= 20; value++) {
        set_value(page, value);
        wrong += run(page, value);
    }
    print("sealed page: %d wrong results\n", wrong);

    /* 2) Sandboxed after repeated writes, then sealed. */
    wrong = 0;
    page = map_code_page();
    if (page == NULL)
        return 1;
    for (value = 1; value <= WRITES_TO_SANDBOX; value++) {
        set_value(page, value);
        if (((func_t)page)() != value)
            wrong++;
    }
    seal(page);
    for (; value <= WRITES_TO_SANDBOX + 20; value++) {
        set_value(page, value);
        wrong += run(page, value);
    }
    print("page sealed while sandboxed: %d wrong results\n", wrong);

    print("all done\n");
    return 0;
}
