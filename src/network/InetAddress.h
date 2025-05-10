// Inspired by Muduo https://github.com/chenshuo/muduo
#pragma once
#include <netinet/in.h>
#include <network/tcp/SocketsOps.h>
#include <string>
#include <string_view>
namespace hdc {
namespace network {

///
/// Wrapper of sockaddr_in.
///
/// This is an POD interface class.
class InetAddress {
public:
  /// Constructs an endpoint with given port number.
  /// Mostly used in TcpServer listening.
  explicit InetAddress(uint16_t port = 0, bool loopbackOnly = false,
                       bool ipv6 = false);

  /// Constructs an endpoint with given ip and port.
  /// @c ip should be "1.2.3.4"
  InetAddress(std::string_view ip, uint16_t port, bool ipv6 = false);

  /// Constructs an endpoint with given struct @c sockaddr_in
  /// Mostly used when accepting new connections
  explicit InetAddress(const struct sockaddr_in &addr) : addr_(addr) {}

  explicit InetAddress(const struct sockaddr_in6 &addr) : addr6_(addr) {}

  sa_family_t family() const { return addr_.sin_family; }

  std::string toIp() const;

  std::string toIpPort() const;

  uint16_t toPort() const;

  // default copy/assignment are Okay

  const struct sockaddr *getSockAddr() const {
    return hdc::network::sockets::sockaddr_cast(&addr6_);
  }

  void setSockAddrInet6(const struct sockaddr_in6 &addr6) { addr6_ = addr6; }

  uint32_t ipNetEndian() const;

  uint16_t portNetEndian() const { return addr_.sin_port; }

  // set IPv6 ScopeID
  void setScopeId(uint32_t scope_id);
  
  friend bool operator<(const InetAddress& lhs, const InetAddress& rhs);
private:
  union {
    struct sockaddr_in addr_;
    struct sockaddr_in6 addr6_;
  };
};

} // namespace network
} // namespace hdc
