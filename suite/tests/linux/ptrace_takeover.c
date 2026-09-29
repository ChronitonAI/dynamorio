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

/* Tests injecting DR with ptrace from a custom injector that stays the tracer, using
 * dr_ptrace_takeover_args_t (see dr_inject.h).
 *
 * Usage: ptrace_takeover <libdynamorio.so> <client.so> <drrun>
 *   Runs this program in "app" mode twice with address space randomization
 *   disabled: once natively, and once under DR, which it injects at the exec stop
 *   into a range of its choosing.  It checks that DR is configured by the options
 *   in the argument block only, that it applies the register state in the block,
 *   that DR's initialization maps nothing outside the chosen range, and that the
 *   application's mappings outside that range are the same as in the native run.
 *   Finally, it runs this program under drinjectlib's own ptrace injection
 *   ("drrun -use_ptrace").
 * Usage: ptrace_takeover app|app-brief
 *   Prints the application state checked above (app-brief omits the mappings).
 */

#include "configure.h"
#include "dr_api.h"
#include "dr_inject.h"

#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/personality.h>
#include <sys/ptrace.h>
#include <sys/syscall.h>
#include <sys/user.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef ARCH_GET_GS
#    define ARCH_GET_GS 0x1004
#endif
#ifndef F_SETPIPE_SZ
#    define F_SETPIPE_SZ 1031
#endif
#ifndef MAP_FIXED_NOREPLACE
#    define MAP_FIXED_NOREPLACE 0x100000
#endif

/* The range we give to DR, far from where the kernel places anything without
 * address space randomization.
 */
#define DR_RANGE_BASE 0x100000000000ULL
#define DR_RANGE_SIZE 0x20000000ULL
#define DR_INIT_STACK_OFFS 0x08000000ULL
#define DR_INIT_STACK_SIZE (1024 * 1024)
#define DR_VMCODE_OFFS 0x10000000ULL
#define DR_VMCODE_SIZE "256M"
/* An application gs base for DR to install, and an eflags bit (ID) that user mode
 * can set but that nothing in the application changes.
 */
#define APP_GS_BASE 0x12345000ULL
#define EFLAGS_ID (1UL << 21)

#define MAX_LINES 256
#define MAX_OUTPUT (64 * 1024)

/* The process we are running and the read end of the pipe with its output. */
static pid_t child;
static int child_output = -1;

static void
fatal(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    if (child_output >= 0) {
        /* Show what the child printed, e.g., an error from DR. */
        char buf[4096];
        ssize_t len;
        fcntl(child_output, F_SETFL, O_NONBLOCK);
        while ((len = read(child_output, buf, sizeof(buf))) > 0)
            fwrite(buf, 1, len, stderr);
    }
    exit(1);
}

static void
handle_alarm(int sig)
{
    const char msg[] = "timed out\n";
    if (child > 0)
        kill(child, SIGKILL);
    write(1, msg, sizeof(msg) - 1);
    _exit(1);
}

static bool
in_dr_range(uint64_t start, uint64_t end)
{
    return start >= DR_RANGE_BASE && end <= DR_RANGE_BASE + DR_RANGE_SIZE;
}

/***************************************************************************
 * The application.
 */

static int
app_main(bool print_maps)
{
    unsigned long flags, gs_base = 0;
    char line[512];
    FILE *f;
    DIR *dir;
    struct dirent *ent;
    bool leaked = false;
    __asm__ __volatile__("pushfq; popq %0" : "=r"(flags));
    printf("eflags.ID=%d\n", (flags & EFLAGS_ID) != 0);
    if (syscall(SYS_arch_prctl, ARCH_GET_GS, &gs_base) != 0)
        printf("arch_prctl failed\n");
    printf("gs_base=0x%lx\n", gs_base);
    /* An injector must not leave a descriptor for the DR library behind. */
    dir = opendir("/proc/self/fd");
    while (dir != NULL && (ent = readdir(dir)) != NULL) {
        char path[64], target[512];
        ssize_t len;
        snprintf(path, sizeof(path), "/proc/self/fd/%s", ent->d_name);
        len = readlink(path, target, sizeof(target) - 1);
        if (len > 0) {
            target[len] = '\0';
            if (strstr(target, "libdynamorio") != NULL) {
                printf("leaked descriptor %s -> %s\n", ent->d_name, target);
                leaked = true;
            }
        }
    }
    if (dir != NULL)
        closedir(dir);
    if (!leaked)
        printf("no leaked descriptors\n");
    if (print_maps) {
        f = fopen("/proc/self/maps", "r");
        while (f != NULL && fgets(line, sizeof(line), f) != NULL)
            printf("map %s", line);
        if (f != NULL)
            fclose(f);
    }
    return 0;
}

