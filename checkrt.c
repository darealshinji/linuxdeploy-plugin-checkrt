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


#ifndef ElfW
# if defined(_LP64) || defined(__LP64__)
#  define ElfW(x) Elf64_##x
# else
#  define ElfW(x) Elf32_##x
# endif
#endif


#define LIBGCC_SO "libgcc_s.so.1"
#define STDCXX_SO "libstdc++.so.6"


/* debug messages */
#define DEBUG_PRINT(MSG, ...) \
    if (debug_mode) { \
        fprintf(stderr, "[DEBUG] %s: " MSG "\n", __func__, __VA_ARGS__); \
    }


/* global variables */
static bool debug_mode = false;
static bool full_debug_mode = false;
static void *addr = MAP_FAILED;
static size_t size = 0;
static ElfW(Ehdr) *ehdr = NULL;
static ElfW(Shdr) *shdr = NULL;


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
static char *get_system_library_path(const char *filename)
{
    struct link_map *map = NULL;
    void *handle = load_lib_new_namespace(filename);

    if (dlinfo(handle, RTLD_DI_LINKMAP, &map) == -1) {
        errx_dlerror(filename, "dlinfo() could not retrieve information from library");
    }

    if (!map->l_name || map->l_name[0] == 0) {
        errx(1, "%s: %s", filename, "dlinfo() failed to get absolute pathname");
    }

    char *path = strdup(map->l_name);
    DEBUG_PRINT("%s resolved to: %s", filename, path);

    dlclose(handle);

    return path;
}


