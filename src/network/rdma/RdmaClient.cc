#include "network/rdma/Callbacks.h"
#include "network/rdma/RdmaConfig.h"
#include "network/rdma/RdmaConnector.h"
#include "network/tcp/TcpClient.h"
#include <any>
#include <cassert>
#include <mutex>
#include <network/rdma/RdmaClient.h>
#include <spdlog/spdlog.h>

namespace hdc::network::rdma {

RdmaClient::RdmaClient(EventLoop *loop, const InetAddress &serverAddr,
                       const std::string &name, RdmaConfig rdmaConfig)
    : loop_(loop), name_(name),
      tcpClient_(loop, serverAddr, name + "_tcpClient"), isConnect_(false),
      rdmaConfig_(std::move(rdmaConfig)) {

  tcpClient_.setConnectionCallback(
      [this](const TcpConnectionPtr &conn) { this->onTcpConnection(conn); });
  tcpClient_.setMessageCallback([this](const TcpConnectionPtr &conn,
                                       Buffer *buffer, Timestamp timestmap) {
    this->onTcpMessage(conn, buffer, timestmap);
  });
  tcpClient_.setWriteCompleteCallback(
      [this](const TcpConnectionPtr &conn) { this->onTcpWriteComplete(conn); });
}

void RdmaClient::newRdmaConnection(RdmaConnectionPtr conn) {
  loop_->assertInLoopThread();
  conn->setConnectedCallback(connectedCallback_);
  conn->setDisconnectedCallback(disconnectedCallback_);
  conn->setRecvSuccessCallback(recvSuccessCallback_);
  conn->setRecvFailCallback(recvFailCallback_);
  conn->setSendCompleteSuccessCallback(sendCompleteSuccessCallback_);
  conn->setSendCompleteFailCallback(sendCompleteFailCallback_);

  {
    std::lock_guard<std::mutex> lock(mutex_);
    connection_ = conn;
  }
  connection_->connectEstablished();
  isConnect_ = true;
}

void RdmaClient::connect() {
  assert(isConnect_ == false);
  tcpClient_.connect();
}

void RdmaClient::onTcpConnection(const TcpConnectionPtr &conn) {
  if (conn->connected()) {
    assert(conn->getContext().has_value() == false);
    conn->setContext(std::make_any<RdmaConnector>());
    auto connector = std::any_cast<RdmaConnector>(conn->getMutableContext());
    connector->setNewRdmaConnectionCallback(
        [this](RdmaConnectionPtr conn) { this->newRdmaConnection(conn); });
    connector->initialRdmaResource(rdmaConfig_, conn);
  } else {
    if (!isConnect_) {
      SPDLOG_ERROR("RdmaConnector: Tcp disconnected but RDMA is not connect");
    } else {
      // SPDLOG_DEBUG("RdmaConnector: Tcp disconnect and RDMA is connected");
    }
  }
}

void RdmaClient::onTcpMessage(const TcpConnectionPtr &conn, Buffer *buffer,
                              Timestamp timestmap) {
  if (!conn->connected()) {
    SPDLOG_ERROR("not connected");
    return;
  }
  auto connector = std::any_cast<RdmaConnector>(conn->getMutableContext());
  connector->handshakeQp(conn, buffer);
}

void RdmaClient::onTcpWriteComplete(const TcpConnectionPtr &conn) {
}

} // namespace hdc::network::rdma