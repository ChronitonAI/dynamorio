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

/* Tests fragment variants: see fragment_variants.c.
 * Variant v makes which_variant() return v.  Variants 1 and 2 also spill rbx and
 * overwrite it (with CLOBBER_RBX_V1 or CLOBBER_RBX_V2) around one app instruction
 * in spin_loop() and in fault_func(), which the restore state event undoes: this
 * checks that state translation re-creates a block in the variant of its
 * fragment.  Every other block of the app gets a clean call that checks that the
 * block's variant is the thread's current variant.  The deletion, restore state,
 * and signal events check the variants they report.
 */

#include "dr_api.h"
#include "client_tools.h"
#include "fragment_variants-shared.h"

#include <signal.h>
#include <stddef.h>
#include <string.h>
#include <sys/syscall.h>

#define MAX_VARIANTS 8

static uint num_variants;
static app_pc exe_start, exe_end;
static app_pc asm_start, asm_end;
static app_pc client_request_pc, which_variant_pc, spin_loop_clobber_pc,
    fault_func_clobber_pc;
static int which_variant_builds[MAX_VARIANTS];
static int build_mismatches;
static int exec_mismatches;
static int exec_counts[MAX_VARIANTS];
static int clobbers_restored[MAX_VARIANTS];
/* Blocks at which_variant() built and not yet deleted, and deleted, per variant. */
static int which_variant_live[MAX_VARIANTS];
static int which_variant_deletions[MAX_VARIANTS];
/* Events that reported a wrong variant. */
static int event_mismatches;
/* Faults in the app's code, per variant the signal event reported. */
static int faults_seen[MAX_VARIANTS];
/* Use dr_unlink_flush_region() (which needs -thread_private) instead of
 * dr_delay_flush_region().
 */
static bool use_unlink_flush;

static app_pc
lookup(module_data_t *exe, const char *name)
{
    app_pc pc = (app_pc)dr_get_proc_address(exe->handle, name);
    CHECK(pc != NULL, "failed to find app function");
    return pc;
}

/* Returns from client_request() to its caller with the result res. */
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
flush_test_code(bool delay)
{
    if (delay && use_unlink_flush) {
        CHECK(dr_unlink_flush_region(which_variant_pc, 1),
              "dr_unlink_flush_region failed");
        CHECK(dr_unlink_flush_region(spin_loop_clobber_pc, 1),
              "dr_unlink_flush_region failed");
    } else if (delay) {
        CHECK(dr_delay_flush_region(which_variant_pc, 1, 0, NULL),
              "dr_delay_flush_region failed");
        CHECK(dr_delay_flush_region(spin_loop_clobber_pc, 1, 0, NULL),
              "dr_delay_flush_region failed");
    } else {
        CHECK(dr_flush_region(which_variant_pc, 1), "dr_flush_region failed");
        CHECK(dr_flush_region(spin_loop_clobber_pc, 1), "dr_flush_region failed");
    }
}

static void
on_request(reg_t op, reg_t arg)
{
    void *drcontext = dr_get_current_drcontext();
    reg_t res = 0;
    switch ((int)op) {
    case REQ_NUM_VARIANTS: res = num_variants; break;
    case REQ_SELECT: res = dr_select_thread_fragment_variant(drcontext, (uint)arg); break;
    case REQ_SELECT_LAZY:
        CHECK(dr_select_thread_fragment_variant(drcontext, (uint)arg), "select failed");
        /* Return into the cache, which does not switch yet. */
        return;
    case REQ_SELECT_AT_PRE_SYSCALL:
    case REQ_SELECT_AT_POST_SYSCALL:
        CHECK(arg < num_variants, "invalid variant");
        /* We encode the variant + 1 and whether to select before (0) or after (1)
         * the system call.
         */
        dr_set_tls_field(
            drcontext,
            (void *)(ptr_uint_t)(((arg + 1) << 1) | (op == REQ_SELECT_AT_POST_SYSCALL)));
        return;
    case REQ_CURRENT_VARIANT: res = dr_get_thread_fragment_variant(drcontext); break;
    case REQ_FLUSH: flush_test_code(false); break;
    case REQ_DELAY_FLUSH: flush_test_code(true); break;
    case REQ_BUILDS:
        CHECK(arg < num_variants, "invalid variant");
        res = which_variant_builds[arg];
        break;
    case REQ_MISMATCHES:
        res = exec_mismatches + build_mismatches + event_mismatches;
        break;
    case REQ_DELETIONS:
        CHECK(arg < num_variants, "invalid variant");
        res = which_variant_deletions[arg];
        break;
    default: CHECK(false, "unknown request");
    }
    /* Any selection takes effect right away as we go through the dispatcher. */
    return_from_request(drcontext, res);
}

