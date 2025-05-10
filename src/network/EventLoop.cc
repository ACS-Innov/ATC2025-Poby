// Inspired by Muduo https://github.com/chenshuo/muduo
#include "network/CurrentThread.h"
#include <algorithm>
#include <mutex>
#include <network/Channel.h>
#include <network/EventLoop.h>
#include <network/Poller.h>
#include <network/TimerQueue.h>
#include <network/tcp/SocketsOps.h>
#include <signal.h>
#include <spdlog/common.h>
#include <spdlog/spdlog.h>
#include <sys/eventfd.h>
#include <unistd.h>
using namespace hdc;
using namespace hdc::network;

namespace {
__thread EventLoop *t_loopInThisThread = 0;

const int kPollTimeMs = 10000;

int createEventfd() {
  int evtfd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  if (evtfd < 0) {
    SPDLOG_ERROR("Fail to create an eventfd");
    abort();
  }
  return evtfd;
}

#pragma GCC diagnostic ignored "-Wold-style-cast"
class IgnoreSigPipe {
public:
  IgnoreSigPipe() {
    ::signal(SIGPIPE, SIG_IGN);
    // LOG_TRACE << "Ignore SIGPIPE";
  }
};
#pragma GCC diagnostic error "-Wold-style-cast"

IgnoreSigPipe initObj;
} // namespace

EventLoop *EventLoop::getEventLoopOfCurrentThread() {
  return t_loopInThisThread;
}

EventLoop::EventLoop()
    : looping_(false), quit_(false), eventHandling_(false),
      callingPendingFunctors_(false), iteration_(0),
      threadId_(CurrentThread::tid()), poller_(std::make_unique<Poller>(this)),
      timerQueue_(new TimerQueue(this)), wakeupFd_(createEventfd()),
      wakeupChannel_(new Channel(this, wakeupFd_, "wakeup")),
      currentActiveChannel_(NULL) {
  SPDLOG_TRACE("EventLoop created {} in thread {}", fmt::ptr(this), threadId_);
  if (t_loopInThisThread) {
    SPDLOG_ERROR("Another EventLoop {}  exists in this thread {}",
                 fmt::ptr(t_loopInThisThread), threadId_);
  } else {
    t_loopInThisThread = this;
  }
  wakeupChannel_->setReadCallback(std::bind(&EventLoop::handleRead, this));
  // we are always reading the wakeupfd
  wakeupChannel_->enableReading();
}

EventLoop::~EventLoop() {
  SPDLOG_DEBUG("EventLoop {} of thread {} destructs in thread {}",
               fmt::ptr(this), threadId_, CurrentThread::tid());
  wakeupChannel_->disableAll();
  wakeupChannel_->remove();
  ::close(wakeupFd_);
  t_loopInThisThread = NULL;
}

void EventLoop::loop() {
  assert(!looping_);
  assertInLoopThread();
  looping_ = true;
  quit_ = false; // FIXME: what if someone calls quit() before loop() ?
  SPDLOG_TRACE("EventLoop {} start looping", fmt::ptr(this));
  while (!quit_) {
    activeChannels_.clear();
    pollReturnTime_ = poller_->poll(kPollTimeMs, &activeChannels_);
    ++iteration_;
    if (spdlog::should_log(spdlog::level::trace)) {
      printActiveChannels();
    }
    // TODO sort channel by priority
    eventHandling_ = true;
    for (Channel *channel : activeChannels_) {
      currentActiveChannel_ = channel;
      // SPDLOG_DEBUG("active fd: {}, event {}", currentActiveChannel_->fd(),
      //              currentActiveChannel_->events());
      currentActiveChannel_->handleEvent(pollReturnTime_);
    }
    currentActiveChannel_ = NULL;
    eventHandling_ = false;
    doPendingFunctors();
  }
  SPDLOG_TRACE("EventLoop {} stop looping", fmt::ptr(this));
  looping_ = false;
}

