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

/* Tests dr_get_app_segment_base() and dr_set_app_segment_base(). */

#include "dr_api.h"
#include "client_tools.h"

#include <asm/prctl.h>
#include <sys/syscall.h>

#define MARKER_CHECK_FS 0x7eca0101
#define MARKER_SET_GS 0x7eca0102

static void
at_marker(uint kind, reg_t arg)
{
    void *drcontext = dr_get_current_drcontext();
    if (kind == MARKER_CHECK_FS) {
        void *base = dr_get_app_segment_base(drcontext, DR_SEG_FS);
        if (base == (void *)arg && base != dr_get_dr_segment_base(DR_SEG_FS))
            dr_fprintf(STDERR, "client: fs base matches the application's\n");
        else
            dr_fprintf(STDERR, "client: fs base %p != %p\n", base, (void *)arg);
        CHECK(dr_get_app_segment_base(drcontext, DR_SEG_ES) == NULL,
              "only fs and gs are supported");
    } else if (kind == MARKER_SET_GS) {
        if (!dr_set_app_segment_base(drcontext, DR_SEG_GS, (void *)arg))
            dr_fprintf(STDERR, "client: dr_set_app_segment_base failed\n");
        CHECK(dr_get_app_segment_base(drcontext, DR_SEG_GS) == (void *)arg,
              "gs base not set");
    }
}

static dr_emit_flags_t
event_bb(void *drcontext, void *tag, instrlist_t *bb, bool for_trace, bool translating)
{
    instr_t *instr;
    for (instr = instrlist_first_app(bb); instr != NULL;
         instr = instr_get_next_app(instr)) {
        if (instr_get_opcode(instr) == OP_mov_imm &&
            opnd_is_reg(instr_get_dst(instr, 0)) &&
            opnd_get_reg(instr_get_dst(instr, 0)) == DR_REG_EAX &&
            opnd_is_immed_int(instr_get_src(instr, 0))) {
            ptr_int_t kind = opnd_get_immed_int(instr_get_src(instr, 0));
            if (kind == MARKER_CHECK_FS || kind == MARKER_SET_GS) {
                dr_insert_clean_call(drcontext, bb, instr, (void *)at_marker,
                                     false /*fpstate*/, 2, OPND_CREATE_INT32(kind),
                                     opnd_create_reg(DR_REG_XDX));
            }
        }
    }
    return DR_EMIT_DEFAULT;
}

static reg_t set_gs_base;

static bool
event_filter_syscall(void *drcontext, int sysnum)
{
    return sysnum == SYS_arch_prctl;
}

static bool
event_pre_syscall(void *drcontext, int sysnum)
{
    if (dr_syscall_get_param(drcontext, 0) == ARCH_SET_GS)
        set_gs_base = dr_syscall_get_param(drcontext, 1);
    else
        set_gs_base = 0;
    return true;
}

static void
event_post_syscall(void *drcontext, int sysnum)
{
    static bool reported;
    if (set_gs_base != 0 && !reported &&
        dr_get_app_segment_base(drcontext, DR_SEG_GS) == (void *)set_gs_base) {
        dr_fprintf(STDERR, "client: sees the gs base the application set\n");
        reported = true;
    }
}

DR_EXPORT void
dr_client_main(client_id_t id, int argc, const char *argv[])
{
    dr_register_bb_event(event_bb);
    dr_register_filter_syscall_event(event_filter_syscall);
    dr_register_pre_syscall_event(event_pre_syscall);
    dr_register_post_syscall_event(event_post_syscall);
}
