/* Copyright (c) 2022-2023 <djcj@gmx.de>
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
#include <ctype.h>
#include <dlfcn.h>
#include <elf.h>
#include <err.h>
#include <fcntl.h>
#include <libgen.h>
#include <link.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
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

typedef struct {
    const char *filename;
    int fd;
    size_t size;
    uint8_t *address;
} mmap_t;


#define FREE(x) \
    if (x) { free(x); }

#ifdef NDEBUG
#define DEBUG_PRINT(MSG, ...) /**/
#else
#define DEBUG_PRINT(MSG, ...) \
    if (getenv("CHECKRT_DEBUG")) { \
        fprintf(stderr, "[DEBUG] %s():  " MSG "\n", __func__, __VA_ARGS__); \
    }
#endif


/* retrieve full path of (system) library */
char *get_libpath(const char *lib)
{
    /* not only do we get the full path but dlmopen()
     * also does all the compatibility checks for us */

    struct link_map *map = NULL;

    /* open in new namespace */
    void *handle = dlmopen(LM_ID_NEWLM, lib, RTLD_LAZY);

    if (!handle) {
        char *p = dlerror();
        if (p) fprintf(stderr, "%s\n", p);
        err(EXIT_FAILURE, "dlmopen() failed to load library: %s\n", lib);
    }

    if (dlinfo(handle, RTLD_DI_LINKMAP, &map) == -1 || map->l_name[0] == 0) {
        dlclose(handle);
        err(EXIT_FAILURE, "dlinfo() could not retrieve information from library: %s\n", lib);
    }

    char *path = strdup(map->l_name);
    DEBUG_PRINT("%s resolved to: %s", lib, path);

    dlclose(handle);

    return path;
}

/* copy library from system into directory next to binary */
void copy_lib(const char *libname, const char *dirname1, const char *dirname2)
{
    /* find library */
    char *src = get_libpath(libname);
    if (!src) err(EXIT_FAILURE, "cannot find %s in system", libname);

    /* create target directory */
    const size_t len = strlen(dirname1) + strlen(dirname2) + strlen(libname) + 4;
    char *target = malloc(len);
    sprintf(target, "%s/%s", dirname1, dirname2);
    mkdir(target, 0775);

    /* open source file for reading */
    int fd_in = open(src, O_RDONLY);
    if (fd_in < 0) err(EXIT_FAILURE, "cannot open file for reading: %s", src);

    /* open target file for writing */
    sprintf(target, "%s/%s/%s", dirname1, dirname2, libname);
    int fd_out = open(target, O_WRONLY | O_CREAT | O_TRUNC, 0664);
    if (fd_out < 0) err(EXIT_FAILURE, "cannot open file for writing: %s", target);

    /* copy file content */
    ssize_t nread;
    uint8_t buf[512*1024];

    DEBUG_PRINT("copy from [[%s]] to: %s", src, target);

    while ((nread = read(fd_in, buf, sizeof(buf))) > 0) {
        if (write(fd_out, buf, nread) != nread) {
            err(EXIT_FAILURE, "error writing to file: %s", target);
        }
    }

    if (nread == -1) {
        err(EXIT_FAILURE, "error reading from file: %s", src);
    }

    /* free resources */
    close(fd_out);
    close(fd_in);
    FREE(src);
    FREE(target);
}

/* find symbol by prefix */
const char *find_symbol(mmap_t *mem, const char *sym_prefix)
{
    size_t tmp = 0;

    /* filesize check */
#define CHECK_FSIZE(x) \
    if (x >= mem->size) { \
        DEBUG_PRINT("%s", "offset exceeds filesize"); \
        return NULL; \
    }

    /* overflow + filesize check */
#define CHECK_OVERFLOW(a, b) \
    if (__builtin_add_overflow(a, b, &tmp)) { \
        DEBUG_PRINT("%s", "overflow detected"); \
        return NULL; \
    } \
    CHECK_FSIZE(tmp)


    /* get string table */
    Elf_Ehdr *ehdr = (Elf_Ehdr *)mem->address;

    CHECK_OVERFLOW(ehdr->e_shoff, ehdr->e_shstrndx);
    Elf_Shdr *shdr = (Elf_Shdr *)(mem->address + ehdr->e_shoff);
    Elf_Shdr *strtab = shdr + ehdr->e_shstrndx;
    size_t dynstr_off = 0;

    CHECK_FSIZE(strtab->sh_offset);
    const char *data = (const char *)(mem->address + strtab->sh_offset);

    /* get .dynstr section */
    for (size_t i = 0; i < ehdr->e_shnum; i++) {
        CHECK_OVERFLOW(strtab->sh_offset, shdr[i].sh_name);

        if (strcmp(data + shdr[i].sh_name, ".dynstr") == 0) {
            dynstr_off = shdr[i].sh_offset;
            break;
        }
    }

    if (dynstr_off == 0) return NULL;
    CHECK_FSIZE(dynstr_off);

    static char buf[32] = {0};
    const char *symbol = NULL;
    const size_t pfxlen = strlen(sym_prefix);
    data = (const char *)(mem->address + dynstr_off);

    /* look for section header type SHT_DYNSYM */
    for (size_t i = 0; i < ehdr->e_shnum; i++) {
        if (shdr[i].sh_type != SHT_DYNSYM) {
            continue;
        }

        CHECK_FSIZE(shdr[i].sh_offset);
        Elf_Sym *sym = (Elf_Sym *)(mem->address + shdr[i].sh_offset);
        const size_t sh_sz = shdr[i].sh_size;
        const size_t sh_esz = shdr[i].sh_entsize;

        /* do some extra checks to prevent floating-point exceptions
         * or similar issues */
        if (sh_sz < sh_esz || sh_sz == 0 || sh_esz == 0) {
            DEBUG_PRINT("error: problematic math division [%ld/%ld] in section header found: %s",
                sh_sz, sh_esz, mem->filename);
            return NULL;
        }

        const size_t num = sh_sz / sh_esz;

        /* parse symbols */
        for (size_t j = 0; j < num; j++) {
            CHECK_OVERFLOW(dynstr_off, sym[j].st_name);
            const char *name = data + sym[j].st_name;

            if (/* check symbol type */
                ELF64_ST_TYPE(sym[j].st_info) == STT_OBJECT &&
                ELF64_ST_BIND(sym[j].st_info) == STB_GLOBAL &&
                ELF64_ST_VISIBILITY(sym[j].st_other) == STV_DEFAULT &&
                /* check length */
                strlen(name) < sizeof(buf) &&
                /* compare with sym_prefix */
                strncmp(name, sym_prefix, pfxlen) == 0 &&
                isdigit(name[pfxlen]) &&
                strchr(name, '.') &&
                /* compare version strings */
                (!symbol || strverscmp(symbol, name) < 0))
            {
                symbol = name;
                //DEBUG_PRINT("symbol: %s", name);
            }
        }
    }

    return symbol ? strcpy(buf, symbol) : NULL;
}

