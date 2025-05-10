#include "doca/compress.h"
#include "doca/common.h"
#include "doca/core.h"
#include "doca/doca_buf.h"
#include "network/Timestamp.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <doca_buf.h>
#include <doca_buf_inventory.h>
#include <doca_compress.h>
#include <doca_ctx.h>
#include <doca_error.h>
#include <doca_mmap.h>
#include <doca_types.h>
#include <exception>
#include <spdlog/spdlog.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <tl/expected.hpp>
#include <type_traits>
#include <unistd.h>
#include <vector>
using hdc::network::Timestamp;

/**
 * Check if given device is capable of executing a DOCA_COMPRESS_DEFLATE_JOB.
 *
 * @devinfo [in]: The DOCA device information
 * @return: DOCA_SUCCESS if the device supports DOCA_COMPRESS_DEFLATE_JOB and
 * DOCA_ERROR otherwise.
 */
static doca_error_t
compress_jobs_compress_is_supported(struct doca_devinfo *devinfo) {
  return doca_compress_job_get_supported(devinfo, DOCA_COMPRESS_DEFLATE_JOB);
}

/**
 * Check if given device is capable of executing a DOCA_DECOMPRESS_DEFLATE_JOB.
 *
 * @devinfo [in]: The DOCA device information
 * @return: DOCA_SUCCESS if the device supports DOCA_DECOMPRESS_DEFLATE_JOB and
 * DOCA_ERROR otherwise.
 */
static doca_error_t
compress_jobs_decompress_is_supported(struct doca_devinfo *devinfo) {
  return doca_compress_job_get_supported(devinfo, DOCA_DECOMPRESS_DEFLATE_JOB);
}

CompressEngine::CompressEngine(doca_compress *compress, DocaCore core,
                               EventLoop *loop, size_t src_mem_size,
                               size_t dst_mem_size, int mem_num) noexcept
    : compress_(compress), core_(std::move(core)), loop_(loop),
      channel_(new Channel(loop, static_cast<int>(core_.event_handle_),
                           "DocaCompress")) {
  bufpairs_.reserve(mem_num);
  for (int i = 0; i < mem_num; ++i) {
    bufpairs_.emplace_back(src_mem_size, dst_mem_size, i);
    free_bufpairs_.emplace_back(i);
  }
  channel_->setReadCallback(
      [this](Timestamp recv_time) { handleRead(recv_time); });
  channel_->setErrorCallback([this]() { handleError(); });
}

CompressEngine::CompressEngine() noexcept {}

CompressEngine::~CompressEngine() {
  if (!compress_) {
    return;
  }
  auto res = DOCA_SUCCESS;
  for (auto &buf : bufpairs_) {
    if (buf.src_mmap != nullptr) {
      if (buf.src_mmap_start) {
        res = doca_mmap_stop(buf.src_mmap);
        if (res != DOCA_SUCCESS) {
          SPDLOG_ERROR("src_mmap stop error: {}", doca_get_error_string(res));
        }
      }
      res = doca_mmap_dev_rm(buf.src_mmap, core_.dev_);
      if (res != DOCA_SUCCESS) {
        SPDLOG_ERROR("src_mmap dev rm error: {}", doca_get_error_string(res));
      }
      res = doca_mmap_destroy(buf.src_mmap);
      if (res != DOCA_SUCCESS) {
        SPDLOG_ERROR("src_mmap destroy error: {}", doca_get_error_string(res));
      }
    }
    if (buf.dst_mmap != nullptr) {
      if (buf.dst_mmap_start) {
        res = doca_mmap_stop(buf.dst_mmap);
        if (res != DOCA_SUCCESS) {
          SPDLOG_ERROR("dst_mmap stop error: {}", doca_get_error_string(res));
        }
      }
      res = doca_mmap_dev_rm(buf.dst_mmap, core_.dev_);
      if (res != DOCA_SUCCESS) {
        SPDLOG_ERROR("dst_mmap dev rm error: {}", doca_get_error_string(res));
      }
      res = doca_mmap_destroy(buf.dst_mmap);
      if (res != DOCA_SUCCESS) {
        SPDLOG_ERROR("dst_mmap destory error: {}", doca_get_error_string(res));
      }
    }
  }
  {
    // deconstruct the DocaCore first
    DocaCore c{};
    core_.swap(c);
  }
  doca_error_t result = doca_compress_destroy(compress_);
  if (result != DOCA_SUCCESS) {
    SPDLOG_ERROR("compress destroy error");
  }
}

