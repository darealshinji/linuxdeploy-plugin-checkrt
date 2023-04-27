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

#include "res.h"

#ifdef __x86_64__
typedef Elf64_Ehdr  Elf_Ehdr;
typedef Elf64_Shdr  Elf_Shdr;
typedef Elf64_Sym   Elf_Sym;
#else
typedef Elf32_Ehdr  Elf_Ehdr;
typedef Elf32_Shdr  Elf_Shdr;
typedef Elf32_Sym   Elf_Sym;
#endif

#define LIBGCC_S_SO  "libgcc_s.so.1"
#define LIBSTDCXX_SO "libstdc++.so.6"
#define LIBGCC_DIR   "gcc"
#define STDCXX_DIR   "cxx"

#define MAX(X,Y)  (((X) > (Y)) ? (X) : (Y))
#define PRINT_VERBOSE(FMT, ...)  if (verbose) { fprintf(stderr, FMT, __VA_ARGS__); }


static char *get_libpath(const char *lib, char verbose)
{
  struct link_map *map = NULL;
  char *path;

  /* It's very important to use dlmopen() together with the argument LM_ID_NEWLM,
   * otherwise the bundled library and the system library will use the same
   * namespace and the path of the bundled library might be returned when you
   * expected the system one.
   *
   * Note: if dlmopen() gets stuck in a loop that's a bug in glibc which was
   * fixed in release 2.37
   * https://sourceware.org/bugzilla/show_bug.cgi?id=29600
   */
  void *handle = dlmopen(LM_ID_NEWLM, lib, RTLD_LAZY);
  if (!handle) {
    PRINT_VERBOSE("error: failed to dlmopen() file: %s\n", lib);
    return NULL;
  }

  if (dlinfo(handle, RTLD_DI_LINKMAP, &map) == -1) {
    PRINT_VERBOSE("error: could not retrieve information: %s\n", lib);
    dlclose(handle);
    return NULL;
  }

  path = strdup(map->l_name);
  dlclose(handle);

  return path;
}