/* copy library from system into directory next to binary */
static void copy_lib(const char *dir, const char *subdir, const char *libname)
{
    int fd_in, fd_out;
    ssize_t nread;
    uint8_t buf[512*1024];

    /* find library */
    char *src = get_system_library_path(libname);
    printf("Copy library: %s\n", src);

    /* create target directory */
    char *target = malloc(strlen(dir) + strlen(subdir) + strlen(libname) + 3);
    sprintf(target, "%s/%s/", dir, subdir);
    mkdir(target, 0775);

    /* open source file for reading */
    if ((fd_in = open(src, O_RDONLY)) < 0) {
        err(1, "cannot open file for reading: %s", src);
    }

    /* open target file for writing */
    strcat(target, libname);

    if ((fd_out = open(target, O_WRONLY | O_CREAT | O_TRUNC, 0664)) < 0) {
        err(1, "cannot open file for writing: %s", target);
    }

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


/* perform filesize check and get offset */
static void *get_offset(ElfW(Off) offset)
{
    if (offset >= size) {
        errx(1, "%s", "*** offset exceeds filesize ***"); \
    }

    return (addr + offset);
}


/* get section header by name */
static ElfW(Shdr) *get_shdr(ElfW(Word) type, const char *name)
{
    ElfW(Shdr) *strtab = &shdr[ehdr->e_shstrndx];

    for (size_t i = 0; i < ehdr->e_shnum; i++) {
        if (shdr[i].sh_type != type) {
            continue;
        }

        const char *ptr = get_offset(strtab->sh_offset + shdr[i].sh_name);

        if (strcmp(ptr, name) == 0) {
            return &shdr[i];
        }
    }

    return NULL;
}


/* get dynamic entry value by tag */
static size_t get_dyn_val(ElfW(Shdr) *dynamic, ElfW(Sword) tag)
{
    if (dynamic->sh_size == 0 || dynamic->sh_entsize == 0) {
        return 0;
    }

    ElfW(Dyn) *dyn = get_offset(dynamic->sh_offset);

    for (size_t i = 0; i < (dynamic->sh_size / dynamic->sh_entsize); i++, dyn++) {
        if (dyn->d_tag == tag) {
            return dyn->d_un.d_val;
        }
    }

    return 0;
}


static bool is_prefixed_and_higher_version(const char *new, const char *old, const char *prefix, size_t pfxlen)
{
    return (strncmp(new, prefix, pfxlen) == 0 &&  /* symbol name starts with prefix */
            isdigit(*(new + pfxlen)) &&           /* first byte after prefix is a digit */
            strchr(new + pfxlen, '.') &&          /* symbol name contains a dot */
            (!old || strverscmp(old, new) < 0));  /* get higher version string */
}


/**
 * Find symbol in SHT_GNU_verdef section
 * https://refspecs.linuxfoundation.org/LSB_3.0.0/LSB-PDA/LSB-PDA.junk/symversion.html
 *
 * Parse the .dynamic section and look for the entry with the DT_VERDEFNUM flag.
 * Its value holds the number of entries in the SHT_GNU_verdef section.
 *
 * Get the sh_link entry from the SHT_GNU_verdef section header. Its value is
 * the index number of the section header pointing to the section that holds the
 * version definition strings.
 *
 * Parse the Elfxx_Verdef arrays in the SHT_GNU_verdef section. The value
 * vd_next holds a relative offset to the next Elfxx_Verdef array.
 *
 * The Elfxx_Verdef arrays will have one or more Elfxx_Verdaux arrays associated
 * to them. The entry vd_aux is a relative offset to that array.
 *
 * The first Elfxx_Verdaux array holds information to the latest version string.
 * It's a relative offset into the section previously obtained from the sh_link
 * entry and points to a NUL-termintated string.
 */
static char *find_symbol(const char *prefix)
{
    size_t verdefnum;

    /* get numbers of .gnu.version_d entries from .dynamic's DT_VERDEFNUM entry */
    ElfW(Shdr) *dynamic = get_shdr(SHT_DYNAMIC, ".dynamic");

    if (!dynamic || (verdefnum = get_dyn_val(dynamic, DT_VERDEFNUM)) == 0) {
        return NULL;
    }

    /* get link to section that holds the strings referenced
     * by .gnu.version_d section */
    ElfW(Shdr) *verdef = get_shdr(SHT_GNU_verdef, ".gnu.version_d");

    if (!verdef || verdef->sh_link >= ehdr->e_shnum) {
        return NULL;
    }

    ElfW(Shdr) *strings = &shdr[verdef->sh_link];

    /* parse .gnu.version_d section */
    ElfW(Off) vd_off = verdef->sh_offset;
    const char *symbol = NULL;
    const size_t pfxlen = strlen(prefix);

    for (size_t i = 0; i < verdefnum; i++) {
        ElfW(Verdef) *vd = get_offset(vd_off);

        if (vd->vd_version == 1 &&               /* must be 1 */
            vd->vd_flags != VER_FLG_BASE &&      /* skip library name entry */
            vd->vd_aux >= sizeof(ElfW(Verdef)))  /* placed after ElfW(Verdef) array */
        {
            /* get only the latest version instead of iterating all ElfXX_Verdaux entries */
            ElfW(Verdaux) *vda = get_offset(vd_off + vd->vd_aux);
            const char *name = get_offset(strings->sh_offset + vda->vda_name);

            if (is_prefixed_and_higher_version(name, symbol, prefix, pfxlen)) {
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
static char *symbol_version(const char *path, const char *prefix)
{
    struct stat st;
    int fd;

    /* let dlmopen() do compatibility checks for us */
    void *handle = load_lib_new_namespace(path);
    dlclose(handle);

    /* mmap() library */
    if ((fd = open(path, O_RDONLY)) == -1) {
        err(1, "open(): %s", path);
    }

    if (fstat(fd, &st) == -1) {
        err(1, "fstat(): %s", path);
    }

    if ((addr = mmap(NULL, st.st_size, PROT_READ, MAP_SHARED, fd, 0)) == MAP_FAILED) {
        err(1, "mmap(): %s", path);
    }

    /* file descriptor can now be closed */
    close(fd);

    /* set global variables */
    size = st.st_size;
    ehdr = addr;
    shdr = get_offset(ehdr->e_shoff);

    /* look for symbol */
    char *symbol = find_symbol(prefix);

    if (symbol) {
        DEBUG_PRINT("symbol %s found in: %s", symbol, path);
    }

    /* unmap */
    if (munmap(addr, size) == -1) {
        warn("%s", "munmap() returned with an error");
    }

    return symbol;
}


/* compare symbol versions and return true
 * if we should use the bundled library */
static bool use_bundled_library(const char *dir, const char *subdir, const char *libname, const char *prefix)
{
    bool rv = false;

    char *lib_bundle = malloc(strlen(dir) + strlen(subdir) + strlen(libname) + 3);
    sprintf(lib_bundle, "%s/%s/%s", dir, subdir, libname);

    /* check if bundled file exists */
    if (access(lib_bundle, F_OK) == 0) {
        /* get symbols */
        char *sym_bundle = symbol_version(lib_bundle, prefix);
        char *lib_sys = get_system_library_path(libname);
        char *sym_sys = symbol_version(lib_sys, prefix);

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

    DEBUG_PRINT("use %s %s library", rv ? "BUNDLED" : "SYSTEM", libname);
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

    if (pdirname != self || pdirname[0] != '/') {
        errx(1, "dirname() returned an unexpected result: %s", pdirname);
    }

    DEBUG_PRINT("exe directory found at: %s", self);

    return self;
}


/* compare symbol versions of bundled and system libraries */
static void compare_library_symbols()
{
    int res = 0;
    char *dir = get_exe_dir();

    if (use_bundled_library(dir, "gcc", LIBGCC_SO, "GCC_")) {
        res = 1;
    }

    if (use_bundled_library(dir, "cxx", STDCXX_SO, "GLIBCXX_")) {
        res += 2;
    }

    switch(res)
    {
    case 1:
        printf("%s/gcc\n", dir);
        break;
    case 2:
        printf("%s/cxx\n", dir);
        break;
    case 3:
        /* load libgcc before libstdc++ */
        printf("%s/gcc:%s/cxx\n", dir, dir);
        break;
    default:
        break;
    }

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
        copy_lib(dir, "gcc", LIBGCC_SO);
        copy_lib(dir, "cxx", STDCXX_SO);
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
