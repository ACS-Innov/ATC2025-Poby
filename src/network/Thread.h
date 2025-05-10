// Inspired by Muduo https://github.com/chenshuo/muduo
#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <network/CountDownLatch.h>
#include <pthread.h>
#include <string>
namespace hdc {

class Thread {
public:
  typedef std::function<void()> ThreadFunc;

  Thread(const Thread &) = delete;
  Thread &operator=(const Thread &) = delete;

  explicit Thread(ThreadFunc, const std::string &name = std::string());
  // FIXME: make it movable in C++11
  ~Thread();

  void start();
  int join(); // return pthread_join()

  bool started() const { return started_; }
  // pthread_t pthreadId() const { return pthreadId_; }
  pid_t tid() const { return tid_; }
  const std::string &name() const { return name_; }

  static int numCreated() { return numCreated_.load(); }

private:
  void setDefaultName();

  bool started_;
  bool joined_;
  pthread_t pthreadId_;
  pid_t tid_;
  ThreadFunc func_;
  std::string name_;
  CountDownLatch latch_;

  static std::atomic_int numCreated_;
};

} // namespace hdc