void EventLoop::quit() {
  if (looping_ == false && quit_ == false) {
    SPDLOG_ERROR("EventLoop can not quit before loop");
    return;
  }
  quit_ = true;
  // There is a chance that loop() just executes while(!quit_) and exits,
  // then EventLoop destructs, then we are accessing an invalid object.
  // Can be fixed using mutex_ in both places.
  if (!isInLoopThread()) {
    wakeup();
  }
}
void EventLoop::runInLoop(Functor cb) {
  if (isInLoopThread()) {
    cb();
  } else {
    queueInLoop(std::move(cb));
  }
}

void EventLoop::queueInLoop(Functor cb) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    pendingFunctors_.push_back(std::move(cb));
  }

  if (!isInLoopThread() || callingPendingFunctors_) {
    wakeup();
  }
}

size_t EventLoop::queueSize() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return pendingFunctors_.size();
}

TimerId EventLoop::runAt(Timestamp time, TimerCallback cb) {
  return timerQueue_->addTimer(std::move(cb), time, 0.0);
}

TimerId EventLoop::runAfter(double delay, TimerCallback cb) {
  Timestamp time(addTime(Timestamp::now(), delay));
  return runAt(time, std::move(cb));
}

TimerId EventLoop::runEvery(double interval, TimerCallback cb) {
  Timestamp time(addTime(Timestamp::now(), interval));
  return timerQueue_->addTimer(std::move(cb), time, interval);
}

void EventLoop::cancel(TimerId timerId) { return timerQueue_->cancel(timerId); }

/// @brief: Update the events of Channel monitored by Poller.
/// @detail: If it is a new channel, the Poller will also add the Channel to
/// the Channel Set it holds. The Poller won't remove the Channel from the
/// Channel Set in any cases.
void EventLoop::updateChannel(Channel *channel) {
  assert(channel->ownerLoop() == this);
  assertInLoopThread();
  poller_->updateChannel(channel);
}

void EventLoop::removeChannel(Channel *channel) {
  assert(channel->ownerLoop() == this);
  assertInLoopThread();
  if (eventHandling_) {
    assert(currentActiveChannel_ == channel ||
           std::find(activeChannels_.begin(), activeChannels_.end(), channel) ==
               activeChannels_.end());
  }
  poller_->removeChannel(channel);
}

bool EventLoop::hasChannel(Channel *channel) {
  assert(channel->ownerLoop() == this);
  assertInLoopThread();
  return poller_->hasChannel(channel);
}

void EventLoop::abortNotInLoopThread() {

  SPDLOG_ERROR("EventLoop::abortNotInLoopThread - EventLoop {} was created in "
               "threadId_ = {}, current thread id = {}",
               fmt::ptr(this), threadId_, CurrentThread::tid());
}

void EventLoop::wakeup() {
  uint64_t one = 1;
  ssize_t n = sockets::write(wakeupFd_, &one, sizeof one);
  if (n != sizeof one) {
    SPDLOG_ERROR("EventLoop::wakeup() writes {} bytes instead of 8", n);
  }
}

void EventLoop::handleRead() {
  uint64_t one = 1;
  ssize_t n = sockets::read(wakeupFd_, &one, sizeof one);
  if (n != sizeof one) {
    SPDLOG_ERROR("EventLoop::handleRead() reads {} bytes instead of 8", n);
  }
}

void EventLoop::doPendingFunctors() {
  std::vector<Functor> functors;
  callingPendingFunctors_ = true;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    functors.swap(pendingFunctors_);
  }

  for (const Functor &functor : functors) {
    functor();
  }
  callingPendingFunctors_ = false;
}

void EventLoop::printActiveChannels() const {
  SPDLOG_TRACE("TRACE active channels");
  for (const Channel *channel : activeChannels_) {
    SPDLOG_TRACE("Channel name: {}, fd: {}, revents: {}", channel->get_name(),
                 channel->fd(), channel->reventsToString());
  }
  SPDLOG_TRACE("END TRACE active channels");
}
