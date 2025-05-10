#include "doca/doca_buf.h"
#include <spdlog/spdlog.h>
DocaBuf::DocaBuf() noexcept : buf_(nullptr), ptr_(nullptr), capacity_(0) {}

DocaBuf::DocaBuf(doca_buf *buf, uint8_t *ptr, size_t capacity) noexcept
    : buf_(buf), ptr_(ptr), capacity_(capacity) {}

DocaBuf::DocaBuf(DocaBuf &&buf)
    : buf_(buf.buf_), ptr_(buf.ptr_), capacity_(buf.capacity_) {
  buf.buf_ = nullptr;
  buf.ptr_ = nullptr;
  buf.capacity_ = 0;
}

DocaBuf &DocaBuf::operator=(DocaBuf &&rhs) {
  if (this == &rhs) {
    return *this;
  }
  buf_ = rhs.buf_;
  ptr_ = rhs.ptr_;
  capacity_ = rhs.capacity_;
  rhs.buf_ = nullptr;
  rhs.ptr_ = nullptr;
  rhs.capacity_ = 0;
  return *this;
}

DocaBuf::~DocaBuf() {
  if (buf_ == nullptr) {
    return;
  }
  auto res = doca_buf_refcount_rm(buf_, nullptr);
  if (res != DOCA_SUCCESS) {
    SPDLOG_ERROR("doca_buf refcount rm error");
  }
}

doca_error_t DocaBuf::set_data_by_offset(size_t offset, size_t len) {
  if (offset + len > capacity_) {
    SPDLOG_ERROR("overflow in DocaBuf set_data_by_offset");
    return DOCA_ERROR_INVALID_VALUE;
  }
  auto res = doca_buf_set_data(buf_, ptr_ + offset, len);
  if (res != DOCA_SUCCESS) {
    SPDLOG_ERROR("DocaBuf set data error: {}", doca_get_error_string(res));
  }

  return res;
}

tl::expected<size_t, doca_error_t> DocaBuf::get_doca_buf_data_len() {
  size_t len = 0;
  auto res = doca_buf_get_data_len(buf_, &len);
  if (res != DOCA_SUCCESS) {
    SPDLOG_ERROR("DocaBuf get_doca_buf_len error: {}",
                 doca_get_error_string(res));
    return tl::unexpected(res);
  }
  return len;
}