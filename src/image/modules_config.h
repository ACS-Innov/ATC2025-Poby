#include <stddef.h>
#include <stdbool.h>

typedef struct {
    char *host;
    char *root_dir;
    char *storage_driver;
    char *graph;
    char *state;
    char **storage_opts;
    size_t storage_opts_len;
    bool insecure_registry;
} modules_config;

extern modules_config g_modules_config;

void init_modules_config();

modules_config *get_modules_config();