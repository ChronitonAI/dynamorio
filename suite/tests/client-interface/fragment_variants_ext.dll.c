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

/* Tests fragment variants with the basic block filter event, drbbdup, and drreg: see
 * fragment_variants_ext.c.  Variant 0 is a cheap mode: the filter passes only the
 * blocks with a marker (and the block of client_request()), which get a check of
 * their variant, and drbbdup and drreg leave all of its blocks alone.  In variant 1,
 * the filter passes every block, and drbbdup duplicates and drreg processes them:
 * drbbdup's default case checks the variant and clobbers a register and the flags
 * that drreg reserves around every application instruction, which drreg must restore
 * at faults.  The filter, the drbbdup and drreg decisions and the restore state and
 * signal events check the variants that DR reports.
 */

#include "dr_api.h"
#include "client_tools.h"
#include "drmgr.h"
#include "drreg.h"
#include "drbbdup.h"
#include "fragment_variants_ext.h"

#include <signal.h>

#define NUM_VARIANTS 2

static app_pc client_request_pc, marked_pc, plain_pc;
/* Builds of marked() and plain(), per variant, as the filter sees them. */
static int builds[NUM_VARIANTS];
/* Calls of the filter with translating set, per variant. */
static int filter_translations[NUM_VARIANTS];
/* Blocks that drbbdup set up, and checks of blocks executed, per variant. */
static int drbbdup_set_ups[NUM_VARIANTS];
static int checks_executed[NUM_VARIANTS];
/* Failed checks. */
static int errors;
/* drbbdup's runtime case: always the default one. */
static uintptr_t encode_val = 0;

static void
error(const char *what)
{
    dr_atomic_add32_return_sum(&errors, 1);
    dr_fprintf(STDERR, "client error: %s\n", what);
}

static app_pc
lookup(module_data_t *exe, const char *name)
{
    app_pc pc = (app_pc)dr_get_proc_address(exe->handle, name);
    CHECK(pc != NULL, "failed to find app function");
    return pc;
}

static void
return_from_request(void *drcontext, reg_t res)
{
    dr_mcontext_t mc = { sizeof(mc), DR_MC_ALL };
    dr_get_mcontext(drcontext, &mc);
    mc.pc = *(app_pc *)mc.xsp;
    mc.xsp += sizeof(app_pc);
    mc.xax = res;
    dr_redirect_execution(&mc);
    CHECK(false, "dr_redirect_execution returned");
}

static void
on_request(reg_t op, reg_t arg)
{
    void *drcontext = dr_get_current_drcontext();
    reg_t res = 0;
    switch ((int)op) {
    case REQ_NUM_VARIANTS: res = dr_get_num_fragment_variants(); break;
    case REQ_SELECT: res = dr_select_thread_fragment_variant(drcontext, (uint)arg); break;
    case REQ_CURRENT_VARIANT: res = dr_get_thread_fragment_variant(drcontext); break;
    case REQ_BUILDS:
        CHECK(arg < NUM_VARIANTS, "invalid variant");
        res = builds[arg];
        break;
    case REQ_ERRORS: res = errors; break;
    default: CHECK(false, "unknown request");
    }
    return_from_request(drcontext, res);
}

static void
check_block(uint variant)
{
    if (dr_get_thread_fragment_variant(dr_get_current_drcontext()) != variant)
        error("block executed outside its variant");
    dr_atomic_add32_return_sum(&checks_executed[variant], 1);
}

static bool
has_marker(app_pc start, app_pc end)
{
    app_pc pc;
    for (pc = start; pc + 5 <= end; pc++) {
        if (*pc == 0xb8 /* mov imm32 to eax */ && *(uint *)(pc + 1) == MARKER)
            return true;
    }
    return false;
}

