/* **********************************************************
 * Copyright (c) 2013-2023 Google, Inc.  All rights reserved.
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
 * * Neither the name of VMware, Inc. nor the names of its contributors may be
 *   used to endorse or promote products derived from this software without
 *   specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL VMWARE, INC. OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 */

#ifndef ASM_CODE_ONLY  /* C code */
#    include "tools.h" /* for print() */

#    include <stdio.h>
#    ifdef WINDOWS
#        include <intrin.h>
#    else
#        include <cpuid.h>
#    endif

extern ptr_int_t
test_fnstenv_intra(ptr_int_t *real_pc);
extern ptr_int_t
test_fnstenv_inter(ptr_int_t *real_pc);
extern ptr_int_t
test_fnsave_inter(ptr_int_t *real_pc);
#    ifdef X64
extern ptr_int_t
test_fxsave64_intra(ptr_int_t *real_pc);
extern ptr_int_t
test_fxsave64_inter(ptr_int_t *real_pc);
#    endif
extern ptr_int_t
test_fxsave_intra(ptr_int_t *real_pc);
extern ptr_int_t
test_fxsave_inter(ptr_int_t *real_pc);
extern ptr_int_t
test_fxsave_after_sse(ptr_int_t *real_pc);
extern ptr_int_t
test_fxsave_after_fnstcw(ptr_int_t *real_pc);
extern ptr_int_t
test_fxsave_after_fxrstor(ptr_int_t *real_pc);
extern ptr_int_t
test_xsave_intra(ptr_int_t *real_pc);
extern ptr_int_t
test_xsave_inter(ptr_int_t *real_pc);

static bool
has_xsave(void)
{
    int regs[4];
#    ifdef WINDOWS
    __cpuid(regs, 1);
#    else
    __cpuid(1, regs[0], regs[1], regs[2], regs[3]);
#    endif
    return (regs[2] & (1 << 27)) != 0; /* OSXSAVE */
}

/* The 32-bit formats hold the bottom 32 bits of the pc. */
static void
check32(const char *name, ptr_int_t (*test)(ptr_int_t *))
{
    ptr_int_t rpc, fpc;
    fpc = test(&rpc);
    if ((int)fpc == (int)rpc)
        print("%s is correctly handled\n", name);
    else
        print("%s is **incorrectly** handled\n", name);
}

int
main(void)
{
    ptr_int_t rpc, fpc;
    bool fxsave_inter_ok;

    check32("FNSTENV intra", test_fnstenv_intra);
    check32("FNSTENV inter", test_fnstenv_inter);
    check32("FNSAVE inter", test_fnsave_inter);

#    ifdef X64
    fpc = test_fxsave64_intra(&rpc);
    if (fpc == rpc)
        print("FXSAVE64 intra is correctly handled\n");
    else
        print("FXSAVE64 intra is **incorrectly** handled\n");
    fpc = test_fxsave64_inter(&rpc);
    if (fpc == rpc)
        print("FXSAVE64 inter is correctly handled\n");
    else
        print("FXSAVE64 inter is **incorrectly** handled\n");
#    endif

    fpc = test_fxsave_intra(&rpc);
    if ((int)fpc == (int)rpc)
        print("FXSAVE intra is correctly handled\n");
    else
        print("FXSAVE intra is **incorrectly** handled\n");

    fpc = test_fxsave_inter(&rpc);
    fxsave_inter_ok = (int)fpc == (int)rpc;
    if (fxsave_inter_ok)
        print("FXSAVE inter is correctly handled\n");
    else
        print("FXSAVE inter is **incorrectly** handled\n");

    /* Only x87 instructions other than the control ones set the pc, and one that
     * loads the state loads the pc.
     */
    check32("FXSAVE after SSE", test_fxsave_after_sse);
    check32("FXSAVE after FNSTCW", test_fxsave_after_fnstcw);
    check32("FXSAVE after FXRSTOR", test_fxsave_after_fxrstor);

    /* XSAVE (of the x87 state alone) stores the pc as FXSAVE does; its pc is never
     * updated inline, so intra-block too needs -translate_fpu_pc.  Without XSAVE, we
     * report what FXSAVE inter found.
     */
    if (has_xsave()) {
        check32("XSAVE intra", test_xsave_intra);
        check32("XSAVE inter", test_xsave_inter);
    } else {
        const char *result = fxsave_inter_ok ? "correctly" : "**incorrectly**";
        print("XSAVE intra is %s handled\n", result);
        print("XSAVE inter is %s handled\n", result);
    }

    return 0;
}

