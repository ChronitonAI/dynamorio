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

/* Shared by flush_exact.c and flush_exact.dll.c. */

#ifndef FLUSH_EXACT_H
#define FLUSH_EXACT_H

/* The application asks the client to act with "mov $MARKER_*, %eax" preceded by
 * "mov <address>, %rdx" and "mov <size>, %rsi".
 */
#define MARKER_SETUP 0x7eca0401        /* the code buffer is at <address> */
#define MARKER_REPORT 0x7eca0402       /* print the traces built since the last report */
#define MARKER_FLUSH_TAG 0x7eca0403    /* dr_unlink_flush_fragment(<address>) */
#define MARKER_FLUSH_EXACT 0x7eca0404  /* ..._region_ex(<address>, <size>, EXACT) */
#define MARKER_FLUSH_REGION 0x7eca0405 /* dr_unlink_flush_region(<address>, <size>) */
#define MARKER_DELAY_EXACT 0x7eca0406  /* dr_delay_flush_region_ex(...EXACT) */
#define MARKER_SELF_FLUSH 0x7eca0407   /* the block at <address> flushes itself once */
/* As MARKER_FLUSH_TAG if <size> is 0, else as MARKER_FLUSH_EXACT, without checks. */
#define MARKER_FLUSH_RUNNING 0x7eca0408

/* The code buffer has three pages.  The first two are read-only and the third is
 * writable, so that the two parts are separate areas of code for DR.  The blocks, as
 * offsets into the buffer (pg is the page size), and their code:
 */
#define OFFS_A 0x000                  /* mov $1, %eax; ret */
#define OFFS_B 0x040                  /* mov $2, %eax; ret */
#define OFFS_C1 0x080                 /* mov $3, %eax; jmp C2 */
#define OFFS_C2 0x087                 /* add $0x10, %eax; ret */
#define OFFS_D 0x100                  /* 100 nops; mov $4, %eax; ret */
#define OFFS_D_LAST_NOP 0x163         /* (the last nop) */
#define OFFS_W 0x180                  /* mov $9, %eax; ret */
#define OFFS_E 0x200                  /* mov $5, %eax; ret */
#define OFFS_T 0x300                  /* mov $200, %ecx */
#define OFFS_L 0x305                  /* L: dec %ecx; jnz L */
#define OFFS_L_END 0x309              /* mov $6, %eax; ret */
#define OFFS_X(pg) (2 * (pg) - 3)     /* nop; nop; nop; (next page) mov $7, %eax; ret */
#define OFFS_Y(pg) (2 * (pg) + 0x100) /* mov $8, %eax; ret */

#endif /* FLUSH_EXACT_H */
