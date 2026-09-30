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

/* Tests flushing only some of the fragments of an area of code: see flush_exact.c.
 * After each flush we check that the fragments of the blocks that the flush was to
 * flush are gone and that the others are not.  (We cannot check which blocks DR
 * builds again: with thread-private caches, a new fragment can take a flushed one's
 * space and replace its neighbors.)
 */

#include "dr_api.h"
#include "client_tools.h"
#include "flush_exact.h"

static byte *code;

enum {
    BLOCK_A,
    BLOCK_B,
    BLOCK_C1,
    BLOCK_C2,
    BLOCK_D,
    BLOCK_W,
    BLOCK_E,
    BLOCK_X,
    BLOCK_Y,
    BLOCK_T,
    BLOCK_L,
    BLOCK_L_END,
    NUM_BLOCKS,
};
static const char *const block_names[NUM_BLOCKS] = { "A", "B", "C1", "C2", "D", "W",
                                                     "E", "X", "Y",  "T",  "L", "L_END" };
/* The code of each block, as offsets into the buffer. */
static size_t block_start[NUM_BLOCKS];
static size_t block_end[NUM_BLOCKS];
/* Whether each block had a fragment before the current flush. */
static bool block_existed[NUM_BLOCKS];
/* The number of traces with each block as tag since the last report. */
static uint block_traces[NUM_BLOCKS];

/* The block that flushes itself when it next runs (MARKER_SELF_FLUSH). */
static app_pc self_flush_tag;
static uint delay_expected;

static void
set_up_blocks(void)
{
    size_t pg = dr_page_size();
    block_start[BLOCK_A] = OFFS_A;
    block_end[BLOCK_A] = OFFS_A + 6;
    block_start[BLOCK_B] = OFFS_B;
    block_end[BLOCK_B] = OFFS_B + 6;
    block_start[BLOCK_C1] = OFFS_C1;
    block_end[BLOCK_C1] = OFFS_C2;
    block_start[BLOCK_C2] = OFFS_C2;
    block_end[BLOCK_C2] = OFFS_C2 + 4;
    block_start[BLOCK_D] = OFFS_D;
    block_end[BLOCK_D] = OFFS_D_LAST_NOP + 1 + 6;
    block_start[BLOCK_W] = OFFS_W;
    block_end[BLOCK_W] = OFFS_W + 6;
    block_start[BLOCK_E] = OFFS_E;
    block_end[BLOCK_E] = OFFS_E + 6;
    block_start[BLOCK_X] = OFFS_X(pg);
    block_end[BLOCK_X] = OFFS_X(pg) + 9;
    block_start[BLOCK_Y] = OFFS_Y(pg);
    block_end[BLOCK_Y] = OFFS_Y(pg) + 6;
    block_start[BLOCK_T] = OFFS_T;
    block_end[BLOCK_T] = OFFS_L_END;
    block_start[BLOCK_L] = OFFS_L;
    block_end[BLOCK_L] = OFFS_L_END;
    block_start[BLOCK_L_END] = OFFS_L_END;
    block_end[BLOCK_L_END] = OFFS_L_END + 6;
}

static int
find_block(void *tag)
{
    int i;
    if (code == NULL)
        return -1;
    for (i = 0; i < NUM_BLOCKS; i++) {
        if ((app_pc)tag == code + block_start[i])
            return i;
    }
    return -1;
}

/* The blocks with code from [start, start + size), as a bitmask. */
static uint
blocks_in_region(byte *start, size_t size)
{
    uint res = 0;
    int i;
    for (i = 0; i < NUM_BLOCKS; i++) {
        if (start < code + block_end[i] && start + size > code + block_start[i])
            res |= 1 << i;
    }
    return res;
}

/* The blocks whose tags are in the same area of code (see flush_exact.h) as pc. */
static uint
blocks_in_area(byte *pc)
{
    byte *second_area = code + 2 * dr_page_size();
    uint res = 0;
    int i;
    for (i = 0; i < NUM_BLOCKS; i++) {
        if ((pc < second_area) == (code + block_start[i] < second_area))
            res |= 1 << i;
    }
    return res;
}

static void
note_existing(void *drcontext)
{
    int i;
    for (i = 0; i < NUM_BLOCKS; i++)
        block_existed[i] = dr_fragment_exists_at(drcontext, code + block_start[i]);
}

/* Checks that the fragments of the blocks in expected are gone and that those of the
 * others are not (unless they were already), and prints the former.
 */
static void
check_flushed(void *drcontext, const char *what, uint expected)
{
    int i;
    dr_fprintf(STDERR, "%s:", what);
    for (i = 0; i < NUM_BLOCKS; i++) {
        bool exists = dr_fragment_exists_at(drcontext, code + block_start[i]);
        if ((expected & (1 << i)) != 0) {
            CHECK(!exists, "fragment not flushed");
            dr_fprintf(STDERR, " %s", block_names[i]);
        } else
            CHECK(exists || !block_existed[i], "fragment flushed");
    }
    dr_fprintf(STDERR, "\n");
}

static void
report(void)
{
    int i;
    dr_fprintf(STDERR, "traces built:");
    for (i = 0; i < NUM_BLOCKS; i++) {
        if (block_traces[i] > 0)
            dr_fprintf(STDERR, " %s", block_names[i]);
        block_traces[i] = 0;
    }
    dr_fprintf(STDERR, "\n");
}

static void
delay_flush_done(int flush_id)
{
    char what[64];
    dr_snprintf(what, BUFFER_SIZE_ELEMENTS(what), "delayed flush %d", flush_id);
    NULL_TERMINATE_BUFFER(what);
    check_flushed(dr_get_current_drcontext(), what, delay_expected);
}

