/* Copyright (c) 2022-2025 Carsten Janssen <djcj@gmx.de>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <assert.h>
#include <ctype.h>
#include <dlfcn.h>
#include <elf.h>
#include <err.h>
#include <fcntl.h>
#include <libgen.h>
#include <link.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>


#if defined(_LP64) || defined(__LP64__)
#define DEF(x) typedef Elf64_##x Elf_##x
#else
#define DEF(x) typedef Elf32_##x Elf_##x
#endif

DEF(Half);
DEF(Word);
DEF(Ehdr);
DEF(Shdr);
DEF(Dyn);
DEF(Verdef);
DEF(Verdaux);


typedef struct {
    int fd;
    size_t size;
    uint8_t *addr;
} mem_map_t;


/* debug messages */
#define DEBUG_PRINT(MSG, ...) \
    if (debug_mode) { \
        fprintf(stderr, "[DEBUG] %s: " MSG "\n", __func__, __VA_ARGS__); \
    }

static bool debug_mode = false;
static bool full_debug_mode = false;


/* index values */
#define STDCXX 0
#define LIBGCC 1
static int idx = STDCXX;


/* save strings and string lengths as const values */
#define SET_ARRAY(VARNAME, ENTRY1, ENTRY2) \
    static const char * const VARNAME[2] = { ENTRY1, ENTRY2 }; \
    static const size_t VARNAME##_len[2] = { sizeof(ENTRY1)-1, sizeof(ENTRY2)-1 }

SET_ARRAY(libname, "libstdc++.so.6", "libgcc_s.so.1");
SET_ARRAY(subdir,  "cxx",            "gcc");
SET_ARRAY(prefix,  "GLIBCXX_",       "GCC_");


static void errx_dlerror(const char *filename, const char *msg) __attribute__((noreturn));
static mem_map_t *map_file(const char *path) __attribute__((returns_nonnull));
static void *load_lib_new_namespace(const char *filename) __attribute__((returns_nonnull));



static void errx_dlerror(const char *filename, const char *msg)
{
    const char *p = dlerror();

    if (p) {
        errx(1, "%s\n%s", msg, p);
    } else {
        errx(1, "%s: %s", msg, filename);
    }
}


static mem_map_t *map_file(const char *path)
{
    struct stat st;
    int fd;
    uint8_t *addr;

    if ((fd = open(path, O_RDONLY)) == -1) {
        err(1, "open(): %s", path);
    }

    if (fstat(fd, &st) == -1) {
        err(1, "fstat(): %s", path);
    }

    if ((addr = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0)) == MAP_FAILED) {
        err(1, "mmap(): %s", path);
    }

    mem_map_t *mm = malloc(sizeof(mem_map_t));
    mm->fd = fd;
    mm->size = st.st_size;
    mm->addr = addr;

    return mm;
}


static void unmap_file(mem_map_t *mm)
{
    /* free resources */
    if (munmap(mm->addr, mm->size) == -1) {
        warn("%s", "munmap() returned with an error");
    }

    close(mm->fd);
    free(mm);
}


/* load library into new namespace;
 * dlmopen() will also perform compatibility checks for us */
static void *load_lib_new_namespace(const char *filename)
{
    void *handle = dlmopen(LM_ID_NEWLM, filename, RTLD_LAZY);

    if (!handle) {
        errx_dlerror(filename, "dlmopen() failed to load library");
    }

    return handle;
}


/* retrieve full path of system library */
static char *get_system_library_path(void)
{
    struct link_map *map = NULL;

    void *handle = load_lib_new_namespace(libname[idx]);

    if (dlinfo(handle, RTLD_DI_LINKMAP, &map) == -1) {
        errx_dlerror(libname[idx], "dlinfo() could not retrieve information from library");
    }

    if (!map->l_name || map->l_name[0] == 0) {
        errx(1, "%s: %s", libname[idx], "dlinfo() failed to get absolute pathname");
    }

    char *path = strdup(map->l_name);
    DEBUG_PRINT("%s resolved to: %s", libname[idx], path);

    dlclose(handle);

    return path;
}


