#pragma once
#if defined(__linux__)
#include "capture.h"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>
#include <X11/extensions/Xdamage.h>
#include <X11/extensions/Xrandr.h>
#include <atomic>
#include <mutex>
#include <vector>

namespace xg {

class X11Capture : public ScreenCapture {
public:
  X11Capture();
  ~X11Capture() override;

  bool initialize() override;
  bool start() override;
  CaptureStatus capture(Frame& out) override;
  void stop() override;
  /// Blocks on the X connection and an internal eventfd (no polling): true when damage events may be waiting, the
  /// safety re-check is due, or interrupt_wait() was called; false on timeout.
  bool wait_for_change(int timeout_ms) override;
  void interrupt_wait() override;
  void request_full() override { damage_all_ = true; }
  /// Uses its own X connection (opened on first call), so it never races the capture thread's Display.
  bool cursor_position(int& x, int& y) override;

  /// XRandR monitors (read in initialize(); fewer than 2 = none). Primary first, then top to bottom, left to right.
  std::vector<MonitorInfo> monitors() override;
  bool select_monitor(uint16_t id) override;
  uint16_t selected_monitor() const override { return selected_.load(); }

  /// SysV shared memory id of the capture image (-1 when none); for tests.
  int shm_segment_id() const { return shminfo_.shmaddr ? shminfo_.shmid : -1; }

private:
  void cleanup();
  /// Reads pending X events: damage rectangles go to pending_, a screen-size change sets lost_.
  void drain_events();
  void copy_rect(int x, int y, int w, int h);
  /// Safety re-check: compares the screen read with the shadow and adds the changed rows as full-width bands.
  void add_changed_bands();
  bool row_differs(int row) const;
  bool display_blanked();

  Display* display_ = nullptr;
  std::mutex cursor_mu_;
  Display* cursor_display_ = nullptr;  // second connection, only for cursor_position(); guarded by cursor_mu_
  int screen_ = 0;
  Window root_ = 0;
  XImage* shm_image_ = nullptr;
  XShmSegmentInfo shminfo_{};
  bool shm_removed_ = false;  // IPC_RMID already done after attach
  Damage damage_ = 0;
  int damage_event_base_ = -1;
  bool dpms_ = false;                     // the server has DPMS: a blanked display pauses capture
  bool blanked_ = false;                  // last DPMS answer
  int64_t dpms_checked_ms_ = -1000;       // asked at most every 250 ms (one X round trip)
  int wake_fd_ = -1;                      // eventfd for interrupt_wait(); lives as long as the object
  std::vector<Frame::Rect> pending_;      // damage since the last capture, clipped to the screen
  bool damage_all_ = false;               // too many rectangles, or none known: copy everything
  bool lost_ = false;                     // XRandR changed the screen size
  std::vector<uint8_t> shadow_;           // last captured frame, BGRA with opaque alpha; only damaged parts are re-copied
  bool have_shadow_ = false;
  int64_t last_full_ms_ = 0;              // last time the whole screen was re-read
  int width_ = 0;
  int height_ = 0;
  bool running_ = false;

  std::mutex mon_mu_;
  std::vector<MonitorInfo> monitors_;      // guarded by mon_mu_
  std::atomic<uint16_t> selected_{0};      // 0 whole screen, else a 1-based monitors_ index
  std::atomic<bool> selection_changed_{false};
  bool monitor_rect(MonitorInfo& out);     // the selected monitor's rectangle, false = whole screen
};

}  // namespace xg
#endif
