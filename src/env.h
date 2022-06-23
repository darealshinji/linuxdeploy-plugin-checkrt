#ifndef ENV_H
#define END_H

#include <stdio.h>
#include <unistd.h>

pid_t get_parent_pid();
char* const* read_parent_env();
void env_free(char* const *env);

#define DEBUG(...) do { \
    if (getenv("APPIMAGE_CHECKRT_DEBUG")) \
        printf("APPIMAGE_CHECKRT>> " __VA_ARGS__); \
} while (0)

#endif
