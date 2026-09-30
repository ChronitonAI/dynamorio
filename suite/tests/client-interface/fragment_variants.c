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

/* Tests fragment variants (-num_fragment_variants): the client instruments each
 * variant differently, and this app switches threads between variants, checks
 * that the instrumentation of the selected variant is what executes, that
 * switching back does not rebuild code, that a selection takes effect at a
 * system call, that a flush removes every variant, that faults in instrumented
 * variants are translated correctly, and that threads in different variants
 * can run and be flushed concurrently.
 */

#include "tools.h"
#include "fragment_variants-shared.h"

#include <pthread.h>
#include <setjmp.h>
#include <signal.h>
#include <stdint.h>
#include <sys/syscall.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>

/* Functions written in assembly so that their blocks do not depend on the
 * compiler (see fragment_variants.dll.c for how each variant instruments them).
 */
long
client_request(long op, long arg);
int
which_variant(void);
uint64_t
spin_loop(uint64_t iters);
void
fault_func(void *addr);
extern char fault_func_clobber[];

/* clang-format off */
__asm__(".text\n"
        ".globl variant_asm_start\n"
        "variant_asm_start:\n"
        /* long client_request(long op, long arg): intercepted by the client. */
        ".globl client_request\n"
        ".type client_request, @function\n"
        "client_request:\n"
        "  mov $-1, %rax\n"
        "  ret\n"
        /* int which_variant(void): each variant makes it return its number. */
        ".globl which_variant\n"
        ".type which_variant, @function\n"
        "which_variant:\n"
        "  mov $-1, %eax\n"
        "  ret\n"
        /* uint64_t spin_loop(uint64_t iters): returns iters * APP_RBX_VALUE,
         * accumulated from rbx, which variants 1 and 2 clobber around the dec.
         */
        ".globl spin_loop\n"
        ".type spin_loop, @function\n"
        "spin_loop:\n"
        "  push %rbx\n"
        "  mov $" STRINGIFY(APP_RBX_VALUE) ", %rbx\n"
        "  xor %eax, %eax\n"
        "1:\n"
        "  add %rbx, %rax\n"
        ".globl spin_loop_clobber\n"
        "spin_loop_clobber:\n"
        "  dec %rdi\n"
        "  jnz 1b\n"
        "  pop %rbx\n"
        "  ret\n"
        /* void fault_func(void *addr): loads from addr with rbx holding
         * APP_RBX_VALUE; variants 1 and 2 clobber rbx around the load.
         */
        ".globl fault_func\n"
        ".type fault_func, @function\n"
        "fault_func:\n"
        "  push %rbx\n"
        "  mov $" STRINGIFY(APP_RBX_VALUE) ", %rbx\n"
        ".globl fault_func_clobber\n"
        "fault_func_clobber:\n"
        "  mov (%rdi), %rax\n"
        "  pop %rbx\n"
        "  ret\n"
        ".globl variant_asm_end\n"
        "variant_asm_end:\n"
        "  nop\n");
/* clang-format on */

#define NUM_THREADS 6
#define THREAD_ROUNDS 150
#define SPIN_ITERS 3000

static int num_variants;
static volatile int errors;

static void
error(const char *msg, long value, long expected)
{
    __sync_fetch_and_add(&errors, 1);
    print("ERROR: %s: %ld instead of %ld\n", msg, value, expected);
}

static void
select_variant(int v)
{
    long res = client_request(REQ_SELECT, v);
    if (res != 1)
        error("select failed", res, 1);
    res = client_request(REQ_CURRENT_VARIANT, 0);
    if (res != v)
        error("current variant after select", res, v);
}

/* Some C code with indirect calls and returns, for the client's check that
 * every block that executes belongs to the thread's current variant.
 */
static int
add1(int x)
{
    return x + 1;
}
static int
add2(int x)
{
    return x + 2;
}
static int
add3(int x)
{
    return x + 3;
}
static int (*volatile funcs[])(int) = { add1, add2, add3 };

static NOINLINE int
work(int n)
{
    int i, sum = 0;
    for (i = 0; i < n; i++)
        sum += funcs[i % 3](i);
    return sum;
}

static int
expected_work(int n)
{
    int i, sum = 0;
    for (i = 0; i < n; i++)
        sum += i + 1 + i % 3;
    return sum;
}

static void
check_variant(int v)
{
    int res = which_variant();
    if (res != v)
        error("which_variant()", res, v);
    uint64_t sum = spin_loop(SPIN_ITERS);
    if (sum != (uint64_t)SPIN_ITERS * APP_RBX_VALUE)
        error("spin_loop()", (long)sum, (long)SPIN_ITERS * APP_RBX_VALUE);
    res = work(20);
    if (res != expected_work(20))
        error("work()", res, expected_work(20));
}

static NOINLINE void
lazy_switch(int to, long *before, long *after)
{
    client_request(REQ_SELECT_LAZY, to);
    *before = which_variant();
    /* The client intercepts getpid, which thus leaves the code cache. */
    syscall(SYS_getpid);
    *after = which_variant();
}

/* Has the client select variant to in the pre- (op REQ_SELECT_AT_PRE_SYSCALL) or
 * post-system call event (REQ_SELECT_AT_POST_SYSCALL) of a getpid, and returns
 * which_variant() after it.
 */
static NOINLINE long
select_at_syscall(long op, int to)
{
    client_request(op, to);
    syscall(SYS_getpid);
    return which_variant();
}

