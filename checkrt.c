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
typedef Elf64_Ehdr Elf_Ehdr;
typedef Elf64_Shdr Elf_Shdr;
typedef Elf64_Sym  Elf_Sym;
#else
typedef Elf32_Ehdr Elf_Ehdr;
typedef Elf32_Shdr Elf_Shdr;
typedef Elf32_Sym  Elf_Sym;
#endif


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

/* find symbol by prefix */
static char *find_symbol(const char *path, uint8_t *addr)
{
    Elf_Ehdr *ehdr = (Elf_Ehdr *)addr;
    Elf_Shdr *shdr = (Elf_Shdr *)(addr + ehdr->e_shoff);
    Elf_Shdr *dyn = NULL;

    /* look for section header type SHT_DYNSYM */
    for (size_t i = 0; i < ehdr->e_shnum; i++) {
        if (shdr[i].sh_type == SHT_DYNSYM) {
            dyn = &shdr[i];
            break;
        }
    }

    if (!dyn) {
        return NULL;
    }

    /* get string table */
    Elf_Shdr *strtab = shdr + ehdr->e_shstrndx;
    const char *strings = (const char *)(addr + strtab->sh_offset);
    size_t dynstr_off = 0;

    /* get .dynstr section */
    for (size_t i = 0; i < ehdr->e_shnum; i++) {
        if (strcmp(strings + shdr[i].sh_name, ".dynstr") == 0) {
            dynstr_off = shdr[i].sh_offset;
            break;
        }
    }

    if (dynstr_off == 0) {
        return NULL;
    }

    const char *symbol = NULL;
    const size_t num = dyn->sh_size / dyn->sh_entsize;
    Elf_Sym *sym = (Elf_Sym *)(addr + dyn->sh_offset);
    strings = (const char *)(addr + dynstr_off);

    /* parse symbols */
    for (size_t i = 0; i < num; i++) {
        const char *name = strings + sym[i].st_name;

        if (*name == *prefix[idx] &&
            strncmp(name, prefix[idx], prefix_len[idx]) == 0 &&  /* symbol name starts with prefix */
            isdigit(*(name + prefix_len[idx])) &&  /* first byte after prefix is a digit */
            strchr(name + prefix_len[idx], '.') &&  /* symbol name contains a dot */
            ELF64_ST_TYPE(sym[i].st_info) == STT_OBJECT &&  /* data object */
            ELF64_ST_BIND(sym[i].st_info) == STB_GLOBAL &&  /* global symbol */
            ELF64_ST_VISIBILITY(sym[i].st_other) == STV_DEFAULT &&  /* default symbol visibility */
            (!symbol || strverscmp(symbol, name) < 0))  /* get higher version string */
        {
            if (full_debug_mode) {
                DEBUG_PRINT("%s", name);
            }
            symbol = name;
        }
    }

    return symbol ? strdup(symbol) : NULL;
}

/* mmap() library and look for symbol by prefix */
static char *symbol_version(const char *path)
{
    struct stat st;
    int fd;

    /* let dlmopen() do compatibility checks for us (OS/API, bitness, etc.) */
    void *handle = load_lib_new_namespace(path);
    dlclose(handle);

    /* open file */
    if ((fd = open(path, O_RDONLY)) < 0) {
        err(1, "failed to open() file for reading: %s", path);
    }

    if (fstat(fd, &st) < 0) {
        err(1, "fstat() failed on file: %s", path);
    }

    /* mmap() library */
    uint8_t *addr = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);

    if (addr == MAP_FAILED) {
        err(1, "mmap() failed: %s", path);
    }

    /* look for symbol */
    char *symbol = find_symbol(path, addr);

    if (symbol) {
        DEBUG_PRINT("symbol %s found in: %s", symbol, path);
    }

    /* free resources */
    if (munmap(addr, st.st_size) == -1) {
        warn("%s", "munmap() returned with an error");
    }

    close(fd);

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
    if (!self) err(1, "%s", "realpath() failed to resolve /proc/self/exe");

    /* modifies "self" */
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
