#include "doca/core.h"
#include <cstdint>
#include <doca_buf.h>
#include <doca_buf_inventory.h>
#include <doca_ctx.h>
#include <doca_error.h>
#include <doca_mmap.h>
#include <iostream>
#include <ostream>
#include <spdlog/spdlog.h>
#include <tl/expected.hpp>

DocaCore::DocaCore() noexcept {}

DocaCore::DocaCore(doca_dev *dev, doca_buf_inventory *buf_inv, doca_ctx *ctx,
                   doca_workq *workq, uint32_t workq_depth,
                   doca_event_handle_t event_handle) noexcept
    : dev_(dev), buf_inv_(buf_inv), ctx_(ctx), workq_(workq),
      workq_depth_(workq_depth), event_handle_(event_handle) {}

DocaCore::DocaCore(DocaCore &&core) noexcept {
  dev_ = core.dev_;
  buf_inv_ = core.buf_inv_;
  ctx_ = core.ctx_;
  workq_ = core.workq_;
  workq_depth_ = core.workq_depth_;
  event_handle_ = core.event_handle_;
  core.dev_ = nullptr;
  core.buf_inv_ = nullptr;
  core.ctx_ = nullptr;
  core.workq_ = nullptr;
  core.workq_depth_ = 0;
  core.event_handle_ = doca_event_invalid_handle;
}

void DocaCore::swap(DocaCore &rhs) noexcept {
  using std::swap;
  swap(dev_, rhs.dev_);
  swap(buf_inv_, rhs.buf_inv_);
  swap(ctx_, rhs.ctx_);
  swap(workq_, rhs.workq_);
  swap(workq_depth_, rhs.workq_depth_);
  swap(event_handle_, rhs.event_handle_);
}

DocaCore &DocaCore::operator=(DocaCore rhs) noexcept {
  rhs.swap(*this);
  return *this;
}

void swap(DocaCore &lhs, DocaCore &rhs) noexcept { lhs.swap(rhs); }

DocaCore::~DocaCore() {

  if (!dev_ || !buf_inv_ || !ctx_ || !workq_) {
    return;
  }

  auto result = DOCA_SUCCESS;
  result = doca_ctx_workq_rm(ctx_, workq_);
  if (result != DOCA_SUCCESS) {
    SPDLOG_ERROR("Failed to remove work queue from ctx: {}",
                 doca_get_error_string(result));
  }

  result = doca_workq_destroy(workq_);
  if (result != DOCA_SUCCESS) {
    SPDLOG_ERROR("Failed to destroy work queue: {}",
                 doca_get_error_string(result));
  }

  result = doca_ctx_stop(ctx_);
  if (result != DOCA_SUCCESS) {
    SPDLOG_ERROR("Unable to stop context: {}", doca_get_error_string(result));
  }

  result = doca_ctx_dev_rm(ctx_, dev_);
  if (result != DOCA_SUCCESS) {
    SPDLOG_ERROR("Failed to remove device from ctx: {}",
                 doca_get_error_string(result));
  }

  result = doca_dev_close(dev_);
  if (result != DOCA_SUCCESS) {
    SPDLOG_ERROR("Failed to close device: {}", doca_get_error_string(result));
  }
  result = doca_buf_inventory_destroy(buf_inv_);
  if (result != DOCA_SUCCESS) {
    SPDLOG_ERROR("Failed to destroy buf inventory: {}",
                 doca_get_error_string(result));
  }
}

