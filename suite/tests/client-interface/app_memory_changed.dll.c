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

/* Tests dr_app_memory_changed(): see app_memory_changed.c. */

#include "dr_api.h"
#include "client_tools.h"

#include <sys/mman.h>
#include <sys/syscall.h>

#define MARKER_MEMORY_CHANGED 0x7eca0301
#define MARKER_CONTENTS_CHANGED 0x7eca0302

/* An mprotect that DR does not see. */
static long
raw_mprotect(void *addr, size_t len, int prot)
{
    long ret;
    __asm__ __volatile__("syscall"
                         : "=a"(ret)
                         : "0"(SYS_mprotect), "D"(addr), "S"(len), "d"(prot)
                         : "rcx", "r11", "memory");
    return ret;
}

static void
at_marker(byte *code, app_pc next_pc)
{
    void *drcontext = dr_get_current_drcontext();
    dr_mcontext_t mc = { sizeof(mc), DR_MC_ALL };
    byte *data = code + dr_page_size();
    uint prot;
    if (raw_mprotect(data, dr_page_size(), PROT_READ) != 0)
        dr_fprintf(STDERR, "mprotect failed\n");
    if (!dr_app_memory_changed(code, dr_page_size(),
                               DR_MEMPROT_READ | DR_MEMPROT_WRITE | DR_MEMPROT_EXEC) ||
        !dr_app_memory_changed(data, dr_page_size(), DR_MEMPROT_READ))
        dr_fprintf(STDERR, "dr_app_memory_changed failed\n");
    if (dr_query_memory(data, NULL, NULL, &prot)) {
        dr_fprintf(STDERR, "client: data page is %s\n",
                   prot == DR_MEMPROT_READ ? "read-only" : "not read-only");
    }
    /* The code cache may have been flushed. */
    dr_get_mcontext(drcontext, &mc);
    mc.pc = next_pc;
    dr_redirect_execution(&mc);
    CHECK(false, "should not be reached");
}

/* The application changed the second byte of the block at code (through
 * /proc/self/mem), where another block follows at code + 64.
 */
static void
at_contents_marker(byte *code, app_pc next_pc)
{
    void *drcontext = dr_get_current_drcontext();
    dr_mcontext_t mc = { sizeof(mc), DR_MC_ALL };
    CHECK(dr_fragment_exists_at(drcontext, code) &&
              dr_fragment_exists_at(drcontext, code + 64),
          "blocks not built");
    if (!dr_app_memory_changed(code + 1, 1, DR_MEMPROT_READ | DR_MEMPROT_EXEC))
        dr_fprintf(STDERR, "dr_app_memory_changed failed\n");
    /* Only the changed block's fragment is gone. */
    CHECK(!dr_fragment_exists_at(drcontext, code), "changed block not flushed");
    CHECK(dr_fragment_exists_at(drcontext, code + 64), "other block flushed");
    dr_fprintf(STDERR, "client: only the changed block was flushed\n");
    dr_get_mcontext(drcontext, &mc);
    mc.pc = next_pc;
    dr_redirect_execution(&mc);
    CHECK(false, "should not be reached");
}

static dr_emit_flags_t
event_bb(void *drcontext, void *tag, instrlist_t *bb, bool for_trace, bool translating)
{
    instr_t *instr;
    for (instr = instrlist_first_app(bb); instr != NULL;
         instr = instr_get_next_app(instr)) {
        ptr_int_t marker;
        if (instr_get_opcode(instr) != OP_mov_imm ||
            !opnd_is_reg(instr_get_dst(instr, 0)) ||
            opnd_get_reg(instr_get_dst(instr, 0)) != DR_REG_EAX ||
            !opnd_is_immed_int(instr_get_src(instr, 0)))
            continue;
        marker = opnd_get_immed_int(instr_get_src(instr, 0));
        if (marker == MARKER_MEMORY_CHANGED || marker == MARKER_CONTENTS_CHANGED) {
            dr_insert_clean_call(drcontext, bb, instr,
                                 marker == MARKER_MEMORY_CHANGED
                                     ? (void *)at_marker
                                     : (void *)at_contents_marker,
                                 false /*fpstate*/, 2, opnd_create_reg(DR_REG_XDX),
                                 OPND_CREATE_INTPTR(instr_get_app_pc(instr) +
                                                    instr_length(drcontext, instr)));
        }
    }
    return DR_EMIT_DEFAULT;
}

DR_EXPORT void
dr_client_main(client_id_t id, int argc, const char *argv[])
{
    dr_register_bb_event(event_bb);
}
