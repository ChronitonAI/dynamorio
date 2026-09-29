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

/* Tests -vm_reserve_strict: exhausting the vmcode reservation must be a fatal
 * error rather than silently falling back to memory from the OS outside of the
 * reservation.  Also tests that -vm_max_offset accepts a plain "0" and places the
 * reservation exactly at -vm_base (these values must match the test's options).
 */

#include "dr_api.h"
#include "client_tools.h"

#define VM_BASE 0x100000000ULL
#define VM_SIZE (32 * 1024 * 1024)
#define CHUNK_SIZE (1024 * 1024)
#define MAX_CHUNKS 64 /* More than VM_SIZE in total. */

DR_EXPORT void
dr_client_main(client_id_t id, int argc, const char *argv[])
{
    int i;
    for (i = 0; i < MAX_CHUNKS; i++) {
        byte *p = dr_nonheap_alloc(CHUNK_SIZE, DR_MEMPROT_READ | DR_MEMPROT_WRITE);
        /* We should die before any allocation lands outside the reservation. */
        if (p == NULL || p < (byte *)VM_BASE ||
            p + CHUNK_SIZE > (byte *)VM_BASE + VM_SIZE)
            dr_fprintf(STDERR, "allocation %d at " PFX " is outside the reservation\n", i,
                       p);
        else if (i == 0)
            dr_fprintf(STDERR, "first allocation is inside the reservation\n");
    }
    dr_fprintf(STDERR, "should not get here\n");
}