static void
print_deletions(const char *when)
{
    int v;
    print("which_variant blocks deleted %s:", when);
    for (v = 0; v < num_variants; v++)
        print(" %ld", client_request(REQ_DELETIONS, v));
    print("\n");
}

static void
print_builds(const char *when)
{
    int v;
    print("which_variant blocks built %s:", when);
    for (v = 0; v < num_variants; v++)
        print(" %ld", client_request(REQ_BUILDS, v));
    print("\n");
}

static sigjmp_buf fault_env;
static volatile int fault_pc_ok, fault_rbx_ok;

static void
handle_segv(int sig, siginfo_t *info, void *ucxt)
{
    ucontext_t *uc = (ucontext_t *)ucxt;
    fault_pc_ok = uc->uc_mcontext.gregs[REG_RIP] == (greg_t)fault_func_clobber;
    fault_rbx_ok = uc->uc_mcontext.gregs[REG_RBX] == APP_RBX_VALUE;
    siglongjmp(fault_env, 1);
}

static void *
thread_func(void *arg)
{
    int id = (int)(intptr_t)arg;
    int r;
    /* The client's thread init event selects variant tid % num_variants. */
    int initial = (int)(syscall(SYS_gettid) % num_variants);
    long cur = client_request(REQ_CURRENT_VARIANT, 0);
    if (cur != initial)
        error("initial variant of a new thread", cur, initial);
    check_variant(initial);
    for (r = 0; r < THREAD_ROUNDS; r++) {
        int v = (id + r) % num_variants;
        if (r % 5 == 4) {
            /* Switch at the next cache exit, here the system call. */
            client_request(REQ_SELECT_LAZY, v);
            syscall(SYS_getpid);
            cur = client_request(REQ_CURRENT_VARIANT, 0);
            if (cur != v)
                error("variant after a lazy select and a system call", cur, v);
        } else
            select_variant(v);
        check_variant(v);
    }
    return NULL;
}

int
main(int argc, char **argv)
{
    int v, i;
    long cur, before, after;
    struct sigaction act;
    pthread_t threads[NUM_THREADS];

    num_variants = (int)client_request(REQ_NUM_VARIANTS, 0);
    print("%d fragment variants\n", num_variants);
    if (num_variants < 3) {
        print("this test needs at least 3 variants\n");
        return 1;
    }
    cur = client_request(REQ_CURRENT_VARIANT, 0);
    if (cur != syscall(SYS_gettid) % num_variants)
        error("initial variant", cur, syscall(SYS_gettid) % num_variants);

    /* Switch back and forth: each variant's blocks are built once. */
    for (i = 0; i < 90; i++) {
        v = i % num_variants;
        select_variant(v);
        check_variant(v);
    }
    print_builds("after 90 switches");

    /* A switch without redirection waits for the next cache exit, which happens
     * at the system call at the latest.  We first run the same code in variant 0
     * a few times so that it likely does not leave the cache before the system
     * call (an indirect branch target is only added to the lookup tables when it
     * is missed after being built), but that depends on DR's options.
     */
    select_variant(0);
    for (i = 0; i < 3; i++)
        lazy_switch(0, &before, &after);
    lazy_switch(1, &before, &after);
    if (before != 0 && before != 1)
        error("variant before the system call", before, 0);
    print("lazy switch: variant %ld after the system call\n", after);

    /* A selection in a system call event takes effect after the system call. */
    select_variant(0);
    print("select in the pre-system call event: variant %ld after the system call\n",
          select_at_syscall(REQ_SELECT_AT_PRE_SYSCALL, 2));
    print("select in the post-system call event: variant %ld after the system call\n",
          select_at_syscall(REQ_SELECT_AT_POST_SYSCALL, 1));

    /* Faults in each variant are translated to the app state. */
    memset(&act, 0, sizeof(act));
    act.sa_sigaction = handle_segv;
    act.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &act, NULL);
    for (v = 0; v < num_variants; v++) {
        select_variant(v);
        fault_pc_ok = fault_rbx_ok = 0;
        if (sigsetjmp(fault_env, 1) == 0) {
            fault_func(NULL);
            print("ERROR: no fault\n");
        }
        print("fault in variant %d: pc %s, rbx %s\n", v, fault_pc_ok ? "ok" : "WRONG",
              fault_rbx_ok ? "ok" : "WRONG");
    }

    /* A flush removes the fragments of every variant. */
    select_variant(0);
    print_deletions("before a flush");
    client_request(REQ_FLUSH, 0);
    print_deletions("by the flush");
    for (v = 0; v < num_variants; v++) {
        select_variant(v);
        check_variant(v);
    }
    print_builds("after a flush");

    /* Threads in different variants, switching and flushed concurrently. */
    for (i = 0; i < NUM_THREADS; i++)
        pthread_create(&threads[i], NULL, thread_func, (void *)(intptr_t)i);
    for (i = 0; i < 40; i++) {
        struct timespec sleeptime = { 0, 2 * 1000 * 1000 };
        client_request(i % 2 == 0 ? REQ_FLUSH : REQ_DELAY_FLUSH, 0);
        nanosleep(&sleeptime, NULL);
        check_variant((int)client_request(REQ_CURRENT_VARIANT, 0));
    }
    for (i = 0; i < NUM_THREADS; i++)
        pthread_join(threads[i], NULL);

    print("blocks executed outside their variant: %ld\n",
          client_request(REQ_MISMATCHES, 0));
    print("%d errors\n", errors);
    return 0;
}
