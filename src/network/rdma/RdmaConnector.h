#pragma once
#include "network/EventLoop.h"
#include "network/InetAddress.h"
#include "network/rdma/Callbacks.h"
#include "network/rdma/RdmaConfig.h"
#include "network/rdma/RdmaExchangeInfo.h"
#include "network/tcp/Buffer.h"
#include "network/tcp/Callbacks.h"
#include "network/tcp/TcpClient.h"
#include <atomic>
#include <cstdint>
#include <memory>
namespace hdc {
namespace network {
using tcp::Buffer;
using tcp::TcpConnectionPtr;
namespace rdma {
class RdmaConnector {

public:
  using NewRdmaConnectionCallback = std::function<void(RdmaConnectionPtr conn)>;
  static constexpr uint8_t kHandshakeCompleteMsg = 42;

  RdmaConnector();

  /// @brief: This function will be called when a new RDMA connection is
  /// established. This function is registered by RDMAClient or RDMAServer to
  /// set Send/Recv Callbacks.
  void setNewRdmaConnectionCallback(const NewRdmaConnectionCallback &cb) {
    newRdmaConnectionCallback_ = cb;
  }

  /// @brief: Initialize RDMA resources when a TcpConnection is established. It
  /// will shutdown the TcpConnection if error occours.
  void initialRdmaResource(const RdmaConfig &config,
                           const TcpConnectionPtr &tcpConn);

  const RdmaExchangeInfo &getLocalExchangeInfo() const;

  /// @brief: Handshake for establishing a RdmaConnection. Once successfully
  /// established, the TcpConnection will shutdown. It will also shutdown the
  /// TcpConnection if error occours.
  void handshakeQp(const TcpConnectionPtr &conn, Buffer *buffer);

private:
  enum class State {
    kRdmaNotInit,
    kRdmaInit,
    kRdmaRTR,
    kRdmaRTS,
    kRdmaConnected,
  };

  void setLocalExchangeInfo();

  State state_;
  NewRdmaConnectionCallback newRdmaConnectionCallback_;
  RdmaConnectionPtr connection_;
  RdmaExchangeInfo localExchangeInfo_;
};

using RdmaConnectorPtr = std::shared_ptr<RdmaConnector>;
} // namespace rdma
} // namespace network
} // namespace hdc