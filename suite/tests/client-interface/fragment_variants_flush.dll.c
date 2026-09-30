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

/* Tests flushes with fragment variants: see fragment_variants_flush.c.  The client
 * intercepts client_request() with a clean call and answers every request by
 * redirecting to its return address, so that selections take effect right away and
 * no flushed fragment is returned to.
 */

#include "dr_api.h"
#include "client_tools.h"
#include "fragment_variants_flush.h"

#include <string.h>

#define MAX_VARIANTS 2

static app_pc client_request_pc, block_a_pc, block_b_pc;
static int builds_a[MAX_VARIANTS], builds_b[MAX_VARIANTS];

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
on_request(reg_t op, reg_t arg1, reg_t arg2)
{
    void *drcontext = dr_get_current_drcontext();
    app_pc pc = (app_pc)arg1;
    reg_t res = 1;
    switch ((int)op) {
    case REQ_NUM_VARIANTS: res = dr_get_num_fragment_variants(); break;
    case REQ_SELECT:
        res = dr_select_thread_fragment_variant(drcontext, (uint)arg1);
        break;
    case REQ_CURRENT_VARIANT: res = dr_get_thread_fragment_variant(drcontext); break;
    case REQ_EXISTS: res = dr_fragment_exists_at(drcontext, pc); break;
    case REQ_BUILDS:
        CHECK(pc == block_a_pc && arg2 < MAX_VARIANTS, "invalid request");
        res = builds_a[arg2];
        break;
    case REQ_FLUSH_TAG: res = dr_unlink_flush_fragment(drcontext, pc); break;
    case REQ_FLUSH_EXACT:
        res = dr_unlink_flush_region_ex(pc, block_b_pc - pc, DR_FLUSH_EXACT);
        break;
    case REQ_DELAY_EXACT:
        /* The flush happens before the thread enters the code cache again. */
        res = dr_delay_flush_region_ex(pc, block_b_pc - pc, DR_FLUSH_EXACT, 0, NULL);
        break;
    case REQ_MEMORY_CHANGED: {
        /* The app's code is not writable. */
        uint prot = DR_MEMPROT_READ | DR_MEMPROT_EXEC;
        if (arg2 != 0)
            prot |= DR_MEMPROT_WRITE;
        res = dr_app_memory_changed(pc, block_b_pc - pc, prot);
        break;
    }
    case REQ_FLUSH_REGION: res = dr_unlink_flush_region(pc, 1); break;
    case REQ_FLUSH_SYNCHALL: res = dr_flush_region(pc, 1); break;
    default: CHECK(false, "unknown request");
    }
    return_from_request(drcontext, res);
}

static dr_emit_flags_t
event_bb(void *drcontext, void *tag, instrlist_t *bb, bool for_trace, bool translating)
{
    uint variant = dr_get_fragment_variant(drcontext);
    CHECK(variant < MAX_VARIANTS, "invalid fragment variant");
    if (!translating && (app_pc)tag == block_a_pc)
        builds_a[variant]++;
    if (!translating && (app_pc)tag == block_b_pc)
        builds_b[variant]++;
    if ((app_pc)tag == client_request_pc) {
        dr_insert_clean_call(drcontext, bb, instrlist_first_app(bb), (void *)on_request,
                             false, 3, opnd_create_reg(DR_REG_XDI),
                             opnd_create_reg(DR_REG_XSI), opnd_create_reg(DR_REG_XDX));
    }
    return DR_EMIT_DEFAULT;
}

DR_EXPORT void
dr_client_main(client_id_t id, int argc, const char *argv[])
{
    module_data_t *exe = dr_get_main_module();
    CHECK(exe != NULL, "failed to find the executable");
    CHECK(dr_get_num_fragment_variants() == MAX_VARIANTS,
          "test needs -num_fragment_variants 2");
    client_request_pc = lookup(exe, "client_request");
    block_a_pc = lookup(exe, "block_a");
    block_b_pc = lookup(exe, "block_b");
    dr_free_module_data(exe);
    dr_register_bb_event(event_bb);
}