/***************************************************************************
 * The injector.
 */

static int memfd = -1;
static uint64_t stub_pc;

static void
mem_write(uint64_t addr, const void *buf, size_t len)
{
    if (pwrite(memfd, buf, len, (off_t)addr) != (ssize_t)len)
        fatal("pwrite at 0x%lx failed: %s", (unsigned long)addr, strerror(errno));
}

static void
mem_read(uint64_t addr, void *buf, size_t len)
{
    if (pread(memfd, buf, len, (off_t)addr) != (ssize_t)len)
        fatal("pread at 0x%lx failed: %s", (unsigned long)addr, strerror(errno));
}

static int
wait_stop(void)
{
    int status;
    if (waitpid(child, &status, __WALL) != child)
        fatal("waitpid failed: %s", strerror(errno));
    if (!WIFSTOPPED(status))
        fatal("child did not stop: status 0x%x", status);
    return status;
}

/* Executes a system call in the child using a "syscall; int3" stub at stub_pc. */
static long
remote_syscall(const struct user_regs_struct *saved, long nr, long a1, long a2, long a3,
               long a4, long a5, long a6)
{
    struct user_regs_struct regs = *saved;
    int status;
    regs.rip = stub_pc;
    regs.rax = nr;
    regs.orig_rax = -1;
    regs.rdi = a1;
    regs.rsi = a2;
    regs.rdx = a3;
    regs.r10 = a4;
    regs.r8 = a5;
    regs.r9 = a6;
    if (ptrace(PTRACE_SETREGS, child, 0, &regs) != 0 ||
        ptrace(PTRACE_CONT, child, 0, 0) != 0)
        fatal("ptrace failed: %s", strerror(errno));
    status = wait_stop();
    if (WSTOPSIG(status) != SIGTRAP)
        fatal("remote syscall %ld: unexpected signal %d", nr, WSTOPSIG(status));
    if (ptrace(PTRACE_GETREGS, child, 0, &regs) != 0)
        fatal("PTRACE_GETREGS failed: %s", strerror(errno));
    return (long)regs.rax;
}

