#pragma once

namespace xg {
// CPU governor for the live stream (screen Step 52): keeps the capture + encode thread's own CPU time under a budget
// (percent of one core). Over budget it lowers the frame rate first (never below the floor), then the quality;
// with headroom it recovers slowly, quality first. Pure logic: the caller measures the thread's CPU time.
// (Raising the rectangle size, the third lever in the plan, needs the v2 rectangles of M5-3.)
class CpuGovernor {
public:
  explicit CpuGovernor(double budget_pct = 8.0, double max_fps = 15.0, double floor_fps = 2.0);

  void set_budget_pct(double pct);  // clamped to 1..100
  // Called about once per second: thread CPU seconds used, wall seconds elapsed and frames sent since the previous
  // call. The frames tell the real rate when something else (flow control, a static screen) sends fewer than fps().
  void update(double cpu_s, double wall_s, int frames = -1);

  double fps() const { return fps_; }
  double quality_scale() const { return quality_; }  // 0.5..1.0, multiplies the encoder quality
  double cpu_pct() const { return cpu_pct_; }        // last measured, percent of one core
  double budget_pct() const { return budget_; }
  int level() const;                                  // 0 = not throttled, higher = more throttled
  // Encoder threads to use (screen Step 29): 2 only while not throttled and well under the budget, else 1. The
  // caller must pass CPU time that includes the helper threads, so the extra worker is paid for from the budget.
  int workers() const { return workers_; }
  int min_interval_ms() const { return static_cast<int>(1000.0 / fps_); }

private:
  double budget_, max_fps_, floor_fps_;
  double fps_, quality_ = 1.0, cpu_pct_ = 0.0;
  int workers_ = 1;
};
}  // namespace xg
