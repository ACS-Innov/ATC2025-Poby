// Inspired by Muduo https://github.com/chenshuo/muduo
#include <cassert>
#include <mutex>
#include <network/EventLoop.h>
#include <network/EventLoopThread.h>
using namespace hdc;
using namespace hdc::network;

EventLoopThread::EventLoopThread(const ThreadInitCallback &cb,
                                 const std::string &name)
    : loop_(NULL), exiting_(false),
      thread_(std::bind(&EventLoopThread::threadFunc, this), name), mutex_(),
      cond_(), callback_(cb) {}

EventLoopThread::~EventLoopThread() {
  exiting_ = true;
  if (loop_ !=
      NULL) // not 100% race-free, eg. threadFunc could be running callback_.
  {
    // still a tiny chance to call destructed object, if threadFunc exits just
    // now. but when EventLoopThread destructs, usually programming is exiting
    // anyway.
    loop_->quit();
    thread_.join();
  }
}

EventLoop *EventLoopThread::startLoop() {
  assert(!thread_.started());
  thread_.start();

  EventLoop *loop = NULL;
  {
    std::unique_lock<std::mutex> lock(mutex_);
    while (loop_ == NULL) {
      cond_.wait(lock);
    }
    loop = loop_;
  }
  return loop;
}

void EventLoopThread::threadFunc() {
  EventLoop loop;

  if (callback_) {
    callback_(&loop);
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    loop_ = &loop;
    cond_.notify_one();
  }

  loop.loop();
  std::lock_guard<std::mutex> lock(mutex_);
  loop_ = NULL;
}
