#pragma once
#include <cstddef>
#include <doca_buf.h>
#include <tl/expected.hpp>
class DocaBuf {
  doca_buf *buf_;
  uint8_t *ptr_;
  size_t capacity_;

public:
  DocaBuf() noexcept;

  DocaBuf(doca_buf *buf, uint8_t *ptr, size_t capacity) noexcept;

  DocaBuf(const DocaBuf &) = delete;

  DocaBuf &operator=(const DocaBuf &) = delete;

  DocaBuf(DocaBuf &&buf);

  DocaBuf &operator=(DocaBuf &&rhs);

  ~DocaBuf();

  inline doca_buf *get_doca_buf() { return buf_; }

  inline size_t get_capacity() { return capacity_; }

  doca_error_t set_data_by_offset(size_t offset, size_t len);

  tl::expected<size_t, doca_error_t> get_doca_buf_data_len();
};

struct ExportDesc {
  const void *export_desc;
  size_t export_desc_len;

  ExportDesc(const void *export_desc, size_t export_desc_len) noexcept
      : export_desc(export_desc), export_desc_len(export_desc_len) {}
};

struct ExportDescRemote {
  const void *export_desc;
  size_t export_desc_len;
  uint8_t *remote_addr;
  size_t remote_len;

  ExportDescRemote(const void *export_desc, size_t export_desc_len,
                   uint8_t *remote_addr, size_t remote_len)
      : export_desc(export_desc), export_desc_len(export_desc_len),
        remote_addr(remote_addr), remote_len(remote_len) {}
};