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
#include <link.h>
#include <ctype.h>
#include <dlfcn.h>
#include <elf.h>
#include <fcntl.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <unistd.h>

#if defined(_LP64) || defined(__LP64__)
#define ELF_T(x)  typedef Elf64_##x Elf_##x
#define ELF_ST_TYPE(x)  ELF64_ST_TYPE(x)
#else
#define ELF_T(x)  typedef Elf32_##x Elf_##x
#define ELF_ST_TYPE(x)  ELF32_ST_TYPE(x)
#endif
ELF_T(Ehdr);
ELF_T(Shdr);
ELF_T(Sym);


char *get_libpath(const char *lib)
{
    /* not only do we get the full path but dlopen()
     * also does all the compatibility checks for us */

    struct link_map *map = NULL;
    char *path, *err;
    void *handle = dlopen(lib, RTLD_LAZY);

    if (!handle) {
        if ((err = dlerror()) == NULL) {
            fprintf(stderr, "failed to dlopen() file: %s\n", lib);
        } else {
            fprintf(stderr, "%s\n", err);
        }
        return NULL;
    }

    if (dlinfo(handle, RTLD_DI_LINKMAP, &map) == -1 || map->l_name[0] == 0) {
	    fprintf(stderr, "could not retrieve information from library: %s\n", lib);
	    dlclose(handle);
	    return NULL;
    }

    path = strdup(map->l_name);
    dlclose(handle);

    return path;
}

const char *find_symbol(uint8_t *addr, const char *sym_prefix)
{
    const char *symbol = NULL;
    Elf_Ehdr *ehdr = (Elf_Ehdr *)addr;
    Elf_Shdr *shdr = (Elf_Shdr *)(addr + ehdr->e_shoff);
    Elf_Shdr *strtab = shdr + ehdr->e_shstrndx;
    size_t dynstr_off = 0;
    const char *data = (const char *)(addr + strtab->sh_offset);

    /* get .dynstr offset */
    for (size_t i = 0; i < ehdr->e_shnum; i++) {
        if (strcmp(data + shdr[i].sh_name, ".dynstr") == 0) {
            dynstr_off = shdr[i].sh_offset;
            break;
        }
    }

    if (dynstr_off == 0) {
        return NULL;
    }

    const size_t len = strlen(sym_prefix);
    data = (const char *)(addr + dynstr_off);

    /* parse symbols */
    for (size_t i = 0; i < ehdr->e_shnum; i++) {
        if (shdr[i].sh_type != SHT_DYNSYM) {
            continue;
        }

        Elf_Sym *sym = (Elf_Sym *)(addr + shdr[i].sh_offset);
        const size_t num = shdr[i].sh_size / shdr[i].sh_entsize;

        for (size_t j = 0; j < num; j++) {
            const char *name = data + sym[j].st_name;

            if (ELF_ST_TYPE(sym[j].st_info) == STT_OBJECT &&
                strncmp(name, sym_prefix, len) == 0 &&
                isdigit(name[len]) &&
                strchr(name, '.') &&
                (!symbol || strverscmp(symbol, name) < 0))
            {
                symbol = name;
            }
        }
    }

    return symbol;
}

int symbol_version(const char *lib, const char *sym_prefix, char *buf, size_t bufsize)
{
    int rv = -1;

    buf[0] = 0;

    /* open file */
    int fd = open(lib, O_RDONLY);

    if (fd < 0) {
        fprintf(stderr, "failed to open() file: %s\n", lib);
        return -1;
    }

    /* make sure file size is larger than the required ELF header size */
    struct stat st;

    if (fstat(fd, &st) < 0 || st.st_size < (off_t) (sizeof(Elf_Ehdr))) {
        fprintf(stderr, "fstat() failed or returned a too low file size: %s\n", lib);
        close(fd);
        return -1;
    }

    /* mmap() library */
    uint8_t *addr = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);

    if (addr == MAP_FAILED) {
        fprintf(stderr, "mmap() failed: %s\n", lib);
        close(fd);
        return -1;
    }

    /* look for symbol */
    size_t len = strlen(sym_prefix);
    const char *symbol = find_symbol(addr, sym_prefix);

    if (symbol && strncmp(symbol, sym_prefix, len) == 0) {
        int maj = 0, min = 0, pat = 0;

        if (sscanf(symbol + len, "%d.%d.%d", &maj, &min, &pat) == 3) {
            rv = pat + min * 1000 + maj * 1000000;
            strncpy(buf, symbol, bufsize - 1);
        }
    }

    munmap(addr, st.st_size);
    close(fd);

    return rv;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: %s LIBRARY\n"
            "LIBRARY must be 'libgcc_s.so.1' or 'libstdc++.so.6' or\n"
            "a relative or absolute path to either one.\n", argv[0]);
        return 1;
    }

    const char *sym = "GLIBCXX_";
    char *lib = argv[1];
    char *base = basename(lib);
    char buf[32] = { 0 };

    if (strcmp(base, "libgcc_s.so.1") == 0) {
	    sym = "GCC_";
    } else if (strcmp(base, "libstdc++.so.6") != 0) {
	    return 1;
    }

    char *path = get_libpath(lib);
    if (!path) return 1;

    int version = symbol_version(path, sym, buf, sizeof(buf));

    if (version == -1) {
        fprintf(stderr, "no symbol with prefix %s found: %s\n", sym, lib);
        free(path);
        return 1;
    }

    printf("%s\n%s\n%d\n", path, buf, version);
    free(path);

    return 0;
}
