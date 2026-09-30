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

/* Tests fragment variants together with the basic block filter event, drbbdup, and
 * drreg: the client has its filter pass only the blocks with a marker in variant 0,
 * which drbbdup and drreg leave alone (drbbdup_set_enabled_func() and
 * drreg_set_enabled_func()), and all blocks in variant 1, which drbbdup duplicates
 * and whose instrumentation clobbers registers and flags that drreg reserves.  This
 * app runs code and faults in blocks of both kinds in both variants, switching
 * between them without flushing, and checks the state at each fault.
 */

#include "tools.h"
#include "fragment_variants_ext.h"

#include <setjmp.h>
#include <signal.h>
#include <stdint.h>
#include <string.h>
#include <ucontext.h>

long
client_request(long op, long arg);
long
marked(void);
long
plain(void);
void
fault_func(void *addr);
void
fault_func_marked(void *addr);
extern char fault_func_load[];
extern char fault_func_marked_load[];

/* clang-format off */
#define FAULT_FUNC(name, marker)                                         \
        ".globl " #name "\n"                                             \
        ".type " #name ", @function\n"                                   \
        #name ":\n"                                                      \
        "  push %rbx\n"                                                  \
        "  push %r12\n"                                                  \
        "  push %r13\n"                                                  \
        "  push %r14\n"                                                  \
        "  push %r15\n"                                                  \
        marker                                                           \
        "  mov $" STRINGIFY(VAL_RBX) ", %rbx\n"                          \
        "  mov $" STRINGIFY(VAL_R12) ", %r12\n"                          \
        "  mov $" STRINGIFY(VAL_R13) ", %r13\n"                          \
        "  mov $" STRINGIFY(VAL_R14) ", %r14\n"                          \
        "  mov $" STRINGIFY(VAL_R15) ", %r15\n"                          \
        "  stc\n"                                                        \
        ".globl " #name "_load\n"                                        \
        #name "_load:\n"                                                 \
        "  mov (%rdi), %rax\n"                                           \
        /* Keep the registers and the flags live at the load. */         \
        "  adc %rbx, %rax\n"                                             \
        "  add %r12, %rax\n"                                             \
        "  add %r13, %rax\n"                                             \
        "  add %r14, %rax\n"                                             \
        "  add %r15, %rax\n"                                             \
        "  pop %r15\n"                                                   \
        "  pop %r14\n"                                                   \
        "  pop %r13\n"                                                   \
        "  pop %r12\n"                                                   \
        "  pop %rbx\n"                                                   \
        "  ret\n"
__asm__(".text\n"
        ".globl client_request\n"
        ".type client_request, @function\n"
        "client_request:\n"
        "  mov $-1, %rax\n"
        "  ret\n"
        ".globl marked\n"
        ".type marked, @function\n"
        "marked:\n"
        "  mov $" STRINGIFY(MARKER) ", %eax\n"
        "  mov $3, %eax\n"
        "  ret\n"
        ".globl plain\n"
        ".type plain, @function\n"
        "plain:\n"
        "  mov $4, %eax\n"
        "  ret\n"
        FAULT_FUNC(fault_func, "")
        FAULT_FUNC(fault_func_marked, "  mov $" STRINGIFY(MARKER) ", %eax\n"));
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
    check(client_request(REQ_SELECT, v), 1, "select");
    check(client_request(REQ_CURRENT_VARIANT, 0), v, "current variant");
}

static sigjmp_buf fault_env;
static ucontext_t fault_uc;

static void
handle_segv(int sig, siginfo_t *info, void *ucxt)
{
    memcpy(&fault_uc, ucxt, sizeof(fault_uc));
    siglongjmp(fault_env, 1);
}

static void
test_fault(int v, void (*func)(void *), char *load_pc, const char *what)
{
    greg_t *regs = fault_uc.uc_mcontext.gregs;
    bool regs_ok;
    select_variant(v);
    memset(&fault_uc, 0, sizeof(fault_uc));
    if (sigsetjmp(fault_env, 1) == 0) {
        (*func)(NULL);
        print("ERROR: no fault\n");
    }
    regs_ok = regs[REG_RBX] == VAL_RBX && regs[REG_R12] == VAL_R12 &&
        regs[REG_R13] == VAL_R13 && regs[REG_R14] == VAL_R14 &&
        regs[REG_R15] == VAL_R15 && (regs[REG_EFL] & 1 /*CF*/) != 0;
    print("fault in variant %d (%s): pc %s, registers %s\n", v, what,
          regs[REG_RIP] == (greg_t)load_pc ? "ok" : "WRONG", regs_ok ? "ok" : "WRONG");
}

int
main(int argc, char **argv)
{
    struct sigaction act;
    int i, v;

    check(client_request(REQ_NUM_VARIANTS, 0), NUM_VARIANTS, "number of variants");
    memset(&act, 0, sizeof(act));
    act.sa_sigaction = handle_segv;
    act.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &act, NULL);

    for (v = 0; v < NUM_VARIANTS; v++) {
        test_fault(v, fault_func, fault_func_load,
                   v == 0 ? "not passed to the bb event" : "drbbdup and drreg");
        test_fault(v, fault_func_marked, fault_func_marked_load,
                   v == 0 ? "passed to the bb event" : "drbbdup and drreg, marked");
    }

    /* Switching between the variants builds each block once in each variant. */
    for (i = 0; i < 20; i++) {
        select_variant(i % NUM_VARIANTS);
        check(marked(), 3, "marked()");
        check(plain(), 4, "plain()");
    }
    print("blocks of marked() and plain() built in each variant:");
    for (v = 0; v < NUM_VARIANTS; v++)
        print(" %ld", client_request(REQ_BUILDS, v));
    print("\n");

    print("client errors: %ld\n", client_request(REQ_ERRORS, 0));
    print("%d errors\n", errors);
    return 0;
}
