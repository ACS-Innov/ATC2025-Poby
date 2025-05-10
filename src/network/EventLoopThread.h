// Inspired by Muduo https://github.com/chenshuo/muduo
#pragma once
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <network/Thread.h>
namespace hdc {
namespace network {

class EventLoop;

class EventLoopThread {
public:
  typedef std::function<void(EventLoop *)> ThreadInitCallback;

  EventLoopThread(const EventLoopThread &) = delete;
  EventLoopThread &operator=(const EventLoopThread &) = delete;

  EventLoopThread(const ThreadInitCallback &cb = ThreadInitCallback(),
                  const std::string &name = std::string());
  ~EventLoopThread();
  EventLoop *startLoop();

private:
  void threadFunc();

  EventLoop *loop_;
  bool exiting_;
  Thread thread_;
  std::mutex mutex_;
  std::condition_variable cond_;
  ThreadInitCallback callback_;
};

} // namespace network
} // namespace hdc