CompressEngine::CompressEngine(CompressEngine &&engine) noexcept
    : compress_(engine.compress_), core_(std::move(engine.core_)),
      loop_(engine.loop_), bufpairs_(std::move(engine.bufpairs_)),
      free_bufpairs_(std::move(engine.free_bufpairs_)),
      channel_(std::move(engine.channel_)),
      compress_success_cb_(std::move(engine.compress_success_cb_)),
      compress_error_cb_(std::move(engine.compress_error_cb_)),
      engine_busy_(std::move(engine.engine_busy_)),
      ongoing_bufpair_id_(engine.ongoing_bufpair_id_) {
  channel_->setReadCallback(
      [this](Timestamp recv_time) { handleRead(recv_time); });
  channel_->setErrorCallback([this]() { handleError(); });
  engine.compress_ = nullptr;
  engine.loop_ = nullptr;
}

void CompressEngine::swap(CompressEngine &rhs) noexcept {
  using std::swap;
  swap(compress_, rhs.compress_);
  swap(core_, rhs.core_);
  swap(loop_, rhs.loop_);
  swap(bufpairs_, rhs.bufpairs_);
  swap(free_bufpairs_, rhs.free_bufpairs_);
  swap(channel_, rhs.channel_);
  swap(compress_success_cb_, rhs.compress_success_cb_);
  swap(compress_error_cb_, rhs.compress_error_cb_);
  swap(engine_busy_, rhs.engine_busy_);
  swap(ongoing_bufpair_id_, rhs.ongoing_bufpair_id_);
  channel_->setReadCallback(
      [this](Timestamp recv_time) { handleRead(recv_time); });
  channel_->setErrorCallback([this]() { handleError(); });
  rhs.channel_->setReadCallback(
      [&rhs](Timestamp recv_time) { rhs.handleRead(recv_time); });
  rhs.channel_->setErrorCallback([&rhs]() { rhs.handleError(); });
}

CompressEngine &CompressEngine::operator=(CompressEngine rhs) noexcept {
  rhs.swap(*this);
  return *this;
}

void swap(CompressEngine &lhs, CompressEngine &rhs) noexcept { lhs.swap(rhs); }

tl::expected<CompressEngine, doca_error_t>
CompressEngine::create(const char *pci_addr, uint32_t extensions,
                       uint32_t workq_depth, EventLoop *loop,
                       size_t src_mem_size, size_t dst_mem_size, int mem_num) {
  assert(workq_depth >= mem_num);
  doca_error_t result = DOCA_SUCCESS;
  doca_pci_bdf pci_dev;

  result = parse_pci_addr(pci_addr, &pci_dev);
  if (result != DOCA_SUCCESS) {
    SPDLOG_ERROR("parse pci addr {} error: {}", pci_addr,
                 doca_get_error_string(result));
    return tl::unexpected(result);
  }

  doca_dev *dev;
  result =
      open_doca_device_with_pci(&pci_dev,
                                vector{compress_jobs_compress_is_supported,
                                       compress_jobs_decompress_is_supported},
                                &dev);

  if (result != DOCA_SUCCESS) {
    SPDLOG_ERROR("Fail to open doca device with pci: {}",
                 doca_get_error_string(result));
    goto FAIL_OPEN_DOCA_DEVICE;
  }
  doca_compress *compress;
  result = doca_compress_create(&compress);
  if (result != DOCA_SUCCESS) {
    SPDLOG_ERROR("Unable to create compress engine:{}",
                 doca_get_error_string(result));
    return tl::unexpected(result);
  }

  doca_ctx *ctx;
  ctx = doca_compress_as_ctx(compress);
  if (ctx == nullptr) {
    SPDLOG_ERROR("contert compress to ctx error");
    return tl::unexpected(DOCA_ERROR_UNEXPECTED);
  }

  {
    uint32_t max_bufs = mem_num * 2;
    auto doca_core = DocaCore::create(ctx, dev, DOCA_BUF_EXTENSION_NONE,
                                      workq_depth, max_bufs);
    if (!doca_core.has_value()) {
      result = doca_core.error();
      goto FAIL_CREATE_DOCA_CORE;
    }

    return CompressEngine(compress, std::move(*doca_core), loop, src_mem_size,
                          dst_mem_size, mem_num);
  }
FAIL_CREATE_DOCA_CORE:
FAIL_OPEN_DOCA_DEVICE:
  doca_compress_destroy(compress);
  return tl::unexpected(result);
}

