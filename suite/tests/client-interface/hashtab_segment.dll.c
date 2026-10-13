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

/* Checks DR's view of the libraries that the app loads, which have their hash table
 * and .dynstr, and for one of them .dynamic, in segments that the loader maps over
 * its initial mapping of the file only after DR has seen that mapping (i#3385).
 * After each of the app's mappings of a library before its module load event, a
 * lookup of the library's export must find nothing (and not crash) until the loader
 * has mapped what it needs, and the export from then on.  In the module load event,
 * the module's name must be its soname and lookups and the export iterator must
 * find the export.  The app then calls the export: we check that the call reaches
 * the address we found.
 */

#include "dr_api.h"
#include "client_tools.h"
#include <string.h>
#include <sys/syscall.h>

#define LIB_PREFIX "libclient.hashtab_segment"
#define LIB_SUFFIX ".appdll.so"
#define EXPORT_NAME "hashtab_segment_export"
#define NUM_LIBS 3

static app_pc exports[NUM_LIBS];
static bool loaded[NUM_LIBS];
static bool reached[NUM_LIBS];
/* What lookups found after the app's mappings of each library before its module
 * load event, and whether one found something else than an earlier one did.
 */
static app_pc early_lookup[NUM_LIBS];
static bool early_mismatch[NUM_LIBS];

/* Returns the index of the test library that mod is, or -1 if it is none. */
static int
lib_index(const module_data_t *mod)
{
    if (mod->full_path == NULL || strstr(mod->full_path, LIB_PREFIX) == NULL ||
        strstr(mod->full_path, LIB_SUFFIX) == NULL)
        return -1;
    if (strstr(mod->full_path, "_sysv") != NULL)
        return 1;
    if (strstr(mod->full_path, "_dynamic") != NULL)
        return 2;
    return 0;
}

static app_pc
code_start(app_pc func)
{
#ifdef ARM
    /* Thumb function addresses have their least significant bit set. */
    return dr_app_pc_as_load_target(DR_ISA_ARM_THUMB, func);
#else
    return func;
#endif
}

static bool
event_filter_syscall(void *drcontext, int sysnum)
{
#ifdef SYS_mmap2
    if (sysnum == SYS_mmap2)
        return true;
#endif
    return sysnum == SYS_mmap;
}

static void
event_post_syscall(void *drcontext, int sysnum)
{
    dr_syscall_result_info_t result = { sizeof(result) };
    module_data_t *mod;
    int idx;
    if (!dr_syscall_get_result_ex(drcontext, &result) || !result.succeeded)
        return;
    mod = dr_lookup_module((byte *)result.value);
    if (mod == NULL)
        return;
    idx = lib_index(mod);
    if (idx >= 0 && !loaded[idx]) {
        app_pc func = (app_pc)dr_get_proc_address(mod->handle, EXPORT_NAME);
        if (early_lookup[idx] != NULL && func != early_lookup[idx])
            early_mismatch[idx] = true;
        early_lookup[idx] = func;
    }
    dr_free_module_data(mod);
}

static void
event_module_load(void *drcontext, const module_data_t *mod, bool loaded_now)
{
    const char *name = dr_module_preferred_name(mod);
    dr_symbol_export_iterator_t *iter;
    bool exported = false;
    app_pc func;
    int idx = lib_index(mod);
    if (idx < 0)
        return;
    dr_fprintf(STDERR, "loaded library %d with soname %s\n", idx + 1,
               name == NULL ? "<null>" : name);
    loaded[idx] = true;
    func = (app_pc)dr_get_proc_address(mod->handle, EXPORT_NAME);
    if (func == NULL || !dr_module_contains_addr(mod, func))
        dr_fprintf(STDERR, "ERROR: wrong address for " EXPORT_NAME ": " PFX "\n", func);
    /* The loader mapped the whole library before this event. */
    if (early_lookup[idx] != func || early_mismatch[idx]) {
        dr_fprintf(STDERR, "ERROR: lookups before the module load event found " PFX "\n",
                   early_lookup[idx]);
    }
    if (dr_get_proc_address(mod->handle, "hashtab_segment_missing") != NULL)
        dr_fprintf(STDERR, "ERROR: found a missing symbol\n");
    iter = dr_symbol_export_iterator_start(mod->handle);
    while (dr_symbol_export_iterator_hasnext(iter)) {
        dr_symbol_export_t *sym = dr_symbol_export_iterator_next(iter);
        if (strcmp(sym->name, EXPORT_NAME) == 0) {
            exported = true;
            if (sym->addr != func)
                dr_fprintf(STDERR, "ERROR: export iterator disagrees: " PFX "\n",
                           sym->addr);
        }
    }
    dr_symbol_export_iterator_stop(iter);
    if (!exported)
        dr_fprintf(STDERR, "ERROR: export iterator missed " EXPORT_NAME "\n");
    exports[idx] = func;
}

static dr_emit_flags_t
event_bb(void *drcontext, void *tag, instrlist_t *bb, bool for_trace, bool translating)
{
    int i;
    for (i = 0; i < NUM_LIBS; i++) {
        if (loaded[i] && !reached[i] && exports[i] != NULL &&
            (app_pc)tag == code_start(exports[i])) {
            reached[i] = true;
            dr_fprintf(STDERR, "reached the export of library %d\n", i + 1);
        }
    }
    return DR_EMIT_DEFAULT;
}

DR_EXPORT void
dr_client_main(client_id_t id, int argc, const char *argv[])
{
    dr_register_filter_syscall_event(event_filter_syscall);
    dr_register_post_syscall_event(event_post_syscall);
    dr_register_module_load_event(event_module_load);
    dr_register_bb_event(event_bb);
}
