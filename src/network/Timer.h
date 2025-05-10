// Inspired by Muduo https://github.com/chenshuo/muduo
#pragma once

#include <atomic>
#include <network/tcp/Callbacks.h>
#include <network/Timestamp.h>
namespace hdc {
namespace network {
using tcp::TimerCallback;
///
/// Internal class for timer event.
///
class Timer {
public:
  Timer(TimerCallback cb, Timestamp when, double interval)
      : callback_(std::move(cb)), expiration_(when), interval_(interval),
        repeat_(interval > 0.0), sequence_(s_numCreated_.fetch_add(1)) {}
  Timer(const Timer &) = delete;
  Timer &operator=(const Timer &) = delete;
  void run() const { callback_(); }

  Timestamp expiration() const { return expiration_; }
  bool repeat() const { return repeat_; }
  int64_t sequence() const { return sequence_; }

  void restart(Timestamp now);

  static int64_t numCreated() { return s_numCreated_.load(); }

private:
  const TimerCallback callback_;
  Timestamp expiration_;
  const double interval_;
  const bool repeat_;
  const int64_t sequence_;

  static std::atomic_int64_t s_numCreated_;
};

} // namespace network
} // namespace hdc
