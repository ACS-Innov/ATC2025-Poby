#pragma once
#include "doca/doca_buf.h"
#include <cstddef>
#include <cstdint>
#include <deque>
#include <doca_buf_inventory.h>
#include <doca_compress.h>
#include <doca_ctx.h>
#include <doca_dev.h>
#include <doca_error.h>
#include <doca_mmap.h>
#include <doca_types.h>
#include <map>
#include <spdlog/spdlog.h>
#include <tl/expected.hpp>

class DocaCore {

private:
  friend class CompressEngine;

  doca_dev *dev_{nullptr};
  doca_buf_inventory *buf_inv_{nullptr};
  doca_ctx *ctx_{nullptr};
  doca_workq *workq_{nullptr};
  uint32_t workq_depth_{0};
  doca_event_handle_t event_handle_{doca_event_invalid_handle};

  DocaCore(doca_dev *dev, doca_buf_inventory *buf_inv, doca_ctx *ctx,
           doca_workq *workq, uint32_t workq_depth,
           doca_event_handle_t event_handle) noexcept;

public:
  doca_error_t submit_job(const doca_job *job);

  tl::expected<uint64_t, doca_error_t> retrieve_job();

  tl::expected<uint64_t, doca_error_t> retrieve_job(long nanos);

  tl::expected<uint64_t, doca_error_t> retrieve_job_once();
  /// Initialize a series of DOCA Core objects needed for the program's
  /// execution
  /// @extensions [in]: bitmap of extensions enabled for the inventory described
  /// in doca_buf.h.
  /// @workq_depth [in]: depth for the created Work Queue
  /// @max_bufs [in]: maximum number of bufs for DOCA Mmap
  static tl::expected<DocaCore, doca_error_t>
  create(doca_ctx *ctx, doca_dev *dev, uint32_t extensions,
         uint32_t workq_depth, uint32_t max_bufs);

  DocaCore() noexcept;

  DocaCore(DocaCore &&rhs) noexcept;

  ~DocaCore();

  void swap(DocaCore &rhs) noexcept;

  DocaCore &operator=(DocaCore rhs) noexcept;

  DocaCore(const DocaCore &) = delete;
};
void swap(DocaCore &lhs, DocaCore &rhs) noexcept;
