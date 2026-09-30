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

/* Tests drbbdup_set_enabled_func(): drbbdup processes exactly the blocks for which the
 * client's function returns true, deciding for each block (here from its tag) with no
 * flush.
 */

#include "dr_api.h"
#include "client_tools.h"
#include "drmgr.h"
#include "drbbdup.h"

/* The runtime case: always the default one. */
static uintptr_t encode_val = 0;

/* Per decision (0: left alone, 1: processed): blocks built (seen by an app2app pass). */
static int built[2];
/* Calls of the set-up and instrumentation call-backs, and executions of drbbdup's
 * instrumentation.
 */
static int set_up_calls;
static int instrument_calls;
static int executions;
/* Call-backs or executions for blocks that drbbdup should have left alone. */
static int wrong;

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

static uintptr_t
set_up_bb_dups(void *drbbdup_ctx, void *drcontext, void *tag, instrlist_t *bb,
               bool *enable_dups, bool *enable_dynamic_handling, void *user_data)
{
    set_up_calls++;
    if (!decide(tag))
        wrong++;
    drbbdup_status_t res = drbbdup_register_case_encoding(drbbdup_ctx, 1);
    CHECK(res == DRBBDUP_SUCCESS, "failed to register case 1");
    *enable_dups = true;
    *enable_dynamic_handling = false;
    return 0;
}

static void
count_execution(void *tag)
{
    executions++;
    if (!decide(tag))
        wrong++;
}

static void
instrument_instr(void *drcontext, void *tag, instrlist_t *bb, instr_t *instr,
                 instr_t *where, uintptr_t encoding, void *user_data,
                 void *orig_analysis_data, void *analysis_data)
{
    bool is_start;
    instrument_calls++;
    if (!decide(tag))
        wrong++;
    drbbdup_status_t res = drbbdup_is_first_instr(drcontext, instr, &is_start);
    CHECK(res == DRBBDUP_SUCCESS, "failed to check whether instr is start");
    if (is_start && encoding == 0) {
        dr_insert_clean_call(drcontext, bb, where, (void *)count_execution, false, 1,
                             OPND_CREATE_INTPTR(tag));
    }
}

static dr_emit_flags_t
event_bb_app2app(void *drcontext, void *tag, instrlist_t *bb, bool for_trace,
                 bool translating)
{
    if (!translating)
        built[decide(tag)]++;
    return DR_EMIT_DEFAULT;
}

static void
event_exit(void)
{
    CHECK(built[0] > 0 && built[1] > 0, "blocks should be built with both decisions");
    CHECK(set_up_calls > 0 && instrument_calls > 0 && executions > 0,
          "drbbdup should process the blocks the function accepts");
    CHECK(wrong == 0, "drbbdup should leave the blocks the function refuses alone");
    CHECK(drbbdup_set_enabled_func(NULL) == DRBBDUP_SUCCESS,
          "drbbdup_set_enabled_func failed");
    drbbdup_status_t res = drbbdup_exit();
    CHECK(res == DRBBDUP_SUCCESS, "drbbdup exit failed");
    CHECK(drbbdup_set_enabled_func(enabled_func) == DRBBDUP_ERROR_NOT_INITIALIZED,
          "drbbdup_set_enabled_func should fail after drbbdup_exit");
    drmgr_exit();
}

DR_EXPORT void
dr_init(client_id_t id)
{
    drmgr_init();
    CHECK(drbbdup_set_enabled_func(enabled_func) == DRBBDUP_ERROR_NOT_INITIALIZED,
          "drbbdup_set_enabled_func should fail before drbbdup_init");

    drbbdup_options_t opts = { 0 };
    opts.struct_size = sizeof(drbbdup_options_t);
    opts.set_up_bb_dups = set_up_bb_dups;
    opts.instrument_instr = instrument_instr;
    opts.runtime_case_opnd = OPND_CREATE_ABSMEM(&encode_val, OPSZ_PTR);
    opts.atomic_load_encoding = false;
    opts.non_default_case_limit = 1;
    opts.never_enable_dynamic_handling = true;
    drbbdup_status_t res = drbbdup_init(&opts);
    CHECK(res == DRBBDUP_SUCCESS, "drbbdup init failed");
    CHECK(drbbdup_set_enabled_func(enabled_func) == DRBBDUP_SUCCESS,
          "drbbdup_set_enabled_func failed");

    CHECK(drmgr_register_bb_app2app_event(event_bb_app2app, NULL),
          "failed to register the bb event");
    drmgr_register_exit_event(event_exit);
}
