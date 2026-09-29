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

/* Tests dr_app_memory_changed(): the app changes its code through /proc/self/mem,
 * which DR does not notice, and the client changes a page's protection with a raw
 * system call.  See app_memory_changed.dll.c.
 */

#include "tools.h"

#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

/* The client acts on "mov $MARKER_*, %eax" preceded by "mov <arg>, %rdx". */
#define MARKER_MEMORY_CHANGED 0x7eca0301
#define MARKER(kind, arg)                              \
    __asm__ __volatile__("mov %0, %%rdx\n\t"           \
                         "mov %1, %%eax"               \
                         :                             \
                         : "r"((long)(arg)), "i"(kind) \
                         : "rdx", "rax", "memory")

typedef int (*func_t)(void);

int
main(int argc, char **argv)
{
    /* mov $1, %eax; ret */
    static const unsigned char return_1[] = { 0xb8, 0x01, 0x00, 0x00, 0x00, 0xc3 };
    static const unsigned char return_2[] = { 0xb8, 0x02, 0x00, 0x00, 0x00, 0xc3 };
    int fd, result;
    /* A code page followed by a data page. */
    byte *code = mmap(NULL, 2 * PAGE_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (code == MAP_FAILED) {
        print("mmap failed\n");
        return 1;
    }
    memcpy(code, return_1, sizeof(return_1));
    result = ((func_t)code)();
    result += ((func_t)code)();
    print("before the change: %d\n", result);
    /* Writes through /proc/self/mem bypass page protections, and with them DR's
     * detection of code changes.
     */
    fd = open("/proc/self/mem", O_RDWR);
    if (fd < 0 || pwrite(fd, return_2, sizeof(return_2), (off_t)code) != sizeof(return_2))
        print("writing /proc/self/mem failed\n");
    close(fd);
    /* The client makes the data page read-only behind DR's back and then tells DR
     * about both pages.
     */
    MARKER(MARKER_MEMORY_CHANGED, code);
    print("after the change: %d\n", ((func_t)code)());
    /* DR must still notice ordinary changes to the code. */
    code[1] = 3;
    print("after writing the code: %d\n", ((func_t)code)());
    print("all done\n");
    return 0;
}
