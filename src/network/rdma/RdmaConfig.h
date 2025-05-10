#pragma once
#include <cstddef>
#include <string>

namespace hdc {
namespace network {
namespace rdma {
struct RdmaConfig {
  std::string ibDevName_;
  int ibDevPort_;
  size_t memSize_;
  size_t memNum_;

  RdmaConfig(std::string ibDevName, int ibDevPort, size_t memSize,
             size_t memNum)
      : ibDevName_(std::move(ibDevName)), ibDevPort_(ibDevPort),
        memSize_(memSize), memNum_(memNum) {}
};
} // namespace rdma
} // namespace network
} // namespace hdc
