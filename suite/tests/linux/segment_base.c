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

/* Tests the application's access to its fs and gs segment bases, which DR
 * virtualizes on x86-64 as it uses these registers itself: with the arch_prctl
 * system call and with the rdfsbase, wrfsbase, rdgsbase and wrgsbase instructions.
 */

#include "tools.h"

#include <asm/prctl.h>
#include <errno.h>
#include <stdint.h>
#include <sys/auxv.h>
#include <sys/syscall.h>
#include <unistd.h>

#ifndef HWCAP2_FSGSBASE
#    define HWCAP2_FSGSBASE (1 << 1)
#endif

static uint64_t gs_data[2] = { 0x1122334455667788ULL, 0x0123456789abcdefULL };

static uint64_t
arch_get(int code)
{
    uint64_t base = 0;
    if (syscall(SYS_arch_prctl, code, &base) != 0)
        print("arch_prctl failed\n");
    return base;
}

static uint64_t
read_gs(int index)
{
    uint64_t val;
    if (index == 0)
        __asm__ __volatile__("mov %%gs:0, %0" : "=r"(val));
    else
        __asm__ __volatile__("mov %%gs:8, %0" : "=r"(val));
    return val;
}

static void
test_arch_prctl(void)
{
    uint64_t orig_gs = arch_get(ARCH_GET_GS);
    if (syscall(SYS_arch_prctl, ARCH_SET_GS, (uint64_t)gs_data) != 0)
        print("arch_prctl(ARCH_SET_GS) failed\n");
    if (arch_get(ARCH_GET_GS) == (uint64_t)gs_data && read_gs(1) == gs_data[1])
        print("arch_prctl gs base ok\n");
    /* A kernel address is rejected. */
    if (syscall(SYS_arch_prctl, ARCH_SET_GS, 0xffff880000000000ULL) == -1 &&
        errno == EPERM && arch_get(ARCH_GET_GS) == (uint64_t)gs_data)
        print("arch_prctl bad gs base ok\n");
    if (syscall(SYS_arch_prctl, ARCH_SET_GS, orig_gs) != 0)
        print("arch_prctl(ARCH_SET_GS) failed\n");
}

static uint64_t
rdfsbase(void)
{
    uint64_t base;
    __asm__ __volatile__("rdfsbase %0" : "=r"(base));
    return base;
}

static uint64_t
rdgsbase(void)
{
    uint64_t base;
    __asm__ __volatile__("rdgsbase %0" : "=r"(base));
    return base;
}

static uint32_t
rdgsbase32(void)
{
    uint32_t base;
    __asm__ __volatile__("rdgsbase %0" : "=r"(base));
    return base;
}

static void
wrfsbase(uint64_t base)
{
    __asm__ __volatile__("wrfsbase %0" : : "r"(base) : "memory");
}

static void
wrgsbase(uint64_t base)
{
    __asm__ __volatile__("wrgsbase %0" : : "r"(base) : "memory");
}

static void
wrgsbase32(uint32_t base)
{
    __asm__ __volatile__("wrgsbase %0" : : "r"(base) : "memory");
}

static void
test_fsgsbase(void)
{
    uint64_t fs_base, orig_gs;
    if ((getauxval(AT_HWCAP2) & HWCAP2_FSGSBASE) == 0) {
        /* The instructions are not available: we print the same output. */
        print("fs base ok\ngs base ok\n32-bit forms ok\n");
        return;
    }

    /* The fs base is libc's TLS. */
    fs_base = rdfsbase();
    if (fs_base != arch_get(ARCH_GET_FS) || fs_base == 0)
        print("rdfsbase mismatch\n");
    /* Write it back: the application must keep working. */
    wrfsbase(fs_base);
    if (rdfsbase() == fs_base && arch_get(ARCH_GET_FS) == fs_base)
        print("fs base ok\n");

    orig_gs = rdgsbase();
    if (orig_gs != arch_get(ARCH_GET_GS))
        print("rdgsbase mismatch\n");
    wrgsbase((uint64_t)gs_data);
    /* The system call, memory references and rdgsbase all see the new base. */
    if (arch_get(ARCH_GET_GS) == (uint64_t)gs_data && read_gs(0) == gs_data[0] &&
        rdgsbase() == (uint64_t)gs_data)
        print("gs base ok\n");

    /* The 32-bit forms: rdgsbase reads the low half, and wrgsbase clears the upper
     * half of the base.
     */
    wrgsbase(0x00007def12345678ULL);
    if (rdgsbase32() == 0x12345678) {
        wrgsbase32(0x87654321);
        if (rdgsbase() == 0x87654321)
            print("32-bit forms ok\n");
    }
    wrgsbase(orig_gs);
}

int
main(int argc, char **argv)
{
    test_arch_prctl();
    test_fsgsbase();
    print("all done\n");
    return 0;
}
