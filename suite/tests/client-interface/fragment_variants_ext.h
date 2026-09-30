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

/* Shared by fragment_variants_ext.c and fragment_variants_ext.dll.c. */

#ifndef FRAGMENT_VARIANTS_EXT_H
#define FRAGMENT_VARIANTS_EXT_H

/* The requests of client_request(op, arg). */
enum {
    /* Returns the number of fragment variants. */
    REQ_NUM_VARIANTS = 1,
    /* Selects variant arg and redirects: the call returns in variant arg. */
    REQ_SELECT = 2,
    /* Returns the thread's current variant. */
    REQ_CURRENT_VARIANT = 3,
    /* Returns how many times the blocks of marked() and plain() were built (not
     * counting translations) in variant arg.
     */
    REQ_BUILDS = 4,
    /* Returns the number of checks that failed in the client. */
    REQ_ERRORS = 5,
};

/* The immediate of the marker instruction, "mov $MARKER, %eax", which makes the
 * client's bb filter pass a block in variant 0.  In variant 1 it passes all blocks.
 */
#define MARKER 0x7eca0501

/* The values that fault_func() and fault_func_marked() keep in callee-saved registers
 * at their faulting load, which they execute with the carry flag set.
 */
#define VAL_RBX 0x1111
#define VAL_R12 0x2222
#define VAL_R13 0x3333
#define VAL_R14 0x4444
#define VAL_R15 0x5555

#endif /* FRAGMENT_VARIANTS_EXT_H */
