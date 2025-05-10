// Inspired by Muduo https://github.com/chenshuo/muduo
#include <atomic>
#include <cassert>
#include <errno.h>
#include <exception>
#include <linux/unistd.h>
#include <network/CurrentThread.h>
#include <network/Thread.h>
#include <network/Timestamp.h>
#include <spdlog/spdlog.h>
#include <stdio.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <type_traits>
#include <unistd.h>
using hdc::network::Timestamp;
namespace hdc {
namespace detail {

pid_t gettid() { return static_cast<pid_t>(::syscall(SYS_gettid)); }

void afterFork() {
  hdc::CurrentThread::t_cachedTid = 0;
  hdc::CurrentThread::t_threadName = "main";
  CurrentThread::tid();
  // no need to call pthread_atfork(NULL, NULL, &afterFork);
}

class ThreadNameInitializer {
public:
  ThreadNameInitializer() {
    hdc::CurrentThread::t_threadName = "main";
    CurrentThread::tid();
    pthread_atfork(NULL, NULL, &afterFork);
  }
};

ThreadNameInitializer init;

struct ThreadData {
  typedef hdc::Thread::ThreadFunc ThreadFunc;
  ThreadFunc func_;
  std::string name_;
  pid_t *tid_;
  CountDownLatch *latch_;

  ThreadData(ThreadFunc func, const std::string &name, pid_t *tid,
             CountDownLatch *latch)
      : func_(std::move(func)), name_(name), tid_(tid), latch_(latch) {}

  void runInThread() {
    *tid_ = hdc::CurrentThread::tid();
    tid_ = NULL;
    latch_->countDown();
    latch_ = NULL;

    hdc::CurrentThread::t_threadName =
        name_.empty() ? "muduoThread" : name_.c_str();
    ::prctl(PR_SET_NAME, hdc::CurrentThread::t_threadName);
    try {
      func_();
      hdc::CurrentThread::t_threadName = "finished";
    } catch (const std::exception &ex) {
      hdc::CurrentThread::t_threadName = "crashed";
      fprintf(stderr, "exception caught in Thread %s\n", name_.c_str());
      fprintf(stderr, "reason: %s\n", ex.what());
      //   fprintf(stderr, "stack trace: %s\n", );
      abort();
    } catch (...) {
      hdc::CurrentThread::t_threadName = "crashed";
      fprintf(stderr, "unknown exception caught in Thread %s\n", name_.c_str());
      throw; // rethrow
    }
  }
};

void *startThread(void *obj) {
  ThreadData *data = static_cast<ThreadData *>(obj);
  data->runInThread();
  delete data;
  return NULL;
}

} // namespace detail

void CurrentThread::cacheTid() {
  if (t_cachedTid == 0) {
    t_cachedTid = detail::gettid();
    t_tidStringLength =
        snprintf(t_tidString, sizeof t_tidString, "%5d ", t_cachedTid);
  }
}

bool CurrentThread::isMainThread() { return tid() == ::getpid(); }

void CurrentThread::sleepUsec(int64_t usec) {
  struct timespec ts = {0, 0};
  ts.tv_sec = static_cast<time_t>(usec / Timestamp::kMicroSecondsPerSecond);
  ts.tv_nsec =
      static_cast<long>(usec % Timestamp::kMicroSecondsPerSecond * 1000);
  ::nanosleep(&ts, NULL);
}

std::atomic_int Thread::numCreated_ = 0;

Thread::Thread(ThreadFunc func, const std::string &n)
    : started_(false), joined_(false), pthreadId_(0), tid_(0),
      func_(std::move(func)), name_(n), latch_(1) {
  setDefaultName();
}

Thread::~Thread() {
  if (started_ && !joined_) {
    pthread_detach(pthreadId_);
  }
}

void Thread::setDefaultName() {
  int num = numCreated_.fetch_add(1);
  if (name_.empty()) {
    char buf[32];
    snprintf(buf, sizeof buf, "Thread%d", num);
    name_ = buf;
  }
}

void Thread::start() {
  assert(!started_);
  started_ = true;
  // FIXME: move(func_)
  detail::ThreadData *data =
      new detail::ThreadData(func_, name_, &tid_, &latch_);
  if (pthread_create(&pthreadId_, NULL, &detail::startThread, data)) {
    started_ = false;
    delete data; // or no delete?
    SPDLOG_ERROR("Failed in pthread_create");
  } else {
    latch_.wait();
    assert(tid_ > 0);
  }
}

int Thread::join() {
  assert(started_);
  assert(!joined_);
  joined_ = true;
  return pthread_join(pthreadId_, NULL);
}

} // namespace hdc
