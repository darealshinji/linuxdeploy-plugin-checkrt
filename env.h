#ifndef ENV_H
#define END_H

#include <unistd.h>

pid_t get_parent_pid();
char* const* read_parent_env();
void env_free(char* const *env);

#endif
