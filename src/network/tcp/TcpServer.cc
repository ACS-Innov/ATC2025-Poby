// Inspired by Muduo https://github.com/chenshuo/muduo
#include <network/EventLoop.h>
#include <network/EventLoopThreadPool.h>
#include <network/tcp/Acceptor.h>
#include <network/tcp/SocketsOps.h>
#include <network/tcp/TcpServer.h>
#include <spdlog/spdlog.h>
#include <stdio.h> // snprintf

using namespace hdc::network;
using namespace hdc::network::tcp;
TcpServer::TcpServer(EventLoop *loop, const InetAddress &listenAddr,
                     const std::string &nameArg, Option option)
    : loop_(loop), ipPort_(listenAddr.toIpPort()), name_(nameArg),
      acceptor_(new Acceptor(loop, listenAddr, option == kReusePort)),
      threadPool_(new EventLoopThreadPool(loop, name_)),
      connectionCallback_(defaultConnectionCallback),
      messageCallback_(defaultMessageCallback), nextConnId_(1) {
  acceptor_->setNewConnectionCallback(
      std::bind(&TcpServer::newConnection, this, _1, _2));
}

TcpServer::~TcpServer() {
  loop_->assertInLoopThread();

  SPDLOG_TRACE("TcpServer::~TcpServer [{}] destructing", name_);

  for (auto &item : connections_) {
    TcpConnectionPtr conn(item.second);
    item.second.reset();
    conn->getLoop()->runInLoop(
        std::bind(&TcpConnection::connectDestroyed, conn));
  }
}

void TcpServer::setThreadNum(int numThreads) {
  assert(0 <= numThreads);
  threadPool_->setThreadNum(numThreads);
}

void TcpServer::start() {
  auto not_start = 0;
  auto start = 1;
  if (started_.compare_exchange_strong(not_start, 1)) {
    threadPool_->start(threadInitCallback_);

    assert(!acceptor_->listenning());
    loop_->runInLoop(std::bind(&Acceptor::listen, get_pointer(acceptor_)));
    // loop_->runInLoop([this]() { acceptor_->listen(); });
  }
}

/// 这个函数被注册给Accpetor了。当出现新的连接的时候被调用。
void TcpServer::newConnection(int sockfd, const InetAddress &peerAddr) {
  loop_->assertInLoopThread();
  EventLoop *ioLoop = threadPool_->getNextLoop();
  char buf[64];
  snprintf(buf, sizeof buf, "-%s#%d", ipPort_.c_str(), nextConnId_);
  ++nextConnId_;
  std::string connName = name_ + buf;

  // SPDLOG_INFO("TcpServer::newConnection [{}] - new connection [{}] from {}",
  //             name_, connName, peerAddr.toIpPort());
  InetAddress localAddr(sockets::getLocalAddr(sockfd));
  // FIXME poll with zero timeout to double confirm the new connection
  // FIXME use make_shared if necessary
  TcpConnectionPtr conn(
      new TcpConnection(ioLoop, connName, sockfd, localAddr, peerAddr));
  connections_[connName] = conn;
  conn->setConnectionCallback(connectionCallback_);
  conn->setMessageCallback(messageCallback_);
  conn->setWriteCompleteCallback(writeCompleteCallback_);
  // 这里设置了一个Conn断开的时候，把该连接从eventloop里面删除
  conn->setCloseCallback(
      std::bind(&TcpServer::removeConnection, this, _1)); // FIXME: unsafe
  ioLoop->runInLoop(std::bind(&TcpConnection::connectEstablished, conn));
}

void TcpServer::removeConnection(const TcpConnectionPtr &conn) {
  // FIXME: unsafe
  loop_->runInLoop(std::bind(&TcpServer::removeConnectionInLoop, this, conn));
}

void TcpServer::removeConnectionInLoop(const TcpConnectionPtr &conn) {
  loop_->assertInLoopThread();
  // SPDLOG_INFO("TcpServer::removeConnectionInLoop [{}] - connection", name_,
  //             conn->name());
  size_t n = connections_.erase(conn->name());
  (void)n;
  assert(n == 1);
  EventLoop *ioLoop = conn->getLoop();
  ioLoop->queueInLoop(std::bind(&TcpConnection::connectDestroyed, conn));
}
