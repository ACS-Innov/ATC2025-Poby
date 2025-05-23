// Inspired by Muduo https://github.com/chenshuo/muduo
#pragma once
#include <functional>
#include <memory>
#include <network/Timestamp.h>

namespace hdc {

using std::placeholders::_1;
using std::placeholders::_2;
using std::placeholders::_3;

// should really belong to base/Types.h, but <memory> is not included there.

template <typename T> inline T *get_pointer(const std::shared_ptr<T> &ptr) {
  return ptr.get();
}

template <typename T> inline T *get_pointer(const std::unique_ptr<T> &ptr) {
  return ptr.get();
}

// Adapted from google-protobuf stubs/common.h
// see License in muduo/base/Types.h
template <typename To, typename From>
inline ::std::shared_ptr<To>
down_pointer_cast(const ::std::shared_ptr<From> &f) {
#ifndef NDEBUG
  assert(f == NULL || dynamic_cast<To *>(get_pointer(f)) != NULL);
#endif
  return ::std::static_pointer_cast<To>(f);
}

namespace network {
namespace tcp {
// All client visible callbacks go here.

class Buffer;
class TcpConnection;
typedef std::shared_ptr<TcpConnection> TcpConnectionPtr;
typedef std::function<void()> TimerCallback;

/// @brief: When connected or disconnected.
typedef std::function<void(const TcpConnectionPtr &)> ConnectionCallback;
typedef std::function<void(const TcpConnectionPtr &)> CloseCallback;
/// @brief: When message are sent completely to remote.
typedef std::function<void(const TcpConnectionPtr &)> WriteCompleteCallback;
typedef std::function<void(const TcpConnectionPtr &, size_t)>
    HighWaterMarkCallback;

// the data has been read to (buf, len)
typedef std::function<void(const TcpConnectionPtr &, Buffer *, Timestamp)>
    MessageCallback;

void defaultConnectionCallback(const TcpConnectionPtr &conn);
void defaultMessageCallback(const TcpConnectionPtr &conn, Buffer *buffer,
                            Timestamp receiveTime);
} // namespace tcp
} // namespace network
} // namespace hdc