static void
redirect(void *drcontext, app_pc pc)
{
    dr_mcontext_t mc = { sizeof(mc), DR_MC_ALL };
    dr_get_mcontext(drcontext, &mc);
    mc.pc = pc;
    dr_redirect_execution(&mc);
    CHECK(false, "should not be reached");
}

static void
at_marker(uint kind, byte *addr, size_t size, app_pc next_pc)
{
    void *drcontext = dr_get_current_drcontext();
    if (kind == MARKER_SETUP) {
        set_up_blocks();
        code = addr;
        return;
    }
    note_existing(drcontext);
    switch (kind) {
    case MARKER_REPORT: report(); break;
    case MARKER_FLUSH_TAG:
        CHECK(dr_fragment_exists_at(drcontext, addr), "no fragment to flush");
        CHECK(dr_unlink_flush_fragment(drcontext, addr), "flush failed");
        check_flushed(drcontext, "flushed the tag", 1 << find_block(addr));
        /* We return to the code cache. */
        break;
    case MARKER_FLUSH_EXACT:
        CHECK(dr_unlink_flush_region_ex(addr, size, DR_FLUSH_EXACT), "flush failed");
        check_flushed(drcontext, "flushed the region exactly",
                      blocks_in_region(addr, size));
        redirect(drcontext, next_pc);
        break;
    case MARKER_FLUSH_REGION:
        CHECK(dr_unlink_flush_region(addr, size), "flush failed");
        check_flushed(
            drcontext, "flushed the region",
            blocks_in_area(addr) &
                ~(1 << BLOCK_T | 1 << BLOCK_L | 1 << BLOCK_L_END) /*not yet built*/);
        break;
    case MARKER_DELAY_EXACT:
        delay_expected = blocks_in_region(addr, size);
        CHECK(dr_delay_flush_region_ex(addr, size, DR_FLUSH_EXACT, 1, delay_flush_done),
              "flush failed");
        /* Through DR, which performs the flush before it enters the cache. */
        redirect(drcontext, next_pc);
        break;
    case MARKER_SELF_FLUSH: self_flush_tag = addr; break;
    case MARKER_FLUSH_RUNNING:
        /* Another thread may build the block again at any time. */
        if (size == 0) {
            CHECK(dr_unlink_flush_fragment(drcontext, addr), "flush failed");
            dr_fprintf(STDERR, "flushed the tag of a block another thread runs\n");
        } else {
            CHECK(dr_unlink_flush_region_ex(addr, size, DR_FLUSH_EXACT), "flush failed");
            dr_fprintf(STDERR, "flushed a block another thread runs exactly\n");
        }
        break;
    default: CHECK(false, "unknown marker");
    }
}

/* Called at the start of the block at self_flush_tag. */
static void
at_self_flush(app_pc tag)
{
    void *drcontext = dr_get_current_drcontext();
    if (tag != self_flush_tag)
        return;
    self_flush_tag = NULL;
    note_existing(drcontext);
    CHECK(dr_unlink_flush_fragment(drcontext, tag), "flush failed");
    check_flushed(drcontext, "the block flushed itself", 1 << find_block(tag));
    /* Continue in a new fragment. */
    redirect(drcontext, tag);
}

static bool
is_marker(instr_t *instr, uint *kind)
{
    ptr_int_t imm;
    if (instr_get_opcode(instr) != OP_mov_imm || !opnd_is_reg(instr_get_dst(instr, 0)) ||
        opnd_get_reg(instr_get_dst(instr, 0)) != DR_REG_EAX ||
        !opnd_is_immed_int(instr_get_src(instr, 0)))
        return false;
    imm = opnd_get_immed_int(instr_get_src(instr, 0));
    if (imm < MARKER_SETUP || imm > MARKER_FLUSH_RUNNING)
        return false;
    *kind = (uint)imm;
    return true;
}

static dr_emit_flags_t
event_bb(void *drcontext, void *tag, instrlist_t *bb, bool for_trace, bool translating)
{
    instr_t *instr;
    uint kind;
    int i = find_block(tag);
    if (i == BLOCK_E) {
        dr_insert_clean_call(drcontext, bb, instrlist_first(bb), (void *)at_self_flush,
                             false /*fpstate*/, 1, OPND_CREATE_INTPTR(tag));
    }
    if (i >= 0)
        return DR_EMIT_DEFAULT;
    for (instr = instrlist_first_app(bb); instr != NULL;
         instr = instr_get_next_app(instr)) {
        if (is_marker(instr, &kind)) {
            dr_insert_clean_call(drcontext, bb, instr, (void *)at_marker,
                                 false /*fpstate*/, 4, OPND_CREATE_INT32(kind),
                                 opnd_create_reg(DR_REG_XDX), opnd_create_reg(DR_REG_XSI),
                                 OPND_CREATE_INTPTR(instr_get_app_pc(instr) +
                                                    instr_length(drcontext, instr)));
        }
    }
    return DR_EMIT_DEFAULT;
}

static dr_emit_flags_t
event_trace(void *drcontext, void *tag, instrlist_t *trace, bool translating)
{
    int i = find_block(tag);
    if (i >= 0 && !translating)
        block_traces[i]++;
    return DR_EMIT_DEFAULT;
}

DR_EXPORT void
dr_client_main(client_id_t id, int argc, const char *argv[])
{
    dr_register_bb_event(event_bb);
    dr_register_trace_event(event_trace);
}
