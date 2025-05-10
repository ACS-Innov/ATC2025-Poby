// Inspired by Muduo https://github.com/chenshuo/muduo
#pragma once
#include <set>
#include <vector>
#include <network/tcp/Callbacks.h>
#include <network/Channel.h>
#include <network/Timestamp.h>

namespace hdc {
namespace network {

class EventLoop;
class Timer;
class TimerId;
using tcp::TimerCallback;
///
/// A best efforts timer queue.
/// No guarantee that the callback will be on time.
///
class TimerQueue {
public:
  explicit TimerQueue(EventLoop *loop);
  TimerQueue(const TimerQueue &) = delete;
  TimerQueue &operator=(const TimerQueue &) = delete;
  ~TimerQueue();

  ///
  /// Schedules the callback to be run at given time,
  /// repeats if @c interval > 0.0.
  ///
  /// Must be thread safe. Usually be called from other threads.
  TimerId addTimer(TimerCallback cb, Timestamp when, double interval);

  void cancel(TimerId timerId);

private:
  // FIXME: use unique_ptr<Timer> instead of raw pointers.
  // This requires heterogeneous comparison lookup (N3465) from C++14
  // so that we can find an T* in a set<unique_ptr<T>>.
  typedef std::pair<Timestamp, Timer *> Entry;
  typedef std::set<Entry> TimerList;
  typedef std::pair<Timer *, int64_t> ActiveTimer;
  typedef std::set<ActiveTimer> ActiveTimerSet;

  void addTimerInLoop(Timer *timer);
  void cancelInLoop(TimerId timerId);
  // called when timerfd alarms
  void handleRead();
  // move out all expired timers
  std::vector<Entry> getExpired(Timestamp now);
  void reset(const std::vector<Entry> &expired, Timestamp now);

  bool insert(Timer *timer);

  EventLoop *loop_;
  const int timerfd_;
  Channel timerfdChannel_;
  // Timer list sorted by expiration
  TimerList timers_;

  // for cancel()
  ActiveTimerSet activeTimers_;
  bool callingExpiredTimers_; /* atomic */
  ActiveTimerSet cancelingTimers_;
};

} // namespace network
} // namespace hdc
