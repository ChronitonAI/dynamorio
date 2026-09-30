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

/* Tests drreg_set_enabled(): no register or flags can be reserved in the blocks built
 * while drreg is disabled, and they can in the blocks built after it is enabled again.
 * The client clobbers every register and the flags it reserves, so drreg must have
 * restored the application's values where it processed a block.  It switches the state
 * (and flushes all code, as the state must not change for a block that exists) in every
 * pre-syscall event.
 */

#include "dr_api.h"
#include "client_tools.h"
#include "drmgr.h"
#include "drreg.h"

static bool drreg_enabled = true;
static int switches;
/* Per state (0: disabled, 1: enabled): successful and refused reservations. */
static int reserved[2];
static int refused[2];
/* Reservations after drreg was disabled at least once. */
static int reserved_again;

static void
clean_call(void)
{
}

static dr_emit_flags_t
event_bb_insert(void *drcontext, void *tag, instrlist_t *bb, instr_t *instr,
                bool for_trace, bool translating, void *user_data)
{
    if (!instr_is_app(instr))
        return DR_EMIT_DEFAULT;
    reg_id_t reg;
    drreg_status_t res_flags = drreg_reserve_aflags(drcontext, bb, instr);
    drreg_status_t res_reg = drreg_reserve_register(drcontext, bb, instr, NULL, &reg);
    if (res_flags == DRREG_SUCCESS && res_reg == DRREG_SUCCESS) {
        if (!translating) {
            reserved[drreg_enabled]++;
            if (switches > 1)
                reserved_again++;
        }
        /* Clobber the register and the flags. */
        instrlist_insert_mov_immed_ptrsz(drcontext, (ptr_int_t)0xbadcafe,
                                         opnd_create_reg(reg), bb, instr, NULL, NULL);
#ifdef X86
        instrlist_meta_preinsert(
            bb, instr,
            INSTR_CREATE_add(drcontext, opnd_create_reg(reg), OPND_CREATE_INT8(1)));
#endif
        CHECK(drreg_unreserve_register(drcontext, bb, instr, reg) == DRREG_SUCCESS,
              "failed to unreserve the register");
        CHECK(drreg_unreserve_aflags(drcontext, bb, instr) == DRREG_SUCCESS,
              "failed to unreserve the flags");
    } else {
        CHECK(res_flags == DRREG_ERROR_FEATURE_NOT_AVAILABLE &&
                  res_reg == DRREG_ERROR_FEATURE_NOT_AVAILABLE,
              "reservations should fail only while drreg is disabled");
        if (!translating)
            refused[drreg_enabled]++;
    }
    if (drmgr_is_first_instr(drcontext, instr)) {
        /* drreg must leave clean calls in disabled blocks alone. */
        dr_insert_clean_call_ex(drcontext, bb, instr, (void *)clean_call,
                                DR_CLEANCALL_READS_APP_CONTEXT, 0);
    }
    return DR_EMIT_DEFAULT;
}

static bool
event_filter_syscall(void *drcontext, int sysnum)
{
    return true;
}

static bool
event_pre_syscall(void *drcontext, int sysnum)
{
    /* Switch the state, flushing all code built in the other state. */
    drreg_enabled = !drreg_enabled;
    switches++;
    CHECK(drreg_set_enabled(drreg_enabled) == DRREG_SUCCESS, "drreg_set_enabled failed");
    CHECK(dr_flush_region((app_pc)0, ~(size_t)0), "flush failed");
    return true;
}

static void
event_exit(void)
{
    CHECK(switches > 2, "too few syscalls to test");
    CHECK(reserved[0] == 0 && refused[0] > 0, "reservations should fail while disabled");
    CHECK(reserved[1] > 0 && refused[1] == 0, "reservations should work while enabled");
    CHECK(reserved_again > 0, "reservations should work after drreg is enabled again");
    CHECK(drreg_exit() == DRREG_SUCCESS, "drreg exit failed");
    drmgr_exit();
}

DR_EXPORT void
dr_init(client_id_t id)
{
    drreg_options_t ops = { sizeof(ops), 1 /*max slots needed*/, false };
    drmgr_init();
    CHECK(drreg_set_enabled(false) == DRREG_ERROR,
          "drreg_set_enabled should fail before drreg_init");
    CHECK(drreg_init(&ops) == DRREG_SUCCESS, "drreg init failed");
    CHECK(drmgr_register_bb_instrumentation_event(NULL, event_bb_insert, NULL),
          "failed to register the bb event");
    CHECK(drmgr_register_filter_syscall_event(event_filter_syscall),
          "failed to register the syscall filter");
    CHECK(drmgr_register_pre_syscall_event(event_pre_syscall),
          "failed to register the pre-syscall event");
    drmgr_register_exit_event(event_exit);
}
