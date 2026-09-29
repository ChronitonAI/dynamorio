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

/* Tests dr_futex_wait_at_safe_spot(): a thread waits in a clean call, in a system
 * call event and in a signal event while another thread flushes the code cache,
 * which must not have to wait for the waiting thread.  The waiting thread is then
 * woken with FUTEX_WAKE by the other thread and must continue correctly.
 * See futex_wait_safe_spot.dll.c.
 */

#include "tools.h"
#include "futex_wait_safe_spot-shared.h"

#include <linux/futex.h>
#include <pthread.h>
#include <sys/syscall.h>
#include <unistd.h>

/* The client acts on these markers: see futex_wait_safe_spot-shared.h. */
#define MARKER(kind, arg)                              \
    __asm__ __volatile__("mov %0, %%rdx\n\t"           \
                         "mov %1, %%eax"               \
                         :                             \
                         : "r"((long)(arg)), "i"(kind) \
                         : "rdx", "rax", "memory")

static volatile int gate[NUM_PHASES];

static void
wake(volatile int *futex)
{
    __atomic_store_n(futex, 1, __ATOMIC_SEQ_CST);
    syscall(SYS_futex, futex, FUTEX_WAKE, 1, NULL, NULL, 0);
}

static void *
waiter(void *arg)
{
    int i, sum = 0;
    for (i = 0; i < 10; i++)
        sum += i;

    print("waiter: waiting in a clean call\n");
    /* The client waits in a clean call inserted before this marker. */
    MARKER(MARKER_WAIT_IN_CLEAN_CALL, &gate[PHASE_CLEAN_CALL]);
    print("waiter: resumed after the clean call wait, gate %d\n", gate[PHASE_CLEAN_CALL]);

    print("waiter: waiting in a system call event\n");
    /* The client waits in the pre-syscall event of the next getpid. */
    MARKER(MARKER_WAIT_IN_SYSCALL, &gate[PHASE_SYSCALL]);
    if (syscall(SYS_getpid) != getpid())
        print("getpid returned the wrong value\n");
    print("waiter: resumed after the system call wait, gate %d\n", gate[PHASE_SYSCALL]);

    print("waiter: waiting in a signal event\n");
    /* The client waits in the signal event for the next SIGILL and then skips the
     * faulting instruction.
     */
    MARKER(MARKER_WAIT_IN_SIGNAL, &gate[PHASE_SIGNAL]);
    __asm__ __volatile__("ud2");
    print("waiter: resumed after the signal wait, gate %d\n", gate[PHASE_SIGNAL]);

    /* Check that the state of this thread survived. */
    for (i = 0; i < 10; i++)
        sum -= i;
    return (void *)(long)sum;
}

int
main(int argc, char **argv)
{
    pthread_t thread;
    void *retval;
    int phase;
    if (pthread_create(&thread, NULL, waiter, NULL) != 0) {
        print("pthread_create failed\n");
        return 1;
    }
    for (phase = 0; phase < NUM_PHASES; phase++) {
        /* The client waits for the other thread to be waiting and flushes. */
        MARKER(MARKER_FLUSH, phase);
        wake(&gate[phase]);
    }
    if (pthread_join(thread, &retval) != 0 || retval != NULL)
        print("waiter thread failed\n");
    print("all done\n");
    return 0;
}
