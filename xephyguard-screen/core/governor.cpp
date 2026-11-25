#include "governor.h"
#include <algorithm>
#include <cmath>

namespace xg {

namespace {
constexpr double kMinQuality = 0.5;
constexpr double kHeadroom = 0.6;  // recover only below 60 % of the budget, so it does not oscillate
constexpr double kWorkerUp = 0.5;    // second encoder worker only below 50 % of the budget and not throttled
constexpr double kWorkerDown = 0.8;  // back to one worker above 80 % of the budget or when throttled
}  // namespace

CpuGovernor::CpuGovernor(double budget_pct, double max_fps, double floor_fps)
    : budget_(std::clamp(budget_pct, 1.0, 100.0)),
      max_fps_(std::max(max_fps, floor_fps)),
      floor_fps_(floor_fps),
      fps_(max_fps_) {}

void CpuGovernor::set_budget_pct(double pct) { budget_ = std::clamp(pct, 1.0, 100.0); }

void CpuGovernor::update(double cpu_s, double wall_s, int frames) {
  if (!(wall_s > 0.0) || !(cpu_s >= 0.0)) return;
  cpu_pct_ = 100.0 * cpu_s / wall_s;
  if (cpu_pct_ > budget_) {
    if (fps_ > floor_fps_) {
      // Cost is about proportional to the frame rate: aim straight at the budget, at least one step down.
      const double sent = frames >= 0 ? std::min(fps_, frames / wall_s) : fps_;
      fps_ = std::max(floor_fps_, std::min(sent * budget_ / cpu_pct_, fps_ - 1.0));
    } else {
      quality_ = std::max(kMinQuality, quality_ - 0.1);
    }
  } else if (cpu_pct_ < budget_ * kHeadroom) {
    if (quality_ < 1.0 - 1e-9) {
      quality_ = std::min(1.0, quality_ + 0.05);
    } else {
      fps_ = std::min(max_fps_, fps_ + 1.0);
    }
  }
  if (level() > 0 || cpu_pct_ > budget_ * kWorkerDown) {
    workers_ = 1;
  } else if (cpu_pct_ < budget_ * kWorkerUp) {
    workers_ = 2;
  }
}

int CpuGovernor::level() const {
  const int fps_steps = static_cast<int>(std::ceil(max_fps_ - fps_ - 1e-9));
  const int q_steps = static_cast<int>(std::round((1.0 - quality_) / 0.05));
  return std::max(0, fps_steps) + std::max(0, q_steps);
}

}  // namespace xg
