// Inspired by Muduo https://github.com/chenshuo/muduo
#include <errno.h>
#include <fmt/format.h>
#include <network/Channel.h>
#include <network/EventLoop.h>
#include <network/tcp/SocketsOps.h>
#include <network/tcp/TcpConnector.h>
#include <spdlog/spdlog.h>
using namespace hdc::network;
using namespace hdc::network::tcp;
const int TcpConnector::kMaxRetryDelayMs;

auto fmt::formatter<TcpConnector::States>::format(TcpConnector::States s,
                                               format_context &ctx) const {
  string_view name = "unknown";
  switch (s) {
  case TcpConnector::States::kConnected:
    name = "Connected";
    break;
  case TcpConnector::States::kDisconnected:
    name = "Disconnected";
    break;
  case TcpConnector::States::kConnecting:
    name = "Connecting";
    break;
  }
  return formatter<string_view>::format(name, ctx);
}

TcpConnector::TcpConnector(EventLoop *loop, const InetAddress &serverAddr)
    : loop_(loop), serverAddr_(serverAddr), connect_(false),
      state_(States::kDisconnected), retryDelayMs_(kInitRetryDelayMs) {}

TcpConnector::~TcpConnector() { assert(!channel_); }

void TcpConnector::start() {
  connect_ = true;
  loop_->runInLoop(std::bind(&TcpConnector::startInLoop, this)); // FIXME: unsafe
}

void TcpConnector::startInLoop() {
  loop_->assertInLoopThread();
  assert(state_ == States::kDisconnected);
  if (connect_) {
    connect();
  } else {
    SPDLOG_DEBUG("do not connect");
  }
}

void TcpConnector::stop() {
  connect_ = false;
  loop_->queueInLoop(std::bind(&TcpConnector::stopInLoop, this)); // FIXME: unsafe
  // FIXME: cancel timer
}

void TcpConnector::stopInLoop() {
  loop_->assertInLoopThread();
  if (state_ == States::kConnecting) {
    setState(States::kDisconnected);
    int sockfd = removeAndResetChannel();
    retry(sockfd);
  }
}

void TcpConnector::connect() {
  int sockfd = sockets::createNonblockingOrDie(serverAddr_.family());
  int ret = sockets::connect(sockfd, serverAddr_.getSockAddr());
  int savedErrno = (ret == 0) ? 0 : errno;
  switch (savedErrno) {
  case 0:
  case EINPROGRESS:
  case EINTR:
  case EISCONN:
    connecting(sockfd);
    break;

  case EAGAIN:
  case EADDRINUSE:
  case EADDRNOTAVAIL:
  case ECONNREFUSED:
  case ENETUNREACH:
    retry(sockfd);
    break;

  case EACCES:
  case EPERM:
  case EAFNOSUPPORT:
  case EALREADY:
  case EBADF:
  case EFAULT:
  case ENOTSOCK:
    SPDLOG_ERROR("connect error in TcpConnector::startInLoop {}", savedErrno);
    sockets::close(sockfd);
    break;

  default:
    SPDLOG_ERROR("Unexpected error in TcpConnector::startInLoop {}", savedErrno);
    sockets::close(sockfd);
    break;
  }
}

void TcpConnector::restart() {
  loop_->assertInLoopThread();
  setState(States::kDisconnected);
  retryDelayMs_ = kInitRetryDelayMs;
  connect_ = true;
  startInLoop();
}

void TcpConnector::connecting(int sockfd) {
  setState(States::kConnecting);
  assert(!channel_);
  channel_.reset(new Channel(loop_, sockfd, "tcp"));
  channel_->setWriteCallback(
      std::bind(&TcpConnector::handleWrite, this)); // FIXME: unsafe
  channel_->setErrorCallback(
      std::bind(&TcpConnector::handleError, this)); // FIXME: unsafe

  // channel_->tie(shared_from_this()); is not working,
  // as channel_ is not managed by shared_ptr
  channel_->enableWriting();
}

int TcpConnector::removeAndResetChannel() {
  channel_->disableAll();
  channel_->remove();
  int sockfd = channel_->fd();
  // Can't reset channel_ here, because we are inside Channel::handleEvent
  loop_->queueInLoop(
      std::bind(&TcpConnector::resetChannel, this)); // FIXME: unsafe
  return sockfd;
}

void TcpConnector::resetChannel() { channel_.reset(); }

void TcpConnector::handleWrite() {
  SPDLOG_TRACE("TcpConnector::handleWrite {}", state_);

  if (state_ == States::kConnecting) {
    int sockfd = removeAndResetChannel();
    int err = sockets::getSocketError(sockfd);
    if (err) {
      SPDLOG_WARN("TcpConnector::handleWrite - SO_ERROR = {}", err);
      retry(sockfd);
    } else if (sockets::isSelfConnect(sockfd)) {
      SPDLOG_WARN("TcpConnector::handleWrite - Self connect");
      retry(sockfd);
    } else {
      setState(States::kConnected);
      if (connect_) {
        newConnectionCallback_(sockfd);
      } else {
        sockets::close(sockfd);
      }
    }
  } else {
    // what happened?
    assert(state_ == States::kDisconnected);
  }
}

void TcpConnector::handleError() {
  SPDLOG_ERROR("TcpConnector::handleError state={}", state_);
  if (state_ == States::kConnecting) {

    int sockfd = removeAndResetChannel();
    int err = sockets::getSocketError(sockfd);
    SPDLOG_TRACE("SO_ERROR = {}", err);
    retry(sockfd);
  }
}

void TcpConnector::retry(int sockfd) {
  sockets::close(sockfd);
  setState(States::kDisconnected);
  if (connect_) {
    SPDLOG_INFO("TcpConnector::retry - Retry connecting to {} in {} ms.",
                serverAddr_.toIpPort(), retryDelayMs_);
    loop_->runAfter(retryDelayMs_ / 1000.0,
                    std::bind(&TcpConnector::startInLoop, shared_from_this()));
    retryDelayMs_ = std::min(retryDelayMs_ * 2, kMaxRetryDelayMs);
  } else {
    SPDLOG_DEBUG("do not connect");
  }
}
