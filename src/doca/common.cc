#include "doca/common.h"
#include "doca_error.h"
#include <cstdint>
#include <doca_dma.h>
#include <fstream>
#include <spdlog/spdlog.h>
#include <stdlib.h>
#include <string.h>
#include <tl/expected.hpp>
using std::ifstream;
using std::ofstream;
/// @brief parse the PCIe address to the out_buf
/// @param pci_addr [in]: PCIe address.
/// @param out_bdf [out]: store the PCIe address.
/// @return
doca_error_t parse_pci_addr(char const *pci_addr,
                            struct doca_pci_bdf *out_bdf) {
  unsigned int bus_bitmask = 0xFFFFFF00;
  unsigned int dev_bitmask = 0xFFFFFFE0;
  unsigned int func_bitmask = 0xFFFFFFF8;
  uint32_t tmpu;
  char tmps[4];

  if (pci_addr == NULL || strlen(pci_addr) != 7 || pci_addr[2] != ':' ||
      pci_addr[5] != '.')
    return DOCA_ERROR_INVALID_VALUE;

  tmps[0] = pci_addr[0];
  tmps[1] = pci_addr[1];
  tmps[2] = '\0';
  tmpu = strtoul(tmps, NULL, 16);
  if ((tmpu & bus_bitmask) != 0)
    return DOCA_ERROR_INVALID_VALUE;
  out_bdf->bus = tmpu;

  tmps[0] = pci_addr[3];
  tmps[1] = pci_addr[4];
  tmps[2] = '\0';
  tmpu = strtoul(tmps, NULL, 16);
  if ((tmpu & dev_bitmask) != 0)
    return DOCA_ERROR_INVALID_VALUE;
  out_bdf->device = tmpu;

  tmps[0] = pci_addr[6];
  tmps[1] = '\0';
  tmpu = strtoul(tmps, NULL, 16);
  if ((tmpu & func_bitmask) != 0)
    return DOCA_ERROR_INVALID_VALUE;
  out_bdf->function = tmpu;

  return DOCA_SUCCESS;
}

doca_error_t dma_jobs_is_supported(struct doca_devinfo *devinfo) {
  return doca_dma_job_get_supported(devinfo, DOCA_DMA_JOB_MEMCPY);
}

tl::expected<vector<uint8_t>, doca_error_t> read_file(string_view file_path) {
  auto in_file = ifstream{file_path.data(), std::ios::binary};
  if (!in_file) {
    SPDLOG_ERROR("Fail to open file {}", file_path);
    return tl::unexpected(DOCA_ERROR_UNEXPECTED);
  }
  in_file.seekg(0, std::ios::end);
  auto in_file_size = in_file.tellg();
  in_file.seekg(0, std::ios::beg);

  vector<uint8_t> in_file_data(in_file_size);
  in_file.read(reinterpret_cast<char *>(in_file_data.data()), in_file_size);
  in_file.close();
  return in_file_data;
}

doca_error_t write_file(string_view file_path, vector<uint8_t> &data) { 
  std::ofstream outputFile(file_path.data(), std::ios::binary);

  if (outputFile.is_open()) {
    outputFile.write(reinterpret_cast<char *>(data.data()), data.size());
    outputFile.close();
    return DOCA_SUCCESS;
  } else {
    SPDLOG_ERROR("Fail to open file {}", file_path);
    return DOCA_ERROR_UNEXPECTED;
  }
}