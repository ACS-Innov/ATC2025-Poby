#include "doca/params.h"
#include "doca/common.h"
#include "doca_error.h"
#include <doca_argp.h>
#include <optional>
#include <spdlog/spdlog.h>
#include <string.h>
using std::make_optional;
using std::nullopt;
using std::optional;
doca_error_t register_param(optional<const char *> short_name,
                            optional<const char *> long_name,
                            optional<const char *> description,
                            callback_func callback, doca_argp_type type) {

  doca_error_t result;
  doca_argp_param *param;
  result = doca_argp_param_create(&param);
  if (result != DOCA_SUCCESS) {
    SPDLOG_ERROR("Fail to create param {}", doca_get_error_string(result));
    return result;
  }
  if (short_name.has_value()) {
    doca_argp_param_set_short_name(param, short_name.value());
  }
  if (long_name.has_value()) {
    doca_argp_param_set_long_name(param, long_name.value());
  }
  if (description.has_value()) {
    doca_argp_param_set_description(param, description.value());
  }
  doca_argp_param_set_callback(param, callback);
  doca_argp_param_set_type(param, type);
  result = doca_argp_register_param(param);
  if (result != DOCA_SUCCESS) {
    SPDLOG_ERROR("Fail to register param: {}", doca_get_error_string(result));
  }
  return result;
}

static doca_error_t dpu_pci_callback(void *param, void *config) {
  ProgramConfig *conf = (ProgramConfig *)config;
  const char *addr = (char *)param;
  int addr_len = strlen(addr);
  if (addr_len == MAX_ARG_SIZE) {
    SPDLOG_ERROR("Entered pci address exceeded buffer size of: %d",
                 MAX_ARG_SIZE - 1);
    return DOCA_ERROR_INVALID_VALUE;
  }
  strcpy(conf->dpu_pci_address, addr);
  return DOCA_SUCCESS;
}

static doca_error_t host_pci_callback(void *param, void *config) {
  ProgramConfig *conf = (ProgramConfig *)config;
  const char *addr = (char *)param;
  int addr_len = strlen(addr);
  if (addr_len == MAX_ARG_SIZE) {
    SPDLOG_ERROR("Entered pci address exceeded buffer size of: %d",
                 MAX_ARG_SIZE - 1);
    return DOCA_ERROR_INVALID_VALUE;
  }
  strcpy(conf->host_pci_address, addr);
  return DOCA_SUCCESS;
}

static doca_error_t in_file_callback(void *param, void *config) {
  auto conf = reinterpret_cast<ProgramConfig *>(config);
  auto file = reinterpret_cast<const char *>(param);
  auto len = strnlen(file, MAX_FILE_NAME);
  if (len == MAX_FILE_NAME) {
    SPDLOG_ERROR("Invalid in file name length, max {}", MAX_FILE_NAME - 1);

    return DOCA_ERROR_INVALID_VALUE;
  }
  strcpy(conf->in_file, file);
  return DOCA_SUCCESS;
}

static doca_error_t out_file_callback(void *param, void *config) {
  auto conf = reinterpret_cast<ProgramConfig *>(config);
  auto file = reinterpret_cast<const char *>(param);
  auto len = strnlen(file, MAX_FILE_NAME);
  if (len == MAX_FILE_NAME) {
    SPDLOG_ERROR("Invalid in file name length, max {}", MAX_FILE_NAME - 1);
    return DOCA_ERROR_INVALID_VALUE;
  }
  strcpy(conf->out_file, file);
  return DOCA_SUCCESS;
}

static doca_error_t mode_callback(void *param, void *config) {
  auto conf = reinterpret_cast<ProgramConfig *>(config);
  auto mode = reinterpret_cast<const char *>(param);

  if (strcmp(mode, "compress") == 0)
    conf->mode = DOCA_COMPRESS_DEFLATE_JOB;
  else if (strcmp(mode, "decompress") == 0)
    conf->mode = DOCA_DECOMPRESS_DEFLATE_JOB;
  else {
    SPDLOG_ERROR("Illegal mode = {}", mode);
    return DOCA_ERROR_INVALID_VALUE;
  }
  return DOCA_SUCCESS;
}

static doca_error_t doca_workq_depth_callback(void *param, void *config) {
  auto conf = reinterpret_cast<ProgramConfig *>(config);
  auto workq_depth = *reinterpret_cast<uint32_t *>(param);
  conf->doca_workq_depth = workq_depth;
  return DOCA_SUCCESS;
}

