/* **********************************************************
 * Copyright (c) 2026 Keno Fischer.  All rights reserved.
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

/* Tests dr_deliver_signal_frame(): the client delivers signals to handlers of this
 * app through frames it builds itself, from a clean call and from the signal event.
 * See deliver_signal_frame.dll.c.
 */

#include "tools.h"

#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "deliver_signal_frame-shared.h"

#ifndef SS_AUTODISARM
#    define SS_AUTODISARM (1U << 31)
#endif

#define MARKER(kind, arg)                              \
    __asm__ __volatile__("mov %0, %%rdx\n\t"           \
                         "mov %1, %%eax"               \
                         :                             \
                         : "r"((long)(arg)), "i"(kind) \
                         : "rdx", "rax", "memory")

#define ALT_STACK_SIZE (64 * 1024)

static char *alt_stack;

/* The part of the frame that sigreturn reads: the kernel's struct ucontext. */
#define KERNEL_UCONTEXT_SIZE (8 + 8 + sizeof(stack_t) + 256 + 8)
static char frame_copy[KERNEL_UCONTEXT_SIZE];
static void *frame_ucxt;

static void
handler(int sig, siginfo_t *info, void *ucxt)
{
    sigset_t cur;
    stack_t ss;
    struct sigaction act;
    char local;
    sigprocmask(SIG_BLOCK, NULL, &cur);
    sigaction(sig, NULL, &act);
    sigaltstack(NULL, &ss);
    if (sig == SIGUSR1) {
        print("handler: signal %d, code %d, blocked self %d, blocked SIGUSR2 %d, reset "
              "%d\n",
              sig, info->si_code, sigismember(&cur, SIGUSR1), sigismember(&cur, SIGUSR2),
              act.sa_handler == SIG_DFL);
    } else {
        /* Remember the frame, which is left alone on the alternate stack. */
        frame_ucxt = ucxt;
        memcpy(frame_copy, ucxt, sizeof(frame_copy));
        print("handler: signal %d, blocked self %d, on alternate stack %d, alternate "
              "stack disabled %d\n",
              sig, sigismember(&cur, sig),
              &local >= alt_stack && &local < alt_stack + ALT_STACK_SIZE,
              (ss.ss_flags & SS_DISABLE) != 0);
    }
}

static void
fill_request(deliver_request_t *req, int sig, int on_altstack)
{
    struct sigaction act;
    sigset_t cur;
    memset(req, 0, sizeof(*req));
    req->sig = sig;
    sigaction(sig, NULL, &act);
    req->handler = (void *)act.sa_sigaction;
    req->restorer = (void *)act.sa_restorer;
    sigprocmask(SIG_BLOCK, NULL, &cur);
    memcpy(&req->blocked, &cur, sizeof(req->blocked));
    sigaltstack(NULL, &req->altstack);
    req->on_altstack = on_altstack;
}

int
main(int argc, char **argv)
{
    struct sigaction act;
    deliver_request_t req;
    sigset_t cur;
    stack_t ss;
    /* Values that must survive the handlers. */
    volatile int a = 1, b = 2;

    /* A handler that blocks SIGUSR2 and resets itself. */
    memset(&act, 0, sizeof(act));
    act.sa_sigaction = handler;
    act.sa_flags = SA_SIGINFO | SA_RESETHAND;
    sigemptyset(&act.sa_mask);
    sigaddset(&act.sa_mask, SIGUSR2);
    sigaction(SIGUSR1, &act, NULL);
    fill_request(&req, SIGUSR1, 0);
    /* The client delivers SIGUSR1 from a clean call here. */
    MARKER(MARKER_DELIVER, &req);
    sigprocmask(SIG_BLOCK, NULL, &cur);
    sigaction(SIGUSR1, NULL, &act);
    print("after handler: blocked SIGUSR1 %d, blocked SIGUSR2 %d, reset %d, values %s\n",
          sigismember(&cur, SIGUSR1), sigismember(&cur, SIGUSR2),
          act.sa_handler == SIG_DFL, a + b == 3 ? "ok" : "corrupted");

    /* A handler on an alternate stack that disarms itself while in use. */
    alt_stack = malloc(ALT_STACK_SIZE);
    ss.ss_sp = alt_stack;
    ss.ss_size = ALT_STACK_SIZE;
    ss.ss_flags = SS_AUTODISARM;
    if (sigaltstack(&ss, NULL) != 0)
        print("sigaltstack failed\n");
    memset(&act, 0, sizeof(act));
    act.sa_sigaction = handler;
    act.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_NODEFER;
    sigemptyset(&act.sa_mask);
    sigaction(SIGUSR2, &act, NULL);
    fill_request(&req, SIGUSR2, 1);
    /* The client delivers SIGUSR2 from the signal event for this SIGILL. */
    MARKER(MARKER_DELIVER_AT_SIGILL, &req);
    __asm__ __volatile__("ud2");
    sigaltstack(NULL, &ss);
    print("after handler: alternate stack enabled %d, values %s, frame %s\n",
          (ss.ss_flags & SS_DISABLE) == 0 && ss.ss_sp == alt_stack,
          a + b == 3 ? "ok" : "corrupted",
          memcmp(frame_ucxt, frame_copy, sizeof(frame_copy)) == 0 ? "unchanged"
                                                                  : "changed");
    print("all done\n");
    return 0;
}
