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
 * * Neither the name of Google, Inc. nor the names of its contributors may be
 *   used to endorse or promote products derived from this software without
 *   specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL VMWARE, INC. OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 */


/* AVX-512 state the application sets without an AVX-512 instruction (XRSTOR), so that
 * DR still saves and restores only the SSE/AVX state, must survive DR's switch to
 * preserving the AVX-512 state when it first decodes an AVX-512 instruction: a context
 * switch whose save precedes the switch (here, around the syscall) must not be restored
 * as if it had saved zmm16-31 and k0-7.  No libc: its startup code could run AVX-512
 * instructions first.
 */

.text
.globl _start
.type _start, @function

        .align   8
_start:
        and      rsp, -16
        // XSAVE x87, SSE, AVX, opmask, ZMM_Hi256 and Hi16_ZMM into img (standard format).
        mov      eax, 0xe7
        xor      edx, edx
        lea      rdi, img
        xsave64  [rdi]
        // zmm16 (Hi16_ZMM, component 7) and k1 (opmask, component 5) in the image.
        mov      eax, 0xd
        mov      ecx, 7
        cpuid
        mov      rax, 0x1122334455667788
        mov      QWORD PTR [rdi + rbx], rax
        mov      eax, 0xd
        mov      ecx, 5
        cpuid
        mov      QWORD PTR [rdi + rbx + 8], 0xa5a5
        or       QWORD PTR [rdi + 512], 0xa0
        mov      eax, 0xe7
        xor      edx, edx
        xrstor64 [rdi]
        // A context switch before DR preserves any AVX-512 state.
        mov      eax, 39          // SYS_getpid
        syscall
        jmp      evex
evex:
        // The application's first AVX-512 instructions.
        vmovq    rax, xmm16
        kmovq    rbx, k1
        mov      rcx, 0x1122334455667788
        lea      rsi, zmm16_ok
        lea      rdx, zmm16_bad
        cmp      rax, rcx
        cmovne   rsi, rdx
        mov      r12, rbx
        call     print_line
        lea      rsi, k1_ok
        lea      rdx, k1_bad
        cmp      r12, 0xa5a5
        cmovne   rsi, rdx
        call     print_line
        mov      rdi, 0           // exit code
        mov      eax, 231         // SYS_exit_group
        syscall

        // Writes the 16-byte line at rsi to stdout.
print_line:
        mov      edi, 1           // stdout
        mov      edx, 16
        mov      eax, 1           // SYS_write
        syscall
        ret

        .data
        .align   64
img:
        .zero    4096
zmm16_ok:
        .ascii   "zmm16 preserved\n"
zmm16_bad:
        .ascii   "zmm16 clobbered\n"
k1_ok:
        .ascii   "k1 preserved   \n"
k1_bad:
        .ascii   "k1 clobbered   \n"
