#pragma once
#if defined(_WIN32)
#include "capture.h"
#include "win_dxgi_capture.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

namespace xg {

/// Screen Step 36: every output attached to the desktop can be shown, one at a time. Each output is its own DXGI
/// duplication (WinDxgiCapture); only the shown one is open. There is no stitched whole-desktop picture: it starts
/// on output 1 and select_monitor(kAllScreens) is refused. With a single output it behaves like WinDxgiCapture
/// (no monitor list, frames carry monitor_id 0). Compile-checked only, never run on Windows.
class WinMultiOutputCapture : public ScreenCapture {
public:
  bool initialize() override;
  bool start() override;
  CaptureStatus capture(Frame& out) override;
  void stop() override;
  void request_full() override;
  bool cursor_position(int& x, int& y) override;  // relative to the shown output; false while the pointer is elsewhere
  std::vector<MonitorInfo> monitors() override;
  bool select_monitor(uint16_t id) override;
  uint16_t selected_monitor() const override { return selected_.load(); }
  bool all_screens_view() const override { return false; }

private:
  mutable std::mutex mu_;                 // guards outputs_
  std::vector<RECT> outputs_;
  std::unique_ptr<WinDxgiCapture> active_;  // capture thread only
  std::atomic<uint16_t> selected_{1};     // 1-based output being captured
  std::atomic<uint16_t> wanted_{1};       // switch applied by the capture thread at the next capture()
  bool running_ = false;
};

}  // namespace xg
#endif
