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

/* Tests the basic block filter event (dr_register_bb_filter_event()): the client
 * (bb_filter.dll.c) passes to its basic block event only the blocks that contain a
 * marker instruction, and instruments the markers.  The application runs code from a
 * buffer: blocks with and without the marker, faults in both kinds (whose addresses
 * DR must translate the same way the blocks were built), system calls, and loops
 * that become traces.
 */

#include "tools.h"

#include <setjmp.h>
#include <signal.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

/* The marker the client looks for: nopl 0x12345678(%rax). */
#define MARKER 0x0f, 0x1f, 0x80, 0x78, 0x56, 0x34, 0x12
#define MARKER_SIZE 7

typedef long (*func_t)(void *arg);

static byte *code;
static size_t code_used;

static SIGJMP_BUF mark;
static byte *expected_fault_pc;

/* Copies a function into the code buffer and returns its offset. */
static size_t
put(const byte *bytes, size_t size)
{
    size_t offs = code_used;
    memcpy(code + offs, bytes, size);
    code_used += (size + 15) & ~(size_t)15;
    return offs;
}

static long
call_at(size_t offs, void *arg)
{
    return ((func_t)(code + offs))(arg);
}

static void
handle_segv(int signal, siginfo_t *siginfo, ucontext_t *ucxt)
{
    sigcontext_t *sc = SIGCXT_FROM_UCXT(ucxt);
    if ((byte *)sc->SC_XIP == expected_fault_pc)
        print("fault at the expected pc\n");
    else {
        print("fault at +0x%lx instead of +0x%lx\n", (long)((byte *)sc->SC_XIP - code),
              (long)(expected_fault_pc - code));
    }
    SIGLONGJMP(mark, 1);
}

static void
fault_at(size_t offs, size_t fault_offs)
{
    expected_fault_pc = code + fault_offs;
    if (SIGSETJMP(mark) == 0) {
        call_at(offs, (void *)8 /* unmapped */);
        print("no fault\n");
    }
}

