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

/* Tests dr_futex_wait_at_safe_spot(): see futex_wait_safe_spot.c. */

#include "dr_api.h"
#include "client_tools.h"
#include "futex_wait_safe_spot-shared.h"

#include <signal.h>
#include <sys/syscall.h>

/* Without the safe spot, a flush waits for the waiting thread for tens of seconds
 * before giving up.
 */
#define MAX_FLUSH_MS 10000

static const char *const phase_names[NUM_PHASES] = { "a clean call",
                                                     "a system call event",
                                                     "a signal event" };

/* The futexes to wait on at the next getpid system call or SIGILL. */
static volatile int *syscall_futex;
static volatile int *signal_futex;
/* Set by the waiting thread just before it waits, where it waits. */
static int waiting_phase = -1;
static app_pc waiting_pc;
/* The number of phases for which the other thread flushed. */
static int flushed_phases;

static void
wait_on(void *drcontext, int phase, volatile int *futex, dr_mcontext_t *mc)
{
    waiting_pc = mc->pc;
    dr_atomic_store32(&waiting_phase, phase);
    if (!dr_futex_wait_at_safe_spot(drcontext, futex, 0, mc))
        dr_fprintf(STDERR, "dr_futex_wait_at_safe_spot failed\n");
}

static void
flush_done(void *user_data)
{
}

static void
at_marker(uint kind, app_pc pc)
{
    void *drcontext = dr_get_current_drcontext();
    dr_mcontext_t mc = { sizeof(mc), DR_MC_ALL };
    dr_get_mcontext(drcontext, &mc);
    mc.pc = pc;
    switch (kind) {
    case MARKER_WAIT_IN_CLEAN_CALL: {
        volatile int *futex = (volatile int *)mc.xdx;
        /* After the redirect below, we come back here with the gate open. */
        if (*futex != 0)
            return;
        wait_on(drcontext, PHASE_CLEAN_CALL, futex, &mc);
        /* The code cache may have been flushed while we waited. */
        dr_redirect_execution(&mc);
        CHECK(false, "should not be reached");
        break;
    }
    case MARKER_WAIT_IN_SYSCALL: syscall_futex = (volatile int *)mc.xdx; break;
    case MARKER_WAIT_IN_SIGNAL: signal_futex = (volatile int *)mc.xdx; break;
    case MARKER_FLUSH: {
        int phase = (int)mc.xdx;
        uint64 start, elapsed;
        /* After the redirect below, we come back here. */
        if (flushed_phases > phase)
            return;
        while (dr_atomic_load32(&waiting_phase) != phase)
            dr_sleep(1);
        start = dr_get_milliseconds();
        /* A synchronous flush of the waiting thread's code. */
        if (!dr_flush_region_ex(waiting_pc, 1, flush_done, NULL))
            dr_fprintf(STDERR, "dr_flush_region_ex failed\n");
        elapsed = dr_get_milliseconds() - start;
        if (elapsed < MAX_FLUSH_MS) {
            dr_fprintf(STDERR, "flush while waiting in %s: done\n", phase_names[phase]);
        } else {
            dr_fprintf(STDERR, "flush while waiting in %s: took %d ms\n",
                       phase_names[phase], (int)elapsed);
        }
        flushed_phases = phase + 1;
        dr_redirect_execution(&mc);
        CHECK(false, "should not be reached");
        break;
    }
    }
}

static dr_emit_flags_t
event_bb(void *drcontext, void *tag, instrlist_t *bb, bool for_trace, bool translating)
{
    instr_t *instr;
    for (instr = instrlist_first_app(bb); instr != NULL;
         instr = instr_get_next_app(instr)) {
        ptr_int_t kind;
        if (instr_get_opcode(instr) == OP_mov_imm &&
            opnd_is_reg(instr_get_dst(instr, 0)) &&
            opnd_get_reg(instr_get_dst(instr, 0)) == DR_REG_EAX &&
            opnd_is_immed_int(instr_get_src(instr, 0))) {
            kind = opnd_get_immed_int(instr_get_src(instr, 0));
            if (kind >= MARKER_WAIT_IN_CLEAN_CALL && kind <= MARKER_FLUSH) {
                dr_insert_clean_call(drcontext, bb, instr, (void *)at_marker,
                                     false /*fpstate*/, 2, OPND_CREATE_INT32(kind),
                                     OPND_CREATE_INTPTR(instr_get_app_pc(instr)));
            }
        }
    }
    return DR_EMIT_DEFAULT;
}

static bool
event_filter_syscall(void *drcontext, int sysnum)
{
    return sysnum == SYS_getpid;
}

static bool
event_pre_syscall(void *drcontext, int sysnum)
{
    if (sysnum == SYS_getpid && syscall_futex != NULL) {
        volatile int *futex = syscall_futex;
        dr_mcontext_t mc = { sizeof(mc), DR_MC_ALL };
        syscall_futex = NULL;
        dr_get_mcontext(drcontext, &mc);
        wait_on(drcontext, PHASE_SYSCALL, futex, &mc);
    }
    return true;
}

static dr_signal_action_t
event_signal(void *drcontext, dr_siginfo_t *info)
{
    if (info->sig == SIGILL && signal_futex != NULL) {
        volatile int *futex = signal_futex;
        signal_futex = NULL;
        wait_on(drcontext, PHASE_SIGNAL, futex, info->mcontext);
        /* Skip the ud2.  We must not resume at the interrupted cache pc, which
         * may have been flushed.
         */
        info->mcontext->pc += 2;
        return DR_SIGNAL_REDIRECT;
    }
    return DR_SIGNAL_DELIVER;
}

DR_EXPORT void
dr_client_main(client_id_t id, int argc, const char *argv[])
{
    dr_register_bb_event(event_bb);
    dr_register_filter_syscall_event(event_filter_syscall);
    dr_register_pre_syscall_event(event_pre_syscall);
    dr_register_signal_event(event_signal);
}
