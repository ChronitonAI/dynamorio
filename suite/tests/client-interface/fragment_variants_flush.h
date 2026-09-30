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

/* Shared by fragment_variants_flush.c and fragment_variants_flush.dll.c. */

#ifndef FRAGMENT_VARIANTS_FLUSH_H
#define FRAGMENT_VARIANTS_FLUSH_H

/* The requests of client_request(op, arg1, arg2).  Those that flush flush the block
 * at arg1 (A) and return 1 on success.
 */
enum {
    /* Returns the number of fragment variants. */
    REQ_NUM_VARIANTS = 1,
    /* Selects variant arg1 and redirects: the call returns in variant arg1. */
    REQ_SELECT = 2,
    /* Returns the thread's current variant. */
    REQ_CURRENT_VARIANT = 3,
    /* Returns whether a fragment with tag arg1 exists in the current variant. */
    REQ_EXISTS = 4,
    /* Returns how many times the block at arg1 was built in variant arg2. */
    REQ_BUILDS = 5,
    /* dr_unlink_flush_fragment(). */
    REQ_FLUSH_TAG = 6,
    /* dr_unlink_flush_region_ex() with DR_FLUSH_EXACT. */
    REQ_FLUSH_EXACT = 7,
    /* dr_delay_flush_region_ex() with DR_FLUSH_EXACT. */
    REQ_DELAY_EXACT = 8,
    /* dr_app_memory_changed(), with the memory's protection if arg2 is 0, else with
     * another one.
     */
    REQ_MEMORY_CHANGED = 9,
    /* dr_unlink_flush_region(). */
    REQ_FLUSH_REGION = 10,
    /* dr_flush_region(). */
    REQ_FLUSH_SYNCHALL = 11,
};

#endif /* FRAGMENT_VARIANTS_FLUSH_H */
