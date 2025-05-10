// Inspired by Muduo https://github.com/chenshuo/muduo
#pragma once
#include <map>
#include <vector>

#include <network/EventLoop.h>
#include <network/Timestamp.h>
#include <sys/epoll.h>
namespace hdc {
namespace network {

class Channel;

class Poller {
public:
  typedef std::vector<Channel *> ChannelList;

  Poller(const Poller &) = delete;
  Poller &operator=(const Poller &) = delete;
  Poller(EventLoop *loop);
  ~Poller();

  /// Polls the I/O events.
  /// Must be called in the loop thread.
  Timestamp poll(int timeoutMs, ChannelList *activeChannels);

  /// @brief: Changes the interested I/O events of Channel. This function must
  /// be called in the loop thread.
  /// @detail: If it is a new channel, the Poller will also add the Channel to
  /// the Channel Set it holds. The Poller won't remove the Channel from the
  /// Channel Set in any cases.

  void updateChannel(Channel *channel);

  ///@brief: Remove the Channel from the Channel Set, because it will destruct.
  /// This function must be called in the EventLoop thread.
  void removeChannel(Channel *channel);

  bool hasChannel(Channel *channel) const;

  void assertInLoopThread() const { ownerLoop_->assertInLoopThread(); }

private:
  static const int kInitEventListSize = 16;

  static const char *operationToString(int op);

  void fillActiveChannels(int numEvents, ChannelList *activeChannels) const;
  void update(int operation, Channel *channel);

  typedef std::vector<struct epoll_event> EventList;

  int epollfd_;

  EventList events_; // It is used to get occurred events from epoll.

  typedef std::map<int, Channel *> ChannelMap;
  ChannelMap channels_;
  EventLoop *ownerLoop_;
};

} // namespace network
} // namespace hdc