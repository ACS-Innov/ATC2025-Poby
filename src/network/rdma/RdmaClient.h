#pragma once
#include "network/tcp/Buffer.h"
#include "network/tcp/Callbacks.h"
#include "network/EventLoop.h"
#include "network/rdma/Callbacks.h"
#include "network/rdma/RdmaConfig.h"
#include "network/InetAddress.h"
#include "network/tcp/Socket.h"
#include <atomic>
#include <mutex>
#include <network/rdma/RdmaConnection.h>
#include <network/rdma/RdmaConnector.h>
#include <network/tcp/TcpClient.h>

namespace hdc {
namespace network {
  using tcp::TcpClient;
  using tcp::Buffer;
namespace rdma {

class RdmaClient {

public:


  RdmaClient(EventLoop *loop, const InetAddress &serverAddr,
             const std::string &name, RdmaConfig rdmaConfig);

  RdmaClient(const RdmaClient &) = delete;

  RdmaClient &operator=(const RdmaClient &) = delete;


  // ~RdmaClient();


  /// @brief: Try to establish a TCP connection by using TCP client. When TCP
  /// connection is established, RDMA connection will begin to establish.
  void connect();

  // ToDo.
  // void disconnect();

  /// @brief: Users register it.
  inline void setRecvSuccessCallback(const RecvSuccessCallback &cb) {
    recvSuccessCallback_ = cb;
  }

  /// @brief: Users register it.
  inline void setRecvFailCallback(const RecvFailCallback &cb) {
    recvFailCallback_ = cb;
  }

  /// @brief: Users register it.
  inline void
  setSendCompleteSuccessCallback(const SendCompleteSuccessCallback &cb) {
    sendCompleteSuccessCallback_ = cb;
  }

  /// @brief: Users register it.
  inline void setSendCompleteFailCallback(const SendCompleteFailCallback &cb) {
    sendCompleteFailCallback_ = cb;
  }

  /// @brief: Users register it.
  inline void setConnectedCallback(const ConnectedCallback &cb) {
    connectedCallback_ = cb;
  }

  /// @brief: Users register it.
  inline void setDisconnectedCallback(const DisconnectedCallback &cb) {
    disconnectedCallback_ = cb;
  }

private:
  /// @brief: Register to RdmaConnector's newRdmaConnectionCallback;
  void newRdmaConnection(RdmaConnectionPtr conn);

  /// @brief: Register to TcpClient for establishing RDMA connection.
  /// @detail: It will create a RdmaConnector for establish RDMA connection. The
  /// RdmaConnector struct will be stored inside TcpConnection struct.
  void onTcpConnection(const TcpConnectionPtr &conn);

  /// @brief: Register to TcpClient for establishing RDMA connection.
  void onTcpMessage(const TcpConnectionPtr &conn, Buffer *buffer,
                    Timestamp timestmap);

  /// @brief: Register to TcpClient for establishing RDMA connection.
  void onTcpWriteComplete(const TcpConnectionPtr &conn);
  
  TcpClient tcpClient_;
  std::string name_;
  std::atomic_bool isConnect_;
  EventLoop *loop_;
  RdmaConfig rdmaConfig_;
  std::mutex mutex_;
  RdmaConnectionPtr connection_;
  // Those callbacks are implemented by users for user logics.
  RecvSuccessCallback recvSuccessCallback_;
  RecvFailCallback recvFailCallback_;
  SendCompleteSuccessCallback sendCompleteSuccessCallback_;
  SendCompleteFailCallback sendCompleteFailCallback_;
  ConnectedCallback connectedCallback_;
  DisconnectedCallback disconnectedCallback_;
};
} // namespace rdma
} // namespace network
} // namespace hdc