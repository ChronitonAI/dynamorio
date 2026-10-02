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

/* Calls dr_sandbox_app_memory() for the application: see sandbox_app_memory.c. */

#include "dr_api.h"
#include "client_tools.h"
#include "sandbox_app_memory.h"

static bool
event_filter_syscall(void *drcontext, int sysnum)
{
    return sysnum == SANDBOX_SYSCALL;
}

static bool
event_pre_syscall(void *drcontext, int sysnum)
{
    if (sysnum != SANDBOX_SYSCALL)
        return true;
    app_pc start = (app_pc)dr_syscall_get_param(drcontext, 0);
    size_t size = (size_t)dr_syscall_get_param(drcontext, 1);
    bool sandbox = dr_syscall_get_param(drcontext, 2) != 0;
    bool ok;
    CHECK(!dr_sandbox_app_memory(start, 0, sandbox), "empty range not refused");
    ok = dr_sandbox_app_memory(start, size, sandbox);
    CHECK(ok, "dr_sandbox_app_memory failed");
    dr_syscall_set_result(drcontext, ok ? 0 : -1);
    return false; /* skip the system call */
}

DR_EXPORT void
dr_client_main(client_id_t id, int argc, const char *argv[])
{
    dr_register_filter_syscall_event(event_filter_syscall);
    dr_register_pre_syscall_event(event_pre_syscall);
}
