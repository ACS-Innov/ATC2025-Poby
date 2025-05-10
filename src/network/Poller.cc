// Inspired by Muduo https://github.com/chenshuo/muduo
#include <assert.h>
#include <errno.h>
#include <network/Channel.h>
#include <network/Poller.h>
#include <network/utils.h>
#include <poll.h>
#include <spdlog/spdlog.h>
#include <sys/epoll.h>
#include <unistd.h>
using namespace hdc;
using namespace hdc::network;

// On Linux, the constants of poll(2) and epoll(4)
// are expected to be the same.
static_assert(EPOLLIN == POLLIN, "epoll uses same flag values as poll");
static_assert(EPOLLPRI == POLLPRI, "epoll uses same flag values as poll");
static_assert(EPOLLOUT == POLLOUT, "epoll uses same flag values as poll");
static_assert(EPOLLRDHUP == POLLRDHUP, "epoll uses same flag values as poll");
static_assert(EPOLLERR == POLLERR, "epoll uses same flag values as poll");
static_assert(EPOLLHUP == POLLHUP, "epoll uses same flag values as poll");

namespace {
const int kNew = -1;
const int kAdded = 1;
const int kDeleted = 2;
} // namespace

Poller::Poller(EventLoop *loop)
    : ownerLoop_(loop), epollfd_(::epoll_create1(EPOLL_CLOEXEC)),
      events_(kInitEventListSize) {
  if (epollfd_ < 0) {
    SPDLOG_ERROR("Poller::Poller");
  }
}

bool Poller::hasChannel(Channel *channel) const {
  assertInLoopThread();
  ChannelMap::const_iterator it = channels_.find(channel->fd());
  return it != channels_.end() && it->second == channel;
}

Poller::~Poller() { ::close(epollfd_); }

Timestamp Poller::poll(int timeoutMs, ChannelList *activeChannels) {
  SPDLOG_TRACE("fd total count {}", channels_.size());
  int numEvents = ::epoll_wait(epollfd_, &*events_.begin(),
                               static_cast<int>(events_.size()), timeoutMs);
  int savedErrno = errno;
  Timestamp now(Timestamp::now());
  if (numEvents > 0) {
    SPDLOG_TRACE("{} events happened", numEvents);
    fillActiveChannels(numEvents, activeChannels);
    if (static_cast<size_t>(numEvents) == events_.size()) {
      events_.resize(events_.size() * 2);
    }
  } else if (numEvents == 0) {
    SPDLOG_TRACE("nothing happened");
  } else {
    // error happens, log uncommon ones
    if (savedErrno != EINTR) {
      errno = savedErrno;
      SPDLOG_ERROR("Poller::poll()");
    }
  }
  return now;
}

void Poller::fillActiveChannels(int numEvents,
                                ChannelList *activeChannels) const {
  assert(static_cast<size_t>(numEvents) <= events_.size());
  for (int i = 0; i < numEvents; ++i) {
    Channel *channel = static_cast<Channel *>(events_[i].data.ptr);
#ifndef NDEBUG
    int fd = channel->fd();
    ChannelMap::const_iterator it = channels_.find(fd);
    assert(it != channels_.end());
    assert(it->second == channel);
#endif
    channel->set_revents(events_[i].events);
    activeChannels->push_back(channel);
  }
}

void Poller::updateChannel(Channel *channel) {
  Poller::assertInLoopThread();
  const int index = channel->index();
  // SPDLOG_TRACE("Poller::update Channel: fd = {} events = {} index = ",
  // channel->fd(), channel->events(),
  //              index);
  if (index == kNew || index == kDeleted) {
    // A new one, add with EPOLL_CTL_ADD.
    int fd = channel->fd();
    if (index == kNew) {
      // Assert channel is not in Channel Set.
      assert(channels_.find(fd) == channels_.end());
      channels_[fd] = channel;
    } else // index == kDeleted
    {
      // Assert channel is already in Channel Set.
      assert(channels_.find(fd) != channels_.end());
      assert(channels_[fd] == channel);
    }

    channel->set_index(kAdded);
    update(EPOLL_CTL_ADD, channel);
  } else {
    // update existing one with EPOLL_CTL_MOD/DEL
    int fd = channel->fd();
    (void)fd;
    assert(channels_.find(fd) != channels_.end());
    assert(channels_[fd] == channel);
    assert(index == kAdded);
    if (channel->isNoneEvent()) {
      update(EPOLL_CTL_DEL, channel);
      channel->set_index(kDeleted);
    } else {
      update(EPOLL_CTL_MOD, channel);
    }
  }
}

void Poller::removeChannel(Channel *channel) {
  Poller::assertInLoopThread();
  int fd = channel->fd();
  assert(channels_.find(fd) != channels_.end());
  assert(channels_[fd] == channel);
  assert(channel->isNoneEvent());
  int index = channel->index();
  assert(index == kAdded || index == kDeleted);
  size_t n = channels_.erase(fd);
  (void)n;
  assert(n == 1);

  if (index == kAdded) {
    update(EPOLL_CTL_DEL, channel);
  }
  channel->set_index(kNew);
}

void Poller::update(int operation, Channel *channel) {
  struct epoll_event event;
  memZero(&event, sizeof event);
  event.events = channel->events();
  event.data.ptr = channel;
  int fd = channel->fd();
  SPDLOG_TRACE("epoll_ctl op = {} fd = {} name = {} event = {}",
               operationToString(operation), fd, channel->get_name(),
               channel->eventsToString());
  if (::epoll_ctl(epollfd_, operation, fd, &event) < 0) {
    if (operation == EPOLL_CTL_DEL) {
      SPDLOG_ERROR("epoll_ctl op = {} fd = {}", operationToString(operation),
                   fd);
    } else {
      SPDLOG_ERROR("epoll_ctl op ={} fd = {}", operationToString(operation),
                   fd);
    }
  }
}

const char *Poller::operationToString(int op) {
  switch (op) {
  case EPOLL_CTL_ADD:
    return "ADD";
  case EPOLL_CTL_DEL:
    return "DEL";
  case EPOLL_CTL_MOD:
    return "MOD";
  default:
    assert(false && "ERROR op");
    return "Unknown Operation";
  }
}
