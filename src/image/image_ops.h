#include "isula_libutils/oci_image_spec.h"
#include "isula_libutils/registry_manifest_schema2.h"

#define MAX_LAYER_NUM 125
#ifdef __cplusplus
extern "C" {
#endif

registry_manifest_schema2 *oci_parse_manifest_schema2(char *jsondata);
oci_image_spec *parse_oci_config(char *jsondata);

int image_init();
int image_pull(const char *image_name);

#ifdef __cplusplus
}
#endif