/* copy library from system into directory next to binary */
static void copy_lib(const char *dir, int flag)
{
    ssize_t nread;
    uint8_t buf[512*1024];

    assert(flag == STDCXX || flag == LIBGCC);
    idx = flag;

    /* find library */
    char *src = get_system_library_path();
    printf("Copy library: %s\n", src);

    /* create target directory */
    char *target = malloc(strlen(dir) + subdir_len[idx] + libname_len[idx] + 3);
    sprintf(target, "%s/%s/", dir, subdir[idx]);
    mkdir(target, 0775);

    /* open source file for reading */
    int fd_in = open(src, O_RDONLY);
    if (fd_in < 0) err(1, "cannot open file for reading: %s", src);

    /* open target file for writing */
    strcat(target, libname[idx]);
    int fd_out = open(target, O_WRONLY | O_CREAT | O_TRUNC, 0664);
    if (fd_out < 0) err(1, "cannot open file for writing: %s", target);

    /* copy file content */
    while ((nread = read(fd_in, buf, sizeof(buf))) > 0) {
        if (write(fd_out, buf, nread) != nread) {
            err(1, "error writing to file: %s", target);
        }
    }

    if (nread == -1) {
        err(1, "error reading from file: %s", src);
    }

    /* free resources */
    close(fd_out);
    close(fd_in);
    free(src);
    free(target);
}


/* perform filesize check */
static inline uint8_t *get_offset(mem_map_t *mm, size_t offset)
{
    if (offset >= mm->size) {
        errx(1, "%s", "*** offset exceeds filesize ***"); \
    }

    return mm->addr + offset;
}


/* get section header by name */
static Elf_Shdr *shdr_by_name(mem_map_t *mm, Elf_Ehdr *ehdr, Elf_Shdr *shdr, const char *name)
{
    Elf_Shdr *strtab = &shdr[ehdr->e_shstrndx];

    for (size_t i = 0; i < ehdr->e_shnum; i++) {
        const char *ptr = (const char *)get_offset(mm, strtab->sh_offset + shdr[i].sh_name);

        if (strcmp(ptr, name) == 0) {
            return &shdr[i];
        }
    }

    return NULL;
}


/* get section header by type */
static Elf_Shdr *shdr_by_type(Elf_Shdr *shdr, Elf_Half shnum, Elf_Word type)
{
    for (Elf_Half i = 0; i < shnum; i++) {
        if (shdr[i].sh_type == type) {
            return &shdr[i];
        }
    }

    return NULL;
}


/* get value from DT_VERDEFNUM entry */
static size_t get_verdefnum(mem_map_t *mm, Elf_Shdr *dynamic)
{
    if (dynamic->sh_size == 0 || dynamic->sh_entsize == 0) {
        return 0;
    }

    Elf_Dyn *dyn = (Elf_Dyn *)get_offset(mm, dynamic->sh_offset);

    for (size_t i = 0; i < (dynamic->sh_size / dynamic->sh_entsize); i++, dyn++) {
        if (dyn->d_tag == DT_VERDEFNUM) {
            return dyn->d_un.d_val;
        }
    }

    return 0;
}


/**
 * find symbol in SHT_GNU_verdef section
 * https://refspecs.linuxfoundation.org/LSB_3.0.0/LSB-PDA/LSB-PDA.junk/symversion.html
 */
static char *find_symbol(mem_map_t *mm)
{
    Elf_Ehdr *ehdr = (Elf_Ehdr *)mm->addr;
    Elf_Shdr *shdr = (Elf_Shdr *)get_offset(mm, ehdr->e_shoff);

    if (ehdr->e_type != ET_DYN) {
        /* not a shared object file */
        return NULL;
    }

    /* section headers */
    Elf_Shdr *dynamic = shdr_by_name(mm, ehdr, shdr, ".dynamic");
    Elf_Shdr *verdef = shdr_by_type(shdr, ehdr->e_shnum, SHT_GNU_verdef);

    if (!dynamic || !verdef) {
        return NULL;
    }

    /* get numbers of SHT_GNU_verdef entries from .dynamic's DT_VERDEFNUM entry */
    size_t verdefnum = get_verdefnum(mm, dynamic);

    if (verdefnum == 0) {
        return NULL;
    }

    /* link to section that holds the strings referenced
     * by SHT_GNU_verdef section */
    Elf_Shdr *link = &shdr[verdef->sh_link];

    /* parse SHT_GNU_verdef section */
    size_t vd_off = verdef->sh_offset;
    const char *symbol = NULL;

    for (size_t i = 0; i < verdefnum; i++) {
        Elf_Verdef *vd = (Elf_Verdef *)get_offset(mm, vd_off);

        /* vd_version must be 1, skip VER_FLG_BASE entry (library name) */
        if (vd->vd_version == 1 && vd->vd_flags != VER_FLG_BASE) {
            /* get only the latest version instead of iterating all Elf_Verdaux entries */
            Elf_Verdaux *vda = (Elf_Verdaux *)get_offset(mm, vd_off + vd->vd_aux);
            const char *name = (const char *)get_offset(mm, link->sh_offset + vda->vda_name);

            if (strncmp(name, prefix[idx], prefix_len[idx]) == 0 && /* symbol name starts with prefix */
                isdigit(*(name + prefix_len[idx])) &&       /* first byte after prefix is a digit */
                strchr(name + prefix_len[idx], '.') &&      /* symbol name contains a dot */
                (!symbol || strverscmp(symbol, name) < 0))  /* get higher version string */
            {
                if (full_debug_mode) {
                    DEBUG_PRINT("%s", name);
                }

                symbol = name;
            }
        }

        vd_off += vd->vd_next;
    }

    return symbol ? strdup(symbol) : NULL;
}


