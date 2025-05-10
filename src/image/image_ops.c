#include <libgen.h>
#include <pthread.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "image_ops.h"
#include "mediatype.h"
#include "modules_config.h"
#include "registry.h"
#include "registry_type.h"
#include "spdlog_wrapper.h"
#include "storage.h"
#include "utils.h"
#include "utils_images.h"

static int init_descriptor(pull_descriptor *desc, const char *image_name) {
  int ret = 0;
  int sret = 0;
  char *image_tmp_path = NULL;
  char blobpath[PATH_MAX] = {0};
  char scope[PATH_MAX] = {0};
  char tmp_dir[PATH_MAX] = {0};
  char *slashPos = NULL;

  if (!util_valid_image_name(image_name)) {
    C_SPDLOG_ERROR("Invalid image name: %s", image_name);
    return -1;
  }

  desc->insecure_registry = g_modules_config.insecure_registry;

  desc->dest_image_name = oci_normalize_image_name(image_name);
  desc->host = oci_get_host(desc->dest_image_name);
  if (desc->host == NULL) {
    free(desc->image_name);
    desc->image_name =
        oci_add_host(g_modules_config.host, desc->dest_image_name);
  } else {
    desc->image_name = oci_default_tag(desc->dest_image_name);
  }

  ret = oci_split_image_name(desc->image_name, &desc->host, &desc->name,
                             &desc->tag);
  if (ret != 0) {
    C_SPDLOG_ERROR("split image name %s failed", desc->image_name);
    return -1;
  }
  if (desc->host == NULL || desc->name == NULL || desc->tag == NULL) {
    C_SPDLOG_ERROR("Invalid image %s, host or name or tag not found",
                   desc->image_name);
    return -1;
  }

  // TODO: CZH: fix gloabal data for oci, or check it at start
  ret = makesure_isulad_tmpdir_perm_right(g_modules_config.root_dir);
  if (ret != 0) {
    C_SPDLOG_ERROR("failed to make sure permission of image tmp work dir");
  }

  image_tmp_path = oci_get_isulad_tmpdir(g_modules_config.root_dir);
  if (image_tmp_path == NULL) {
    C_SPDLOG_ERROR("failed to get image tmp work dir");
    return -1;
  }

  slashPos = strchr(desc->name, '/');
  slashPos = slashPos == NULL ? desc->name : slashPos + 1;
  sret = snprintf(blobpath, PATH_MAX, "%s/%s-XXXXXX", image_tmp_path, slashPos);
  if (sret < 0 || (size_t)sret >= PATH_MAX) {
    C_SPDLOG_ERROR("image tmp work path too long");
    return -1;
  }
  if (mkdtemp(blobpath) == NULL) {
    C_SPDLOG_ERROR("make temporary direcory failed: %s", strerror(errno));
    return -1;
  }
  if (chmod(blobpath, DEFAULT_HIGHEST_DIRECTORY_MODE) == -1) {
        perror("chmod");
        return 1;
  }
  desc->blobpath = util_strdup_s(blobpath);

  desc->tmp_dir = util_strdup_s(basename(blobpath));

  // sret = snprintf(scope, sizeof(scope), "repository:%s:pull", desc->name);
  // if (sret < 0 || (size_t)sret >= sizeof(scope)) {
  //   C_SPDLOG_ERROR("Failed to sprintf scope");
  //   return false;
  // }
  // desc->scope = util_strdup_s(scope);

  ret = pthread_mutex_init(&desc->mutex, NULL);
  if (ret != 0) {
    C_SPDLOG_ERROR("Failed to init mutex for pull");
    return -1;
  }
  desc->mutex_inited = true;

  // ret = pthread_mutex_init(&desc->challenges_mutex, NULL);
  // if (ret != 0) {
  //   C_SPDLOG_ERROR("Failed to init challenges mutex for pull");
  //   return false;
  // }
  // desc->challenges_mutex_inited = true;

  ret = pthread_cond_init(&desc->cond, NULL);
  if (ret != 0) {
    C_SPDLOG_ERROR("Failed to init cond for pull");
    return -1;
  }
  desc->cond_inited = true;
  
  desc->protocol = util_strdup_s("http");

  return 0;
}

static char *format_driver_name(const char *driver) {
  if (driver == NULL) {
    return NULL;
  }

  if (strcmp(driver, "overlay") == 0 || strcmp(driver, "overlay2") == 0) {
    return util_strdup_s("overlay");
  } else {
    return util_strdup_s(driver);
  }
}