static doca_error_t doca_max_bufs_callback(void *param, void *config) {
  auto conf = reinterpret_cast<ProgramConfig *>(config);
  auto max_bufs = *reinterpret_cast<uint32_t *>(param);
  conf->doca_max_bufs = max_bufs;
  return DOCA_SUCCESS;
}

static doca_error_t is_host_callback(void *param, void *config) {
  auto conf = reinterpret_cast<ProgramConfig *>(config);
  auto is_host = *reinterpret_cast<bool *>(param);
  conf->is_host = is_host;
  return DOCA_SUCCESS;
}

static doca_error_t is_rdma_server_callback(void *param, void *config) {
  auto conf = reinterpret_cast<ProgramConfig *>(config);
  auto is_rdma_server = *reinterpret_cast<bool *>(param);
  conf->is_rdma_server = is_rdma_server;
  return DOCA_SUCCESS;
}

doca_error_t register_params() {

  doca_error_t result = DOCA_SUCCESS;

  // Create and register PCI address param
  result = register_param(nullopt, make_optional("dpu_pci"),
                          make_optional("DOCA device PCI address in DPU"),
                          dpu_pci_callback, DOCA_ARGP_TYPE_STRING);
  if (result != DOCA_SUCCESS) {
    SPDLOG_ERROR("Failed to register DPU PCI address param: {}",
                 doca_get_error_string(result));
    return result;
  }

  result = register_param(nullopt, make_optional("host_pci"),
                          make_optional("DOCA device PCI address in host"),
                          host_pci_callback, DOCA_ARGP_TYPE_STRING);
  if (result != DOCA_SUCCESS) {
    SPDLOG_ERROR("Failed to register Host PCI address param: {}",
                 doca_get_error_string(result));
    return result;
  }

  // Create and register in file param
  result = register_param(nullopt, make_optional("in-file"),
                          make_optional("input file path"), in_file_callback,
                          DOCA_ARGP_TYPE_STRING);
  if (result != DOCA_SUCCESS) {
    SPDLOG_ERROR("Failed to register in file param: {}",
                 doca_get_error_string(result));
    return result;
  }
  // Create and register out file param
  result = register_param(nullopt, make_optional("out-file"),
                          make_optional("output file path"), out_file_callback,
                          DOCA_ARGP_TYPE_STRING);
  if (result != DOCA_SUCCESS) {
    SPDLOG_ERROR("Failed to register out file param: {}",
                 doca_get_error_string(result));
    return result;
  }
  // Create and register mode param
  result = register_param(nullopt, make_optional("mode"),
                          make_optional("compress or decompress"),
                          mode_callback, DOCA_ARGP_TYPE_STRING);
  if (result != DOCA_SUCCESS) {
    SPDLOG_ERROR("Failed to register out mode param: {}",
                 doca_get_error_string(result));
    return result;
  }

  // Create and register is_host param
  result = register_param(nullopt, make_optional("is_host"),
                          make_optional("Host or DPU"), is_host_callback,
                          DOCA_ARGP_TYPE_BOOLEAN);
  if (result != DOCA_SUCCESS) {
    SPDLOG_ERROR("Failed to register is_host param: {}",
                 doca_get_error_string(result));
    return result;
  }

  result = register_param(nullopt, make_optional("is_rdma_server"),
                          make_optional("RDMA Server or RDMA Client"),
                          is_rdma_server_callback, DOCA_ARGP_TYPE_BOOLEAN);
  if (result != DOCA_SUCCESS) {
    SPDLOG_ERROR("Failed to register is_host param: {}",
                 doca_get_error_string(result));
    return result;
  }

  result = register_param(nullopt, make_optional("doca_workq_depth"),
                          make_optional("doca workq depth"),
                          doca_workq_depth_callback, DOCA_ARGP_TYPE_INT);
  if (result != DOCA_SUCCESS) {
    SPDLOG_ERROR("Failed to register doca_workq_depth param: {}",
                 doca_get_error_string(result));
    return result;
  }

  result = register_param(nullopt, make_optional("doca_max_bufs"),
                          make_optional("doca max bufs"),
                          doca_max_bufs_callback, DOCA_ARGP_TYPE_INT);
  if (result != DOCA_SUCCESS) {
    SPDLOG_ERROR("Failed to register doca_max_bufs param: {}",
                 doca_get_error_string(result));
    return result;
  }

  return DOCA_SUCCESS;
}