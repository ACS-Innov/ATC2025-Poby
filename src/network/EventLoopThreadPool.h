// Inspired by Muduo https://github.com/chenshuo/muduo
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>
namespace hdc {

namespace network {

class EventLoop;
class EventLoopThread;

class EventLoopThreadPool {
public:
  typedef std::function<void(EventLoop *)> ThreadInitCallback;

  EventLoopThreadPool(const EventLoopThreadPool &);
  EventLoopThreadPool &operator=(const EventLoopThreadPool &);

  EventLoopThreadPool(EventLoop *baseLoop, const std::string &nameArg);
  ~EventLoopThreadPool();
  void setThreadNum(int numThreads) { numThreads_ = numThreads; }
  void start(const ThreadInitCallback &cb = ThreadInitCallback());

  // valid after calling start()
  /// round-robin
  EventLoop *getNextLoop();

  /// with the same hash code, it will always return the same EventLoop
  EventLoop *getLoopForHash(size_t hashCode);

  std::vector<EventLoop *> getAllLoops();

  bool started() const { return started_; }

  const std::string &name() const { return name_; }

private:
  EventLoop *baseLoop_;
  std::string name_;
  bool started_;
  int numThreads_;
  int next_;
  std::vector<std::unique_ptr<EventLoopThread>> threads_;
  std::vector<EventLoop *> loops_;
};

} // namespace network
} // namespace hdc
