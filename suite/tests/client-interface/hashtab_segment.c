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

/* Loads the libraries named on the command line, which have their hash table in a
 * segment whose file offset differs from its offset in the image (see
 * hashtab_segment.appdll.c), and calls the function each one exports.  Then does
 * the same for a copy of the first library with its .dynamic section moved to a
 * new segment at the end of the file, past what the loader's initial mapping of
 * the file holds, as patchelf does when it has to grow .dynamic.
 */

#include "tools.h"
#include <dlfcn.h>
#include <elf.h>
#include <link.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MOVED_DYNAMIC_LIB "libclient.hashtab_segment_dynamic.appdll.so"

typedef int (*export_func_t)(int);

typedef struct _layout_query_t {
    const char *path;
    bool hashtab_moved;
    bool dynamic_moved;
} layout_query_t;

/* Sets query->hashtab_moved if the library at query->path has its hash table in a
 * segment whose file offset differs from its offset in the image, and
 * query->dynamic_moved if its .dynamic is past the part of the file that the
 * loader's initial mapping of the file holds: those are the layouts the test is
 * about.
 */
static int
check_layout(struct dl_phdr_info *info, size_t size, void *data)
{
    layout_query_t *query = (layout_query_t *)data;
    const ElfW(Phdr) *first_load = NULL;
    const ElfW(Phdr) *dynamic = NULL;
    ElfW(Addr) hashtab = 0, image_end = 0;
    size_t page_size = (size_t)sysconf(_SC_PAGESIZE);
    int i;
    if (strcmp(info->dlpi_name, query->path) != 0)
        return 0;
    for (i = 0; i < info->dlpi_phnum; i++) {
        const ElfW(Phdr) *phdr = &info->dlpi_phdr[i];
        if (phdr->p_type == PT_LOAD) {
            if (first_load == NULL)
                first_load = phdr;
            if (phdr->p_vaddr + phdr->p_memsz > image_end)
                image_end = phdr->p_vaddr + phdr->p_memsz;
        }
        if (phdr->p_type == PT_DYNAMIC) {
            const ElfW(Dyn) *dyn = (const ElfW(Dyn) *)(info->dlpi_addr + phdr->p_vaddr);
            dynamic = phdr;
            for (; dyn->d_tag != DT_NULL; dyn++) {
                if (dyn->d_tag == DT_GNU_HASH || dyn->d_tag == DT_HASH)
                    hashtab = dyn->d_un.d_ptr;
            }
        }
    }
    /* The loader may have relocated .dynamic. */
    if (hashtab >= info->dlpi_addr)
        hashtab -= info->dlpi_addr;
    for (i = 0; i < info->dlpi_phnum; i++) {
        const ElfW(Phdr) *phdr = &info->dlpi_phdr[i];
        if (phdr->p_type == PT_LOAD && hashtab >= phdr->p_vaddr &&
            hashtab - phdr->p_vaddr < phdr->p_filesz) {
            query->hashtab_moved = first_load != NULL &&
                phdr->p_vaddr - phdr->p_offset !=
                    first_load->p_vaddr - first_load->p_offset;
        }
    }
    /* The initial mapping spans the image from the file offset of its start. */
    query->dynamic_moved = first_load != NULL && dynamic != NULL &&
        dynamic->p_offset + dynamic->p_filesz > ALIGN_FORWARD(image_end, page_size) -
                ALIGN_BACKWARD(first_load->p_vaddr, page_size) +
                ALIGN_BACKWARD(first_load->p_offset, page_size);
    return 1;
}

static void
test_library(const char *path)
{
    const char *name = strrchr(path, '/') == NULL ? path : strrchr(path, '/') + 1;
    layout_query_t query = { path, false, false };
    export_func_t func;
    void *lib = dlopen(path, RTLD_NOW);
    if (lib == NULL) {
        print("failed to load %s: %s\n", path, dlerror());
        return;
    }
    dl_iterate_phdr(check_layout, &query);
    if (dlsym(lib, "hashtab_segment_missing") != NULL)
        print("found a missing symbol in %s\n", name);
    func = (export_func_t)dlsym(lib, "hashtab_segment_export");
    /* The client checks that this call reaches the address it looked up. */
    print("%s: hash table %s, .dynamic %s, export returned %d\n", name,
          query.hashtab_moved ? "moved" : "in place",
          query.dynamic_moved ? "moved" : "not moved", func == NULL ? -1 : func(41));
    dlclose(lib);
}

/* Writes to dst a copy of the library at src whose .dynamic is in a new segment at
 * the end of the file, past the end of the image, in place of the first PT_NOTE
 * program header.  Returns whether it succeeded.
 */