tl::expected<DocaCore, doca_error_t>
DocaCore::create(doca_ctx *ctx, doca_dev *dev, uint32_t extensions,
                 uint32_t workq_depth, uint32_t max_bufs) {
  doca_error_t res;
  uint8_t res_support = 111;

  res = doca_ctx_dev_add(ctx, dev);
  if (res != DOCA_SUCCESS) {
    SPDLOG_ERROR("Unable to register device with lib context: {}",
                 doca_get_error_string(res));
    goto fail_ctx_dev_add;
  }

  res = doca_ctx_start(ctx);
  if (res != DOCA_SUCCESS) {
    SPDLOG_ERROR("Unable to start lib context: {}", doca_get_error_string(res));
    goto fail_ctx_start;
  }
  doca_buf_inventory *buf_inv;
  res = doca_buf_inventory_create(nullptr, max_bufs, extensions, &buf_inv);
  if (res != DOCA_SUCCESS) {
    SPDLOG_ERROR("Unable to create buffer inventory: {}",
                 doca_get_error_string(res));
    goto fail_buf_inv;
  }

  res = doca_buf_inventory_start(buf_inv);
  if (res != DOCA_SUCCESS) {
    SPDLOG_ERROR("Unable to start buffer inventory: {}",
                 doca_get_error_string(res));
    goto fail_buf_inventory_start;
  }
  struct doca_workq *workq;
  res = doca_workq_create(workq_depth, &workq);
  if (res != DOCA_SUCCESS) {
    SPDLOG_ERROR("Unable to create work queue: {}", doca_get_error_string(res));
    goto fail_workq_create;
  }
  {
    uint32_t depth = 0;
    res = doca_workq_get_depth(workq, &depth);
    if (res != DOCA_SUCCESS) {
      SPDLOG_ERROR("Unable to get workq dpeth: {}", doca_get_error_string(res));
    }
  }

  res = doca_ctx_get_event_driven_supported(ctx, &res_support);
  if (res != DOCA_SUCCESS) {
    SPDLOG_ERROR("doca_ctx_get_event_driven_supported failed, {}",
                 doca_get_error_string(res));
  }
  res = doca_workq_set_event_driven_enable(workq, 1);
  if (res != DOCA_SUCCESS) {
    SPDLOG_ERROR("Unable to enable doca workq event mode: {}",
                 doca_get_error_string(res));
    goto fail_ctx_workq_add;
  }
  doca_event_handle_t event_handle;
  res = doca_workq_get_event_handle(workq, &event_handle);
  if (res != DOCA_SUCCESS) {
    SPDLOG_ERROR("Unable to get doca event handle: {}",
                 doca_get_error_string(res));
    goto fail_ctx_workq_add;
  }

  res = doca_ctx_workq_add(ctx, workq);
  if (res != DOCA_SUCCESS) {
    SPDLOG_ERROR("Unable to register work queue with context: {}",
                 doca_get_error_string(res));
    goto fail_ctx_workq_add;
  }

  res = doca_workq_event_handle_arm(workq);
  if (res != DOCA_SUCCESS) {
    SPDLOG_ERROR("Unable to arm event handle: {}", doca_get_error_string(res));
    goto fail_ctx_workq_add;
  }
  res_support = 111;
  res = doca_workq_get_event_driven_enable(workq, &res_support);
  if (res != DOCA_SUCCESS) {
    SPDLOG_ERROR("doca_workq_get_event_driven_enable failed, {}",
                 doca_get_error_string(res));
  }

  return DocaCore(dev, buf_inv, ctx, workq, workq_depth, event_handle);
fail_ctx_workq_add:
  doca_workq_destroy(workq);
fail_workq_create:
  doca_ctx_stop(ctx);
fail_ctx_start:
  doca_ctx_dev_rm(ctx, dev);
fail_ctx_dev_add:
  doca_buf_inventory_stop(buf_inv);
fail_buf_inventory_start:
  doca_buf_inventory_destroy(buf_inv);
fail_buf_inv:
  // doca_mmap_dev_rm(dst_mmap, dev);
fail_add_dst_mmap_to_dev:
  // doca_mmap_destroy(dst_mmap);
fail_dst_mmap:
  // doca_mmap_dev_rm(src_mmap, dev);
fail_add_src_mmap_to_dev:
  // doca_mmap_destroy(src_mmap);
fail_src_mmap:
  return tl::unexpected(res);
}

doca_error_t DocaCore::submit_job(const doca_job *job) {
  auto result = doca_workq_submit(workq_, job);
  if (result != DOCA_SUCCESS) {
    SPDLOG_ERROR("Failed to submit compress job:{}",
                 doca_get_error_string(result));
    return result;
  }
  return result;
}

tl::expected<uint64_t, doca_error_t> DocaCore::retrieve_job() {

  doca_error_t result = DOCA_SUCCESS;
  doca_event event = {0};
  while ((result = doca_workq_progress_retrieve(
              workq_, &event, DOCA_WORKQ_RETRIEVE_FLAGS_NONE)) ==
         DOCA_ERROR_AGAIN) {
  }
  if (result != DOCA_SUCCESS) {
    SPDLOG_ERROR("Failed to retrieve job: {}", doca_get_error_string(result));
    return tl::unexpected(result);
  } else if (event.result.u64 != DOCA_SUCCESS) {
    SPDLOG_ERROR("job finished unsuccessfully: {}", event.result.u64);
    return tl::unexpected(DOCA_ERROR_UNEXPECTED);
  } else {
    return event.user_data.u64;
  }
}


/// Used in EventLoop.
tl::expected<uint64_t, doca_error_t> DocaCore::retrieve_job_once() {

  doca_error_t result = DOCA_SUCCESS;
  doca_event event = {0};
  result = doca_workq_progress_retrieve(workq_, &event,
                                        DOCA_WORKQ_RETRIEVE_FLAGS_NONE);
  if (result == DOCA_ERROR_AGAIN) {
    return tl::unexpected(result);
  }
  if (result != DOCA_SUCCESS) {
    SPDLOG_ERROR("Failed to retrieve job: {}", doca_get_error_string(result));
    return tl::unexpected(result);
  } else if (event.result.u64 != DOCA_SUCCESS) {
    SPDLOG_ERROR("job finished unsuccessfully: {}", event.result.u64);
    return tl::unexpected(DOCA_ERROR_UNEXPECTED);
  } else {
    return event.user_data.u64;
  }
}

tl::expected<uint64_t, doca_error_t> DocaCore::retrieve_job(long nanos) {

  doca_error_t result = DOCA_SUCCESS;
  doca_event event = {0};
  timespec ts;
  while ((result = doca_workq_progress_retrieve(
              workq_, &event, DOCA_WORKQ_RETRIEVE_FLAGS_NONE)) ==
         DOCA_ERROR_AGAIN) {
    /* Wait for the job to complete */
    ts.tv_sec = 0;
    ts.tv_nsec = nanos;
    nanosleep(&ts, &ts);
  }
  doca_workq_event_handle_arm(workq_);
  if (result != DOCA_SUCCESS) {
    SPDLOG_ERROR("Failed to retrieve job: {}", doca_get_error_string(result));
    doca_workq_event_handle_arm(workq_);
    return tl::unexpected(result);
  } else if (event.result.u64 != DOCA_SUCCESS) {
    SPDLOG_ERROR("job finished unsuccessfully: {}", event.result.u64);
    doca_workq_event_handle_arm(workq_);
    return tl::unexpected(DOCA_ERROR_UNEXPECTED);
  } else {
    doca_workq_event_handle_arm(workq_);
    return event.user_data.u64;
  }
}