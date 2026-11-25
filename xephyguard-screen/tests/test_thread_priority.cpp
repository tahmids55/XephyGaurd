#include "doctest.h"
#include "thread_priority.h"
#include <thread>
#if defined(__linux__)
#include <pthread.h>
#include <sched.h>
#endif

TEST_CASE("thread priority: the capture thread drops to idle priority, other threads are untouched") {
#if defined(__linux__)
  int policy = -1, caller_policy = -1;
  bool ok = false;
  std::thread t([&] {
    ok = xg::set_current_thread_idle_priority();
    sched_param sp{};
    pthread_getschedparam(pthread_self(), &policy, &sp);
  });
  t.join();
  sched_param sp{};
  pthread_getschedparam(pthread_self(), &caller_policy, &sp);
  CHECK(ok);
  CHECK(policy == SCHED_IDLE);
  CHECK(caller_policy != SCHED_IDLE);
#else
  MESSAGE("Linux only");
#endif
}