static int storage_module_init_helper() {
  int ret = 0;
  struct storage_module_init_options *storage_opts = NULL;

  storage_opts =
      util_common_calloc_s(sizeof(struct storage_module_init_options));
  if (storage_opts == NULL) {
    c_spdlog_error("Memory oulst");
    ret = -1;
    goto out;
  }

  storage_opts->driver_name =
      format_driver_name(g_modules_config.storage_driver);
  if (storage_opts->driver_name == NULL) {
    c_spdlog_error("Failed to get storage driver name");
    ret = -1;
    goto out;
  }

  storage_opts->storage_root =
      util_path_join(g_modules_config.graph, OCI_IMAGE_GRAPH_ROOTPATH_NAME);
  if (storage_opts->storage_root == NULL) {
    c_spdlog_error("Failed to get storage root dir");
    ret = -1;
    goto out;
  }

  storage_opts->storage_run_root =
      util_path_join(g_modules_config.state, OCI_IMAGE_GRAPH_ROOTPATH_NAME);
  if (storage_opts->storage_run_root == NULL) {
    c_spdlog_error("Failed to get storage run root dir");
    ret = -1;
    goto out;
  }

  if (util_dup_array_of_strings((const char **)g_modules_config.storage_opts,
                                g_modules_config.storage_opts_len,
                                &storage_opts->driver_opts,
                                &storage_opts->driver_opts_len) != 0) {
    c_spdlog_error("Failed to get storage storage opts");
    ret = -1;
    goto out;
  }

  if (simple_storage_module_init(storage_opts) != 0) {
    c_spdlog_error("Failed to init storage module");
    ret = -1;
    goto out;
  }

out:
  free_storage_module_init_options(storage_opts);
  return ret;
}

static int image_fetch(pull_descriptor *desc) {
  int ret = 0;

  ret = external_fetch_and_parse_manifest(desc);
  if (ret != 0) {
    C_SPDLOG_ERROR("fetch and parse manifest failed for image %s",
                   desc->image_name);
    return -1;
  }

  ret = external_fetch_all_without_cache(desc);
  if (ret != 0) {
    C_SPDLOG_ERROR("fetch layers failed for image %s", desc->image_name);
    return -1;
  }
  return ret;
}

static int modules_init() {
  int ret = 0;

  ret = registry_init(NULL, NULL);
  if (ret != 0) {
    C_SPDLOG_ERROR("init registry module failed!");
    return -1;
  }

  ret = storage_module_init_helper();
  if (ret != 0) {
    C_SPDLOG_ERROR("init storage module failed!");
    return -1;
  }

  return ret;
}

int image_pull(const char *image_name) {
  int ret = 0;

  pull_descriptor *desc =
      (pull_descriptor *)util_common_calloc_s(sizeof(pull_descriptor));
  if (!desc) {
    C_SPDLOG_ERROR("alloc memory for desc failed.");
  }

  ret = init_descriptor(desc, image_name);
  if (ret != 0) {
    C_SPDLOG_ERROR("init registry descriptor failed.");
    return -1;
  }

  ret = image_fetch(desc);
  if (ret != 0) {
    C_SPDLOG_ERROR("fetch registry image failed.");
    return -1;
  }

  // ret = register_image(desc);

  free_pull_desc(desc);

  return ret;
}

int image_init() {
  int ret;

  init_modules_config();

  ret = modules_init();
  if (ret != 0) {
    C_SPDLOG_ERROR("modules init failed.");
  }

  return ret;
}

registry_manifest_schema2 *oci_parse_manifest_schema2(char *jsondata) {
  registry_manifest_schema2 *manifest = NULL;
  parser_error err = NULL;
  int ret = 0;

  if (!jsondata) {
    c_spdlog_error("Null jsondata for parse manifest");
    ret = -1;
  }

  manifest = registry_manifest_schema2_parse_data(jsondata, NULL, &err);
  if (manifest == NULL) {
    c_spdlog_error("parse manifest schema2 failed, err: %s", err);
    ret = -1;
    goto out;
  }

  if (manifest->layers_len > MAX_LAYER_NUM) {
    c_spdlog_error("Invalid layer number %zu, maxium is %d",
                   manifest->layers_len, MAX_LAYER_NUM);
    ret = -1;
    free_registry_manifest_schema2(manifest);
    manifest = NULL;
    goto out;
  }

  for (size_t i = 0; i < manifest->layers_len; i++) {
    if (strcmp(manifest->layers[i]->media_type, DOCKER_IMAGE_LAYER_TAR_GZIP) &&
        strcmp(manifest->layers[i]->media_type,
               DOCKER_IMAGE_LAYER_FOREIGN_TAR_GZIP) &&
        strcmp(manifest->layers[i]->media_type, OCI_IMAGE_LAYER_TAR_GZIP)) {
      c_spdlog_error("Unsupported layer's media type %s, layer index %zu",
                     manifest->layers[i]->media_type, i);
      ret = -1;
      free_registry_manifest_schema2(manifest);
      manifest = NULL;
      goto out;
    }
  }

out:
  free(err);
  err = NULL;

  return manifest;
}

oci_image_spec *parse_oci_config(char *jsondata) {
  int ret = 0;
  parser_error err = NULL;
  size_t i = 0;
  oci_image_spec *config = NULL;
  char *diff_id = NULL;
  char *parent_chain_id = "";

  if (jsondata == NULL) {
    c_spdlog_error("Null jsondata for parse config");
    return NULL;
  }

  config = oci_image_spec_parse_data(jsondata, NULL, &err);
  if (config == NULL) {
    c_spdlog_error("parse image config v2 failed, err: %s", err);
    ret = -1;
    goto out;
  }

  if (config->rootfs == NULL || config->rootfs->diff_ids_len == 0) {
    c_spdlog_error("No rootfs found in config");
    free_oci_image_spec(config);
    config = NULL;
    goto out;
  }

out:
  free(err);
  err = NULL;

  return config;
}
