// Inspired by Muduo https://github.com/chenshuo/muduo
#pragma once
#include <functional>
#include <memory>
#include <network/Timestamp.h>
#include <string>
namespace hdc {
namespace network {

class EventLoop;

///
/// A selectable I/O channel.
///
/// This class doesn't own the file descriptor.
/// The file descriptor could be a socket,
/// an eventfd, a timerfd, or a signalfd
class Channel {
public:
  typedef std::function<void()> EventCallback;
  typedef std::function<void(Timestamp)> ReadEventCallback;

  Channel(const Channel &) = delete;
  Channel &operator=(const Channel &) = delete;

  Channel(EventLoop *loop, int fd, const std::string &name);
  ~Channel();

  void handleEvent(Timestamp receiveTime);
  void setReadCallback(ReadEventCallback cb) { readCallback_ = std::move(cb); }
  void setWriteCallback(EventCallback cb) { writeCallback_ = std::move(cb); }
  void setCloseCallback(EventCallback cb) { closeCallback_ = std::move(cb); }
  void setErrorCallback(EventCallback cb) { errorCallback_ = std::move(cb); }

  /// Tie this channel to the owner object managed by shared_ptr,
  /// prevent the owner object being destroyed in handleEvent.
  void tie(const std::shared_ptr<void> &);

  int fd() const { return fd_; }
  int events() const { return events_; }
  void set_revents(int revt) { revents_ = revt; } // used by pollers
  // int revents() const { return revents_; }
  bool isNoneEvent() const { return events_ == kNoneEvent; }

  void enableReading() {
    events_ |= kReadEvent;
    update();
  }
  void disableReading() {
    events_ &= ~kReadEvent;
    update();
  }
  void enableNoPriorityReading() {
    events_ |= kNoPriorityReadEvent;
    update();
  }
  void disableNoPriorityReading() {
    events_ &= ~kNoPriorityReadEvent;
    update();
  }
  void enableWriting() {
    events_ |= kWriteEvent;
    update();
  }
  void disableWriting() {
    events_ &= ~kWriteEvent;
    update();
  }
  void disableAll() {
    events_ = kNoneEvent;
    update();
  }
  bool isWriting() const { return events_ & kWriteEvent; }
  bool isReading() const { return events_ & kReadEvent; }

  // for Poller
  int index() { return index_; }
  void set_index(int idx) { index_ = idx; }

  // for debug
  std::string reventsToString() const;
  std::string eventsToString() const;

  void doNotLogHup() { logHup_ = false; }

  EventLoop *ownerLoop() { return loop_; }

  /// @brief: Remove this channel from the EventLoop. It can only be called in
  /// channel's own IO thread.
  void remove();

  const std::string &get_name() const { return name_; }

private:
  static std::string eventsToString(int fd, int ev);

  /// @brief: Update the events of Channel monitored by Poller. This function
  /// must be called in the EventLoop thread.
  /// @detail: If it is a new channel, the Poller will also add the Channel to
  /// the Channel Set it holds. The Poller won't remove the Channel from the
  /// Channel Set in any cases.
  void update();
  void handleEventWithGuard(Timestamp receiveTime);

  static const int kNoneEvent;
  static const int kReadEvent;
  static const int kWriteEvent;
  static const int kNoPriorityReadEvent;
  EventLoop *loop_;
  const int fd_;
  int events_;
  int revents_; // it's the received event types of epoll or poll

  int index_; // It's used by Poller to record Channel state. Initial state is
              // kNew(-1).
  bool logHup_;
  std::string name_;

  std::weak_ptr<void> tie_;
  bool tied_;
  bool eventHandling_;
  bool addedToLoop_;
  ReadEventCallback readCallback_;
  EventCallback writeCallback_;
  EventCallback closeCallback_;
  EventCallback errorCallback_;
};

} // namespace network
} // namespace hdc