static void
check_block(uint variant)
{
    void *drcontext = dr_get_current_drcontext();
    if (dr_get_thread_fragment_variant(drcontext) != variant)
        dr_atomic_add32_return_sum(&exec_mismatches, 1);
    dr_atomic_add32_return_sum(&exec_counts[variant], 1);
}

static void
insert_clobber(void *drcontext, instrlist_t *bb, instr_t *app, uint variant)
{
    dr_spill_slot_t slot = variant == 1 ? SPILL_SLOT_1 : SPILL_SLOT_2;
    uint64 junk = variant == 1 ? CLOBBER_RBX_V1 : CLOBBER_RBX_V2;
    dr_save_reg(drcontext, bb, app, DR_REG_RBX, slot);
    instrlist_meta_preinsert(bb, app,
                             INSTR_CREATE_mov_imm(drcontext, opnd_create_reg(DR_REG_RBX),
                                                  OPND_CREATE_INT64(junk)));
    dr_restore_reg(drcontext, bb, instr_get_next(app), DR_REG_RBX, slot);
}

static dr_emit_flags_t
event_bb(void *drcontext, void *tag, instrlist_t *bb, bool for_trace, bool translating)
{
    uint variant = dr_get_fragment_variant(drcontext);
    instr_t *instr, *first = instrlist_first_app(bb);
    app_pc start = instr_get_app_pc(first);
    CHECK(variant < num_variants, "invalid fragment variant");
    CHECK(!for_trace, "traces are disabled with fragment variants");
    if (!translating) {
        /* New blocks are built in the thread's current variant. */
        if (variant != dr_get_thread_fragment_variant(drcontext))
            dr_atomic_add32_return_sum(&build_mismatches, 1);
        if (start == which_variant_pc) {
            dr_atomic_add32_return_sum(&which_variant_builds[variant], 1);
            dr_atomic_add32_return_sum(&which_variant_live[variant], 1);
        }
    }
    if (start < exe_start || start >= exe_end)
        return DR_EMIT_DEFAULT;
    if (start == client_request_pc) {
        dr_insert_clean_call(drcontext, bb, first, (void *)on_request, false, 2,
                             opnd_create_reg(DR_REG_XDI), opnd_create_reg(DR_REG_XSI));
        return DR_EMIT_DEFAULT;
    }
    if (start >= asm_start && start < asm_end) {
        for (instr = first; instr != NULL; instr = instr_get_next_app(instr)) {
            app_pc pc = instr_get_app_pc(instr);
            if (start == which_variant_pc && instr_is_return(instr)) {
                instrlist_meta_preinsert(
                    bb, instr,
                    INSTR_CREATE_mov_imm(drcontext, opnd_create_reg(DR_REG_EAX),
                                         OPND_CREATE_INT32(variant)));
            } else if ((pc == spin_loop_clobber_pc || pc == fault_func_clobber_pc) &&
                       (variant == 1 || variant == 2)) {
                insert_clobber(drcontext, bb, instr, variant);
            }
        }
        return DR_EMIT_DEFAULT;
    }
    dr_insert_clean_call(drcontext, bb, first, (void *)check_block, false, 1,
                         OPND_CREATE_INT32(variant));
    return DR_EMIT_DEFAULT;
}

static bool
event_restore_state(void *drcontext, bool restore_memory, dr_restore_state_info_t *info)
{
    /* Translation re-creates the block in the variant of its fragment. */
    uint variant = dr_get_fragment_variant(drcontext);
    if (info->fragment_info.cache_start_pc != NULL &&
        info->fragment_info.variant != variant)
        dr_atomic_add32_return_sum(&event_mismatches, 1);
    if ((variant == 1 && info->mcontext->xbx == CLOBBER_RBX_V1) ||
        (variant == 2 && info->mcontext->xbx == CLOBBER_RBX_V2)) {
        info->mcontext->xbx =
            dr_read_saved_reg(drcontext, variant == 1 ? SPILL_SLOT_1 : SPILL_SLOT_2);
        dr_atomic_add32_return_sum(&clobbers_restored[variant], 1);
    }
    return true;
}

static void
event_delete(void *drcontext, void *tag)
{
    /* drcontext is NULL for deletions outside of a thread's context. */
    uint variant = dr_get_fragment_variant(drcontext);
    if (variant >= num_variants) {
        dr_atomic_add32_return_sum(&event_mismatches, 1);
        return;
    }
    if ((app_pc)tag == which_variant_pc) {
        /* A wrong variant would delete more blocks of it than were built. */
        if (dr_atomic_add32_return_sum(&which_variant_live[variant], -1) < 0)
            dr_atomic_add32_return_sum(&event_mismatches, 1);
        dr_atomic_add32_return_sum(&which_variant_deletions[variant], 1);
    }
}

