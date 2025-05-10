#pragma once
#include "core.h"
#include "doca/common.h"
#include "doca/doca_buf.h"
#include "doca_mmap.h"
#include "folly/concurrency/ConcurrentHashMap.h"
#include "network/Channel.h"
#include "network/EventLoop.h"
#include "network/Timestamp.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <doca_compress.h>
#include <doca_error.h>
#include <memory>
#include <optional>
#include <spdlog/spdlog.h>
#include <tl/expected.hpp>
#include <vector>
using hdc::network::Channel;
using hdc::network::EventLoop;
using hdc::network::Timestamp;
using std::uint8_t;
using std::vector;

class CompressEngine;
using CompressSuccessCallback = std::function<void(
    CompressEngine &engine, uint64_t job_id, uint8_t *src_addr, size_t src_len,
    uint8_t *dst_addr, size_t dst_len)>;
using CompressErrorCallback =
    std::function<void(CompressEngine &engine, doca_error_t err)>;

class CompressEngine {
private:
  struct Job {
    uint64_t job_id;
    doca_compress_job_types job_type;
  };

  struct DocaBufPair {
    std::vector<uint8_t> src_mem;
    std::vector<uint8_t> dst_mem;
    DocaBuf src_doca_buf;
    DocaBuf dst_doca_buf;
    doca_mmap *src_mmap{nullptr};
    doca_mmap *dst_mmap{nullptr};
    int id;
    bool src_mmap_start{false};
    bool dst_mmap_start{false};
    DocaBufPair(size_t src_mem_size, size_t dst_mem_size, int id) noexcept
        : src_mem(src_mem_size, 0), dst_mem(dst_mem_size, 0), id(id) {}
  };

  doca_compress *compress_{nullptr};
  DocaCore core_;
  std::vector<DocaBufPair> bufpairs_;
  // store the id of bufpairs_
  std::vector<int> free_bufpairs_;
  EventLoop *loop_{nullptr};
  std::unique_ptr<Channel> channel_;
  CompressSuccessCallback compress_success_cb_;
  CompressErrorCallback compress_error_cb_;
  bool engine_busy_{false};
  size_t ongoing_bufpair_id_{0};

  CompressEngine(doca_compress *compress, DocaCore core, EventLoop *loop,
                 size_t src_mem_size, size_t dst_mem_size,
                 int mem_num) noexcept;

  tl::expected<DocaBuf, doca_error_t>
  mmap_start(doca_mmap *mmap, uint8_t *addr, size_t len,
             std::optional<uint32_t> access_mask);

  tl::expected<ExportDesc, doca_error_t> mmap_export_dpu(doca_mmap *mmap);

  tl::expected<DocaBuf, doca_error_t>
  mmap_create_from_export(doca_mmap *mmap, const void *export_desc,
                          size_t export_desc_len, uint8_t *remote_addr,
                          size_t remote_len);

  doca_error_t mmap_stop(doca_mmap *mmap) { return doca_mmap_stop(mmap); }

  doca_mmap *create_mmap() {
    auto res = DOCA_SUCCESS;
    doca_mmap *mmap = nullptr;
    res = doca_mmap_create(nullptr, &mmap);
    if (res != DOCA_SUCCESS) {
      SPDLOG_ERROR("Unable to create src_mmap: {}", doca_get_error_string(res));
      goto fail_mmap_create;
    }
    res = doca_mmap_dev_add(mmap, core_.dev_);
    if (res != DOCA_SUCCESS) {
      SPDLOG_ERROR("Fail to add src_mmap to dev: {}",
                   doca_get_error_string(res));
      goto fail_add_mmap_to_dev;
    }
    return mmap;
  fail_add_mmap_to_dev:
    doca_mmap_destroy(mmap);
  fail_mmap_create:
    return nullptr;
  }

public:
  static tl::expected<CompressEngine, doca_error_t>
  create(const char *pci_addr, uint32_t extensions, uint32_t workq_depth,
         EventLoop *loop, size_t src_mem_size, size_t dst_mem_size,
         int mem_num = 1);

  CompressEngine() noexcept;

  CompressEngine(CompressEngine &&engine) noexcept;

  ~CompressEngine();

  void swap(CompressEngine &rhs) noexcept;

  CompressEngine &operator=(CompressEngine rhs) noexcept;

  CompressEngine(const CompressEngine &) = delete;

  void start() { channel_->enableReading(); }

  void setCompressSuccessCallback(CompressSuccessCallback cb) {
    compress_success_cb_ = std::move(cb);
  }

  void setCompressErrorCallback(CompressErrorCallback cb) {
    compress_error_cb_ = std::move(cb);
  }

  doca_error_t src_mmaps_start(std::optional<uint32_t> access_mask) {
    for (auto &buf : bufpairs_) {
      doca_mmap *src_mmap = create_mmap();
      if (src_mmap == nullptr) {
        SPDLOG_ERROR("create src_mmap error");
        return DOCA_ERROR_UNEXPECTED;
      }
      buf.src_mmap = src_mmap;
      auto doca_buf = mmap_start(buf.src_mmap, buf.src_mem.data(),
                                 buf.src_mem.size(), access_mask);
      if (!doca_buf.has_value()) {
        SPDLOG_ERROR("start src_mmap error");
        return doca_buf.error();
      }
      buf.src_doca_buf = std::move(*doca_buf);
      buf.src_mmap_start = true;
    }
    return DOCA_SUCCESS;
  }

