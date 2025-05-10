// Inspired by Muduo https://github.com/chenshuo/muduo
#include <mutex>
#include <network/CountDownLatch.h>

using namespace hdc;

CountDownLatch::CountDownLatch(int count)
    : mutex_(), condition_(), count_(count) {}

void CountDownLatch::wait() {
  std::unique_lock<std::mutex> lock(mutex_);
  while (count_ > 0) {
    condition_.wait(lock);
  }
}

void CountDownLatch::countDown() {
  std::lock_guard<std::mutex> guard(mutex_);
  --count_;
  if (count_ == 0) {
    condition_.notify_all();
  }
}

int CountDownLatch::getCount() const {
  std::lock_guard<std::mutex> guard(mutex_);
  return count_;
}
