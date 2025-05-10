#include "modules_config.h"
#include "utils.h"

#define DEFAULT_INSECURE_REGISTRY true
#define DEFAULT_REGISTRY_HOST "docker.nju.edu.cn"
#define DEFAULT_ROOT_DIR "/tmp/images_temp"
#define DEFAULT_STORAGE_DRIVER "overlay2"
#define DEFAULT_GRAPH DEFAULT_ROOT_DIR"/lib"
#define DEFAULT_STATE DEFAULT_ROOT_DIR"/run"
char *default_storage_opts[] = {
    "overlay2.override_kernel_check=true",
};

modules_config g_modules_config  = { 0 };

void init_modules_config() {
    g_modules_config.insecure_registry = DEFAULT_INSECURE_REGISTRY;
    g_modules_config.host = util_strdup_s(DEFAULT_REGISTRY_HOST);
    g_modules_config.root_dir = util_strdup_s(DEFAULT_ROOT_DIR);
    g_modules_config.storage_driver = util_strdup_s(DEFAULT_STORAGE_DRIVER);
    g_modules_config.graph = util_strdup_s(DEFAULT_GRAPH);
    g_modules_config.state = util_strdup_s(DEFAULT_STATE);
    util_dup_array_of_strings((const char **)default_storage_opts, sizeof(default_storage_opts) / sizeof(default_storage_opts[0]), &g_modules_config.storage_opts, &g_modules_config.storage_opts_len);
}
modules_config *get_modules_config() {
    return &g_modules_config;
}