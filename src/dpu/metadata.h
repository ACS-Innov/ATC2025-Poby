#pragma once
#include <string>
#include <utils/blob_pool.h>
#include <vector>
namespace hdc {
namespace dpu {
struct ContentElement {
  Blob segment;
  int segment_idx;
  int total_segments;
  std::string layer;
  std::string image_name_tag;
  bool is_compressed{true};
};
} // namespace dpu
} // namespace hdc