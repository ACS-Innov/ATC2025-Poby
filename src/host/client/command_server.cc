#include "container.pb.h"
#include "host/client/untar_engine.h"
#include "network/tcp/Callbacks.h"
#include <any>
#include <host/client/command_server.h>
#include <network/EventLoop.h>
#include <optional>
#include <spdlog/common.h>
#include <spdlog/spdlog.h>
#include <utils/MsgFrame.h>

using hdc::host::client::CommandServer;
using hdc::host::client::OffloadTaskQueue;
using hdc::network::EventLoop;
using hdc::network::InetAddress;

namespace hdc::host::client {
CommandServer::CommandServer(EventLoop *event_loop,
                             const InetAddress &listenAddr, int numThreads,
                             OffloadClientEpoll* offload_client)
    : tcp_server_(event_loop, listenAddr, "ClientCommandServer"),
      offload_client_(offload_client) {
  tcp_server_.setThreadNum(numThreads);

  tcp_server_.setConnectionCallback(
      [this](const TcpConnectionPtr &conn) { this->OnConnection(conn); });

  tcp_server_.setMessageCallback([this](const TcpConnectionPtr &conn,
                                        Buffer *buffer, Timestamp timestamp) {
    this->OnMessage(conn, buffer, timestamp);
  });
}

void CommandServer::OnConnection(const TcpConnectionPtr &conn) {
  SPDLOG_DEBUG("{} new connection: {} -> {} is {}", tcp_server_.name(),
               conn->peerAddress().toIpPort(), conn->localAddress().toIpPort(),
               (conn->connected() ? "UP" : "DOWN"));
}

void CommandServer::OnMessage(const TcpConnectionPtr &conn, Buffer *buffer,
                              Timestamp timestamp) {

  container::CreateContainerRequest request{};
  if (receiveTcpPbMsg(buffer, request) == false) {
    return;
  }
  SPDLOG_INFO("Recv CreateContainerRequest. image: {}",
              request.image_name_tag());
  pullImage(request.image_name_tag(), conn);
}

void CommandServer::pullImage(const std::string &imageNameTag,
                              const TcpConnectionPtr &conn) {
  offload_client_->offload(OffloadElement{imageNameTag, conn});
}

} // namespace hdc::host::client

