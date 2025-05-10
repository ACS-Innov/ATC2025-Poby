// Inspired by Muduo https://github.com/chenshuo/muduo
#pragma once

#include <mutex>
#include <network/tcp/TcpConnection.h>
namespace hdc {
namespace network {
namespace tcp {
class TcpConnector;
typedef std::shared_ptr<TcpConnector> ConnectorPtr;

class TcpClient {
public:
  // TcpClient(EventLoop* loop);
  // TcpClient(EventLoop* loop, const string& host, uint16_t port);
  TcpClient(const TcpClient &) = delete;
  TcpClient &operator=(const TcpClient &) = delete;
  TcpClient(EventLoop *loop, const InetAddress &serverAddr,
            const std::string &nameArg);
  ~TcpClient(); // force out-line dtor, for std::unique_ptr members.

  void connect();
  void disconnect();
  void stop();

  TcpConnectionPtr connection() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return connection_;
  }

  EventLoop *getLoop() const { return loop_; }
  bool retry() const { return retry_; }
  void enableRetry() { retry_ = true; }

  const std::string &name() const { return name_; }

  /// @briref: Set connection callback. The cb function is registered to
  /// TcpConnection, it will be called when TcpConnection is established and
  /// when TcpConnection is destroyed. Note that it will not be registered until
  /// the connection is established.
  /// @safety: This funcion is not thread safe.
  void setConnectionCallback(ConnectionCallback cb) {
    connectionCallback_ = std::move(cb);
  }

  /// Set message callback.
  /// Not thread safe.
  void setMessageCallback(MessageCallback cb) {
    messageCallback_ = std::move(cb);
  }

  /// Set write complete callback.
  /// Not thread safe.
  void setWriteCompleteCallback(WriteCompleteCallback cb) {
    writeCompleteCallback_ = std::move(cb);
  }

private:
  /// @brief: It will be called when the sockfd is connected successfully.
  /// @detail: Create a new TcpConnectionPtr and store in TcpClient. Register
  /// and call user define callbacks.
  /// @safety: Not thread safe, but in loop.
  void newConnection(int sockfd);
  /// @brief: It will be called when a TcpConnection is disconnected.
  /// @detail: Remove the TcpConnection from TcpClient, the TcpConnection will
  /// be destroyed.
  /// @safety: Not thread safe, but in loop.
  void removeConnection(const TcpConnectionPtr &conn);

  EventLoop *loop_;
  ConnectorPtr connector_; // avoid revealing Connector
  const std::string name_;
  ConnectionCallback connectionCallback_;
  MessageCallback messageCallback_;
  WriteCompleteCallback writeCompleteCallback_;
  bool retry_;   // atomic
  bool connect_; // atomic
  // always in loop thread
  int nextConnId_;
  mutable std::mutex mutex_;
  TcpConnectionPtr connection_;
};
} // namespace tcp
} // namespace network
} // namespace hdc