#include "network/rdma/Callbacks.h"
#include "network/rdma/RdmaConfig.h"
#include "network/tcp/TcpServer.h"
#include <any>
#include <cassert>
#include <network/rdma/RdmaConnection.h>
#include <network/rdma/RdmaServer.h>
namespace hdc::network::rdma {

RdmaServer::RdmaServer(EventLoop *loop, const InetAddress &listenAddr,
                       const std::string &nameArg, RdmaConfig rdmaConfig)
    : tcpServer_(loop, listenAddr, nameArg + "_tcpServer"), name_(nameArg),
      loop_(loop), rdmaConfig_(std::move(rdmaConfig)) {

  tcpServer_.setConnectionCallback(
      [this](const TcpConnectionPtr &conn) { this->onTcpConnection(conn); });
  tcpServer_.setMessageCallback([this](const TcpConnectionPtr &conn,
                                       Buffer *buffer, Timestamp timestmap) {
    this->onTcpMessage(conn, buffer, timestmap);
  });
  tcpServer_.setWriteCompleteCallback(
      [this](const TcpConnectionPtr &conn) { this->onTcpWriteComplete(conn); });
}

void RdmaServer::newRdmaConnection(RdmaConnectionPtr conn) {

  conn->setConnectedCallback(connectedCallback_);
  conn->setDisconnectedCallback(disconnectedCallback_);
  conn->setRecvSuccessCallback(recvSuccessCallback_);
  conn->setRecvFailCallback(recvFailCallback_);
  conn->setSendCompleteSuccessCallback(sendCompleteSuccessCallback_);
  conn->setSendCompleteFailCallback(sendCompleteFailCallback_);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    assert(connections_.find(conn->name()) == connections_.end());
    connections_.emplace(conn->name(), conn);
  }
  conn->connectEstablished();
}

void RdmaServer::start() { tcpServer_.start(); }

void RdmaServer::onTcpConnection(const TcpConnectionPtr &conn) {
  if (conn->connected()) {
    assert(conn->getContext().has_value() == false);
    conn->setContext(std::make_any<RdmaConnector>());
    auto connector = std::any_cast<RdmaConnector>(conn->getMutableContext());
    connector->setNewRdmaConnectionCallback(
        [this](RdmaConnectionPtr conn) { this->newRdmaConnection(conn); });
    connector->initialRdmaResource(rdmaConfig_, conn);
  } else {
    if (!conn->getContext().has_value()) {
      SPDLOG_ERROR("RdmaConnector: Tcp disconnected but RDMA is not connect");
    } else {
      // SPDLOG_DEBUG("RdmaConnector: Tcp disconnect and RDMA is connected");
    }
  }
}

void RdmaServer::onTcpMessage(const TcpConnectionPtr &conn, Buffer *buffer,
                              Timestamp timestmap) {
  if (!conn->connected()) {
    SPDLOG_ERROR("not connected");
    return;
  }
  auto connector = std::any_cast<RdmaConnector>(conn->getMutableContext());
  connector->handshakeQp(conn, buffer);
}

void RdmaServer::onTcpWriteComplete(const TcpConnectionPtr &conn) {
  // SPDLOG_DEBUG("Tcp Write complete");
}
} // namespace hdc::network::rdma