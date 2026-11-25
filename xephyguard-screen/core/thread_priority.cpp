#include "thread_priority.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <pthread.h>
#include <sched.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#endif

namespace xg {

bool set_current_thread_idle_priority() {
#if defined(_WIN32)
  // Background mode also lowers the thread's I/O and memory priority. (compile-checked only)
  if (SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN)) return true;
  return SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL) != 0;
#elif defined(__linux__)
  sched_param sp{};
  sp.sched_priority = 0;
  if (pthread_setschedparam(pthread_self(), SCHED_IDLE, &sp) == 0) return true;
  // On Linux setpriority with a thread id changes that thread only.
  const auto tid = static_cast<id_t>(::syscall(SYS_gettid));
  return ::setpriority(PRIO_PROCESS, tid, 19) == 0;
#else
  return ::setpriority(PRIO_PROCESS, 0, 19) == 0;
#endif
}

double current_thread_cpu_seconds() {
#if defined(_WIN32)
  FILETIME created, exited, kernel, user;  // (compile-checked only)
  if (!GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user)) return 0.0;
  auto ticks = [](const FILETIME& f) { return (static_cast<unsigned long long>(f.dwHighDateTime) << 32) | f.dwLowDateTime; };
  return static_cast<double>(ticks(kernel) + ticks(user)) * 1e-7;  // 100 ns units
#else
  timespec ts{};
  if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) != 0) return 0.0;
  return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) * 1e-9;
#endif
}

}  // namespace xg
