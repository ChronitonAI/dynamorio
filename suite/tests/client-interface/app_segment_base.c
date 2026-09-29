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

/* Tests dr_get_app_segment_base() and dr_set_app_segment_base(): see
 * app_segment_base.dll.c.
 */

#include "tools.h"

#include <asm/prctl.h>
#include <stdint.h>
#include <sys/syscall.h>
#include <unistd.h>

/* The client acts on "mov $MARKER_*, %eax" preceded by "mov <arg>, %rdx". */
#define MARKER_CHECK_FS 0x7eca0101
#define MARKER_SET_GS 0x7eca0102
#define MARKER(kind, arg)                              \
    __asm__ __volatile__("mov %0, %%rdx\n\t"           \
                         "mov %1, %%eax"               \
                         :                             \
                         : "r"((long)(arg)), "i"(kind) \
                         : "rdx", "rax", "memory")

static uint64_t gs_data[2] = { 0x1122334455667788ULL, 0x0123456789abcdefULL };

static uint64_t
arch_get(int code)
{
    uint64_t base = 0;
    if (syscall(SYS_arch_prctl, code, &base) != 0)
        print("arch_prctl failed\n");
    return base;
}

int
main(int argc, char **argv)
{
    uint64_t val, orig_gs = arch_get(ARCH_GET_GS);
    /* The client compares its view of the fs base with ours. */
    MARKER(MARKER_CHECK_FS, arch_get(ARCH_GET_FS));
    /* The client sets our gs base. */
    MARKER(MARKER_SET_GS, gs_data);
    __asm__ __volatile__("mov %%gs:8, %0" : "=r"(val));
    if (arch_get(ARCH_GET_GS) == (uint64_t)gs_data && val == gs_data[1])
        print("gs base set by the client\n");
    /* The client checks that it sees the base we set. */
    if (syscall(SYS_arch_prctl, ARCH_SET_GS, &gs_data[1]) != 0)
        print("arch_prctl failed\n");
    syscall(SYS_arch_prctl, ARCH_SET_GS, orig_gs);
    print("all done\n");
    return 0;
}
