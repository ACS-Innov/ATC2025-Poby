// Inspired by Muduo https://github.com/chenshuo/muduo
#include <cassert>
#include <network/Channel.h>
#include <network/EventLoop.h>
#include <poll.h>
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#include <spdlog/spdlog.h>
#include <sstream>
using namespace hdc;
using namespace hdc::network;

const int Channel::kNoneEvent = 0;
const int Channel::kReadEvent = POLLIN | POLLPRI;
const int Channel::kNoPriorityReadEvent = POLLIN;
const int Channel::kWriteEvent = POLLOUT;

Channel::Channel(EventLoop *loop, int fd__, const std::string &name)
    : loop_(loop), fd_(fd__), name_(name), events_(0), revents_(0), index_(-1),
      logHup_(true), tied_(false), eventHandling_(false), addedToLoop_(false) {
  // SPDLOG_DEBUG("Construct Channel: name: {}, fd: {}", name_, fd_);
}

Channel::~Channel() {
  SPDLOG_TRACE("~Channel: name: {}, fd: {}", name_, fd_);
  assert(!eventHandling_);
  assert(!addedToLoop_);
  if (loop_->isInLoopThread()) {
    assert(!loop_->hasChannel(this));
  }
}

void Channel::tie(const std::shared_ptr<void> &obj) {
  tie_ = obj;
  tied_ = true;
}

void Channel::update() {
  addedToLoop_ = true;
  loop_->updateChannel(this);
}

void Channel::remove() {
  assert(isNoneEvent());
  addedToLoop_ = false;
  loop_->removeChannel(this);
}

void Channel::handleEvent(Timestamp receiveTime) {
  std::shared_ptr<void> guard;
  if (tied_) {
    guard = tie_.lock();
    if (guard) {
      handleEventWithGuard(receiveTime);
    }
  } else {
    handleEventWithGuard(receiveTime);
  }
}

void Channel::handleEventWithGuard(Timestamp receiveTime) {
  eventHandling_ = true;
  SPDLOG_TRACE("Channel::handleEvent WithGuard. name: {}, fd: {}, revent: {}",
               name_, fd_, reventsToString());
  if ((revents_ & POLLHUP) && !(revents_ & POLLIN)) {
    if (logHup_) {
      SPDLOG_WARN("name: {}, fd = {} Channel::handle_event() POLLHUP", name_,
                  fd_);
    }
    if (closeCallback_)
      closeCallback_();
  }

  if (revents_ & POLLNVAL) {
    SPDLOG_WARN("name: {}, fd: {} Channel::handle_event() POLLNVAL", name_,
                fd_);
  }

  if (revents_ & (POLLERR | POLLNVAL)) {
    if (errorCallback_) {
      errorCallback_();
    } else {
      SPDLOG_WARN("errorCallback null");
    }
  }
  if (revents_ & (POLLIN | POLLPRI | POLLRDHUP)) {
    if (readCallback_) {
      readCallback_(receiveTime);
    } else {
      SPDLOG_WARN("readCallback null");
    }
  }
  if (revents_ & POLLOUT) {
    if (writeCallback_) {
      writeCallback_();
    } else {
      SPDLOG_WARN("writeCallback null");
    }
  }
  eventHandling_ = false;
}

std::string Channel::reventsToString() const {
  return eventsToString(fd_, revents_);
}

std::string Channel::eventsToString() const {
  return eventsToString(fd_, events_);
}

std::string Channel::eventsToString(int fd, int ev) {
  std::ostringstream oss;
  oss << "[ ";
  if (ev & POLLIN)
    oss << "IN ";
  if (ev & POLLPRI)
    oss << "PRI ";
  if (ev & POLLOUT)
    oss << "OUT ";
  if (ev & POLLHUP)
    oss << "HUP ";
  if (ev & POLLRDHUP)
    oss << "RDHUP ";
  if (ev & POLLERR)
    oss << "ERR ";
  if (ev & POLLNVAL)
    oss << "NVAL ";
  oss << "]";
  return oss.str();
}
