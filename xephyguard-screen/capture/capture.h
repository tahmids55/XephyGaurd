#pragma once
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <thread>
#include <vector>

namespace xg {
struct Frame {
  int width = 0, height = 0, stride = 0;     // stride in bytes
  std::vector<uint8_t> bgra;                  // size = stride * height
  struct Rect { int x, y, w, h; };
  std::vector<Rect> dirty;                    // empty + full_dirty=false => unchanged
  bool full_dirty = false;                    // true => treat everything as changed
  bool secure_desktop = false;
  uint16_t monitor_id = 0;                    // 0: the whole captured screen; 1..N: that monitor only (Step 35)
};

/// One monitor inside the whole-desktop picture, in pixels (Step 35). Monitor ids are 1-based list positions.
struct MonitorInfo { int x, y, w, h; };
constexpr uint16_t kAllScreens = 0xFFFF;  // select_monitor(): the whole desktop picture

enum class CaptureStatus { Ok, NoChange, SecureDesktop, Lost, Error };

class ScreenCapture {
public:
  virtual ~ScreenCapture() = default;
  virtual bool initialize() = 0;
  virtual bool start() = 0;
  virtual CaptureStatus capture(Frame& out) = 0;  // fills out when Ok
  virtual void stop() = 0;
  /// Blocks until the screen may have changed or `timeout_ms` passed; false = timed out (nothing changed). Captures
  /// without a change source poll: this default sleeps a short slice and says "maybe", like the old fixed sleeps.
  virtual bool wait_for_change(int timeout_ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(std::min(timeout_ms, 20)));
    return true;
  }
  /// Thread-safe: ends a wait_for_change() blocked on another thread at once.
  virtual void interrupt_wait() {}
  /// Capture thread only: the next capture() must return Ok with the whole screen (a new viewer or a lifted privacy
  /// mask needs a picture even though nothing changed). Captures that return Ok on every call ignore it.
  virtual void request_full() {}
  /// Thread-safe (called from the cursor thread while the capture thread runs): the pointer position in screen
  /// pixels, false when unknown. Position only, never the cursor image.
  virtual bool cursor_position(int& x, int& y) { (void)x; (void)y; return false; }
  /// Monitors the capture can show on their own (positions inside the whole-desktop picture). Fewer than 2 = nothing
  /// to choose. Thread-safe snapshot, geometry only.
  virtual std::vector<MonitorInfo> monitors() { return {}; }
  /// Shows only monitor `id` (1..monitors().size()) or, with kAllScreens, the whole desktop. False = not possible,
  /// nothing changed. Thread-safe; the next capture() returns the new picture in full (its size may differ). While a
  /// monitor is selected, frames carry its id and cursor_position() is relative to it.
  virtual bool select_monitor(uint16_t id) { (void)id; return false; }
  /// 0 = whole desktop, else the selected monitor.
  virtual uint16_t selected_monitor() const { return 0; }
  /// False when the capture cannot stitch all monitors into one picture (select_monitor(kAllScreens) fails).
  virtual bool all_screens_view() const { return true; }
};
}
