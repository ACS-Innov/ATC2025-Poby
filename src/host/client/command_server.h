#pragma once
#include "network/tcp/Callbacks.h"
#include <folly/concurrency/UnboundedQueue.h>
#include <map>
#include <memory>
#include <network/tcp/TcpServer.h>
#include <optional>
#include <string>
#include "host/client/metadata.h"
#include "host/client/offload_client_epoll.h"

namespace hdc {
namespace host {
namespace client {
using namespace hdc::network;
using namespace hdc::network::tcp;

using OffloadTaskQueue = folly::UMPSCQueue<OffloadElement, false>;
using OffloadTaskQueuePtr = std::shared_ptr<OffloadTaskQueue>;
class CommandServer {
public:
  CommandServer(const CommandServer &) = delete;
  CommandServer &operator=(const CommandServer &) = delete;

  CommandServer(EventLoop *event_loop, const InetAddress &listenAddr,
                int numThreads, OffloadClientEpoll* offload_client);

  void start() { tcp_server_.start(); }

private:
  TcpServer tcp_server_;
  OffloadClientEpoll* offload_client_;
  void OnConnection(const TcpConnectionPtr &conn);

  void OnMessage(const TcpConnectionPtr &conn, Buffer *buffer,
                 Timestamp timestamp);

  void OnWriteComplete(const TcpConnectionPtr &conn);

  void pullImage(const std::string &imageNameTag, const TcpConnectionPtr &conn);

};
} // namespace client
} // namespace host
} // namespace hdc