int
main(int argc, char **argv)
{
    /* clang-format off */
    /* mov $1, %eax; ret */
    static const byte plain[] = { 0xb8, 0x01, 0x00, 0x00, 0x00, 0xc3 };
    /* marker; mov $2, %eax; ret */
    static const byte marked[] = { MARKER, 0xb8, 0x02, 0x00, 0x00, 0x00, 0xc3 };
    /* mov $3, %eax; mov (%rdi), %rax; ret */
    static const byte fault_plain[] = { 0xb8, 0x03, 0x00, 0x00, 0x00, 0x48, 0x8b, 0x07,
                                        0xc3 };
    /* marker; mov $4, %eax; mov (%rdi), %rax; ret */
    static const byte fault_marked[] = { MARKER, 0xb8, 0x04, 0x00, 0x00, 0x00, 0x48,
                                         0x8b, 0x07, 0xc3 };
    /* mov $SYS_getpid, %eax; syscall; ret */
    static const byte getpid_plain[] = { 0xb8, 0x27, 0x00, 0x00, 0x00, 0x0f, 0x05, 0xc3 };
    /* marker; mov $SYS_getpid, %eax; syscall; ret */
    static const byte getpid_marked[] = { MARKER, 0xb8, 0x27, 0x00, 0x00, 0x00, 0x0f,
                                          0x05, 0xc3 };
    /* mov $100, %ecx; 1: dec %ecx; jnz 1b; mov $5, %eax; ret */
    static const byte loop_plain[] = { 0xb9, 0x64, 0x00, 0x00, 0x00, 0xff, 0xc9, 0x75,
                                       0xfc, 0xb8, 0x05, 0x00, 0x00, 0x00, 0xc3 };
    /* mov $100, %ecx; 1: marker; dec %ecx; jnz 1b; mov $6, %eax; ret */
    static const byte loop_marked[] = { 0xb9, 0x64, 0x00, 0x00, 0x00, MARKER, 0xff, 0xc9,
                                        0x75, 0xf5, 0xb8, 0x06, 0x00, 0x00, 0x00, 0xc3 };
    /* A Valgrind client request (RUNNING_ON_VALGRIND, the request block at %rdi, the
     * default result 0): mov %rdi, %rax; xor %edx, %edx; rol $3, %rdi; rol $13, %rdi;
     * rol $61, %rdi; rol $51, %rdi; xchg %rbx, %rbx; mov %rdx, %rax; ret
     */
    static const byte vg_request[] = { 0x48, 0x89, 0xf8, 0x31, 0xd2, 0x48, 0xc1, 0xc7,
                                       0x03, 0x48, 0xc1, 0xc7, 0x0d, 0x48, 0xc1, 0xc7,
                                       0x3d, 0x48, 0xc1, 0xc7, 0x33, 0x48, 0x87, 0xdb,
                                       0x48, 0x89, 0xd0, 0xc3 };
    /* clang-format on */
    /* RUNNING_ON_VALGRIND, five unused arguments and the default result (DR reads the
     * default from here; Valgrind's request block ends before it).
     */
    static long vg_args[7] = { 0x1001, 0, 0, 0, 0, 0, 0 };
    long vg_result;
    size_t pg = PAGE_SIZE;
    size_t offs_plain, offs_marked, offs_fault_plain, offs_fault_marked;
    size_t offs_getpid_plain, offs_getpid_marked, offs_loop_plain, offs_loop_marked;
    size_t offs_vg_request;
    int i;

    code = mmap(NULL, pg, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (code == MAP_FAILED) {
        print("mmap failed\n");
        return 1;
    }
    memset(code, 0xcc, pg);
    offs_plain = put(plain, sizeof(plain));
    offs_marked = put(marked, sizeof(marked));
    offs_fault_plain = put(fault_plain, sizeof(fault_plain));
    offs_fault_marked = put(fault_marked, sizeof(fault_marked));
    offs_getpid_plain = put(getpid_plain, sizeof(getpid_plain));
    offs_getpid_marked = put(getpid_marked, sizeof(getpid_marked));
    offs_loop_plain = put(loop_plain, sizeof(loop_plain));
    offs_loop_marked = put(loop_marked, sizeof(loop_marked));
    offs_vg_request = put(vg_request, sizeof(vg_request));
    if (mprotect(code, pg, PROT_READ | PROT_EXEC) != 0) {
        print("mprotect failed\n");
        return 1;
    }
    intercept_signal(SIGSEGV, (handler_3_t)handle_segv, false);

    print("plain: %ld\n", call_at(offs_plain, NULL));
    print("marked: %ld\n", call_at(offs_marked, NULL));

    /* DR translates a fault in a block the client did not see, and in one it
     * instrumented, by building the block again: the filter must decide the same way.
     */
    fault_at(offs_fault_plain, offs_fault_plain + 5);
    fault_at(offs_fault_marked, offs_fault_marked + MARKER_SIZE + 5);

    print("getpid plain: %s\n",
          call_at(offs_getpid_plain, NULL) == getpid() ? "correct" : "wrong");
    print("getpid marked: %s\n",
          call_at(offs_getpid_marked, NULL) == getpid() ? "correct" : "wrong");

    /* Enough iterations for DR to build traces. */
    for (i = 0; i < 10; i++) {
        if (call_at(offs_loop_plain, NULL) != 5 || call_at(offs_loop_marked, NULL) != 6)
            print("loop failed\n");
    }
    print("loops done\n");

    /* A block the filter leaves alone in which DR finds an annotation, which makes DR
     * build the block again with a full decode: the new build must record the block's
     * page again.  (DR's annotation support makes the result 1; natively it is 0.)
     */
    vg_result = call_at(offs_vg_request, vg_args);
    print("valgrind request: %s\n",
          vg_result == 0 || vg_result == 1 ? "answered" : "wrong");

    print("all done\n");
    return 0;
}
