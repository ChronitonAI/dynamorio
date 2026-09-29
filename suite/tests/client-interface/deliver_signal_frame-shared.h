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

#ifndef DELIVER_SIGNAL_FRAME_SHARED_H
#define DELIVER_SIGNAL_FRAME_SHARED_H

/* The app marks the points where the client acts with "mov $MARKER_*, %eax",
 * preceded by "mov <request address>, %rdx".
 */
/* Deliver the requested signal from a clean call. */
#define MARKER_DELIVER 0x7eca0201
/* Deliver the requested signal from the signal event for the next SIGILL. */
#define MARKER_DELIVER_AT_SIGILL 0x7eca0202

/* What the client needs to know to build a signal frame like the kernel. */
typedef struct {
    int sig;
    void *handler;
    void *restorer;
    /* The application's current signal mask and alternate stack, which the frame
     * saves.
     */
    unsigned long long blocked;
    stack_t altstack;
    /* Whether the frame goes on the alternate stack. */
    int on_altstack;
} deliver_request_t;

#endif /* DELIVER_SIGNAL_FRAME_SHARED_H */