static dr_signal_action_t
event_signal(void *drcontext, dr_siginfo_t *info)
{
    /* A fault in the app's code happens in the thread's current variant. */
    if (info->sig == SIGSEGV && info->fault_fragment_info.cache_start_pc != NULL) {
        uint variant = info->fault_fragment_info.variant;
        if (variant != dr_get_thread_fragment_variant(drcontext) ||
            variant >= num_variants)
            dr_atomic_add32_return_sum(&event_mismatches, 1);
        else
            dr_atomic_add32_return_sum(&faults_seen[variant], 1);
    }
    return DR_SIGNAL_DELIVER;
}

static bool
event_filter_syscall(void *drcontext, int sysnum)
{
    /* We intercept getpid, which then leaves the code cache, for the app's
     * switches at system calls.
     */
    return sysnum == SYS_getpid;
}

/* Selects the variant that REQ_SELECT_AT_{PRE,POST}_SYSCALL requested. */
static void
select_at_syscall(void *drcontext, bool post)
{
    ptr_uint_t req = (ptr_uint_t)dr_get_tls_field(drcontext);
    if (req == 0 || (req & 1) != (post ? 1 : 0))
        return;
    dr_set_tls_field(drcontext, NULL);
    CHECK(dr_select_thread_fragment_variant(drcontext, (uint)(req >> 1) - 1),
          "select failed");
}

static bool
event_pre_syscall(void *drcontext, int sysnum)
{
    if (sysnum == SYS_getpid)
        select_at_syscall(drcontext, false);
    return true;
}

static void
event_post_syscall(void *drcontext, int sysnum)
{
    if (sysnum == SYS_getpid)
        select_at_syscall(drcontext, true);
}

static void
event_thread_init(void *drcontext)
{
    /* A new thread starts in variant 0; we select another one for its first
     * entry into the code cache.
     */
    CHECK(dr_get_thread_fragment_variant(drcontext) == 0,
          "a new thread must start in variant 0");
    CHECK(dr_select_thread_fragment_variant(drcontext,
                                            dr_get_thread_id(drcontext) % num_variants),
          "select failed");
    CHECK(!dr_select_thread_fragment_variant(drcontext, num_variants),
          "invalid variant accepted");
    dr_set_tls_field(drcontext, NULL);
}

static void
event_exit(void)
{
    uint v;
    /* The translations that restored clobbered state depend on timing except for
     * the faults (one each in variants 1 and 2), so we only check those.
     */
    for (v = 1; v <= 2; v++) {
        CHECK(clobbers_restored[v] >= 1, "no restored state");
        CHECK(exec_counts[v] > 0, "no execution in variant");
    }
    CHECK(exec_counts[0] > 0, "no execution in variant");
    /* The app faults once in each variant. */
    for (v = 0; v < num_variants; v++)
        CHECK(faults_seen[v] == 1, "fault not reported in its variant");
    dr_fprintf(STDERR,
               "client: %d build mismatches, %d execution mismatches, "
               "%d event mismatches\n",
               build_mismatches, exec_mismatches, event_mismatches);
}

DR_EXPORT void
dr_client_main(client_id_t id, int argc, const char *argv[])
{
    module_data_t *exe = dr_get_main_module();
    int i;
    CHECK(exe != NULL, "failed to find the executable");
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-unlink_flush") == 0)
            use_unlink_flush = true;
        else
            CHECK(false, "unknown client option");
    }
    num_variants = dr_get_num_fragment_variants();
    CHECK(num_variants >= 3 && num_variants <= MAX_VARIANTS,
          "test needs -num_fragment_variants between 3 and 8");
    exe_start = exe->start;
    exe_end = exe->end;
    asm_start = lookup(exe, "variant_asm_start");
    asm_end = lookup(exe, "variant_asm_end");
    client_request_pc = lookup(exe, "client_request");
    which_variant_pc = lookup(exe, "which_variant");
    spin_loop_clobber_pc = lookup(exe, "spin_loop_clobber");
    fault_func_clobber_pc = lookup(exe, "fault_func_clobber");
    dr_free_module_data(exe);

    dr_register_bb_event(event_bb);
    dr_register_restore_state_ex_event(event_restore_state);
    dr_register_delete_event(event_delete);
    dr_register_signal_event(event_signal);
    dr_register_filter_syscall_event(event_filter_syscall);
    dr_register_pre_syscall_event(event_pre_syscall);
    dr_register_post_syscall_event(event_post_syscall);
    dr_register_thread_init_event(event_thread_init);
    dr_register_exit_event(event_exit);
}
