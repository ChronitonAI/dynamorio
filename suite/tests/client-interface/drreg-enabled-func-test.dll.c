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

/* Tests drreg_set_enabled_func(): registers and flags can be reserved in exactly the
 * blocks for which the client's function returns true, which it decides for each block
 * (here from its tag) with no flush.  The client clobbers every register and the flags
 * it reserves, so drreg must have restored the application's values where it processed
 * a block.
 */

#include "dr_api.h"
#include "client_tools.h"
#include "drmgr.h"
#include "drreg.h"

/* Per decision (0: left alone, 1: processed): successful and refused reservations. */
static int reserved[2];
static int refused[2];

static bool
decide(void *tag)
{
    return (((ptr_uint_t)tag) & 0x10) != 0;
}

static bool
enabled_func(void *drcontext, void *tag, bool for_trace, bool translating)
{
    return decide(tag);
}

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
        if (!translating)
            reserved[decide(tag)]++;
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
              "reservations should fail only where drreg leaves the block alone");
        if (!translating)
            refused[decide(tag)]++;
    }
    if (drmgr_is_first_instr(drcontext, instr)) {
        /* drreg must not touch clean calls in the blocks it leaves alone. */
        dr_insert_clean_call_ex(drcontext, bb, instr, (void *)clean_call,
                                DR_CLEANCALL_READS_APP_CONTEXT, 0);
    }
    return DR_EMIT_DEFAULT;
}

static void
event_exit(void)
{
    CHECK(reserved[0] == 0 && refused[0] > 0,
          "reservations should fail in the blocks the function refuses");
    CHECK(reserved[1] > 0 && refused[1] == 0,
          "reservations should work in the blocks the function accepts");
    CHECK(drreg_set_enabled_func(NULL) == DRREG_SUCCESS, "drreg_set_enabled_func failed");
    CHECK(drreg_exit() == DRREG_SUCCESS, "drreg exit failed");
    CHECK(drreg_set_enabled_func(enabled_func) == DRREG_ERROR,
          "drreg_set_enabled_func should fail after drreg_exit");
    drmgr_exit();
}

DR_EXPORT void
dr_init(client_id_t id)
{
    drreg_options_t ops = { sizeof(ops), 1 /*max slots needed*/, false };
    drmgr_init();
    CHECK(drreg_set_enabled_func(enabled_func) == DRREG_ERROR,
          "drreg_set_enabled_func should fail before drreg_init");
    CHECK(drreg_init(&ops) == DRREG_SUCCESS, "drreg init failed");
    CHECK(drreg_set_enabled_func(enabled_func) == DRREG_SUCCESS,
          "drreg_set_enabled_func failed");
    CHECK(drmgr_register_bb_instrumentation_event(NULL, event_bb_insert, NULL),
          "failed to register the bb event");
    drmgr_register_exit_event(event_exit);
}
