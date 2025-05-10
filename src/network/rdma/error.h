#pragma once
namespace hdc {
namespace network {
namespace rdma {
enum class RDMAError {
  kSuccess,
  kFindDeviceError,
  kOpenDeviceError,
  kDeviceInfoError,
  kCompChannelError,
  kPdError,
  kQpError,
  kCqError,
  kMrError,
  kTcpListenError,
  kTcpConnectionError,
  kUnexpectedError,
  kAgain,
};
}
} // namespace network
} // namespace hdc