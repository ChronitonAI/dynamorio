/* **********************************************************
 * Copyright (c) 2013-2026 Google, Inc.  All rights reserved.
 * Copyright (c) 2010 VMware, Inc.  All rights reserved.
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

#ifndef _DR_INJECT_H_
#define _DR_INJECT_H_

/****************************************************************************
 * Injection API
 */
/**
 * @file dr_inject.h
 * @brief Injection API.  Use these functions to launch processes under the
 * control of DynamoRIO.
 */

#ifdef __cplusplus
extern "C" {
#endif

#ifndef ERROR_IMAGE_MACHINE_TYPE_MISMATCH_EXE /* in VS2008+ */
/**
 * Special error code that is returned by \p dr_inject_prepare_to_exec
 * or \p dr_inject_process_create when the target application image
 * does not match the bitwidth of the injection front-end.
 * The target process is still created: it is up to the caller to decide
 * whether to abort (and if so, it should call dr_inject_process_exit()),
 * although on Windows this is generally a fatal error with the current
 * implementation.
 * We use ERROR_IMAGE_MACHINE_TYPE_MISMATCH_EXE in both Windows and Unix
 * assuming no error code conflict on Unix.
 */
#    define ERROR_IMAGE_MACHINE_TYPE_MISMATCH_EXE 720L
#endif
/**
 * Alias of ERROR_IMAGE_MACHINE_TYPE_MISMATCH_EXE to indicate it is not
 * a fatal error on Unix.
 */
#define WARN_IMAGE_MACHINE_TYPE_MISMATCH_EXE ERROR_IMAGE_MACHINE_TYPE_MISMATCH_EXE

DR_EXPORT
/**
 * Creates a new process for the executable and command line specified.
 * The initial thread in the process is suspended.
 * Use dr_inject_process_inject() to inject DynamoRIO into the process
 * (first calling dr_register_process() to configure the process, for
 * one-time targeted configuration), dr_inject_process_run() to resume
 * the thread, and dr_inject_process_exit() to finish and free
 * resources.
 *
 * \param[in]   app_name       The path to the target executable.  The caller
 *                             must ensure this data is valid until the
 *                             inject data is disposed.
 *
 * \param[in]   app_cmdline    A NULL-terminated array of strings representing
 *                             the app's command line.  This should match what
 *                             the app will receive as \p argv in main().  The
 *                             caller must ensure this data is valid until the
 *                             inject data is disposed.
 *
 * \param[out]  data           An opaque pointer that should be passed to
 *                             subsequent dr_inject_* routines to refer to
 *                             this process.
 * \return  Returns 0 on success.  On failure, returns a system error code.
 *          For a mismatched bitwidth, the code is
 *          ERROR_IMAGE_MACHINE_TYPE_MISMATCH_EXE.
 *          On returning ERROR_IMAGE_MACHINE_TYPE_MISMATCH_EXE on Unix, \p data
 *          will be initialized and child process created: i.e., it is merely a
 *          warning, and the caller may continue with cross arch injection.
 *          Regardless of success, caller must call dr_inject_process_exit()
 *          when finished to clean up internally-allocated resources.
 */
int
dr_inject_process_create(const char *app_name, const char **app_cmdline, void **data);

#ifdef WINDOWS
DR_EXPORT
/**
 * Attach to an existing process.
 *
 * \param[in]   pid            PID for process to attach.
 *
 * \param[out]  data           An opaque pointer that should be passed to
 *                             subsequent dr_inject_* routines to refer to
 *                             this process.
 * \param[out]  app_name       Pointer to the name of the target process.
 *                             Only valid until dr_inject_process_exit.
 * \return  Returns 0 on success.  On failure, returns a system error code.
 */
int
dr_inject_process_attach(process_id_t pid, void **data, char **app_name);
#endif

#ifdef UNIX

DR_EXPORT
/**
 * Prepare to exec() the provided command from the current process.  Use
 * dr_inject_process_inject() to perform the exec() under DR.
 *
 * \note Only available on Linux.
 *
 * \param[in]   app_name       The path to the target executable.  The caller
 *                             must ensure this data is valid until the
 *                             inject data is disposed.
 *
 * \param[in]   app_cmdline    A NULL-terminated array of strings representing
 *                             the app's command line.  This should match what
 *                             the app will receive as \p argv in main().  The
 *                             caller must ensure this data is valid until the
 *                             inject data is disposed.
 *
 * \param[out]  data           An opaque pointer that should be passed to
 *                             subsequent dr_inject_* routines to refer to
 *                             this process.
 * \return  Returns 0 on success.  On failure, returns a system error code.
 *          For a mismatched bitwidth, the code is
 *          ERROR_IMAGE_MACHINE_TYPE_MISMATCH_EXE.
 *          On returning ERROR_IMAGE_MACHINE_TYPE_MISMATCH_EXE on Unix, \p data
 *          will be initialized: i.e., it is merely a warning, and the caller
 *          may continue with cross arch injection.
 *          Regardless of success, caller must call dr_inject_process_exit()
 *          when finished to clean up internally-allocated resources.
 */
int
dr_inject_prepare_to_exec(const char *app_name, const char **app_cmdline, void **data);

DR_EXPORT
/**
 * Prepare to ptrace(ATTACH) the provided process.  Use
 * dr_inject_process_inject() to perform the ptrace(ATTACH) under DR.
 *
 * \note Only available on Linux.
 *
 * \param[in]   pid            The pid for the target executable. The caller
 *                             must ensure this data is valid until the
 *                             inject data is disposed.
 *
 * \param[in]   app_name       The path to the target executable.  The caller
 *                             must ensure this data is valid until the
 *                             inject data is disposed.
 *
 * \param[in]   wait_syscall   Syscall handling mode in inject stage.
 *                             If true, will wait for completion of ongoing syscall.
 *                             Else start inject immediately.
 *
 * \param[out]  data           An opaque pointer that should be passed to
 *                             subsequent dr_inject_* routines to refer to
 *                             this process.
 * \return  Whether successful.
 */
int
dr_inject_prepare_to_attach(process_id_t pid, const char *app_name, bool wait_syscall,
                            void **data);

DR_EXPORT
/**
 * Use the ptrace system call to inject into the targetted process.  Must be
 * called before dr_inject_process_inject().  Does not work with
 * dr_inject_prepare_to_exec().
 *
 * Newer Linux distributions restrict which processes can be ptraced.  If DR
 * fails to attach, make sure that gdb can attach to the process in question.
 *
 * Once in the injectee, DynamoRIO searches the $HOME directory of the user of
 * the injector, not the user of the injectee.  Normal usage of drconfig and
 * drinjectlib will ensure that DynamoRIO finds the right config files, however
 * users that wish to examine config files need to check the home directory of
 * the injector's user.
 *
 * \warning ptrace injection is still experimental and subject to change.
 *
 * \param[in]   data           The pointer returned by dr_inject_process_create()
 *
 * \return  Whether successful.
 */
bool
dr_inject_prepare_to_ptrace(void *data);

/**
 * Put the child in a new process group.  If termination is requested with
 * dr_inject_process_exit(), the entire child process group is killed.  Using
 * this option creates a new process group, so if the process group of the
 * injector is killed, the child will survive, which may not be desirable.
 * This routine only operates on child process, and will fail if
 * dr_inject_prepare_to_exec() has been called instead of
 * dr_inject_process_create().
 *
 * \note Only available on Linux.
 *
 * \param[in]   data           The pointer returned by dr_inject_process_create()
 */
bool
dr_inject_prepare_new_process_group(void *data);

#endif /* UNIX */

#ifdef LINUX
/**
 * The value of #dr_ptrace_takeover_args_t.argc.  It occupies the place of the
 * argument count on the initial stack of a new process, which is never negative.
 */
#    define DR_PTRACE_TAKEOVER_ARGC (-2)
/** The value of #dr_ptrace_takeover_args_t.magic ("DRPTRACE"). */
#    define DR_PTRACE_TAKEOVER_MAGIC 0x4543415254505244ULL
/** The current value of #dr_ptrace_takeover_args_t.version. */
#    define DR_PTRACE_TAKEOVER_VERSION 1
/** The size of #dr_ptrace_takeover_args_t.options. */
#    define DR_PTRACE_TAKEOVER_OPTIONS_LENGTH 2048

/** Flags for #dr_ptrace_takeover_args_t.flags. */
typedef enum {
    /**
     * The #dr_ptrace_takeover_args_t.fs_base and #dr_ptrace_takeover_args_t.gs_base
     * fields are valid and are installed as the application's segment bases before
     * DynamoRIO initializes.  Only supported on x86-64.
     */
    DR_PTRACE_TAKEOVER_SEGMENT_BASES = 0x1,
} dr_ptrace_takeover_flags_t;

/**
 * The argument block for injecting DynamoRIO with a custom ptrace-based injector.
 *
 * A tool that already controls a process with ptrace (e.g., because it is the
 * process's tracer and wants to stay its tracer) can inject DynamoRIO itself
 * instead of using dr_inject_prepare_to_ptrace(), which attaches to and later
 * detaches from the target.  The steps mirror what that routine does:
 *
 * -# Stop the target, e.g., at \p PTRACE_EVENT_EXEC (continue it to the exit of
 *    the execve system call first, so that register changes are not clobbered
 *    by the system call's return value).
 * -# Map the \p PT_LOAD segments of libdynamorio.so into the target as an ELF
 *    loader would (including zero-filling the bss), at any page-aligned base:
 *    DynamoRIO relocates itself.
 * -# Write this structure at a 16-byte-aligned address in the target: e.g., at
 *    the top of a stack mapped for this purpose, which DynamoRIO then uses for its
 *    initialization; otherwise below the application's stack pointer and its red
 *    zone.
 * -# Set the stack pointer to the address of this structure, zero the \p xdi
 *    register (x86) and set the program counter to the ELF entry point of
 *    libdynamorio.so.
 * -# Resume the target, passing through the signals it receives (DynamoRIO
 *    raises SIGILL, SIGSEGV and SIGBUS itself during initialization), until it
 *    stops with a SIGTRAP sent by itself: DynamoRIO has then initialized, and
 *    resuming the target once more starts the application under DynamoRIO with
 *    the register state in #mc.
 *
 * The injector remains free to stay the target's tracer.  It must pass through
 * the signals DynamoRIO uses for its own purposes (including the SIGILL it uses
 * to suspend threads).  If the structure is invalid, DynamoRIO prints an error
 * and exits the process.
 *
 * \warning ptrace injection is still experimental and subject to change.
 */
typedef struct _dr_ptrace_takeover_args_t {
    /** Must be #DR_PTRACE_TAKEOVER_ARGC. */
    ptr_int_t argc;
    /** Must be #DR_PTRACE_TAKEOVER_MAGIC. */
    uint64 magic;
    /** Must be #DR_PTRACE_TAKEOVER_VERSION. */
    uint version;
    /** Must be sizeof(#dr_ptrace_takeover_args_t). */
    uint size;
    /** A combination of #dr_ptrace_takeover_flags_t values. */
    uint64 flags;
    /**
     * The page size of the target process (as in its \p AT_PAGESZ auxiliary vector
     * entry).  If 0, DynamoRIO determines the page size itself, which involves
     * temporary memory mappings at addresses chosen by the kernel.
     */
    uint64 page_size;
    /** The application's fs base, if #DR_PTRACE_TAKEOVER_SEGMENT_BASES is set. */
    uint64 fs_base;
    /** The application's gs base, if #DR_PTRACE_TAKEOVER_SEGMENT_BASES is set. */
    uint64 gs_base;
    /**
     * DynamoRIO's runtime options, in the same format as the options in a
     * configuration file (e.g., "-code_api -client_lib path;0;client options").
     * If this string is not empty, DynamoRIO uses it as its options and neither
     * reads configuration files nor needs a home directory.  If it is empty,
     * DynamoRIO reads configuration files as with other injection methods,
     * looking for a local configuration under #home_dir.
     */
    char options[DR_PTRACE_TAKEOVER_OPTIONS_LENGTH];
    /**
     * The value of \p HOME used to find local configuration files if #options is
     * empty.  May be empty.
     */
    char home_dir[MAXIMUM_PATH];
    /**
     * The application state at which DynamoRIO takes over.  The \p size field
     * must be set to sizeof(#dr_mcontext_t) and the \p flags field must include
     * #DR_MC_INTEGER and #DR_MC_CONTROL (the latter includes the flags register).
     * If \p flags includes #DR_MC_MULTIMEDIA, the SIMD register values are used as
     * well; otherwise, the application starts with these registers zeroed.
     */
    dr_mcontext_t mc;
} dr_ptrace_takeover_args_t;

#endif /* LINUX */

#ifdef WINDOWS
DR_EXPORT
/**
 * Specifies that late injection should be used for the process created by
 * dr_inject_process_create().
 *
 * \param[in]   data           The pointer returned by dr_inject_process_create()
 *
 * \return  Whether successful.
 */
bool
dr_inject_use_late_injection(void *data);
#endif

DR_EXPORT
/**
 * Injects DynamoRIO into a process created by dr_inject_process_create(), or
 * the current process if using dr_inject_prepare_to_exec() on Linux.
 *
 * \param[in]   data           The pointer returned by dr_inject_process_create()
 *
 * \param[in]   force_injection  Requests injection even if the process is
 *                               configured to not be run under DynamoRIO.
 *
 * \param[in]   library_path    The path to the DynamoRIO library to use.  If
 *                              NULL, the library that the target process is
 *                              configured for will be used.
 *
 * \return  Whether successful.
 */
bool
dr_inject_process_inject(void *data, bool force_injection, const char *library_path);

DR_EXPORT
/**
 * Resumes the suspended thread in a process created by dr_inject_process_create().
 *
 * \param[in]   data           The pointer returned by dr_inject_process_create()
 *
 * \return  Whether successful.
 */
bool
dr_inject_process_run(void *data);

DR_EXPORT
/**
 * Waits for the child process to exit with the given timeout.
 *
 * \param[in]   data           The pointer returned by dr_inject_process_create()
 * \param[in]   timeout_millis The timeout in milliseconds.  Zero means wait
 *                             forever.
 *
 * \return  Return true if the child exited, and false if we timed out.
 *
 * \note On Linux, this sets a signal handler for SIGALRM.
 */
bool
dr_inject_wait_for_child(void *data, uint64 timeout_millis);

DR_EXPORT
/**
 * Frees resources used by dr_inject_process_create().  Does not wait for the
 * child to exit, unless terminate is true.
 *
 * \param[in]   data           The pointer returned by dr_inject_process_create()
 *
 * \param[in]   terminate      If true, the process is forcibly terminated.
 *
 * \return  Returns the exit code of the process, always returns 0 for ptraced process.
 *          If the caller did not wait for the process to finish before calling this,
 *          the code will be STILL_ACTIVE.
 */
int
dr_inject_process_exit(void *data, bool terminate);

DR_EXPORT
/**
 * Returns the process name of a process created by dr_inject_process_create().
 *
 * \param[in]   data           The pointer returned by dr_inject_process_create()
 *
 * \return  Returns the process name of the process.  This is the file name
 *          without the path, suitable for passing to dr_register_process().
 */
char *
dr_inject_get_image_name(void *data);

#ifdef WINDOWS
DR_EXPORT
/**
 * Returns a handle to a process created by dr_inject_process_create().
 *
 * \param[in]   data           The pointer returned by dr_inject_process_create()
 *
 * \note Windows only.
 *
 * \return  Returns the handle used by drinjectlib.  Do not close the handle: it
 *          will be closed in dr_inject_process_exit().
 */
HANDLE
dr_inject_get_process_handle(void *data);
#endif /* WINDOWS */

DR_EXPORT
/**
 * Returns the pid of a process created by dr_inject_process_create().
 *
 * \param[in]   data           The pointer returned by dr_inject_process_create()
 *
 * \return  Returns the pid of the process.
 */
process_id_t
dr_inject_get_process_id(void *data);

DR_EXPORT
/* Deliberately not documented: not fully supported */
bool
dr_inject_using_debug_key(void *data);

DR_EXPORT
/**
 * Prints statistics for a process created by dr_inject_process_create().
 *
 * \param[in]   data           The pointer returned by dr_inject_process_create()
 *
 * \param[in]   elapsed_secs   Elapsed time recorded by the caller that will be
 *                             printed by this routine if showstats is true.
 *
 * \param[in]   showstats      If true, elapsed_secs and resource usage is printed.
 *
 * \param[in]   showmem        If true, memory usage statistics are printed.
 */
void
dr_inject_print_stats(void *data, int elapsed_secs, bool showstats, bool showmem);

#ifdef __cplusplus
}
#endif

#endif /* _DR_INJECT_H_ */
