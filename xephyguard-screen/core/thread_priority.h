#pragma once

namespace xg {
// Puts the calling thread at idle priority so screen capture and encoding only use CPU nobody else wants:
// Linux SCHED_IDLE (fallback nice 19 for this thread), Windows background mode (fallback below normal).
// Only ever lowers priority, needs no privilege. Returns false if nothing could be changed.
bool set_current_thread_idle_priority();
// CPU time (user + system) the calling thread has used, in seconds; 0 if unknown. Feeds the CPU governor.
double current_thread_cpu_seconds();
}  // namespace xg
