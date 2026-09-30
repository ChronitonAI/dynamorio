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

/* Shared between fragment_variants.c and fragment_variants.dll.c. */

#ifndef FRAGMENT_VARIANTS_SHARED_H
#define FRAGMENT_VARIANTS_SHARED_H

/* The app calls client_request(op, arg), which the client intercepts. */
enum {
    /* Returns the number of fragment variants. */
    REQ_NUM_VARIANTS = 1,
    /* Selects variant arg and redirects: the call returns in variant arg. */
    REQ_SELECT = 2,
    /* Selects variant arg and returns normally, so the switch happens only at the
     * thread's next cache exit.
     */
    REQ_SELECT_LAZY = 3,
    /* Returns the thread's current variant. */
    REQ_CURRENT_VARIANT = 4,
    /* Flushes which_variant() and spin_loop() with dr_flush_region(). */
    REQ_FLUSH = 5,
    /* Requests a flush of which_variant() and spin_loop() with
     * dr_delay_flush_region(), or flushes them with dr_unlink_flush_region() if
     * the client has the -unlink_flush option.
     */
    REQ_DELAY_FLUSH = 6,
    /* Returns how many times the block at which_variant() was built in variant arg
     * (not counting translations).
     */
    REQ_BUILDS = 7,
    /* Returns the number of blocks that executed in a variant other than the
     * thread's current one, plus the number of events that reported a wrong
     * variant (should be 0).
     */
    REQ_MISMATCHES = 8,
    /* Returns how many times a block at which_variant() of variant arg was
     * deleted.
     */
    REQ_DELETIONS = 9,
    /* Makes the client select variant arg in the pre-system call event of the
     * thread's next getpid system call, and return normally.
     */
    REQ_SELECT_AT_PRE_SYSCALL = 10,
    /* Like REQ_SELECT_AT_PRE_SYSCALL, in the post-system call event. */
    REQ_SELECT_AT_POST_SYSCALL = 11,
};

/* The value spin_loop() and fault_func() keep in rbx. */
#define APP_RBX_VALUE 0x1234
/* The values that the instrumentation in variants 1 and 2 writes to rbx around
 * one app instruction (after spilling rbx).
 */
#define CLOBBER_RBX_V1 0xbadc0ffee0ddf00dULL
#define CLOBBER_RBX_V2 0xfeedfacecafebeefULL

#endif /* FRAGMENT_VARIANTS_SHARED_H */
