#pragma once
#include <cstdint>
#include <functional>
#include <infiniband/verbs.h>
#include <memory>
// #include <network/rdma/RdmaConnection.h>
namespace hdc {
namespace network {
namespace rdma {
class RdmaConnection;
using RdmaConnectionPtr = std::shared_ptr<RdmaConnection>;

using RecvSuccessCallback =
    std::function<void(const RdmaConnectionPtr &conn, uint8_t *recv_buf,
                       uint32_t recv_len, const ibv_wc &wc)>;
using RecvFailCallback =
    std::function<void(const RdmaConnectionPtr &conn, const ibv_wc &wc)>;

using SendCompleteSuccessCallback =
    std::function<void(const RdmaConnectionPtr &conn, const ibv_wc &wc)>;

using SendCompleteFailCallback =
    std::function<void(const RdmaConnectionPtr &conn, const ibv_wc &wc)>;

using ConnectedCallback = std::function<void(const RdmaConnectionPtr &conn)>;

using DisconnectedCallback = std::function<void(const RdmaConnectionPtr &conn)>;
} // namespace rdma
} // namespace network
} // namespace hdc