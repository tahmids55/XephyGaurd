#include "doctest.h"
#if defined(__linux__)
#include "x11_capture.h"
#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>
#include <cstdlib>

// Needs an X server with RandR 1.5 ($DISPLAY, e.g. Xvfb); skipped without one. Defines two virtual monitors.
TEST_CASE("X11 capture: XRandR monitors are listed, selectable, and the picture is cropped to the selected one") {
  if (!std::getenv("DISPLAY") || !std::getenv("XG_TEST_DISPOSABLE_DISPLAY")) {
    MESSAGE("no disposable display; skipped");
    return;
  }
  Display* d = XOpenDisplay(nullptr);
  REQUIRE(d != nullptr);
  const Window root = DefaultRootWindow(d);
  const int sw = DisplayWidth(d, DefaultScreen(d)), sh = DisplayHeight(d, DefaultScreen(d));
  const int half = sw / 2;
  XRRMonitorInfo left{}, right{};
  left.name = XInternAtom(d, "xgtest-left", False);
  left.primary = True;
  left.x = 0; left.y = 0; left.width = half; left.height = sh; left.mwidth = left.mheight = 100;
  right.name = XInternAtom(d, "xgtest-right", False);
  right.x = half; right.y = 0; right.width = sw - half; right.height = sh; right.mwidth = right.mheight = 100;
  XRRSetMonitor(d, root, &left);
  XRRSetMonitor(d, root, &right);
  XSync(d, False);

  GC gc = XCreateGC(d, root, 0, nullptr);
  XSetForeground(d, gc, 0xFF0000);  // red left half
  XFillRectangle(d, root, gc, 0, 0, static_cast<unsigned>(half), static_cast<unsigned>(sh));
  XSetForeground(d, gc, 0x0000FF);  // blue right half
  XFillRectangle(d, root, gc, half, 0, static_cast<unsigned>(sw - half), static_cast<unsigned>(sh));
  XSync(d, False);

  xg::X11Capture cap;
  REQUIRE(cap.initialize());
  REQUIRE(cap.start());
  const auto mons = cap.monitors();
  REQUIRE(mons.size() == 2);
  CHECK(mons[0].x == 0);  // primary first
  CHECK(mons[1].x == half);
  CHECK_FALSE(cap.select_monitor(3));
  CHECK_FALSE(cap.select_monitor(0));

  xg::Frame f;
  REQUIRE(cap.capture(f) == xg::CaptureStatus::Ok);  // whole desktop first
  CHECK(f.width == sw);
  CHECK(f.monitor_id == 0);

  REQUIRE(cap.select_monitor(2));
  REQUIRE(cap.capture(f) == xg::CaptureStatus::Ok);  // the new monitor arrives whole
  CHECK(f.monitor_id == 2);
  CHECK(f.width == sw - half);
  CHECK(f.height == sh);
  CHECK(f.full_dirty);
  CHECK(f.bgra[0] == 0xFF);  // B of the blue half
  CHECK(f.bgra[2] == 0x00);  // R

  REQUIRE(cap.select_monitor(xg::kAllScreens));
  REQUIRE(cap.capture(f) == xg::CaptureStatus::Ok);
  CHECK(f.width == sw);
  CHECK(f.monitor_id == 0);
  cap.stop();
  XFreeGC(d, gc);
  XRRDeleteMonitor(d, root, left.name);
  XRRDeleteMonitor(d, root, right.name);
  XCloseDisplay(d);
}
#endif