#else /* asm code *************************************************************/
#    include "asm_defines.asm"
/* clang-format off */
START_FILE

# define FNSAVE_PC_OFFS  12
# define FXSAVE_PC_OFFS   8

/* The 32-bit environment format holds the bottom 32 bits of the pc, which the
 * test compares.
 */
#  define FUNCNAME test_fnstenv_intra
        DECLARE_FUNC_SEH(FUNCNAME)
GLOBAL_LABEL(FUNCNAME:)
        END_PROLOG
        mov      REG_XAX, ARG1
        lea      REG_XDX, SYMREF(fldz_addr)
        mov      PTRSZ [REG_XAX], REG_XDX
ADDRTAKEN_LABEL(fldz_addr:)
        fldz
        sub      REG_XSP, 32 /* make space for fnstenv */
        fnstenv  [REG_XSP]
        mov      eax, DWORD [REG_XSP + FNSAVE_PC_OFFS] /* PC field is 32 bits */
        add      REG_XSP, 32
        ret
        END_FUNC(FUNCNAME)
#  undef FUNCNAME

#  define FUNCNAME test_fnstenv_inter
        DECLARE_FUNC_SEH(FUNCNAME)
GLOBAL_LABEL(FUNCNAME:)
        END_PROLOG
        mov      REG_XAX, ARG1
        lea      REG_XDX, SYMREF(fldz_addr1)
        mov      PTRSZ [REG_XAX], REG_XDX
ADDRTAKEN_LABEL(fldz_addr1:)
        fldz
        /* conditional to put fldz in prior bb */
        mov      eax, 1
        cmp      eax, 1
        jne      skip
        sub      REG_XSP, 32 /* make space for fnstenv */
        fnstenv  [REG_XSP]
        mov      eax, DWORD [REG_XSP + FNSAVE_PC_OFFS] /* PC field is 32 bits */
        add      REG_XSP, 32
skip:
        ret
        END_FUNC(FUNCNAME)
#  undef FUNCNAME

#  define FUNCNAME test_fnsave_inter
        DECLARE_FUNC_SEH(FUNCNAME)
GLOBAL_LABEL(FUNCNAME:)
        END_PROLOG
        fninit
        mov      REG_XAX, ARG1
        lea      REG_XDX, SYMREF(fldz_addr4)
        mov      PTRSZ [REG_XAX], REG_XDX
ADDRTAKEN_LABEL(fldz_addr4:)
        fldz
        /* conditional to put fldz in prior bb */
        mov      eax, 1
        cmp      eax, 1
        jne      skip2
        sub      REG_XSP, 112 /* make space for fnsave */
        fnsave   [REG_XSP]
        mov      eax, DWORD [REG_XSP + FNSAVE_PC_OFFS] /* PC field is 32 bits */
        add      REG_XSP, 112
skip2:
        ret
        END_FUNC(FUNCNAME)
#  undef FUNCNAME

# ifdef X64

#  define FUNCNAME test_fxsave64_intra
        DECLARE_FUNC_SEH(FUNCNAME)
GLOBAL_LABEL(FUNCNAME:)
        END_PROLOG
        mov      REG_XAX, ARG1
        lea      REG_XDX, SYMREF(fldz_addr64)
        mov      PTRSZ [REG_XAX], REG_XDX
ADDRTAKEN_LABEL(fldz_addr64:)
        fldz
        mov      REG_XDX, REG_XSP
        sub      REG_XSP, 512+16 /* make space for fxsave + align */
        and      REG_XSP, -16    /* align to 16 */
        /* VS2005 doesn't know "fxsave64" */
        RAW(48) RAW(0f) RAW(ae) RAW(04) RAW(24) /* fxsave64 [REG_XSP] */
        mov      REG_XAX, PTRSZ [REG_XSP + FXSAVE_PC_OFFS]
        mov      REG_XSP, REG_XDX
        ret
        END_FUNC(FUNCNAME)
#  undef FUNCNAME

#  define FUNCNAME test_fxsave64_inter
        DECLARE_FUNC_SEH(FUNCNAME)
