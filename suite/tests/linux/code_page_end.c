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

/* Tests code at the end of the last page of a mapping, followed by an unmapped or an
 * inaccessible page: instructions that end exactly at the end of the page, which must
 * execute without any fault (the decoder must not read past them), and instructions
 * that extend past it or execution that falls off its end, which must fault where they
 * do natively: at the start of the instruction that cannot be fetched, with the
 * instructions before it executed.  Each case runs twice, the second time from the
 * code cache.
 */

#include <setjmp.h>
#include <signal.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>

#include "tools.h"

typedef struct {
    const char *name;
    /* The code, which ends at the end of the page. */
    unsigned char bytes[8];
    int len;
    /* Where the code is called, from the start of the code. */
    int entry;
} code_case_t;

static const code_case_t cases[] = {
    /* ret */
    { "ret", { 0xc3 }, 1, 0 },
    /* ret 0 */
    { "ret-imm", { 0xc2, 0x00, 0x00 }, 3, 0 },
    /* mov eax, 7; ret */
    { "mov-ret", { 0xb8, 0x07, 0x00, 0x00, 0x00, 0xc3 }, 6, 0 },
    /* ret; jmp to the ret, entered at the jmp */
    { "jmp-back", { 0xc3, 0xeb, 0xfd }, 3, 1 },
    /* inc eax, then off the end of the page */
    { "fall-off", { 0xff, 0xc0 }, 2, 0 },
    /* mov eax, imm32 without the last two bytes of the immediate */
    { "partial", { 0xb8, 0x01, 0x00 }, 3, 0 },
    /* inc eax; mov eax, imm32 without the last two bytes of the immediate */
    { "inc-partial", { 0xff, 0xc0, 0xb8, 0x01, 0x00 }, 5, 0 },
    /* inc eax; mov r32, r/m32 without its ModRM byte */
    { "inc-no-modrm", { 0xff, 0xc0, 0x8b }, 3, 0 },
};

static SIGJMP_BUF mark;
static unsigned char *page_end;
static uintptr_t fault_pc, fault_addr, fault_eax;

static void
handle_segv(int sig, siginfo_t *info, ucontext_t *ucxt)
{
    sigcontext_t *sc = SIGCXT_FROM_UCXT(ucxt);
    fault_pc = (uintptr_t)sc->SC_XIP;
    fault_addr = (uintptr_t)info->si_addr;
    fault_eax = (uintptr_t)sc->SC_XAX;
    SIGLONGJMP(mark, 1);
}

static void
run(const code_case_t *c, const char *next_page)
{
    long eax = 5;
    if (SIGSETJMP(mark) == 0) {
        unsigned char *entry = page_end - c->len + c->entry;
        __asm__ __volatile__("call *%1" : "+a"(eax) : "r"(entry) : "memory");
        print("%s, %s: returned %ld\n", c->name, next_page, eax);
    } else {
        print("%s, %s: SIGSEGV at end%+ld, address end%+ld, eax %ld\n", c->name,
              next_page, (long)(fault_pc - (uintptr_t)page_end),
              (long)(fault_addr - (uintptr_t)page_end), (long)(int)fault_eax);
    }
}

int
main(int argc, char **argv)
{
    intercept_signal(SIGSEGV, (handler_3_t)handle_segv, false);
    for (int inaccessible = 0; inaccessible < 2; inaccessible++) {
        const char *next_page = inaccessible ? "inaccessible" : "unmapped";
        for (int i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
            const code_case_t *c = &cases[i];
            unsigned char *base = mmap(NULL, 2 * PAGE_SIZE, PROT_READ | PROT_WRITE,
                                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (base == MAP_FAILED) {
                print("mmap failed\n");
                return 1;
            }
            if ((inaccessible ? mprotect(base + PAGE_SIZE, PAGE_SIZE, PROT_NONE)
                              : munmap(base + PAGE_SIZE, PAGE_SIZE)) != 0) {
                print("munmap or mprotect failed\n");
                return 1;
            }
            page_end = base + PAGE_SIZE;
            /* int3 before the code. */
            memset(base, 0xcc, PAGE_SIZE);
            memcpy(page_end - c->len, c->bytes, c->len);
            if (mprotect(base, PAGE_SIZE, PROT_READ | PROT_EXEC) != 0) {
                print("mprotect failed\n");
                return 1;
            }
            run(c, next_page);
            run(c, next_page);
            munmap(base, 2 * PAGE_SIZE);
        }
    }
    print("all done\n");
    return 0;
}
