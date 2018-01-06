extern char *optional;
extern char *optional_ld_preload;
extern void checkrt(char *usr_in_appdir);
extern int debug_flag;

#define DEBUG(...) do { \
    if (debug_flag) \
        printf(__VA_ARGS__); \
} while (0)
