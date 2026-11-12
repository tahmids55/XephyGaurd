#pragma once
#if defined(__linux__)
#include "capture.h"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>
#include <X11/extensions/Xdamage.h>
#include <X11/extensions/Xrandr.h>

namespace xg {

class X11Capture : public ScreenCapture {
public:
  X11Capture() = default;
  ~X11Capture() override;

  bool initialize() override;
  bool start() override;
  CaptureStatus capture(Frame& out) override;
  void stop() override;

private:
  void cleanup();

  Display* display_ = nullptr;
  int screen_ = 0;
  Window root_ = 0;
  XImage* shm_image_ = nullptr;
  XShmSegmentInfo shminfo_{};
  Damage damage_ = 0;
  int width_ = 0;
  int height_ = 0;
  bool running_ = false;
  bool first_ = true;
};

}  // namespace xg
#endif
