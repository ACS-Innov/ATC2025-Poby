// Inspired by Muduo https://github.com/chenshuo/muduo
#include <atomic>
#include <network/Timer.h>
using namespace hdc;
using namespace hdc::network;

std::atomic_int64_t Timer::s_numCreated_ = 0;

void Timer::restart(Timestamp now) {
  if (repeat_) {
    expiration_ = addTime(now, interval_);
  } else {
    expiration_ = Timestamp::invalid();
  }
}
