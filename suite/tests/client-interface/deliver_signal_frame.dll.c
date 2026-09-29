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

/* Tests dr_deliver_signal_frame(): see deliver_signal_frame.c.  The client builds
 * x86-64 Linux signal frames like the kernel does.
 */

#include "dr_api.h"
#include "client_tools.h"

#include <signal.h>
#include <stdint.h>
#include <string.h>

#include "deliver_signal_frame-shared.h"

#define REDZONE_SIZE 128
#define FXSAVE_SIZE 512
/* Offsets of the fields of struct _fpx_sw_bytes in the fxsave area. */
#define FPX_SW_BYTES_OFFSET 464
/* From the kernel's uapi/asm/ucontext.h. */
#define UC_SIGCONTEXT_SS 0x2
#define UC_STRICT_RESTORE_SS 0x4
/* The kernel's user segment selectors on x86-64. */
#define USER_CS 0x33
#define USER_DS 0x2b
#define EFLAGS_TF 0x100
#define EFLAGS_DF 0x400

/* The kernel's struct ucontext and struct rt_sigframe. */
typedef struct {
    unsigned long uc_flags;
    void *uc_link;
    stack_t uc_stack;
    struct sigcontext uc_mcontext;
    unsigned long long uc_sigmask;
} kernel_ucontext_t;

typedef struct {
    char *pretcode;
    kernel_ucontext_t uc;
    siginfo_t info;
} rt_sigframe_t;

static deliver_request_t *sigill_request;

/* Builds the frame for req in application memory for an interrupted state
 * interrupted, and sets handler_mc to the state at handler entry.
 */
static void
build_frame(deliver_request_t *req, dr_mcontext_t *interrupted, dr_mcontext_t *handler_mc)
{
    byte fxsave_buf[FXSAVE_SIZE + 16];
    byte *fxsave = (byte *)ALIGN_FORWARD(fxsave_buf, 16);
    struct sigcontext *sc;
    byte *sp, *fpstate;
    rt_sigframe_t *frame;
    uint32_t val;

    if (req->on_altstack)
        sp = (byte *)req->altstack.ss_sp + req->altstack.ss_size;
    else
        sp = (byte *)interrupted->xsp - REDZONE_SIZE;
    fpstate = (byte *)ALIGN_BACKWARD(sp - FXSAVE_SIZE, 64);
    frame = (rt_sigframe_t *)(ALIGN_BACKWARD(fpstate - sizeof(*frame), 16) - 8);

    /* A legacy fxsave image: the kernel restores it without xsave state. */
    proc_save_fpstate(fxsave);
    val = 0; /* No FP_XSTATE_MAGIC1. */
    memcpy(fxsave + FPX_SW_BYTES_OFFSET, &val, sizeof(val));
    val = FXSAVE_SIZE; /* extended_size */
    memcpy(fxsave + FPX_SW_BYTES_OFFSET + 4, &val, sizeof(val));
    memcpy(fpstate, fxsave, FXSAVE_SIZE);

    memset(frame, 0, sizeof(*frame));
    frame->pretcode = req->restorer;
    frame->uc.uc_flags = UC_SIGCONTEXT_SS | UC_STRICT_RESTORE_SS;
    frame->uc.uc_stack = req->altstack;
    frame->uc.uc_sigmask = req->blocked;
    sc = &frame->uc.uc_mcontext;
    sc->r8 = interrupted->r8;
    sc->r9 = interrupted->r9;
    sc->r10 = interrupted->r10;
    sc->r11 = interrupted->r11;
    sc->r12 = interrupted->r12;
    sc->r13 = interrupted->r13;
    sc->r14 = interrupted->r14;
    sc->r15 = interrupted->r15;
    sc->rdi = interrupted->xdi;
    sc->rsi = interrupted->xsi;
    sc->rbp = interrupted->xbp;
    sc->rbx = interrupted->xbx;
    sc->rdx = interrupted->xdx;
    sc->rax = interrupted->xax;
    sc->rcx = interrupted->xcx;
    sc->rsp = interrupted->xsp;
    sc->rip = (uint64_t)interrupted->pc;
    sc->eflags = interrupted->xflags;
    sc->cs = USER_CS;
    sc->__pad0 = USER_DS; /* ss */
    sc->fpstate = (struct _fpstate *)fpstate;
    frame->info.si_signo = req->sig;
    frame->info.si_code = SI_USER;

    *handler_mc = *interrupted;
    handler_mc->xsp = (reg_t)frame;
    handler_mc->pc = (app_pc)req->handler;
    handler_mc->xdi = req->sig;
    handler_mc->xsi = (reg_t)&frame->info;
    handler_mc->xdx = (reg_t)&frame->uc;
    handler_mc->xax = 0;
    handler_mc->xflags &= ~(EFLAGS_DF | EFLAGS_TF);
}

static void
at_marker(uint kind, deliver_request_t *req, app_pc next_pc)
{
    void *drcontext = dr_get_current_drcontext();
    dr_mcontext_t mc = { sizeof(mc), DR_MC_ALL };
    dr_mcontext_t handler_mc;
    if (kind == MARKER_DELIVER_AT_SIGILL) {
        sigill_request = req;
        return;
    }
    dr_get_mcontext(drcontext, &mc);
    /* The handler returns to after the marker. */
    mc.pc = next_pc;
    build_frame(req, &mc, &handler_mc);
    dr_deliver_signal_frame(drcontext, req->sig, &handler_mc, NULL);
    CHECK(false, "should not be reached");
}

static dr_signal_action_t
event_signal(void *drcontext, dr_siginfo_t *info)
{
    if (info->sig == SIGILL && sigill_request != NULL) {
        dr_mcontext_t interrupted = *info->mcontext;
        dr_mcontext_t handler_mc;
        deliver_request_t *req = sigill_request;
        sigill_request = NULL;
        /* The handler returns to after the ud2. */
        interrupted.pc += 2;
        build_frame(req, &interrupted, &handler_mc);
        if (!dr_deliver_signal_frame(drcontext, req->sig, &handler_mc, info))
            dr_fprintf(STDERR, "dr_deliver_signal_frame failed\n");
        return DR_SIGNAL_REDIRECT;
    }
    return DR_SIGNAL_DELIVER;
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
            if (kind == MARKER_DELIVER || kind == MARKER_DELIVER_AT_SIGILL) {
                dr_insert_clean_call(drcontext, bb, instr, (void *)at_marker,
                                     false /*fpstate*/, 3, OPND_CREATE_INT32(kind),
                                     opnd_create_reg(DR_REG_XDX),
                                     OPND_CREATE_INTPTR(instr_get_app_pc(instr) +
                                                        instr_length(drcontext, instr)));
            }
        }
    }
    return DR_EMIT_DEFAULT;
}

DR_EXPORT void
dr_client_main(client_id_t id, int argc, const char *argv[])
{
    dr_register_bb_event(event_bb);
    dr_register_signal_event(event_signal);
}
