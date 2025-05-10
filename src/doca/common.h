#pragma once
#include <cstddef>
#include <cstdint>
#include <doca_buf_inventory.h>
#include <doca_compress.h>
#include <doca_ctx.h>
#include <doca_dev.h>
#include <doca_error.h>
#include <doca_mmap.h>
#include <doca_types.h>
#include <string_view>
#include <tl/expected.hpp>
#include <unistd.h>
#include <vector>
#include <spdlog/spdlog.h>
using std::string_view;
using std::uint8_t;
using std::vector;

// Maximum size of input argument
constexpr int MAX_ARG_SIZE = 256;
constexpr int SLEEP_IN_NANOS = 10 * 1000;
constexpr size_t MAX_FILE_SIZE =
    (128 * 1024 * 1024); /* compress files up to 128MB */
// Page size
#define PAGE_SIZE sysconf(_SC_PAGESIZE)
constexpr int WORKQ_DEPTH = 32;
constexpr int MAX_FILE_NAME = 255;
// Function to check if a given device is capable of executing some job
typedef doca_error_t (*jobs_check)(struct doca_devinfo *);
struct ProgramConfig {
  char host_pci_address[MAX_ARG_SIZE];
  char dpu_pci_address[MAX_ARG_SIZE];
  char in_file[MAX_FILE_NAME];
  char out_file[MAX_FILE_NAME];
  doca_compress_job_types mode;
  uint32_t doca_workq_depth;
  uint32_t doca_max_bufs;
  bool is_host;
  bool is_rdma_server;
};

doca_error_t parse_pci_addr(char const *pci_addr, struct doca_pci_bdf *out_bdf);

template <typename F>
doca_error_t open_doca_device_with_pci(const struct doca_pci_bdf *value,
                                       F &&funcs, struct doca_dev **retval) {
  struct doca_devinfo **dev_list;
  uint32_t nb_devs;
  struct doca_pci_bdf buf = {};
  auto res = DOCA_SUCCESS;
  size_t i;

  /* Set default return value */
  *retval = NULL;

  res = doca_devinfo_list_create(&dev_list, &nb_devs);
  if (res != DOCA_SUCCESS) {
    SPDLOG_ERROR("Failed to load doca devices list. Doca_error value: {}",
                 doca_get_error_string(res));
    return res;
  }

  /* Search */
  for (i = 0; i < nb_devs; i++) {
    res = doca_devinfo_get_pci_addr(dev_list[i], &buf);
    // if the pci address is the same as the one we want to use
    if (res == DOCA_SUCCESS && buf.raw == value->raw) {
      /* If any special capabilities are needed */
      bool job_check = true;
      for (auto &func : std::forward<F>(funcs)) {
        if (func(dev_list[i]) != DOCA_SUCCESS) {
          job_check = false;
          break;
        }
      }
      if (!job_check) {
        continue;
      }
      /* if device can be opened */
      res = doca_dev_open(dev_list[i], retval);
      if (res == DOCA_SUCCESS) {
        doca_devinfo_list_destroy(dev_list);
        return res;
      }
    }
  }

  SPDLOG_ERROR("Matching device not found.");
  res = DOCA_ERROR_NOT_FOUND;

  doca_devinfo_list_destroy(dev_list);
  return res;
}

doca_error_t dma_jobs_is_supported(struct doca_devinfo *devinfo);

tl::expected<vector<uint8_t>, doca_error_t> read_file(string_view file_path);

doca_error_t write_file(string_view file_path, vector<uint8_t> &data);