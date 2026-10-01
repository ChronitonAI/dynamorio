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

#ifndef FUTEX_WAIT_SAFE_SPOT_SHARED_H
#define FUTEX_WAIT_SAFE_SPOT_SHARED_H

/* The app marks the points where the client acts with "mov $MARKER_*, %eax",
 * preceded by "mov <arg>, %rdx".
 */
#define MARKER_WAIT_IN_CLEAN_CALL 0x7eca0001
#define MARKER_WAIT_IN_SYSCALL 0x7eca0002
#define MARKER_WAIT_IN_SIGNAL 0x7eca0003
#define MARKER_WAIT_IN_DECODE_SIGNAL 0x7eca0004
#define MARKER_FLUSH 0x7eca0005

enum {
    PHASE_CLEAN_CALL,
    PHASE_SYSCALL,
    PHASE_SIGNAL,
    PHASE_DECODE_SIGNAL,
    NUM_PHASES,
};

#endif /* FUTEX_WAIT_SAFE_SPOT_SHARED_H */
