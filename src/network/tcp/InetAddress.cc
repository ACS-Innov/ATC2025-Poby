// Inspired by Muduo https://github.com/chenshuo/muduo
#include "network/InetAddress.h"
#include "network/tcp/SocketsOps.h"
#include "network/utils.h"
#include <cassert>
#include <cstddef>
#include <netdb.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>

// INADDR_ANY use (type)value casting.
#pragma GCC diagnostic ignored "-Wold-style-cast"
static const in_addr_t kInaddrAny = INADDR_ANY;
static const in_addr_t kInaddrLoopback = INADDR_LOOPBACK;
#pragma GCC diagnostic error "-Wold-style-cast"

//     /* Structure describing an Internet socket address.  */
//     struct sockaddr_in {
//         sa_family_t    sin_family; /* address family: AF_INET */
//         uint16_t       sin_port;   /* port in network byte order */
//         struct in_addr sin_addr;   /* internet address */
//     };

//     /* Internet address. */
//     typedef uint32_t in_addr_t;
//     struct in_addr {
//         in_addr_t       s_addr;     /* address in network byte order */
//     };

//     struct sockaddr_in6 {
//         sa_family_t     sin6_family;   /* address family: AF_INET6 */
//         uint16_t        sin6_port;     /* port in network byte order */
//         uint32_t        sin6_flowinfo; /* IPv6 flow information */
//         struct in6_addr sin6_addr;     /* IPv6 address */
//         uint32_t        sin6_scope_id; /* IPv6 scope-id */
//     };

using namespace hdc::network;

static_assert(sizeof(InetAddress) == sizeof(struct sockaddr_in6),
              "InetAddress is same size as sockaddr_in6");
static_assert(offsetof(sockaddr_in, sin_family) == 0, "sin_family offset 0");
static_assert(offsetof(sockaddr_in6, sin6_family) == 0, "sin6_family offset 0");
static_assert(offsetof(sockaddr_in, sin_port) == 2, "sin_port offset 2");
static_assert(offsetof(sockaddr_in6, sin6_port) == 2, "sin6_port offset 2");

InetAddress::InetAddress(uint16_t port, bool loopbackOnly, bool ipv6) {
  // static_assert(offsetof(InetAddress, addr6_) == 0, "addr6_ offset 0");
  // static_assert(offsetof(InetAddress, addr_) == 0, "addr_ offset 0");
  if (ipv6) {
    memset(&addr6_, 0, sizeof(addr6_));
    addr6_.sin6_family = AF_INET6;
    in6_addr ip = loopbackOnly ? in6addr_loopback : in6addr_any;
    addr6_.sin6_addr = ip;
    addr6_.sin6_port = sockets::hostToNetwork16(port);
  } else {
    memZero(&addr_, sizeof addr_);
    addr_.sin_family = AF_INET;
    in_addr_t ip = loopbackOnly ? kInaddrLoopback : kInaddrAny;
    addr_.sin_addr.s_addr = sockets::hostToNetwork32(ip);
    addr_.sin_port = sockets::hostToNetwork16(port);
  }
}

InetAddress::InetAddress(std::string_view ip, uint16_t port, bool ipv6) {
  if (ipv6) {
    memZero(&addr6_, sizeof(addr6_));
    sockets::fromIpPort(ip.data(), port, &addr6_);
  } else {
    memZero(&addr_, sizeof addr_);

    sockets::fromIpPort(ip.data(), port, &addr_);
  }
}

std::string InetAddress::toIpPort() const {
  char buf[64] = "";
  sockets::toIpPort(buf, sizeof buf, getSockAddr());
  return buf;
}

std::string InetAddress::toIp() const {
  char buf[64] = "";
  sockets::toIp(buf, sizeof buf, getSockAddr());
  return buf;
}

uint32_t InetAddress::ipNetEndian() const {
  assert(family() == AF_INET);
  return addr_.sin_addr.s_addr;
}

uint16_t InetAddress::toPort() const {
  return sockets::networkToHost16(portNetEndian());
}

void InetAddress::setScopeId(uint32_t scope_id) {
  if (family() == AF_INET6) {
    addr6_.sin6_scope_id = scope_id;
  }
}
namespace hdc {
namespace network {
bool operator<(const InetAddress &lhs, const InetAddress &rhs) {
  return memcmp(&lhs.addr_, &rhs.addr_, sizeof(sockaddr_in)) < 0;
}
} // namespace network
} // namespace hdc
