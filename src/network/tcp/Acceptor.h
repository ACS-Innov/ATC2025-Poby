// Inspired by Muduo https://github.com/chenshuo/muduo
#pragma once


#include <functional>

#include <network/Channel.h>
#include <network/tcp/Socket.h>

namespace hdc {
namespace network {
class InetAddress;
class EventLoop;

namespace tcp {

///
/// Acceptor of incoming TCP connections.
///
class Acceptor {
public:
  typedef std::function<void(int sockfd, const InetAddress &)>
      NewConnectionCallback;

  Acceptor(const Acceptor &) = delete;
  Acceptor &operator=(const Acceptor &) = delete;

  Acceptor(EventLoop *loop, const InetAddress &listenAddr, bool reuseport);
  ~Acceptor();

  void setNewConnectionCallback(const NewConnectionCallback &cb) {
    newConnectionCallback_ = cb;
  }

  bool listenning() const { return listenning_; }
  void listen();

private:
  void handleRead();

  EventLoop *loop_;
  Socket acceptSocket_;
  Channel acceptChannel_;
  NewConnectionCallback newConnectionCallback_;
  bool listenning_;
  int idleFd_;
};
} // namespace tcp
} // namespace network
} // namespace hdc
