#pragma once
#include <algorithm>
#include <cstdint>
#include <memory>
#include <spdlog/spdlog.h>
#include <stddef.h>
class Blob {
public:
  Blob(size_t cap) : cap_(cap) {
    addr_ = std::shared_ptr<uint8_t[]>(new uint8_t[cap_],
                                       std::default_delete<uint8_t[]>());
    // do page mapping
    std::fill_n(addr_.get(), cap_, 1);
  }
  Blob() {}

  Blob(const Blob &rhs) = default;

  Blob &operator=(const Blob &rhs) = delete;

  Blob(Blob &&rhs) = default;

  Blob &operator=(Blob &&rhs) = default;

  uint8_t *get_addr() { return addr_.get(); }

  size_t get_cap() const { return cap_; }

  size_t get_size() const { return size_; }

  void set_size(size_t size) { size_ = size; }

private:
  std::shared_ptr<uint8_t[]> addr_{nullptr};
  size_t cap_{0};
  size_t size_{0};
};
class BlobPool {
public:
  BlobPool(size_t blob_cap, size_t n) : blob_cap_(blob_cap) {
    if (blob_cap_ == 0) {
      is_use = false;
      return;
    }
    for (int i = 0; i < n; ++i) {
      blobs_.emplace_back(blob_cap_);
    }
  }

  BlobPool(const BlobPool &) = delete;

  BlobPool &operator=(const BlobPool &) = delete;

  BlobPool(BlobPool &&) = delete;

  BlobPool &operator=(BlobPool &&) = delete;

  Blob acquireBlob(size_t cap) {
    if (!is_use) {
      return Blob{cap};
    }
    std::lock_guard<std::mutex> guard(mtx_);
    if (blobs_.empty()) {
      for (int i = 0; i < kInc; ++i) {
        blobs_.emplace_back(blob_cap_);
      }
      SPDLOG_WARN("BlobPool is empty");
    }
    Blob b = std::move(blobs_.back());
    blobs_.pop_back();
    return b;
  }

  void releaseBlob(Blob b) {
    if (!is_use) {
      return;
    }
    std::lock_guard<std::mutex> guard(mtx_);
    blobs_.push_back(std::move(b));
  }

private:
  const size_t blob_cap_;
  std::vector<Blob> blobs_;
  std::mutex mtx_;
  constexpr static int kInc{4};
  bool is_use{true};
};