tl::expected<DocaBuf, doca_error_t>
CompressEngine::mmap_start(doca_mmap *mmap, uint8_t *addr, size_t len,
                           std::optional<uint32_t> access_mask) {
  auto result = DOCA_SUCCESS;
  result = doca_mmap_set_memrange(mmap, addr, len);

  if (result != DOCA_SUCCESS) {
    SPDLOG_ERROR("DOCA mmap set memrange error: {}",
                 doca_get_error_string(result));
    return tl::unexpected(result);
  }

  if (access_mask.has_value()) {
    result = doca_mmap_set_permissions(mmap, access_mask.value());
    if (result != DOCA_SUCCESS) {
      SPDLOG_ERROR("set DOCA mmap permission error: {}",
                   doca_get_error_string(result));
      return tl::unexpected(result);
    }
  }

  result = doca_mmap_start(mmap);
  struct doca_buf *buf;
  result =
      doca_buf_inventory_buf_by_addr(core_.buf_inv_, mmap, addr, len, &buf);
  if (result != DOCA_SUCCESS) {
    SPDLOG_ERROR("buf_inventory_buf_by_addr error: {}",
                 doca_get_error_string(result));
    return tl::unexpected(result);
  }
  return DocaBuf{buf, addr, len};
}

tl::expected<ExportDesc, doca_error_t>
CompressEngine::mmap_export_dpu(doca_mmap *mmap) {
  const void *export_desc = nullptr;
  size_t export_desc_len = 0;
  auto result =
      doca_mmap_export_dpu(mmap, core_.dev_, &export_desc, &export_desc_len);
  if (result != DOCA_SUCCESS) {
    SPDLOG_ERROR("mmap export DPU error: {}", doca_get_error_string(result));
    return tl::unexpected(result);
  }
  return ExportDesc{export_desc, export_desc_len};
}

tl::expected<DocaBuf, doca_error_t> CompressEngine::mmap_create_from_export(
    doca_mmap *mmap, const void *export_desc, size_t export_desc_len,
    uint8_t *remote_addr, size_t remote_len) {

  auto result = doca_mmap_create_from_export(
      nullptr, export_desc, export_desc_len, core_.dev_, &mmap);
  if (result != DOCA_SUCCESS) {
    SPDLOG_ERROR("mmap_create_from export error: {}",
                 doca_get_error_string(result));
    return tl::unexpected(result);
  }
  struct doca_buf *buf;
  result = doca_buf_inventory_buf_by_addr(core_.buf_inv_, mmap, remote_addr,
                                          remote_len, &buf);
  if (result != DOCA_SUCCESS) {
    SPDLOG_ERROR("buf_inventory_buf_by_addr error: {}",
                 doca_get_error_string(result));
    return tl::unexpected(result);
  }
  return DocaBuf{buf, remote_addr, remote_len};
}

void CompressEngine::handleRead(Timestamp recvTime) {

  auto err = doca_workq_event_handle_clear(core_.workq_, core_.event_handle_);
  if (err != DOCA_SUCCESS) {
    SPDLOG_ERROR("doca_workq_event_handle_clear error: {}",
                 doca_get_error_string(err));

    return;
  }
  // must ensure that one task is completed before submitting the next task

  auto res = core_.retrieve_job_once();
  if (!res.has_value()) {
    SPDLOG_ERROR("retrive doca job error {}",
                 doca_get_error_string(res.error()));
    compress_error_cb_(*this, err);
    std::terminate();
    return;
  }
  engine_busy_ = false;
  auto &buf = bufpairs_[ongoing_bufpair_id_];
  compress_success_cb_(*this, *res, buf.src_mem.data(),
                       *buf.src_doca_buf.get_doca_buf_data_len(),
                       buf.dst_mem.data(),
                       *buf.dst_doca_buf.get_doca_buf_data_len());

  err = doca_workq_event_handle_arm(core_.workq_);
  if (err != DOCA_SUCCESS) {
    SPDLOG_ERROR("Unable to arm event handle: {}", doca_get_error_string(err));
    return;
  }
}

void CompressEngine::handleError() {
  SPDLOG_ERROR("CompressEngine handle doca event: error");
}