static bool
write_moved_dynamic(const char *src, const char *dst)
{
    FILE *file;
    char *buf = NULL, *new_buf;
    long size = -1;
    size_t new_size, new_offset;
    ElfW(Ehdr) ehdr;
    ElfW(Phdr) * phdrs;
    ElfW(Addr) image_end = 0, align = 0, new_vaddr;
    int i, dynamic = -1, note = -1;
    bool ok = false;
    file = fopen(src, "rb");
    if (file == NULL)
        return false;
    if (fseek(file, 0, SEEK_END) == 0)
        size = ftell(file);
    if (size >= (long)sizeof(ehdr) && fseek(file, 0, SEEK_SET) == 0) {
        buf = malloc(size);
        if (buf != NULL && fread(buf, 1, size, file) != (size_t)size) {
            free(buf);
            buf = NULL;
        }
    }
    fclose(file);
    if (buf == NULL)
        return false;
    memcpy(&ehdr, buf, sizeof(ehdr));
    if (ehdr.e_phentsize != sizeof(*phdrs) ||
        ehdr.e_phoff + ehdr.e_phnum * sizeof(*phdrs) > (size_t)size)
        goto done;
    phdrs = (ElfW(Phdr) *)(buf + ehdr.e_phoff);
    for (i = 0; i < ehdr.e_phnum; i++) {
        if (phdrs[i].p_type == PT_LOAD) {
            /* The new segment must come last to keep the segments sorted. */
            if (note >= 0)
                goto done;
            if (phdrs[i].p_vaddr + phdrs[i].p_memsz > image_end)
                image_end = phdrs[i].p_vaddr + phdrs[i].p_memsz;
            if (phdrs[i].p_align > align)
                align = phdrs[i].p_align;
        } else if (phdrs[i].p_type == PT_DYNAMIC)
            dynamic = i;
        else if (phdrs[i].p_type == PT_NOTE && note < 0)
            note = i;
    }
    if (dynamic < 0 || note < 0 || align == 0 ||
        phdrs[dynamic].p_offset + phdrs[dynamic].p_filesz > (size_t)size)
        goto done;
    /* The initial mapping spans the image, so placing the new segment's data past
     * the image's end in the file keeps it out of that mapping.
     */
    new_vaddr = ALIGN_FORWARD(image_end, align);
    new_offset = new_vaddr + phdrs[dynamic].p_filesz;
    if (new_offset < (size_t)size)
        new_offset = size;
    new_offset = ALIGN_FORWARD(new_offset, align);
    new_size = new_offset + phdrs[dynamic].p_filesz;
    new_buf = calloc(new_size, 1);
    if (new_buf == NULL)
        goto done;
    memcpy(new_buf, buf, size);
    memcpy(new_buf + new_offset, buf + phdrs[dynamic].p_offset, phdrs[dynamic].p_filesz);
    free(buf);
    buf = new_buf;
    phdrs = (ElfW(Phdr) *)(buf + ehdr.e_phoff);
    phdrs[note].p_type = PT_LOAD;
    phdrs[note].p_flags = PF_R | PF_W;
    phdrs[note].p_offset = new_offset;
    phdrs[note].p_vaddr = new_vaddr;
    phdrs[note].p_paddr = new_vaddr;
    phdrs[note].p_filesz = phdrs[dynamic].p_filesz;
    phdrs[note].p_memsz = phdrs[dynamic].p_filesz;
    phdrs[note].p_align = align;
    phdrs[dynamic].p_offset = new_offset;
    phdrs[dynamic].p_vaddr = new_vaddr;
    phdrs[dynamic].p_paddr = new_vaddr;
    file = fopen(dst, "wb");
    if (file == NULL)
        goto done;
    ok = fwrite(buf, 1, new_size, file) == new_size;
    if (fclose(file) != 0)
        ok = false;
done:
    free(buf);
    return ok;
}

int
main(int argc, const char *argv[])
{
    int i;
    for (i = 1; i < argc; i++)
        test_library(argv[i]);
    if (argc > 1) {
        /* We put the copy next to the first library. */
        char path[4096];
        const char *slash = strrchr(argv[1], '/');
        if (slash == NULL) {
            snprintf(path, sizeof(path), "./%s", MOVED_DYNAMIC_LIB);
        } else {
            snprintf(path, sizeof(path), "%.*s/%s", (int)(slash - argv[1]), argv[1],
                     MOVED_DYNAMIC_LIB);
        }
        path[sizeof(path) - 1] = '\0';
        if (write_moved_dynamic(argv[1], path)) {
            test_library(path);
            unlink(path);
        } else
            print("failed to write %s\n", path);
    }
    print("all done\n");
    return 0;
}