static bool
event_bb_filter(void *drcontext, void *tag, app_pc end, bool for_trace, bool translating)
{
    uint variant = dr_get_fragment_variant(drcontext);
    if (variant >= NUM_VARIANTS) {
        error("invalid variant in the filter");
        return true;
    }
    if (!translating) {
        /* New blocks are built in the thread's current variant. */
        if (variant != dr_get_thread_fragment_variant(drcontext))
            error("wrong variant in the filter");
        if ((app_pc)tag == marked_pc || (app_pc)tag == plain_pc)
            dr_atomic_add32_return_sum(&builds[variant], 1);
    } else {
        /* Only faults are translated in this test, by the faulting thread, whose
         * current variant is that of the faulting block.
         */
        if (variant != dr_get_thread_fragment_variant(drcontext))
            error("wrong variant in the filter when translating");
        dr_atomic_add32_return_sum(&filter_translations[variant], 1);
    }
    return variant == 1 || (app_pc)tag == client_request_pc ||
        has_marker((app_pc)tag, end);
}

/* drbbdup and drreg process the blocks of variant 1 but that of client_request(). */
static bool
extensions_enabled(void *drcontext, void *tag, bool for_trace, bool translating)
{
    return dr_get_fragment_variant(drcontext) == 1 && (app_pc)tag != client_request_pc;
}

static uintptr_t
set_up_bb_dups(void *drbbdup_ctx, void *drcontext, void *tag, instrlist_t *bb,
               bool *enable_dups, bool *enable_dynamic_handling, void *user_data)
{
    uint variant = dr_get_fragment_variant(drcontext);
    if (variant != 1)
        error("drbbdup set up a block outside variant 1");
    else
        dr_atomic_add32_return_sum(&drbbdup_set_ups[variant], 1);
    CHECK(drbbdup_register_case_encoding(drbbdup_ctx, 1) == DRBBDUP_SUCCESS,
          "failed to register case 1");
    *enable_dups = true;
    *enable_dynamic_handling = false;
    return 0;
}

/* Clobbers a register and the flags that drreg reserves at where. */
static void
insert_clobber(void *drcontext, instrlist_t *bb, instr_t *where)
{
    reg_id_t reg;
    if (drreg_reserve_aflags(drcontext, bb, where) != DRREG_SUCCESS ||
        drreg_reserve_register(drcontext, bb, where, NULL, &reg) != DRREG_SUCCESS) {
        error("drreg reservation failed in variant 1");
        return;
    }
    instrlist_insert_mov_immed_ptrsz(drcontext, (ptr_int_t)0xbadcafe,
                                     opnd_create_reg(reg), bb, where, NULL, NULL);
    /* Clears the carry flag. */
    instrlist_meta_preinsert(
        bb, where,
        INSTR_CREATE_add(drcontext, opnd_create_reg(reg), OPND_CREATE_INT8(1)));
    CHECK(drreg_unreserve_register(drcontext, bb, where, reg) == DRREG_SUCCESS,
          "failed to unreserve the register");
    CHECK(drreg_unreserve_aflags(drcontext, bb, where) == DRREG_SUCCESS,
          "failed to unreserve the flags");
}

static void
instrument_instr(void *drcontext, void *tag, instrlist_t *bb, instr_t *instr,
                 instr_t *where, uintptr_t encoding, void *user_data,
                 void *orig_analysis_data, void *analysis_data)
{
    bool is_start;
    if (dr_get_fragment_variant(drcontext) != 1)
        error("drbbdup instrumented a block outside variant 1");
    if (encoding != 0)
        return;
    CHECK(drbbdup_is_first_instr(drcontext, instr, &is_start) == DRBBDUP_SUCCESS,
          "drbbdup_is_first_instr failed");
    if (is_start) {
        dr_insert_clean_call(drcontext, bb, where, (void *)check_block, false, 1,
                             OPND_CREATE_INT32(1));
    }
    if (instr_is_app(instr))
        insert_clobber(drcontext, bb, where);
}

