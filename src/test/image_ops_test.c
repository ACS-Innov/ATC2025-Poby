#include "spdlog_wrapper.h"
#include "utils_file.h"
#include "image_ops.h"

int main(int argc, char *argv[]) {
  int ret;
  char *image_name = "10.16.0.183:5000/redis";
  // char image_name[] = "10.16.0.183:5000/openeuler:20.03";
  char *jsondata;
  registry_manifest_schema2 *manifest;
  oci_image_spec *config;

  if (argc > 1) {
    image_name = argv[1];
  }

  // jsondata =
  // util_read_text_file("/tmp/oci_temp/isulad_tmpdir/redis_manifest"); manifest
  // = oci_parse_manifest_schema2(jsondata); if (manifest == NULL) {
  //   C_SPDLOG_ERROR("para manifest json data failed.");
  //   return -1;
  // }
  // free(jsondata);
  // jsondata = NULL;
  // free_registry_manifest_schema2(manifest);

  // jsondata = util_read_text_file("/tmp/oci_temp/isulad_tmpdir/redis_config");
  // config = parse_oci_config(jsondata);
  // if (config == NULL) {
  //   C_SPDLOG_ERROR("para manifest json data failed.");
  //   return -1;
  // }
  // free(jsondata);
  // jsondata = NULL;
  // free_oci_image_spec(config);

  ret = image_init();
  if (ret != 0) {
    C_SPDLOG_ERROR("image init failed.");
  }

  C_SPDLOG_INFO("Start to pull image %s.", image_name);

  ret = image_pull(image_name);
  if (ret != 0) {
    C_SPDLOG_ERROR("image pull failed for image %s.", image_name);
  }
}