GLOBAL_LABEL(FUNCNAME:)
        END_PROLOG
        mov      REG_XAX, ARG1
        lea      REG_XDX, SYMREF(fldz_addr65)
        mov      PTRSZ [REG_XAX], REG_XDX
ADDRTAKEN_LABEL(fldz_addr65:)
        fldz
        /* conditional to put fldz in prior bb */
        mov      eax, 1
        cmp      eax, 1
        jne      skip64
        mov      REG_XDX, REG_XSP
        sub      REG_XSP, 512+16 /* make space for fxsave + align */
        and      REG_XSP, -16    /* align to 16 */
        /* VS2005 doesn't know "fxsave64" */
        RAW(48) RAW(0f) RAW(ae) RAW(04) RAW(24) /* fxsave64 [REG_XSP] */
        mov      REG_XAX, PTRSZ [REG_XSP + FXSAVE_PC_OFFS]
        mov      REG_XSP, REG_XDX
skip64:
        ret
        END_FUNC(FUNCNAME)
#  undef FUNCNAME
# endif

# define FUNCNAME test_fxsave_intra
        DECLARE_FUNC_SEH(FUNCNAME)
GLOBAL_LABEL(FUNCNAME:)
        END_PROLOG
        mov      REG_XAX, ARG1
        lea      REG_XDX, SYMREF(fldz_addr2)
        mov      PTRSZ [REG_XAX], REG_XDX
ADDRTAKEN_LABEL(fldz_addr2:)
        fldz
        mov      REG_XDX, REG_XSP
        sub      REG_XSP, 512+16 /* make space for fxsave + align */
        and      REG_XSP, -16    /* align to 16 */
        fxsave   [REG_XSP]
        mov      eax, DWORD [REG_XSP + FXSAVE_PC_OFFS]
        mov      REG_XSP, REG_XDX
        ret
        END_FUNC(FUNCNAME)
# undef FUNCNAME

# define FUNCNAME test_fxsave_inter
        DECLARE_FUNC_SEH(FUNCNAME)
GLOBAL_LABEL(FUNCNAME:)
        END_PROLOG
        mov      REG_XAX, ARG1
        lea      REG_XDX, SYMREF(fldz_addr3)
        mov      PTRSZ [REG_XAX], REG_XDX
ADDRTAKEN_LABEL(fldz_addr3:)
        fldz
        /* conditional to put fldz in prior bb */
        mov      eax, 1
        cmp      eax, 1
        jne      skip1
        mov      REG_XDX, REG_XSP
        sub      REG_XSP, 512+16 /* make space for fxsave + align */
        and      REG_XSP, -16    /* align to 16 */
        fxsave   [REG_XSP]
        mov      eax, DWORD [REG_XSP + FXSAVE_PC_OFFS]
        mov      REG_XSP, REG_XDX
skip1:
        ret
        END_FUNC(FUNCNAME)
# undef FUNCNAME

/* An SSE instruction does not set the pc. */
# define FUNCNAME test_fxsave_after_sse
        DECLARE_FUNC_SEH(FUNCNAME)
GLOBAL_LABEL(FUNCNAME:)
        END_PROLOG
        fninit
        mov      REG_XAX, ARG1
        lea      REG_XDX, SYMREF(fldz_addr7)
        mov      PTRSZ [REG_XAX], REG_XDX
ADDRTAKEN_LABEL(fldz_addr7:)
        fldz
        /* conditional to put fldz in prior bb */
        mov      eax, 1
        cmp      eax, 1
        jne      skip3
        addps    xmm0, xmm0
        mov      REG_XDX, REG_XSP
        sub      REG_XSP, 512+16 /* make space for fxsave + align */
        and      REG_XSP, -16    /* align to 16 */
        fxsave   [REG_XSP]
        mov      eax, DWORD [REG_XSP + FXSAVE_PC_OFFS]
        mov      REG_XSP, REG_XDX
skip3:
        ret
        END_FUNC(FUNCNAME)
# undef FUNCNAME

/* Nor does an x87 control instruction. */
# define FUNCNAME test_fxsave_after_fnstcw
        DECLARE_FUNC_SEH(FUNCNAME)
GLOBAL_LABEL(FUNCNAME:)
        END_PROLOG
        fninit
        mov      REG_XAX, ARG1
        lea      REG_XDX, SYMREF(fldz_addr8)
        mov      PTRSZ [REG_XAX], REG_XDX
