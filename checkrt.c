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
#include <sys/wait.h>
#include <unistd.h>

#ifdef __x86_64__
typedef Elf64_Ehdr  Elf_Ehdr;
typedef Elf64_Shdr  Elf_Shdr;
typedef Elf64_Sym   Elf_Sym;
#else
typedef Elf32_Ehdr  Elf_Ehdr;
typedef Elf32_Shdr  Elf_Shdr;
typedef Elf32_Sym   Elf_Sym;
#endif

#define MAX(X,Y)  (((X) > (Y)) ? (X) : (Y))


static char *get_libpath(const char *lib)
{
  struct link_map *map = NULL;
  char *path;

  void *handle = dlopen(lib, RTLD_LAZY);
  if (!handle) {
    fprintf(stderr, "error: failed to dlmopen() file: %s\n", lib);
    return NULL;
  }

  if (dlinfo(handle, RTLD_DI_LINKMAP, &map) == -1) {
    fprintf(stderr, "error: could not retrieve information: %s\n", lib);
    dlclose(handle);
    return NULL;
  }

  path = strdup(map->l_name);
  dlclose(handle);

  return path;
}

static int symbol_version(const char *lib, const char *sym_prefix, char *buffer, size_t bufsize)
{
  int fd = -1;
  void *addr = NULL;
  const char *error = "";

#define SYMBOL_VERSION_ERR \
  fprintf(stderr, "error: %s: %s\n", error, lib); \
  munmap(addr, st.st_size); \
  close(fd); \
  return -1;

  fd = open(lib, O_RDONLY);
  if (fd < 0) {
    fprintf(stderr, "error: failed to open() file: %s\n", lib);
    return -1;
  }

  /* make sure file size is larger than the required ELF header size */
  struct stat st;
  if (fstat(fd, &st) < 0 || st.st_size < sizeof(Elf_Ehdr)) {
    error = "fstat() failed or returned a too low file size";
    SYMBOL_VERSION_ERR;
  }

  /* mmap() library */
  addr = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
  if (addr == MAP_FAILED) {
    error = "mmap() failed";
    SYMBOL_VERSION_ERR;
  }

  /* ELF header */
  Elf_Ehdr *ehdr = addr;
  error = "offset exceeding filesize";
  if (ehdr->e_shoff > st.st_size) {
    SYMBOL_VERSION_ERR;
  }

  Elf_Shdr *shdr = addr + ehdr->e_shoff;
  int shnum = ehdr->e_shnum;
  Elf_Shdr *sh_strtab = &shdr[ehdr->e_shstrndx];

  if (sh_strtab->sh_offset > st.st_size) {
    SYMBOL_VERSION_ERR;
  }
  const char *sh_strtab_p = addr + sh_strtab->sh_offset;
  const char *sh_dynstr_p = sh_strtab_p;
  const char * const sh_strtab_p_const = sh_strtab_p;

  /* get strtab/dynstr */
  for (int i = 0; i < shnum; ++i) {
    if (shdr[i].sh_type != SHT_STRTAB) continue;
    const char *name = sh_strtab_p_const + shdr[i].sh_name;

    if (strcmp(name, ".strtab") == 0) {
      if (shdr[i].sh_offset > st.st_size) {
        SYMBOL_VERSION_ERR;
      }
      sh_strtab_p = addr + shdr[i].sh_offset;
    } else if (strcmp(name, ".dynstr") == 0) {
      if (shdr[i].sh_offset > st.st_size) {
        SYMBOL_VERSION_ERR;
      }
      sh_dynstr_p = addr + shdr[i].sh_offset;
    }
  }

  const char *symbol = NULL;
  size_t len = strlen(sym_prefix);

  /* iterate through sections */
  for (int i = 0; i < shnum; ++i) {
    if (shdr[i].sh_type != SHT_SYMTAB && shdr[i].sh_type != SHT_DYNSYM) {
      continue;
    }

    if (shdr[i].sh_offset > st.st_size) {
      SYMBOL_VERSION_ERR;
    }
    Elf_Sym *syms_data = addr + shdr[i].sh_offset;

    /* iterate through symbols */
    for (size_t j = 0; j < shdr[i].sh_size / sizeof(Elf_Sym); ++j) {
      if (syms_data[j].st_shndx != SHN_ABS) {
        continue;
      }

      const char *name;
      if (shdr[i].sh_type == SHT_DYNSYM) {
        name = sh_dynstr_p + syms_data[j].st_name;
      } else {
        name = sh_strtab_p + syms_data[j].st_name;
      }

      if (strncmp(name, sym_prefix, len) != 0) {
        continue;
      }

      if (!symbol) {
        symbol = name;
        continue;
      }

      if (strverscmp(name, symbol) > 0) {
        symbol = name;
      }
    }
  }

  if (!symbol) {
    fprintf(stderr, "error: no symbol with prefix %s found: %s\n", sym_prefix, lib);
    munmap(addr, st.st_size);
    close(fd);
    return -1;
  }

  int maj = 0, min = 0, pat = 0;
  if (sscanf(symbol + len, "%d.%d.%d", &maj, &min, &pat) < 1) {
    SYMBOL_VERSION_ERR;
  }

  strncpy(buffer, symbol, bufsize - 1);
  munmap(addr, st.st_size);
  close(fd);

  return (pat + min*1000 + maj*1000000);
}


int main(int argc, char **argv)
{
  if (argc < 2) {
    printf("usage: %s LIBRARY\n"
      "LIBRARY must be 'libgcc_s.so.1' or 'libstdc++.so.6' or\n"
      "a relative or absolute path to either one.\n",
      argv[0]);
    return 1;
  }

  const char *sym = "GLIBCXX_";
  char *lib = argv[1];
  char *base = basename(lib);
  char buf[32] = {0};

  if (strcmp(base, "libgcc_s.so.1") == 0) {
    sym = "GCC_";
  } else if (strcmp(base, "libstdc++.so.6") != 0) {
    return 1;
  }

  char *path = get_libpath(lib);
  if (!path) return 1;
  int version = symbol_version(path, sym, buf, sizeof(buf));

  if (version == -1) {
    free(path);
    return 1;
  }

  printf("%s\n%s\n%d\n", path, buf, version);
  free(path);

  return 0;
}
