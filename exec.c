/* Copyright (c) 2018 Pablo Marcos Oltra <pablo.marcos.oltra@gmail.com>
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
/*
 * This exec.so library is intended to restore the environment of the AppImage's
 * parent process. This is done to avoid library clashing of bundled libraries
 * with external processes, e.g when running the web browser.
 *
 * The intended usage is as follows:
 *
 * 1. This library is injected to the dynamic loader through LD_PRELOAD
 *    automatically in AppRun **only** if `exec.so` exists.
 *
 * 2. This library will intercept calls to new processes and will detect whether
 *    those calls are for binaries within the AppImage bundle or external ones.
 *
 * 3. In case it's an internal process, it will not change anything.
 *    In case it's an external process, it will restore the environment of
 *    the AppImage parent by reading `/proc/[pid]/environ`.
 *    This is the conservative approach taken.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h> /* MIN() */
#include <unistd.h>


typedef int (*execve_func_t)(const char *filename, char *const argv[], char *const envp[]);

#define VISIBLE __attribute__ ((visibility ("default")))

#if !defined(DEBUG) && (defined(EXEC_TEST) || defined(ENV_TEST))
#define DEBUG 1
#endif

#ifdef DEBUG
#define DEBUG_PRINT(...) \
    if (getenv("APPIMAGE_EXEC_DEBUG")) { \
        printf("APPIMAGE_EXEC>> " __VA_ARGS__); \
    }
#else
#define DEBUG_PRINT(...)  /**/
#endif


static void env_free(char* const *env)
{
    size_t len = 0;
    while (env[len] != 0) {
        free(env[len]);
        len++;
    }
    free((char**)env);
}

static size_t get_number_of_variables(FILE *file, char **buffer, size_t *len)
{
    size_t number = 0;

    if (getline(buffer, len, file) < 0)
        return -1;

    char *ptr = *buffer;
    while (ptr < *buffer + *len) {
        size_t var_len = strlen(ptr);
        ptr += var_len + 1;
        if (var_len == 0)
            break;
        number++;
    }

    return number != 0 ? (ssize_t)number : -1;
}

static char* const* env_from_buffer(FILE *file)
{
    char *buffer = NULL;
    size_t len = 0;
    size_t num_vars = get_number_of_variables(file, &buffer, &len);
    char** env = calloc(num_vars + 1, sizeof(char*));

    size_t n = 0;
    char *ptr = buffer;
    while (ptr < buffer + len && n < num_vars) {
        size_t var_len = strlen(ptr);
        if (var_len == 0)
            break;

        env[n] = calloc(sizeof(char*), var_len + 1);
        strncpy(env[n], ptr, var_len + 1);
        DEBUG_PRINT("\tenv var copied: %s\n", env[n]);
        ptr += var_len + 1;
        n++;
    }
    free(buffer);

    return env;
}

static char* const* read_env_from_process(pid_t pid)
{
    char buffer[256] = {0};

    snprintf(buffer, sizeof(buffer), "/proc/%d/environ", pid);
    DEBUG_PRINT("Reading env from parent process: %s\n", buffer);
    FILE *env_file = fopen(buffer, "r");
    if (!env_file) {
        DEBUG_PRINT("Error reading file: %s (%s)\n", buffer, strerror(errno));
        return NULL;
    }

    char* const* env = env_from_buffer(env_file);
    fclose(env_file);

    return env;
}

static int is_external_process(const char *filename)
{
    const char *appdir = getenv("APPDIR");
    if (!appdir)
        return 0;
    DEBUG_PRINT("APPDIR = %s\n", appdir);

    return strncmp(filename, appdir, MIN(strlen(filename), strlen(appdir)));
}

static int exec_common(execve_func_t function, const char *filename, char* const argv[], char* const envp[])
{
    // Try to get the canonical path in case it's a relative path or symbolic link.
    char *fullpath = canonicalize_file_name(filename);
    DEBUG_PRINT("filename %s, fullpath %s\n", filename, fullpath);

    char* const *env = envp;
    if (is_external_process(fullpath)) {
        DEBUG_PRINT("External process detected. Restoring env vars from parent %d\n", getppid());
        env = read_env_from_process(getppid());
        if (!env) {
            env = envp;
            DEBUG_PRINT("Error restoring env vars from parent\n");
        }
    }
    int ret = function(filename, argv, env);

    if (fullpath != filename)
        free(fullpath);

    if (env != envp)
        env_free(env);

    return ret;
}

VISIBLE int execve(const char *filename, char *const argv[], char *const envp[])
{
    DEBUG_PRINT("execve call hijacked: %s\n", filename);
    execve_func_t execve_orig = dlsym(RTLD_NEXT, "execve");
    if (!execve_orig) {
        DEBUG_PRINT("Error getting execve original symbol: %s\n", strerror(errno));
    }

    return exec_common(execve_orig, filename, argv, envp);
}

VISIBLE int execv(const char *filename, char *const argv[]) {
    DEBUG_PRINT("execv call hijacked: %s\n", filename);
    return execve(filename, argv, environ);
}

VISIBLE int execvpe(const char *filename, char *const argv[], char *const envp[])
{
    DEBUG_PRINT("execvpe call hijacked: %s\n", filename);
    execve_func_t execve_orig = dlsym(RTLD_NEXT, "execvpe");
    if (!execve_orig) {
        DEBUG_PRINT("Error getting execvpe original symbol: %s\n", strerror(errno));
    }

    return exec_common(execve_orig, filename, argv, envp);
}

VISIBLE int execvp(const char *filename, char *const argv[]) {
    DEBUG_PRINT("execvp call hijacked: %s\n", filename);
    return execvpe(filename, argv, environ);
}

#ifdef EXEC_TEST
int main(int argc, char *argv[]) {
    putenv("APPIMAGE_EXEC_DEBUG=1");
    puts("EXEC TEST");
    execv("/bin/true", argv);
    return 0;
}
#elif defined(ENV_TEST)
int main() {
    putenv("APPIMAGE_EXEC_DEBUG=1");
    puts("ENV TEST");
    read_env_from_process(getppid());
    return 0;
}
#endif
