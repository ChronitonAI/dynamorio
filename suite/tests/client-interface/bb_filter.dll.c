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

/* Tests the basic block filter event: see bb_filter.c.  The filter passes to the
 * basic block event only the blocks whose code contains the marker instruction
 * (nopl 0x12345678(%rax)), and the basic block event inserts a clean call before
 * each marker.  The basic block event checks that it sees no other blocks, and the
 * filter that it gets a range of whole instructions.
 */

#include "dr_api.h"
#include "client_tools.h"

#define MARKER_DISP 0x12345678

static int markers_executed;
static int filter_calls, filter_passed, filter_translating, filter_for_trace;
static int bb_calls, bb_translating;
static void *count_lock;

static bool
is_marker(instr_t *instr)
{
    opnd_t src;
    if (instr_get_opcode(instr) != OP_nop_modrm)
        return false;
    src = instr_get_src(instr, 0);
    return opnd_is_base_disp(src) && opnd_get_disp(src) == MARKER_DISP;
}

static bool
event_bb_filter(void *drcontext, void *tag, app_pc end, bool for_trace, bool translating)
{
    byte *pc = dr_fragment_app_pc(tag);
    bool has_marker = false;
    instr_t instr;
    instr_init(drcontext, &instr);
    while (pc < end) {
        instr_reset(drcontext, &instr);
        pc = decode(drcontext, pc, &instr);
        if (pc == NULL || pc > end) {
            dr_fprintf(STDERR, "filter: bad range " PFX "-" PFX "\n", tag, end);
            break;
        }
        if (is_marker(&instr))
            has_marker = true;
    }
    instr_free(drcontext, &instr);
    dr_mutex_lock(count_lock);
    filter_calls++;
    if (has_marker)
        filter_passed++;
    if (translating)
        filter_translating++;
    if (for_trace)
        filter_for_trace++;
    dr_mutex_unlock(count_lock);
    return has_marker;
}

static void
at_marker(void)
{
    dr_mutex_lock(count_lock);
    markers_executed++;
    dr_mutex_unlock(count_lock);
}

static dr_emit_flags_t
event_bb(void *drcontext, void *tag, instrlist_t *bb, bool for_trace, bool translating)
{
    instr_t *instr;
    bool has_marker = false;
    for (instr = instrlist_first_app(bb); instr != NULL;
         instr = instr_get_next_app(instr)) {
        if (is_marker(instr)) {
            has_marker = true;
            dr_insert_clean_call(drcontext, bb, instr, (void *)at_marker, false, 0);
        }
    }
    if (!has_marker)
        dr_fprintf(STDERR, "bb event for a block without the marker: " PFX "\n", tag);
    dr_mutex_lock(count_lock);
    bb_calls++;
    if (translating)
        bb_translating++;
    dr_mutex_unlock(count_lock);
    return DR_EMIT_DEFAULT;
}

static void
event_exit(void)
{
    dr_fprintf(STDERR, "markers executed: %d\n", markers_executed);
    /* The blocks with the marker, which fault translation builds again. */
    dr_fprintf(STDERR, "bb event: %s, when translating: %s\n",
               bb_calls >= 5 ? "called" : "not called",
               bb_translating >= 1 ? "called" : "not called");
    /* Every block DR built, and the one of each fault. */
    dr_fprintf(STDERR, "filter: %s, passed some: %s, when translating: %s\n",
               filter_calls > bb_calls ? "called" : "not called",
               filter_passed == bb_calls ? "yes" : "no",
               filter_translating >= 2 ? "called" : "not called");
    /* The loops become traces, whose blocks DR builds again. */
    dr_fprintf(STDERR, "filter for traces: %s\n",
               filter_for_trace > 0 ? "called" : "not called");
    if (!dr_unregister_bb_filter_event(event_bb_filter) ||
        !dr_unregister_bb_event(event_bb))
        dr_fprintf(STDERR, "unregistering failed\n");
    dr_mutex_destroy(count_lock);
}

DR_EXPORT void
dr_init(client_id_t id)
{
    count_lock = dr_mutex_create();
    dr_register_bb_filter_event(event_bb_filter);
    dr_register_bb_event(event_bb);
    dr_register_exit_event(event_exit);
}
