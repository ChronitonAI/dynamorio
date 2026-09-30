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

/* Tests flushing only some of the fragments built from an area of code: with
 * dr_unlink_flush_fragment(), and with dr_unlink_flush_region_ex() and
 * dr_delay_flush_region_ex() with DR_FLUSH_EXACT.  The application runs code from a
 * buffer and asks the client (flush_exact.dll.c) to flush parts of it; the client
 * checks which of the blocks' fragments are gone.  Another thread runs code from the
 * buffer meanwhile.
 */

#include "tools.h"
#include "flush_exact.h"

#include <fcntl.h>
#include <pthread.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define MARKER(kind, addr, size)                                           \
    __asm__ __volatile__("mov %0, %%rdx\n\t"                               \
                         "mov %1, %%rsi\n\t"                               \
                         "mov %2, %%eax"                                   \
                         :                                                 \
                         : "r"((long)(addr)), "r"((long)(size)), "i"(kind) \
                         : "rdx", "rsi", "rax", "memory")

typedef int (*func_t)(void);

static byte *code;

static int
call_at(size_t offs)
{
    return ((func_t)(code + offs))();
}

/* Runs all blocks but the loop's. */
static int
run(void)
{
    return call_at(OFFS_A) + call_at(OFFS_B) + call_at(OFFS_C1) + call_at(OFFS_D) +
        call_at(OFFS_W) + call_at(OFFS_E) + call_at(OFFS_X(PAGE_SIZE)) +
        call_at(OFFS_Y(PAGE_SIZE));
}

static volatile bool helper_started;
static volatile bool helper_stop;

/* Runs block Y until told to stop. */
static void *
helper(void *arg)
{
    long sum = 0;
    helper_started = true;
    while (!helper_stop)
        sum += call_at(OFFS_Y(PAGE_SIZE));
    return (void *)sum;
}

static void
put(size_t offs, const byte *bytes, size_t size)
{
    memcpy(code + offs, bytes, size);
}

int
main(int argc, char **argv)
{
    static const byte a[] = { 0xb8, 0x01, 0x00, 0x00, 0x00, 0xc3 };
    static const byte b[] = { 0xb8, 0x02, 0x00, 0x00, 0x00, 0xc3 };
    static const byte c1[] = { 0xb8, 0x03, 0x00, 0x00, 0x00, 0xeb, 0x00 };
    static const byte c2[] = { 0x83, 0xc0, 0x10, 0xc3 };
    static const byte d_end[] = { 0xb8, 0x04, 0x00, 0x00, 0x00, 0xc3 };
    static const byte w[] = { 0xb8, 0x09, 0x00, 0x00, 0x00, 0xc3 };
    static const byte e[] = { 0xb8, 0x05, 0x00, 0x00, 0x00, 0xc3 };
    static const byte t[] = { 0xb9, 0xc8, 0x00, 0x00, 0x00, 0xff, 0xc9, 0x75,
                              0xfc, 0xb8, 0x06, 0x00, 0x00, 0x00, 0xc3 };
    static const byte x[] = { 0x90, 0x90, 0x90, 0xb8, 0x07, 0x00, 0x00, 0x00, 0xc3 };
    static const byte y[] = { 0xb8, 0x08, 0x00, 0x00, 0x00, 0xc3 };
    static const byte w_imm = 90;
    size_t pg = PAGE_SIZE;
    pthread_t thread;
    void *helper_sum;
    int fd;

    code = mmap(NULL, 3 * pg, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (code == MAP_FAILED) {
        print("mmap failed\n");
        return 1;
    }
    memset(code, 0xcc, 3 * pg);
    put(OFFS_A, a, sizeof(a));
    put(OFFS_B, b, sizeof(b));
    put(OFFS_C1, c1, sizeof(c1));
    put(OFFS_C2, c2, sizeof(c2));
    memset(code + OFFS_D, 0x90, OFFS_D_LAST_NOP + 1 - OFFS_D);
    put(OFFS_D_LAST_NOP + 1, d_end, sizeof(d_end));
    put(OFFS_W, w, sizeof(w));
    put(OFFS_E, e, sizeof(e));
    put(OFFS_T, t, sizeof(t));
    put(OFFS_X(pg), x, sizeof(x));
    put(OFFS_Y(pg), y, sizeof(y));
    if (mprotect(code, 2 * pg, PROT_READ | PROT_EXEC) != 0 ||
        mprotect(code + 2 * pg, pg, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        print("mprotect failed\n");
        return 1;
    }
    MARKER(MARKER_SETUP, code, 0);

    print("run: %d\n", run());

    if (pthread_create(&thread, NULL, helper, NULL) != 0) {
        print("pthread_create failed\n");
        return 1;
    }
    while (!helper_started)
        sched_yield();

    /* The fragments of one tag. */
    MARKER(MARKER_FLUSH_TAG, code + OFFS_A, 0);
    print("run: %d\n", run());

    /* A block that flushes itself and continues at its start. */
    MARKER(MARKER_SELF_FLUSH, code + OFFS_E, 0);
    print("run: %d\n", run());

    /* The fragments with code from a region: the first byte of a block. */
    MARKER(MARKER_FLUSH_EXACT, code + OFFS_C2, 1);
    print("run: %d\n", run());

    /* The last byte of the block before it. */
    MARKER(MARKER_FLUSH_EXACT, code + OFFS_C2 - 1, 1);
    print("run: %d\n", run());

    /* The end of a long block. */
    MARKER(MARKER_FLUSH_EXACT, code + OFFS_D_LAST_NOP, 1);
    print("run: %d\n", run());

    /* A block that starts in the other area of code. */
    MARKER(MARKER_FLUSH_EXACT, code + 2 * pg + 1, 1);
    print("run: %d\n", run());

    /* Code changed behind DR's back: writes through /proc/self/mem bypass page
     * protections, and with them DR's detection of code changes.
     */
    fd = open("/proc/self/mem", O_RDWR);
    if (fd < 0 || pwrite(fd, &w_imm, 1, (off_t)(code + OFFS_W + 1)) != 1)
        print("writing /proc/self/mem failed\n");
    close(fd);
    print("before the flush: %d\n", call_at(OFFS_W));
    MARKER(MARKER_FLUSH_EXACT, code + OFFS_W + 1, 1);
    print("after the flush: %d\n", call_at(OFFS_W));
    print("run: %d\n", run());

    /* A delayed flush. */
    MARKER(MARKER_DELAY_EXACT, code + OFFS_B + 1, 1);
    print("run: %d\n", run());

    /* Without DR_FLUSH_EXACT, the flush takes the whole area of code. */
    MARKER(MARKER_FLUSH_REGION, code + OFFS_A, 1);
    print("run: %d\n", run());

    /* The fragments of the block the other thread runs. */
    MARKER(MARKER_FLUSH_RUNNING, code + OFFS_Y(pg), 0);
    MARKER(MARKER_FLUSH_RUNNING, code + OFFS_Y(pg) + 1, 1);
    helper_stop = true;
    if (pthread_join(thread, &helper_sum) != 0 || (long)helper_sum % 8 != 0)
        print("helper failed\n");

    /* A loop, which becomes a trace (unless traces are disabled). */
    print("loop: %d\n", call_at(OFFS_T));
    MARKER(MARKER_REPORT, 0, 0);
    MARKER(MARKER_FLUSH_TAG, code + OFFS_L, 0);
    print("loop: %d\n", call_at(OFFS_T));
    MARKER(MARKER_REPORT, 0, 0);

    print("all done\n");
    return 0;
}