ADDRTAKEN_LABEL(fldz_addr8:)
        fldz
        /* conditional to put fldz in prior bb */
        mov      eax, 1
        cmp      eax, 1
        jne      skip5
        mov      REG_XDX, REG_XSP
        sub      REG_XSP, 512+16 /* make space for fxsave + align */
        and      REG_XSP, -16    /* align to 16 */
        fnstcw   WORD [REG_XSP]
        fxsave   [REG_XSP]
        mov      eax, DWORD [REG_XSP + FXSAVE_PC_OFFS]
        mov      REG_XSP, REG_XDX
skip5:
        ret
        END_FUNC(FUNCNAME)
# undef FUNCNAME

/* FXRSTOR loads the pc, from an image whose pc is real_pc's address. */
# define FUNCNAME test_fxsave_after_fxrstor
        DECLARE_FUNC_SEH(FUNCNAME)
GLOBAL_LABEL(FUNCNAME:)
        END_PROLOG
        fninit
        mov      REG_XAX, ARG1
        mov      PTRSZ [REG_XAX], REG_XAX
        mov      REG_XDX, REG_XSP
        sub      REG_XSP, 1024+16 /* make space for two fxsaves + align */
        and      REG_XSP, -16     /* align to 16 */
        fldz
        fxsave   [REG_XSP + 512]
        mov      DWORD [REG_XSP + 512 + FXSAVE_PC_OFFS], eax
        fxrstor  [REG_XSP + 512]
        fxsave   [REG_XSP]
        mov      eax, DWORD [REG_XSP + FXSAVE_PC_OFFS]
        mov      REG_XSP, REG_XDX
        ret
        END_FUNC(FUNCNAME)
# undef FUNCNAME

/* XSAVE of the x87 state alone: 576 bytes, aligned to 64.  We return the bottom 32
 * bits of the pc, which is all that the IA-32 format holds.
 */
# ifdef X64
/* VS2005 doesn't know "xsave64" */
#  define XSAVE_XSP RAW(48) RAW(0f) RAW(ae) RAW(24) RAW(24) /* xsave64 [REG_XSP] */
# else
#  define XSAVE_XSP RAW(0f) RAW(ae) RAW(24) RAW(24) /* xsave [REG_XSP] */
# endif

# define FUNCNAME test_xsave_intra
        DECLARE_FUNC_SEH(FUNCNAME)
GLOBAL_LABEL(FUNCNAME:)
        END_PROLOG
        fninit
        mov      REG_XAX, ARG1
        lea      REG_XDX, SYMREF(fldz_addr9)
        mov      PTRSZ [REG_XAX], REG_XDX
        mov      REG_XCX, REG_XSP
ADDRTAKEN_LABEL(fldz_addr9:)
        fldz
        sub      REG_XSP, 576+64 /* make space for xsave + align */
        and      REG_XSP, -64    /* align to 64 */
        mov      eax, 1          /* the x87 state */
        xor      edx, edx
        XSAVE_XSP
        mov      eax, DWORD [REG_XSP + FXSAVE_PC_OFFS]
        mov      REG_XSP, REG_XCX
        ret
        END_FUNC(FUNCNAME)
# undef FUNCNAME

# define FUNCNAME test_xsave_inter
        DECLARE_FUNC_SEH(FUNCNAME)
GLOBAL_LABEL(FUNCNAME:)
        END_PROLOG
        fninit
        mov      REG_XAX, ARG1
        lea      REG_XDX, SYMREF(fldz_addr10)
        mov      PTRSZ [REG_XAX], REG_XDX
        mov      REG_XCX, REG_XSP
ADDRTAKEN_LABEL(fldz_addr10:)
        fldz
        /* conditional to put fldz in prior bb */
        mov      eax, 1
        cmp      eax, 1
        jne      skip6
        sub      REG_XSP, 576+64 /* make space for xsave + align */
        and      REG_XSP, -64    /* align to 64 */
        mov      eax, 1          /* the x87 state */
        xor      edx, edx
        XSAVE_XSP
        mov      eax, DWORD [REG_XSP + FXSAVE_PC_OFFS]
        mov      REG_XSP, REG_XCX
skip6:
        ret
        END_FUNC(FUNCNAME)
# undef FUNCNAME

END_FILE
/* clang-format on */
#endif /* ASM_CODE_ONLY */
