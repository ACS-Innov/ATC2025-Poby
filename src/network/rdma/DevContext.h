#pragma once
#include "network/rdma/error.h" 
#include <infiniband/verbs.h>
#include <memory>
#include <string>
#include <tl/expected.hpp>
namespace hdc {
namespace network {
namespace rdma {
constexpr int kMaxGidCount = 256;

class RdmaConnection;
class DevContext {
public:
  void get_rocev2_gid_index();

  bool is_ipv4_gid(int gid_index);

  // IB device name
  std::string ib_dev_name_;
  // IB device port
  int ib_dev_port_;
  // Global identifier
  int gid_index_list_[kMaxGidCount];
  union ibv_gid gid_list_[kMaxGidCount];
  size_t gid_count_;
  // GUID
  uint64_t guid_;
  // IB device context
  ibv_context *ctx_;
  // IB device attribute
  ibv_device_attr dev_attr_;
  // IB port attribute
  ibv_port_attr port_attr_;

public:

friend class RdmaConnection;
  DevContext() noexcept;

  DevContext(std::string ib_dev_name, int ib_dev_port, ibv_context *ctx,
             uint64_t guid) noexcept;

  DevContext(const DevContext &) = delete;

  DevContext(DevContext &&dev_ctx) noexcept;

  DevContext &operator=(const DevContext &) = delete;

  DevContext &operator=(DevContext &&) noexcept;

  ~DevContext();

  static tl::expected<std::unique_ptr<DevContext>, RDMAError>
  create(std::string_view ib_dev_name, int ib_dev_port);
};

} // namespace rdma
} // namespace network
} // namespace hdc