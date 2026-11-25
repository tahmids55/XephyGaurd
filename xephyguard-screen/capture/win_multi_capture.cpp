#if defined(_WIN32)
#include "win_multi_capture.h"
#include <algorithm>
#include <climits>

namespace xg {

bool WinMultiOutputCapture::initialize() {
  active_.reset();
  auto rects = WinDxgiCapture::list_outputs();
  if (rects.empty()) return false;
  {
    std::lock_guard<std::mutex> lock(mu_);
    outputs_ = std::move(rects);
    if (wanted_.load() < 1 || wanted_.load() > outputs_.size()) wanted_.store(1);  // the layout shrank
    selected_.store(wanted_.load());
  }
  active_ = std::make_unique<WinDxgiCapture>(selected_.load() - 1);
  if (!active_->initialize()) {
    active_.reset();
    return false;
  }
  return true;
}

bool WinMultiOutputCapture::start() {
  running_ = active_ && active_->start();
  return running_;
}

void WinMultiOutputCapture::stop() {
  running_ = false;
  if (active_) active_->stop();
}

void WinMultiOutputCapture::request_full() {
  if (active_) active_->request_full();
}

CaptureStatus WinMultiOutputCapture::capture(Frame& out) {
  if (!running_ || !active_) return CaptureStatus::Error;
  const uint16_t want = wanted_.load();
  if (want != selected_.load()) {  // switch output: close the old duplication, open the new one
    active_->stop();
    auto next = std::make_unique<WinDxgiCapture>(want - 1);
    if (next->initialize() && next->start()) {
      active_ = std::move(next);
      selected_.store(want);
    } else {
      wanted_.store(selected_.load());
      active_.reset();
      return CaptureStatus::Lost;  // the state machine re-initialises; initialize() reopens the selected output
    }
  }
  const CaptureStatus st = active_->capture(out);
  if (st == CaptureStatus::Ok) {
    std::lock_guard<std::mutex> lock(mu_);
    out.monitor_id = outputs_.size() >= 2 ? selected_.load() : 0;
  }
  return st;
}

std::vector<MonitorInfo> WinMultiOutputCapture::monitors() {
  std::lock_guard<std::mutex> lock(mu_);
  std::vector<MonitorInfo> list;
  if (outputs_.size() < 2) return list;
  LONG min_x = LONG_MAX, min_y = LONG_MAX;
  for (const auto& r : outputs_) {
    min_x = std::min(min_x, r.left);
    min_y = std::min(min_y, r.top);
  }
  for (const auto& r : outputs_)
    list.push_back({static_cast<int>(r.left - min_x), static_cast<int>(r.top - min_y),
                    static_cast<int>(r.right - r.left), static_cast<int>(r.bottom - r.top)});
  return list;
}

bool WinMultiOutputCapture::select_monitor(uint16_t id) {
  std::lock_guard<std::mutex> lock(mu_);
  if (outputs_.size() < 2 || id < 1 || id > outputs_.size()) return false;  // kAllScreens is out of range too
  wanted_.store(id);
  return true;
}

bool WinMultiOutputCapture::cursor_position(int& x, int& y) {
  RECT r;
  {
    std::lock_guard<std::mutex> lock(mu_);
    const uint16_t sel = selected_.load();
    if (sel < 1 || sel > outputs_.size()) return false;
    r = outputs_[sel - 1];
  }
  POINT pt;
  if (!GetCursorPos(&pt)) return false;
  if (pt.x < r.left || pt.x >= r.right || pt.y < r.top || pt.y >= r.bottom) return false;
  x = static_cast<int>(pt.x - r.left);
  y = static_cast<int>(pt.y - r.top);
  return true;
}

}  // namespace xg
#endif
