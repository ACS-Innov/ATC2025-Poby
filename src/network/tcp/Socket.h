// Inspired by Muduo https://github.com/chenshuo/muduo
#pragma once

// struct tcp_info is in <netinet/tcp.h>
struct tcp_info;

namespace hdc {
///
/// TCP networking.
///
namespace network {
class InetAddress;
namespace tcp {

///
/// Wrapper of socket file descriptor.
///
/// It closes the sockfd when desctructs.
/// It's thread safe, all operations are delagated to OS.
class Socket {
public:
  Socket(const Socket &) = delete;

  Socket &operator=(const Socket &) = delete;

  explicit Socket(int sockfd) : sockfd_(sockfd) {}

  // Socket(Socket&&) // move constructor in C++11
  ~Socket();

  int fd() const { return sockfd_; }
  // return true if success.
  bool getTcpInfo(struct tcp_info *) const;
  bool getTcpInfoString(char *buf, int len) const;

  /// abort if address in use
  void bindAddress(const InetAddress &localaddr);
  /// abort if address in use
  void listen();

  /// On success, returns a non-negative integer that is
  /// a descriptor for the accepted socket, which has been
  /// set to non-blocking and close-on-exec. *peeraddr is assigned.
  /// On error, -1 is returned, and *peeraddr is untouched.
  int accept(InetAddress *peeraddr);

  void shutdownWrite();

  ///
  /// Enable/disable TCP_NODELAY (disable/enable Nagle's algorithm).
  ///
  void setTcpNoDelay(bool on);

  ///
  /// Enable/disable SO_REUSEADDR
  ///
  void setReuseAddr(bool on);

  ///
  /// Enable/disable SO_REUSEPORT
  ///
  void setReusePort(bool on);

  ///
  /// Enable/disable SO_KEEPALIVE
  ///
  void setKeepAlive(bool on);

private:
  const int sockfd_;
};
} // namespace tcp
} // namespace network
} // namespace hdc