/* Instruments the blocks that drbbdup leaves alone. */
static dr_emit_flags_t
event_bb_insert(void *drcontext, void *tag, instrlist_t *bb, instr_t *instr,
                bool for_trace, bool translating, void *user_data)
{
    uint variant = dr_get_fragment_variant(drcontext);
    if (extensions_enabled(drcontext, tag, for_trace, translating))
        return DR_EMIT_DEFAULT;
    /* drbbdup.h disallows drmgr_is_first_instr(), which is fine in these blocks. */
    if (instr != instrlist_first_app(bb))
        return DR_EMIT_DEFAULT;
    if ((app_pc)tag == client_request_pc) {
        dr_insert_clean_call(drcontext, bb, instr, (void *)on_request, false, 2,
                             opnd_create_reg(DR_REG_XDI), opnd_create_reg(DR_REG_XSI));
        return DR_EMIT_DEFAULT;
    }
    /* drreg leaves these blocks alone. */
    if (drreg_reserve_aflags(drcontext, bb, instr) != DRREG_ERROR_FEATURE_NOT_AVAILABLE)
        error("drreg processed a block outside variant 1");
    dr_insert_clean_call(drcontext, bb, instr, (void *)check_block, false, 1,
                         OPND_CREATE_INT32(variant));
    return DR_EMIT_DEFAULT;
}

static bool
event_restore_state(void *drcontext, bool restore_memory, dr_restore_state_info_t *info)
{
    if (info->fragment_info.cache_start_pc != NULL &&
        info->fragment_info.variant != dr_get_fragment_variant(drcontext))
        error("wrong variant in the restore state event");
    return true;
}

static dr_signal_action_t
event_signal(void *drcontext, dr_siginfo_t *info)
{
    if (info->sig == SIGSEGV && info->fault_fragment_info.cache_start_pc != NULL &&
        info->fault_fragment_info.variant != dr_get_thread_fragment_variant(drcontext))
        error("wrong variant in the signal event");
    return DR_SIGNAL_DELIVER;
}

static void
event_exit(void)
{
    CHECK(filter_translations[0] > 0 && filter_translations[1] > 0,
          "the filter should see translations in both variants");
    CHECK(drbbdup_set_ups[1] > 0, "drbbdup should set up blocks in variant 1");
    CHECK(checks_executed[0] > 0 && checks_executed[1] > 0,
          "blocks should run in both variants");
    dr_fprintf(STDERR, "client: %d errors\n", errors);
    CHECK(drbbdup_exit() == DRBBDUP_SUCCESS, "drbbdup exit failed");
    CHECK(drreg_exit() == DRREG_SUCCESS, "drreg exit failed");
    drmgr_exit();
}

DR_EXPORT void
dr_client_main(client_id_t id, int argc, const char *argv[])
{
    module_data_t *exe = dr_get_main_module();
    drreg_options_t ops = { sizeof(ops), 2 /*max slots needed*/, false };
    drbbdup_options_t opts = { 0 };

    CHECK(exe != NULL, "failed to find the executable");
    CHECK(dr_get_num_fragment_variants() == NUM_VARIANTS,
          "test needs -num_fragment_variants 2");
    client_request_pc = lookup(exe, "client_request");
    marked_pc = lookup(exe, "marked");
    plain_pc = lookup(exe, "plain");
    dr_free_module_data(exe);

    drmgr_init();
    CHECK(drreg_init(&ops) == DRREG_SUCCESS, "drreg init failed");
    opts.struct_size = sizeof(drbbdup_options_t);
    opts.set_up_bb_dups = set_up_bb_dups;
    opts.instrument_instr = instrument_instr;
    opts.runtime_case_opnd = OPND_CREATE_ABSMEM(&encode_val, OPSZ_PTR);
    opts.atomic_load_encoding = false;
    opts.non_default_case_limit = 1;
    opts.never_enable_dynamic_handling = true;
    CHECK(drbbdup_init(&opts) == DRBBDUP_SUCCESS, "drbbdup init failed");
    CHECK(drbbdup_set_enabled_func(extensions_enabled) == DRBBDUP_SUCCESS,
          "drbbdup_set_enabled_func failed");
    CHECK(drreg_set_enabled_func(extensions_enabled) == DRREG_SUCCESS,
          "drreg_set_enabled_func failed");

    dr_register_bb_filter_event(event_bb_filter);
    CHECK(drmgr_register_bb_instrumentation_event(NULL, event_bb_insert, NULL),
          "failed to register the bb event");
    CHECK(drmgr_register_restore_state_ex_event(event_restore_state),
          "failed to register the restore state event");
    CHECK(drmgr_register_signal_event(event_signal),
          "failed to register the signal event");
    drmgr_register_exit_event(event_exit);
}