/* Maps the PT_LOAD segments of the library at base, like an ELF loader. */
static uint64_t
map_library(const struct user_regs_struct *saved, const char *path, uint64_t base,
            uint64_t path_addr)
{
    Elf64_Ehdr ehdr;
    Elf64_Phdr phdr[32];
    uint64_t min_vaddr = UINT64_MAX, max_vaddr = 0;
    int i, fd = open(path, O_RDONLY);
    long res, remote_fd;
    if (fd < 0 || pread(fd, &ehdr, sizeof(ehdr), 0) != sizeof(ehdr) ||
        ehdr.e_phnum > sizeof(phdr) / sizeof(phdr[0]) ||
        pread(fd, phdr, ehdr.e_phnum * sizeof(phdr[0]), ehdr.e_phoff) !=
            (ssize_t)(ehdr.e_phnum * sizeof(phdr[0])))
        fatal("cannot read ELF headers of %s", path);
    close(fd);
    for (i = 0; i < ehdr.e_phnum; i++) {
        if (phdr[i].p_type != PT_LOAD)
            continue;
        if ((phdr[i].p_vaddr & ~0xfffULL) < min_vaddr)
            min_vaddr = phdr[i].p_vaddr & ~0xfffULL;
        if (phdr[i].p_vaddr + phdr[i].p_memsz > max_vaddr)
            max_vaddr = (phdr[i].p_vaddr + phdr[i].p_memsz + 0xfff) & ~0xfffULL;
    }
    mem_write(path_addr, path, strlen(path) + 1);
    remote_fd = remote_syscall(saved, SYS_openat, AT_FDCWD, path_addr, O_RDONLY, 0, 0, 0);
    if (remote_fd < 0)
        fatal("remote open of %s failed: %ld", path, remote_fd);
    res = remote_syscall(saved, SYS_mmap, base, max_vaddr - min_vaddr, PROT_NONE,
                         MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (res != (long)base)
        fatal("reserving the library range failed: %ld", res);
    for (i = 0; i < ehdr.e_phnum; i++) {
        int prot;
        uint64_t seg, file_end, map_end, mem_end;
        if (phdr[i].p_type != PT_LOAD)
            continue;
        prot = ((phdr[i].p_flags & PF_R) ? PROT_READ : 0) |
            ((phdr[i].p_flags & PF_W) ? PROT_WRITE : 0) |
            ((phdr[i].p_flags & PF_X) ? PROT_EXEC : 0);
        seg = base + (phdr[i].p_vaddr & ~0xfffULL) - min_vaddr;
        file_end = base + phdr[i].p_vaddr - min_vaddr + phdr[i].p_filesz;
        map_end = (file_end + 0xfff) & ~0xfffULL;
        mem_end =
            (base + phdr[i].p_vaddr - min_vaddr + phdr[i].p_memsz + 0xfff) & ~0xfffULL;
        res = remote_syscall(saved, SYS_mmap, seg, map_end - seg, prot,
                             MAP_PRIVATE | MAP_FIXED, remote_fd,
                             phdr[i].p_offset & ~0xfffULL);
        if (res != (long)seg)
            fatal("mapping segment %d failed: %ld", i, res);
        if (phdr[i].p_memsz > phdr[i].p_filesz) {
            /* Zero the rest of the last file page and map the rest of the bss. */
            static const char zeros[4096];
            if (map_end > file_end)
                mem_write(file_end, zeros, map_end - file_end);
            if (mem_end > map_end) {
                res = remote_syscall(saved, SYS_mmap, map_end, mem_end - map_end, prot,
                                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
                if (res != (long)map_end)
                    fatal("mapping the bss failed: %ld", res);
            }
        }
    }
    remote_syscall(saved, SYS_close, remote_fd, 0, 0, 0, 0, 0);
    return base + ehdr.e_entry - min_vaddr;
}

/* Runs DR's initialization up to its SIGTRAP, checking the system calls it makes:
 * it must not map memory outside of its range nor read configuration files.
 */
static void
run_dr_init(void)
{
    bool in_syscall = false, bad = false;
    int sig = 0, status;
    long nr = -1;
    struct user_regs_struct regs;
    while (true) {
        if (ptrace(PTRACE_SYSCALL, child, 0, sig) != 0)
            fatal("PTRACE_SYSCALL failed: %s", strerror(errno));
        status = wait_stop();
        sig = WSTOPSIG(status);
        if (sig == (SIGTRAP | 0x80)) {
            sig = 0;
            if (ptrace(PTRACE_GETREGS, child, 0, &regs) != 0)
                fatal("PTRACE_GETREGS failed: %s", strerror(errno));
            in_syscall = !in_syscall;
            if (in_syscall) {
                nr = (long)regs.orig_rax;
                if (nr == SYS_open || nr == SYS_openat) {
                    char path[256];
                    mem_read(nr == SYS_open ? regs.rdi : regs.rsi, path, sizeof(path));
                    path[sizeof(path) - 1] = '\0';
                    if (strstr(path, "config") != NULL ||
                        strstr(path, ".dynamorio") != NULL) {
                        printf("DR initialization opened %s\n", path);
                        bad = true;
                    }
                }
            } else if ((nr == SYS_mmap || nr == SYS_mremap) &&
                       (uint64_t)regs.rax < (uint64_t)-4096 &&
                       !in_dr_range(regs.rax, regs.rax + 1)) {
                printf("DR initialization mapped memory at 0x%llx\n", regs.rax);
                bad = true;
            }
            continue;
        }
        if (sig == SIGTRAP && (status >> 16) == 0) {
            siginfo_t info;
            if (ptrace(PTRACE_GETSIGINFO, child, 0, &info) == 0 &&
                info.si_code == SI_USER)
                break; /* DR is initialized. */
        }
        /* Pass through the signals DR raises itself during initialization. */
    }
    if (!bad)
        printf("DR initialization stayed inside its range\n");
}

static void
spawn_app(const char *self, bool traced, int out_fd)
{
    /* Make room for all of the output, as we read it once the child exits. */
    fcntl(out_fd, F_SETPIPE_SZ, MAX_OUTPUT);
    child = fork();
    if (child < 0)
        fatal("fork failed: %s", strerror(errno));
    if (child == 0) {
        const char *argv[] = { self, "app", NULL };
        dup2(out_fd, 1);
        dup2(out_fd, 2);
        close(out_fd);
        personality(ADDR_NO_RANDOMIZE);
        if (traced) {
            ptrace(PTRACE_TRACEME, 0, 0, 0);
            raise(SIGSTOP);
        }
        execv(self, (char **)argv);
        _exit(127);
    }
}

/* Waits for the child to exit, passing through all signals. */
static int
wait_exit(void)
{
    int status;
    while (true) {
        if (waitpid(child, &status, __WALL) != child)
            fatal("waitpid failed: %s", strerror(errno));
        if (WIFEXITED(status))
            return WEXITSTATUS(status);
        if (WIFSIGNALED(status))
            return 128 + WTERMSIG(status);
        if (WIFSTOPPED(status)) {
            int sig = WSTOPSIG(status);
            if (sig == SIGTRAP && (status >> 16) != 0)
                sig = 0; /* A ptrace event. */
            ptrace(PTRACE_CONT, child, 0, sig);
        }
    }
}

static int
read_output(int fd, char *buf, size_t size, char *lines[], int max_lines)
{
    size_t len = 0;
    ssize_t res;
    int num = 0;
    char *line, *save;
    while (len < size - 1 && (res = read(fd, buf + len, size - 1 - len)) > 0)
        len += res;
    buf[len] = '\0';
    for (line = strtok_r(buf, "\n", &save); line != NULL && num < max_lines;
         line = strtok_r(NULL, "\n", &save))
        lines[num++] = line;
    return num;
}

static bool
has_line(char *lines[], int num, const char *line)
{
    int i;
    for (i = 0; i < num; i++) {
        if (strcmp(lines[i], line) == 0)
            return true;
    }
    return false;
}

static int
run_native(const char *self, char *buf, char *lines[])
{
    int pipefd[2], status, num;
    if (pipe(pipefd) != 0)
        fatal("pipe failed");
    spawn_app(self, false, pipefd[1]);
    close(pipefd[1]);
    status = wait_exit();
    num = read_output(pipefd[0], buf, MAX_OUTPUT, lines, MAX_LINES);
    close(pipefd[0]);
    printf("native run exited with status %d\n", status);
    return num;
}

static int
run_under_dr(const char *self, const char *drlib, const char *client, char *buf,
             char *lines[])
{
    int pipefd[2], status, num;
    char path[64];
    struct user_regs_struct regs, saved;
    unsigned char orig_code[8];
    const unsigned char stub[8] = { 0x0f, 0x05, 0xcc, 0x90, 0x90, 0x90, 0x90, 0x90 };
    uint64_t entry, stack_base = DR_RANGE_BASE + DR_INIT_STACK_OFFS, args_addr;
    long res;
    static dr_ptrace_takeover_args_t args;

    if (pipe(pipefd) != 0)
        fatal("pipe failed");
    spawn_app(self, true, pipefd[1]);
    close(pipefd[1]);
    child_output = pipefd[0];
    wait_stop(); /* The SIGSTOP. */
    if (ptrace(PTRACE_SETOPTIONS, child, 0,
               PTRACE_O_EXITKILL | PTRACE_O_TRACEEXEC | PTRACE_O_TRACESYSGOOD) != 0)
        fatal("PTRACE_SETOPTIONS failed: %s", strerror(errno));
    if (ptrace(PTRACE_CONT, child, 0, 0) != 0)
        fatal("PTRACE_CONT failed: %s", strerror(errno));
    status = wait_stop();
    if (status >> 8 != (SIGTRAP | (PTRACE_EVENT_EXEC << 8)))
        fatal("expected an exec event, got status 0x%x", status);
    /* The exec event is reported inside execve: go to its exit, so that the
     * system call's return value does not overwrite the registers we set.
     */
    if (ptrace(PTRACE_SYSCALL, child, 0, 0) != 0)
        fatal("PTRACE_SYSCALL failed: %s", strerror(errno));
    status = wait_stop();
    if (WSTOPSIG(status) != (SIGTRAP | 0x80))
        fatal("expected the exit of execve, got status 0x%x", status);

    snprintf(path, sizeof(path), "/proc/%d/mem", child);
    memfd = open(path, O_RDWR);
    if (memfd < 0)
        fatal("cannot open %s: %s", path, strerror(errno));
    if (ptrace(PTRACE_GETREGS, child, 0, &saved) != 0)
        fatal("PTRACE_GETREGS failed: %s", strerror(errno));
    stub_pc = saved.rip;
    mem_read(stub_pc, orig_code, sizeof(orig_code));
    mem_write(stub_pc, stub, sizeof(stub));

    /* A stack in DR's range for its initialization, so it does not touch the
     * application's stack.  We also use it for the library path.
     */
    res = remote_syscall(&saved, SYS_mmap, stack_base, DR_INIT_STACK_SIZE,
                         PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (res != (long)stack_base)
        fatal("mapping the initialization stack failed: %ld", res);
    entry = map_library(&saved, drlib, DR_RANGE_BASE, stack_base);
    mem_write(stub_pc, orig_code, sizeof(orig_code));

    memset(&args, 0, sizeof(args));
    args.argc = DR_PTRACE_TAKEOVER_ARGC;
    args.magic = DR_PTRACE_TAKEOVER_MAGIC;
    args.version = DR_PTRACE_TAKEOVER_VERSION;
    args.size = sizeof(args);
    args.flags = DR_PTRACE_TAKEOVER_SEGMENT_BASES;
    args.page_size = sysconf(_SC_PAGESIZE);
    args.fs_base = saved.fs_base;
    args.gs_base = APP_GS_BASE;
    snprintf(args.options, sizeof(args.options),
             "-code_api -stderr_mask 0xC -dumpcore_mask 0 "
             "-client_lib '%s;0;-greeting hello' "
             "-reachable_heap -vm_base 0x%llx -vm_max_offset 0x0 -vm_size %s "
             "-no_vm_base_near_app -no_vm_allow_not_at_base "
             "-no_switch_to_os_at_vmm_reset_limit",
             client, DR_RANGE_BASE + DR_VMCODE_OFFS, DR_VMCODE_SIZE);
    args.mc.size = sizeof(args.mc);
    args.mc.flags = DR_MC_INTEGER | DR_MC_CONTROL;
    args.mc.xax = saved.rax;
    args.mc.xbx = saved.rbx;
    args.mc.xcx = saved.rcx;
    args.mc.xdx = saved.rdx;
    args.mc.xsi = saved.rsi;
    args.mc.xdi = saved.rdi;
    args.mc.xbp = saved.rbp;
    args.mc.xsp = saved.rsp;
    args.mc.r8 = saved.r8;
    args.mc.r9 = saved.r9;
    args.mc.r10 = saved.r10;
    args.mc.r11 = saved.r11;
    args.mc.r12 = saved.r12;
    args.mc.r13 = saved.r13;
    args.mc.r14 = saved.r14;
    args.mc.r15 = saved.r15;
    args.mc.xflags = saved.eflags | EFLAGS_ID;
    args.mc.pc = (byte *)saved.rip;
    args_addr = (stack_base + DR_INIT_STACK_SIZE - sizeof(args)) & ~0xfULL;
    mem_write(args_addr, &args, sizeof(args));

    regs = saved;
    regs.rip = entry;
    regs.rsp = args_addr;
    regs.rdi = 0;
    regs.orig_rax = -1;
    if (ptrace(PTRACE_SETREGS, child, 0, &regs) != 0)
        fatal("PTRACE_SETREGS failed: %s", strerror(errno));
    run_dr_init();
    if (ptrace(PTRACE_CONT, child, 0, 0) != 0)
        fatal("PTRACE_CONT failed: %s", strerror(errno));
    /* The output fits into the pipe (see spawn_app()): we read it at the end. */
    status = wait_exit();
    num = read_output(pipefd[0], buf, MAX_OUTPUT, lines, MAX_LINES);
    close(pipefd[0]);
    child_output = -1;
    printf("run under DR exited with status %d\n", status);
    close(memfd);
    return num;
}

/* Runs the app under "drrun -use_ptrace", which must return once the app exits. */
static void
run_drrun_ptrace(const char *self, const char *drlib, const char *drrun)
{
    static char buf[MAX_OUTPUT];
    static char *lines[MAX_LINES];
    int pipefd[2], status, num, i;
    if (pipe(pipefd) != 0)
        fatal("pipe failed");
    fcntl(pipefd[1], F_SETPIPE_SZ, MAX_OUTPUT);
    child = fork();
    if (child < 0)
        fatal("fork failed: %s", strerror(errno));
    if (child == 0) {
        const char *argv[] = { drrun, "-use_ptrace", "-quiet",
                               /* Use the same build of DR. */
                               strstr(drlib, "/debug/") != NULL ? "-debug" : "-quiet",
                               "-stderr_mask", "0xC", "-dumpcore_mask", "0", "--", self,
                               "app-brief", NULL };
        dup2(pipefd[1], 1);
        dup2(pipefd[1], 2);
        close(pipefd[0]);
        close(pipefd[1]);
        execv(drrun, (char **)argv);
        _exit(127);
    }
    close(pipefd[1]);
    status = wait_exit();
    num = read_output(pipefd[0], buf, MAX_OUTPUT, lines, MAX_LINES);
    close(pipefd[0]);
    for (i = 0; i < num; i++)
        printf("drrun -use_ptrace: %s\n", lines[i]);
    printf("drrun -use_ptrace exited with status %d\n", status);
}

int
main(int argc, const char *argv[])
{
    static char native_buf[MAX_OUTPUT], dr_buf[MAX_OUTPUT];
    static char *native_lines[MAX_LINES], *dr_lines[MAX_LINES];
    int num_native, num_dr, i, num_in_range = 0, num_bad = 0;
    char self[512];
    ssize_t len;

    if (argc == 2 && strcmp(argv[1], "app") == 0)
        return app_main(true);
    if (argc == 2 && strcmp(argv[1], "app-brief") == 0)
        return app_main(false);
    if (argc != 4)
        fatal("usage: %s <libdynamorio.so> <client.so> <drrun>", argv[0]);
    len = readlink("/proc/self/exe", self, sizeof(self) - 1);
    if (len <= 0)
        fatal("cannot find our own path");
    self[len] = '\0';
    setvbuf(stdout, NULL, _IOLBF, 0);
    signal(SIGALRM, handle_alarm);
    alarm(90);

    num_native = run_native(self, native_buf, native_lines);
    num_dr = run_under_dr(self, argv[1], argv[2], dr_buf, dr_lines);

    for (i = 0; i < num_dr; i++) {
        unsigned long long start, end;
        if (strncmp(dr_lines[i], "map ", 4) != 0) {
            /* The application's and the client's other output. */
            printf("%s\n", dr_lines[i]);
            continue;
        }
        if (sscanf(dr_lines[i] + 4, "%llx-%llx", &start, &end) != 2)
            fatal("cannot parse %s", dr_lines[i]);
        if (in_dr_range(start, end))
            num_in_range++;
        else if (!has_line(native_lines, num_native, dr_lines[i])) {
            printf("unexpected mapping under DR: %s\n", dr_lines[i] + 4);
            num_bad++;
        }
    }
    for (i = 0; i < num_native; i++) {
        if (strncmp(native_lines[i], "map ", 4) == 0 &&
            !has_line(dr_lines, num_dr, native_lines[i])) {
            printf("missing mapping under DR: %s\n", native_lines[i] + 4);
            num_bad++;
        }
    }
    if (num_in_range == 0)
        printf("no mappings in DR's range\n");
    if (num_bad == 0)
        printf("the mappings outside DR's range match the native run\n");

    run_drrun_ptrace(self, argv[1], argv[3]);
    return 0;
}