static int symbol_version(const char *lib, const char *sym_prefix, char verbose)
{
  int fd = -1;
  void *addr = NULL;
  const char *error = "";

  /* let dlopen() do all the compatibility checks */
  char *orig = get_libpath(lib, verbose);
  if (!orig) return -1;

  fd = open(orig, O_RDONLY);
  if (fd < 0) {
    PRINT_VERBOSE("error: failed to open() file: %s\n", orig);
    free(orig);
    return -1;
  }

  if (verbose) {
    if (strcmp(lib, orig) == 0) {
      fprintf(stderr, "%s\n", orig);
    } else {
      fprintf(stderr, "%s -> %s\n", lib, orig);
    }
  }

  free(orig);

  /* make sure file size is larger than the required ELF header size */
  struct stat st;
  if (fstat(fd, &st) < 0 || st.st_size < sizeof(Elf_Ehdr)) {
    error = "fstat() failed or returned a too low file size";
    goto symbol_version_error;
  }

  /* mmap() library */
  addr = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
  if (addr == MAP_FAILED) {
    error = "mmap() failed";
    goto symbol_version_error;
  }

  /* ELF header */
  Elf_Ehdr *ehdr = addr;
  error = "offset exceeding filesize";
  if (ehdr->e_shoff > st.st_size) {
    goto symbol_version_error;
  }

  Elf_Shdr *shdr = addr + ehdr->e_shoff;
  int shnum = ehdr->e_shnum;
  Elf_Shdr *sh_strtab = &shdr[ehdr->e_shstrndx];

  if (sh_strtab->sh_offset > st.st_size) {
    goto symbol_version_error;
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
        goto symbol_version_error;
      }
      sh_strtab_p = addr + shdr[i].sh_offset;
    } else if (strcmp(name, ".dynstr") == 0) {
      if (shdr[i].sh_offset > st.st_size) {
        goto symbol_version_error;
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
      goto symbol_version_error;
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

  int maj = 0, min = 0, pat = 0;
  if (sscanf(symbol + len, "%d.%d.%d", &maj, &min, &pat) < 1) {
    goto symbol_version_error;
  }

  PRINT_VERBOSE("%s%d.%d.%d\n", sym_prefix, maj, min, pat);
  munmap(addr, st.st_size);
  close(fd);
  return (pat + min*1000 + maj*1000000);

symbol_version_error:
  PRINT_VERBOSE("error: %s: %s\n", error, orig);
  munmap(addr, st.st_size);
  close(fd);
  return -1;
}

static void dump_file(const char *dest, unsigned char *data, unsigned int len)
{
  int fd = creat(dest, DEFFILEMODE);
  if (fd == -1) return;
  ssize_t written = write(fd, data, len);
  close(fd);
  if (written != len) unlink(dest);
}

static int copy_lib(const char *lib, const char *destDir, char verbose)
{
  char *destFull = NULL;
  int fdIn = -1, fdOut = -1;

  /* get full source and target paths */
  char *srcFull = get_libpath(lib, verbose);
  if (!srcFull) goto copy_lib_error;

  char *base = basename(srcFull);
  if (!base) goto copy_lib_error;

  size_t len = strlen(destDir);
  size_t len2 = strlen(base);
  destFull = malloc(len + MAX(len2,32) + 2);
  sprintf(destFull, "%s/%s", destDir, base);

  /* open source for reading */
  fdIn = open(srcFull, O_RDONLY|O_CLOEXEC);
  if (fdIn == -1) goto copy_lib_error;

  /* open target for writing */
  mkdir(destDir, ACCESSPERMS);
  fdOut = creat(destFull, DEFFILEMODE);
  if (fdOut == -1) goto copy_lib_error;

  /* copy data into target */
  ssize_t n;
  unsigned char buf[512*1024];
  while ((n = read(fdIn, &buf, sizeof(buf))) > 0) {
    if (write(fdOut, &buf, n) != n) {
      goto copy_lib_error;
    }
  }

  int rv = 0;
  fprintf(stderr, ">> %s\ncopied to -> %s\n", srcFull, destFull);

  /* dump COPYING files */
  char *p = destFull + len + 1;
  strcpy(p, "COPYING.RUNTIME.gz");
  dump_file(destFull, COPYING_RUNTIME_gz, COPYING_RUNTIME_gz_len);
  p += 7;
  strcpy(p, "3.gz");
  dump_file(destFull, COPYING3_gz, COPYING3_gz_len);
  strcpy(p, ".libgcc");
  dump_file(destFull, COPYING_libgcc, COPYING_libgcc_len);
  strcpy(p, ".libstdc++");
  dump_file(destFull, COPYING_libstdc__, COPYING_libstdc___len);
  fprintf(stderr, "(COPYING files were added too)\n");

  goto copy_lib_end;

copy_lib_error:
  rv = -1;

copy_lib_end:
  if (fdOut != -1) close(fdOut);
  if (fdIn != -1) close(fdIn);
  if (srcFull) free(srcFull);
  if (destFull) free(destFull);

  return rv;
}


int main(int argc, char **argv)
{
#if CHECKRT_TEST == 1

  printf("Test:\n\n");
  copy_lib(LIBGCC_S_SO, "./" LIBGCC_DIR, 1);
  copy_lib(LIBSTDCXX_SO, "./" STDCXX_DIR, 1);
  putchar('\n');
  symbol_version("./" LIBGCC_DIR "/" LIBGCC_S_SO, "GCC_", 1);
  putchar('\n');
  symbol_version("./" STDCXX_DIR "/" LIBSTDCXX_SO, "GLIBCXX_", 1);
  putchar('\n');
  symbol_version(LIBGCC_S_SO, "GCC_", 1);
  putchar('\n');
  symbol_version(LIBSTDCXX_SO, "GLIBCXX_", 1);

#else

  char v = 0, copy = 0;

  for (int i=1; i < argc; i++) {
    if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
      fprintf(stderr,
        "usage:\n"
        "  %s -c|--copy-libraries\n"
        "  %s -h|--help\n"
        "  %s -v|--verbose\n"
        "\n"
        "This program will look for the following libraries relative to its\n"
        "location, check if they are usable and add them to LD_LIBRARY_PATH\n"
        "if they are newer than the system's equivalent:\n"
        "\n"
        "  " LIBGCC_DIR "/" LIBGCC_S_SO "\n"
        "  " STDCXX_DIR "/" LIBSTDCXX_SO "\n", argv[0], argv[0], argv[0]);
      return 0;
    } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
      v = 1;
    } else if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--copy-libraries") == 0) {
      copy = 1;
    } else {
      fprintf(stderr, "error: invalid argument: %s\nTry `%s --help' for more information.\n", argv[i], argv[0]);
      return 1;
    }
  }

  char *currdir = realpath("/proc/self/exe", NULL);
  if (!currdir) {
    fprintf(stderr, "error: realpath() failed to resolve /proc/self/exe\n");
    return 1;
  }

  char *p = strrchr(currdir, '/');
  if (!p) {
    perror("strrchr()");
    free(currdir);
    return 1;
  }
  *(p+1) = 0;

  size_t len = strlen(currdir);
  char *libpath = malloc(len + MAX(sizeof(STDCXX_DIR), sizeof(LIBGCC_DIR)) +
    MAX(sizeof(LIBSTDCXX_SO), sizeof(LIBGCC_S_SO)) + 2);
  strcpy(libpath, currdir);
  p = libpath + len;

  if (copy) {
    /* copy system libraries */
    strcpy(p, LIBGCC_DIR);
    copy_lib(LIBGCC_S_SO, libpath, v);
    strcpy(p, STDCXX_DIR);
    copy_lib(LIBSTDCXX_SO, libpath, v);
  } else {
    /* get symbol versions */

    strcpy(p, LIBGCC_DIR "/" LIBGCC_S_SO);
    int ver = symbol_version(libpath, "GCC_", v);
    if (ver != -1 && ver > symbol_version(LIBGCC_S_SO, "GCC_", v)) {
      printf("%s" LIBGCC_DIR ":", currdir);
    }

    strcpy(p, STDCXX_DIR "/" LIBSTDCXX_SO);
    ver = symbol_version(libpath, "GLIBCXX_", v);
    if (ver != -1 && ver > symbol_version(LIBSTDCXX_SO, "GLIBCXX_", v)) {
      printf("%s" STDCXX_DIR ":", currdir);
    }

    putchar('\n');
  }

  free(libpath);
  free(currdir);
#endif

  return 0;
}
