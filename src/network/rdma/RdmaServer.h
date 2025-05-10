#pragma once
#include "network/EventLoop.h"
#include "network/rdma/RdmaConfig.h"
#include "network/rdma/RdmaConnector.h"
#include <mutex>
#include <network/rdma/Callbacks.h>
#include <network/tcp/TcpServer.h>
namespace hdc {
namespace network {
namespace rdma {
  using tcp::TcpServer;

class RdmaServer {

public:
  RdmaServer(EventLoop *loop, const InetAddress &listenAddr,
             const std::string &nameArg, RdmaConfig rdmaConfig);

  RdmaServer(const RdmaServer &) = delete;

  RdmaServer &operator=(const RdmaServer &) = delete;

  /// @brief: Try to start the server.
  void start();

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
  using ConnectionMap = std::map<std::string, RdmaConnectionPtr>;

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

  TcpServer tcpServer_;
  std::string name_;
  EventLoop *loop_;
  RdmaConfig rdmaConfig_;
  std::mutex mutex_;
  ConnectionMap connections_;
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