  doca_error_t dst_mmaps_start(std::optional<uint32_t> access_mask) {
    for (auto &buf : bufpairs_) {
      doca_mmap *dst_mmap = create_mmap();
      if (dst_mmap == nullptr) {
        SPDLOG_ERROR("create dst_mmap error");
        return DOCA_ERROR_UNEXPECTED;
      }
      buf.dst_mmap = dst_mmap;
      auto doca_buf = mmap_start(buf.dst_mmap, buf.dst_mem.data(),
                                 buf.dst_mem.size(), access_mask);
      if (!doca_buf.has_value()) {
        SPDLOG_ERROR("start dst_mmap error");
        return doca_buf.error();
      }
      buf.dst_doca_buf = std::move(*doca_buf);
      buf.dst_mmap_start = true;
    }
    return DOCA_SUCCESS;
  }

  doca_error_t src_mmaps_create_from_export(
      const std::vector<ExportDescRemote> &export_descs) {

    assert(bufpairs_.size() == export_descs.size());
    for (int i = 0, n = bufpairs_.size(); i < n; ++i) {
      auto &buf = bufpairs_[i];
      auto &desc = export_descs[i];
      doca_mmap *src_mmap = create_mmap();
      if (src_mmap == nullptr) {
        SPDLOG_ERROR("create src_mmap error");
        return DOCA_ERROR_UNEXPECTED;
      }
      buf.src_mmap = src_mmap;
      auto doca_buf = mmap_create_from_export(
          buf.src_mmap, desc.export_desc, desc.export_desc_len,
          desc.remote_addr, desc.remote_len);

      if (!doca_buf.has_value()) {
        SPDLOG_ERROR("src_mmap create form export error: {}",
                     doca_get_error_string(doca_buf.error()));
        return doca_buf.error();
      }
      buf.src_doca_buf = std::move(*doca_buf);
    }
    return DOCA_SUCCESS;
  }

  doca_error_t dst_mmaps_create_from_export(
      const std::vector<ExportDescRemote> &export_descs) {
    assert(bufpairs_.size() == export_descs.size());

    for (int i = 0, n = bufpairs_.size(); i < n; ++i) {
      auto &buf = bufpairs_[i];
      auto &desc = export_descs[i];
      doca_mmap *dst_mmap = create_mmap();
      if (dst_mmap == nullptr) {
        SPDLOG_ERROR("create src_mmap error");
        return DOCA_ERROR_UNEXPECTED;
      }
      buf.dst_mmap = dst_mmap;
      auto doca_buf = mmap_create_from_export(
          buf.dst_mmap, desc.export_desc, desc.export_desc_len,
          desc.remote_addr, desc.remote_len);
      if (!doca_buf.has_value()) {
        SPDLOG_ERROR("dst_mmap create from export error: {}",
                     doca_get_error_string(doca_buf.error()));
        return doca_buf.error();
      }
      buf.dst_doca_buf = std::move(*doca_buf);
    }
    return DOCA_SUCCESS;
  }

  tl::expected<std::vector<ExportDesc>, doca_error_t> src_mmaps_export_dpu() {
    std::vector<ExportDesc> res;
    res.reserve(bufpairs_.size());
    for (auto &buf : bufpairs_) {
      auto export_res = mmap_export_dpu(buf.src_mmap);
      if (!export_res.has_value()) {
        SPDLOG_ERROR("src_mmap export dpu error: {}",
                     doca_get_error_string(export_res.error()));
        return tl::unexpected(export_res.error());
      }
      res.emplace_back(*export_res);
    }
    return res;
  }

  tl::expected<std::vector<ExportDesc>, doca_error_t> dst_mmaps_export_dpu() {
    std::vector<ExportDesc> res;
    res.reserve(bufpairs_.size());
    for (auto &buf : bufpairs_) {
      auto export_res = mmap_export_dpu(buf.dst_mmap);
      if (!export_res.has_value()) {
        SPDLOG_ERROR("dst_mmap export dpu error: {}",
                     doca_get_error_string(export_res.error()));
        return tl::unexpected(export_res.error());
      }
      res.emplace_back(*export_res);
    }
    return res;
  }

  /// @brief: Start a compresss job.
  /// @detail: Not thread safe. Must be called in the EventLoop thread of
  /// CompressEngine. Before call this function. You must make sure there are no
  /// ongoing compress tasks.
  void start_job(uint64_t job_id, doca_compress_job_types job_type,
                 size_t bufpair_id) {
    assert(engine_busy_ == false);
    ongoing_bufpair_id_ = bufpair_id;
    auto &buf = bufpairs_[bufpair_id];
    const struct doca_compress_deflate_job compress_job = {
        .base =
            {
                .type = job_type,
                .flags = DOCA_JOB_FLAGS_NONE,
                .ctx = core_.ctx_,
                .user_data = {.u64 = job_id},
            },
        .dst_buff = buf.dst_doca_buf.get_doca_buf(),
        .src_buff = buf.src_doca_buf.get_doca_buf(),
        .output_chksum = nullptr,
    };
    core_.submit_job(&compress_job.base);
    engine_busy_ = true;
  }

  DocaCore *get_core() { return &core_; }

  void handleRead(Timestamp recvTime);

  void handleError();

  DocaBufPair &get_bufpair(size_t idx) { return bufpairs_[idx]; }

  bool engine_busy() { return engine_busy_; }

  size_t bufpair_num() { return bufpairs_.size(); }

  void releaseFreeBufpair(size_t id) {
    assert(id < bufpairs_.size());
    free_bufpairs_.emplace_back(id);
  }

  std::optional<size_t> acquireFreeBufpair() {
    if (!free_bufpairs_.empty()) {
      auto id = free_bufpairs_.back();
      free_bufpairs_.pop_back();
      return id;
    } else {
      return std::nullopt;
    }
  }
};

void swap(CompressEngine &lhs, CompressEngine &rhs) noexcept;
