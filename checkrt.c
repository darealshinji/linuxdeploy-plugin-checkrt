/* Copyright (c) 2017 djcj <djcj@gmx.de>
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <limits.h>
#ifndef LIBC6_ARCH
#  if (__WORDSIZE == 64)
#    define LIBC6_ARCH "libc6,x86-64"
#  else
#    define LIBC6_ARCH "libc6"
#  endif
#endif

#define LINE_SIZE 255

#define SCANLIB(lib,sym,regex) \
  sprintf(command, format, lib, regex); \
  f = popen(command, "r"); \
  ret = fscanf(f, "%s", sym); (void)ret; \
  pclose(f);

#define CXXDIR   "optional/libstdc++"
#define GCCDIR   "optional/libgcc"
#define EXEC_SO  "optional/exec.so"

char *optional = NULL;
char *optional_ld_preload = NULL;

void checkrt(char *usr_in_appdir)
{
    int ret;
    FILE *f;
    char command[LINE_SIZE];
    char stdcxx_sys_lib[LINE_SIZE], gcc_sys_lib[LINE_SIZE];
    char stdcxx_sys_sym[LINE_SIZE], gcc_sys_sym[LINE_SIZE];
    char stdcxx_bundle_sym[LINE_SIZE], gcc_bundle_sym[LINE_SIZE];
    int stdcxx_sys_ver=1, stdcxx_bundle_ver=0, gcc_sys_ver=1, gcc_bundle_ver=0;

    char *stdcxx_bundle_lib = "./" CXXDIR "/libstdc++.so.6";
    char *gcc_bundle_lib = "./" GCCDIR "/libgcc_s.so.1";
    const char *format = "tr '\\0' '\\n' < '%s' | grep -e '%s' | tail -n1";

    if (access(stdcxx_bundle_lib, F_OK) == 0) {
        f = popen("ldconfig -p | grep 'libstdc++.so.6 (" LIBC6_ARCH ")' | awk 'NR==1{print $NF}'", "r");
        ret = fscanf(f, "%s", stdcxx_sys_lib); (void)ret;
        pclose(f);

        if (access(stdcxx_sys_lib, F_OK) == 0) {
            SCANLIB(stdcxx_sys_lib, stdcxx_sys_sym, "^GLIBCXX_3\\.4");
            SCANLIB(stdcxx_bundle_lib, stdcxx_bundle_sym, "^GLIBCXX_3\\.4");
            stdcxx_sys_ver = atoi(stdcxx_sys_sym+12);
            stdcxx_bundle_ver = atoi(stdcxx_bundle_sym+12);
            //printf("%s ==> %s (%d)\n", stdcxx_sys_lib, stdcxx_sys_sym, stdcxx_sys_ver);
            //printf("%s ==> %s (%d)\n\n", stdcxx_bundle_lib, stdcxx_bundle_sym, stdcxx_bundle_ver);
        }
    }

    if (access(gcc_bundle_lib, F_OK) == 0) {
        f = popen("ldconfig -p | grep 'libgcc_s.so.1 (" LIBC6_ARCH ")' | awk 'NR==1{print $NF}'", "r");
        ret = fscanf(f, "%s", gcc_sys_lib); (void)ret;
        pclose(f);

        if (access(gcc_sys_lib, F_OK) == 0) {
            SCANLIB(gcc_sys_lib, gcc_sys_sym, "^GCC_[0-9]\\.[0-9]");
            SCANLIB(gcc_bundle_lib, gcc_bundle_sym, "^GCC_[0-9]\\.[0-9]");
            gcc_sys_ver = atoi(gcc_sys_sym+4) * 100 + atoi(gcc_sys_sym+6) * 10 + atoi(gcc_sys_sym+8);
            gcc_bundle_ver = atoi(gcc_bundle_sym+4) * 100 + atoi(gcc_bundle_sym+6) * 10 + atoi(gcc_bundle_sym+8);
            //printf("%s ==> %s (%d)\n", gcc_sys_lib, gcc_sys_sym, gcc_sys_ver);
            //printf("%s ==> %s (%d)\n\n", gcc_bundle_lib, gcc_bundle_sym, gcc_bundle_ver);
        }
    }

    int bundle_cxx = 0;
    int bundle_gcc = 0;
    size_t len = strlen(usr_in_appdir);

    if (stdcxx_bundle_ver > stdcxx_sys_ver)
        bundle_cxx = 1;

    if (gcc_bundle_ver > gcc_sys_ver)
        bundle_gcc = 1;

    if (bundle_cxx == 1 || bundle_gcc == 1) {
        len = strlen(EXEC_SO) + 12 + len;
        optional_ld_preload = malloc(len);
        sprintf(optional_ld_preload, "LD_PRELOAD=%s/" EXEC_SO, usr_in_appdir);
        optional_ld_preload[len] = '\0';
    }

    if (bundle_cxx == 1 && bundle_gcc == 0) {
        optional = malloc(strlen(CXXDIR) + 2 + len);
        sprintf(optional, "%s/" CXXDIR ":", usr_in_appdir);
    } else if (bundle_cxx == 0 && bundle_gcc == 1) {
        optional = malloc(strlen(GCCDIR) + 2 + len);
        sprintf(optional, "%s/" GCCDIR ":", usr_in_appdir);
    } else if (bundle_cxx == 1 && bundle_gcc == 1) {
        optional = malloc(strlen(GCCDIR) + strlen(CXXDIR) + 4 + len*2);
        sprintf(optional, "%s/" GCCDIR ":%s/" CXXDIR ":", usr_in_appdir, usr_in_appdir);
    } else {
        optional = malloc(2);
        sprintf(optional, "%s", "");
    }
}