/* mmap() library and look for symbol by prefix */
static char *symbol_version(const char *path)
{
    /* let dlmopen() do compatibility checks for us (OS/API, bitness, etc.) */
    void *handle = load_lib_new_namespace(path);
    dlclose(handle);

    /* mmap() library and look for symbol */
    mem_map_t *mm = map_file(path);
    char *symbol = find_symbol(mm);

    if (symbol) {
        DEBUG_PRINT("symbol %s found in: %s", symbol, path);
    }

    unmap_file(mm);

    return symbol;
}


/* compare symbol versions and return true
 * if we should use the bundled library */
static bool use_bundled_library(const char *dir, int flag)
{
    bool rv = false;

    assert(flag == STDCXX || flag == LIBGCC);
    idx = flag;

    char *lib_bundle = malloc(strlen(dir) + subdir_len[idx] + libname_len[idx] + 3);
    sprintf(lib_bundle, "%s/%s/%s", dir, subdir[idx], libname[idx]);

    /* check if bundled file exists */
    if (access(lib_bundle, F_OK) == 0) {
        /* get symbols */
        char *sym_bundle = symbol_version(lib_bundle);
        char *lib_sys = get_system_library_path();
        char *sym_sys = symbol_version(lib_sys);

        /* compare symbols */
        if (sym_bundle && sym_sys && strverscmp(sym_bundle, sym_sys) > 0) {
            rv = true;
        }

        free(lib_sys);
        free(sym_sys);
        free(sym_bundle);
    } else {
        DEBUG_PRINT("no access or file does not exist: %s", lib_bundle);
    }

    DEBUG_PRINT("use %s %s library", rv ? "BUNDLED" : "SYSTEM", libname[idx]);
    free(lib_bundle);

    return rv;
}


/* get full dirname of executable */
static char *get_exe_dir()
{
    char *self = realpath("/proc/self/exe", NULL);

    if (!self) {
        err(1, "%s", "realpath() failed to resolve /proc/self/exe");
    }

    /* dirname() modifies and returns "self" */
    char *pdirname = dirname(self);

    if (pdirname[0] != '/' || pdirname != self) {
        errx(1, "dirname() returned an unexpected result: %s", pdirname);
    }

    DEBUG_PRINT("exe directory found at: %s", self);

    return self;
}


/* compare symbol versions of bundled and system libraries */
static void compare_library_symbols()
{
    char *dir = get_exe_dir();
    bool gcc = use_bundled_library(dir, LIBGCC);
    bool cxx = use_bundled_library(dir, STDCXX);

    /* FIRST add libgcc to search path, THEN libstdc++ */
    if (gcc) printf("%s/gcc", dir);
    if (gcc && cxx) putchar(':');
    if (cxx) printf("%s/cxx", dir);
    if (gcc || cxx) putchar('\n');

    free(dir);
}


int main(int argc, char **argv)
{
    const char *usage =
        "usage: %s [--copy|--help]\n"
        "\n"
        "Set environment variable CHECKRT_DEBUG to enable extra verbose output.\n"
        "Set CHECKRT_DEBUG=FULL to enable full verbosity.\n";

    char *env = getenv("CHECKRT_DEBUG");

    if (env) {
        if (strcasecmp(env, "full") == 0) {
            full_debug_mode = true;
        }
        debug_mode = true;
    }

    if (argc < 2) {
        compare_library_symbols();
        return 0;
    }

    if (argc == 2 && strcmp(argv[1], "--copy") == 0) {
        /* copy system libraries next to executable */
        char *dir = get_exe_dir();
        copy_lib(dir, LIBGCC);
        copy_lib(dir, STDCXX);
        free(dir);
        return 0;
    }

    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        fprintf(stderr, usage, argv[0]);
        return 0;
    }

    fprintf(stderr, "%s\n", "error: unknown argument(s) given");
    fprintf(stderr, usage, argv[0]);

    return 1;
}