/* mmap() library and look for symbol by prefix */
const char *symbol_version(const char *lib, const char *sym_prefix)
{
    struct stat st;
    mmap_t mem;
    mem.filename = lib;

    /* open file */
    if ((mem.fd = open(lib, O_RDONLY)) < 0) {
        err(EXIT_FAILURE, "failed to open() file: %s\n", lib);
    }

    /* make sure file size is larger than the required ELF header size */
    if (fstat(mem.fd, &st) < 0) {
        err(EXIT_FAILURE, "fstat() failed: %s\n", lib);
    }
    mem.size = st.st_size;

    if (mem.size < sizeof(Elf_Ehdr)) {
        err(EXIT_FAILURE, "fstat() returned a too low file size: %s\n", lib);
    }

    /* mmap() library */
    mem.address = mmap(NULL, mem.size, PROT_READ, MAP_PRIVATE, mem.fd, 0);

    if (mem.address == MAP_FAILED) {
        err(EXIT_FAILURE, "mmap() failed: %s\n", lib);
    }

    /* look for symbol */
    const char *symbol = find_symbol(&mem, sym_prefix);

    if (symbol) {
        DEBUG_PRINT("symbol %s found in: %s", symbol, lib);
    }

    /* free resources */
    if (munmap(mem.address, mem.size) == -1) {
        perror("munmap()");
    }
    close(mem.fd);

    return symbol;
}

/* compare symbol versions and return true
 * if we should use the bundled library */
bool use_bundled_library(const char *libname, const char *dirname1, const char *dirname2, const char *sym_prefix)
{
    bool rv = false;
    size_t len = strlen(dirname1) + strlen(dirname2) + strlen(libname) + 4;
    char *lib_bundle = malloc(len);

    sprintf(lib_bundle, "%s/%s/%s", dirname1, dirname2, libname);

    /* check if bundled file exists */
    if (access(lib_bundle, F_OK) == 0) {
        /* get symbols */
        const char *sym_bundle = symbol_version(lib_bundle, sym_prefix);
        char *lib_sys = get_libpath(libname);
        const char *sym_sys = symbol_version(lib_sys, sym_prefix);

        /* compare symbols */
        if (sym_bundle && sym_sys && strverscmp(sym_bundle, sym_sys) > 0) {
            rv = true;
        }

        FREE(lib_sys);
    } else {
        DEBUG_PRINT("no access or file does not exist: %s", lib_bundle);
    }

    DEBUG_PRINT("use %s %s library", rv ? "BUNDLED" : "SYSTEM", libname);
    FREE(lib_bundle);

    return rv;
}

/* get full dirname of executable */
char *get_exe_dir()
{
    char *self = realpath("/proc/self/exe", NULL);
    if (!self) err(EXIT_FAILURE, "%s", "realpath() failed to resolve /proc/self/exe");

    /* modifies "self" */
    char *pdirname = dirname(self);

    if (pdirname[0] != '/' || pdirname != self) {
        err(EXIT_FAILURE, "dirname() returned an unexpected result: %s", pdirname);
    }

    DEBUG_PRINT("exe directory found: %s", self);

    return self;
}

/* compare symbol versions of bundled and system libraries */
void compare_library_symbols()
{
    char *dir = get_exe_dir();
    bool gcc = use_bundled_library("libgcc_s.so.1", dir, "gcc", "GCC_");
    bool cxx = use_bundled_library("libstdc++.so.6", dir, "cxx", "GLIBCXX_");

    /* FIRST add libgcc to search path, THEN libstdc++ */
    if (gcc) printf("%s/gcc", dir);
    if (gcc && cxx) putchar(':');
    if (cxx) printf("%s/cxx", dir);
    if (gcc || cxx) putchar('\n');

    FREE(dir);
}

/* copy system libraries next to executable */
void copy_libraries()
{
    char *dir = get_exe_dir();
    copy_lib("libgcc_s.so.1", dir, "gcc");
    copy_lib("libstdc++.so.6", dir, "cxx");
    fprintf(stderr, "%s\n", "Files copied");
    FREE(dir);
}

int main(int argc, char **argv)
{
    const char *usage =
        "usage: %s [--copy]\n"
        "set environment variable CHECKRT_DEBUG to enable extra verbose output\n";

    if (argc < 2) {
        compare_library_symbols();
        return 0;
    }

    if (argc == 2 && strcmp(argv[1], "--copy") == 0) {
        copy_libraries();
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
