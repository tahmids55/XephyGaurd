#pragma once
#if defined(_WIN32)
#include "capture.h"
#include <windows.h>
#include <chrono>
#include <condition_variable>
#include <mutex>

namespace xg {

class WinGdiCapture : public ScreenCapture {
public:
  WinGdiCapture();
  ~WinGdiCapture() override;

  bool initialize() override;
  bool start() override;
  CaptureStatus capture(Frame& out) override;
  void stop() override;
  /// GDI reads the whole screen on every call (no change source), so it is capped at kMaxFps: capture() returns
  /// NoChange until 1000 / kMaxFps ms after the last read, and this waits until then (or timeout_ms / interrupt).
  bool wait_for_change(int timeout_ms) override;
  void interrupt_wait() override;
  void request_full() override { full_pending_ = true; }
  bool cursor_position(int& x, int& y) override;  // GetCursorPos on the primary desktop (compile-checked only)

  static constexpr int kMaxFps = 5;

private:
  void cleanup();

  HDC hdc_screen_ = nullptr;
  HDC hdc_mem_ = nullptr;
  HBITMAP hbitmap_ = nullptr;
  HBITMAP hold_bitmap_ = nullptr;
  uint8_t* bits_ = nullptr;
  int width_ = 0;
  int height_ = 0;
  bool running_ = false;
  std::chrono::steady_clock::time_point last_capture_{};
  std::mutex wait_mu_;
  std::condition_variable wait_cv_;
  bool interrupted_ = false;
  bool full_pending_ = true;
};

}  // namespace xg
#endif
