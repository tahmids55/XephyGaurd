#include "doctest.h"
#if defined(__linux__)
#include "x11_capture.h"
#include <X11/extensions/dpms.h>
#include <cstdlib>
#include <sys/shm.h>
#include <chrono>
#include <thread>

// Needs an X server ($DISPLAY, e.g. Xvfb); skipped without one.
TEST_CASE("X11 capture: shared memory is owner-only and marked for removal after attach") {
  if (!std::getenv("DISPLAY")) {
    MESSAGE("DISPLAY not set; skipped");
    return;
  }
  xg::X11Capture cap;
  if (!cap.initialize()) {
    MESSAGE("X11 capture not available on this display; skipped");
    return;
  }
  const int id = cap.shm_segment_id();
  REQUIRE(id >= 0);
  shmid_ds ds{};
  REQUIRE(shmctl(id, IPC_STAT, &ds) == 0);
  CHECK((ds.shm_perm.mode & 0777) == 0600);
  CHECK((ds.shm_perm.mode & SHM_DEST) != 0);  // IPC_RMID done: gone once detached
  cap.stop();
}

namespace {
// Draws a filled rectangle on the root window of $DISPLAY from a second connection, like another application.
void paint_root(int x, int y, int w, int h, unsigned long color) {
  Display* d = XOpenDisplay(nullptr);
  REQUIRE(d != nullptr);
  GC gc = XCreateGC(d, DefaultRootWindow(d), 0, nullptr);
  XSetForeground(d, gc, color);
  XFillRectangle(d, DefaultRootWindow(d), gc, x, y, static_cast<unsigned>(w), static_cast<unsigned>(h));
  XFreeGC(d, gc);
  XSync(d, False);
  XCloseDisplay(d);
}

xg::CaptureStatus capture_until_not(xg::X11Capture& cap, xg::Frame& f, xg::CaptureStatus unwanted) {
  xg::CaptureStatus st = unwanted;
  for (int i = 0; i < 100 && st == unwanted; ++i) {
    cap.wait_for_change(20);
    st = cap.capture(f);
  }
  return st;
}
}  // namespace

// These tests draw on the root window, so they run only on a throwaway display: ctest's xgtests_x11 starts Xvfb and
// sets XG_TEST_DISPOSABLE_DISPLAY=1. Never set it for a real desktop.
bool disposable_display() { return std::getenv("DISPLAY") && std::getenv("XG_TEST_DISPOSABLE_DISPLAY"); }

TEST_CASE("X11 capture: no damage means no capture, damage gives just the changed rectangle with opaque alpha") {
  if (!disposable_display()) { MESSAGE("no disposable display; skipped"); return; }
  xg::X11Capture cap;
  if (!cap.initialize() || !cap.start()) { MESSAGE("X11 capture not available on this display; skipped"); return; }
  xg::Frame f;
  REQUIRE(cap.capture(f) == xg::CaptureStatus::Ok);  // first capture reads everything
  CHECK(f.full_dirty);
  CHECK(f.bgra.size() == static_cast<size_t>(f.stride) * static_cast<size_t>(f.height));
  CHECK(f.bgra[3] == 0xFF);
  // Let the server settle, then nothing is drawn: no screen read.
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  while (cap.capture(f) == xg::CaptureStatus::Ok) std::this_thread::sleep_for(std::chrono::milliseconds(50));
  CHECK(cap.capture(f) == xg::CaptureStatus::NoChange);

  paint_root(100, 80, 40, 30, 0x00FF00);
  REQUIRE(capture_until_not(cap, f, xg::CaptureStatus::NoChange) == xg::CaptureStatus::Ok);
  CHECK_FALSE(f.full_dirty);
  REQUIRE_FALSE(f.dirty.empty());
  bool covered = false;
  for (const auto& r : f.dirty) covered |= r.x <= 100 && r.y <= 80 && r.x + r.w >= 140 && r.y + r.h >= 110;
  CHECK(covered);
  const uint8_t* px = &f.bgra[(static_cast<size_t>(90) * static_cast<size_t>(f.width) + 110) * 4];
  CHECK(px[1] == 0xFF);  // green channel of the painted area
  CHECK(px[3] == 0xFF);  // alpha opaque
  cap.stop();
}

TEST_CASE("X11 capture: wait_for_change blocks until a change, a timeout or interrupt_wait") {
  if (!disposable_display()) { MESSAGE("no disposable display; skipped"); return; }
  xg::X11Capture cap;
  if (!cap.initialize() || !cap.start()) { MESSAGE("X11 capture not available on this display; skipped"); return; }
  xg::Frame f;
  REQUIRE(cap.capture(f) == xg::CaptureStatus::Ok);
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  while (cap.capture(f) == xg::CaptureStatus::Ok) std::this_thread::sleep_for(std::chrono::milliseconds(50));
  using clock = std::chrono::steady_clock;
  auto ms = [](auto t0) { return std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - t0).count(); };

  auto t0 = clock::now();
  CHECK_FALSE(cap.wait_for_change(300));  // nothing happens: a full timeout, no early return
  CHECK(ms(t0) >= 250);

  std::thread painter([] { std::this_thread::sleep_for(std::chrono::milliseconds(150)); paint_root(10, 10, 20, 20, 0xFF0000); });
  t0 = clock::now();
  CHECK(cap.wait_for_change(5000));
  CHECK(ms(t0) < 2000);
  painter.join();

  while (cap.capture(f) == xg::CaptureStatus::Ok) std::this_thread::sleep_for(std::chrono::milliseconds(50));
  std::thread waker([&] { std::this_thread::sleep_for(std::chrono::milliseconds(150)); cap.interrupt_wait(); });
  t0 = clock::now();
  CHECK(cap.wait_for_change(5000));
  CHECK(ms(t0) < 2000);
  waker.join();
  cap.stop();
}
#endif

TEST_CASE("X11 capture: the 1 s safety re-read of an unchanged screen reports NoChange, not a full frame") {
  if (!disposable_display()) { MESSAGE("no disposable display; skipped"); return; }
  xg::X11Capture cap;
  if (!cap.initialize() || !cap.start()) { MESSAGE("X11 capture not available on this display; skipped"); return; }
  xg::Frame f;
  REQUIRE(cap.capture(f) == xg::CaptureStatus::Ok);
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  while (cap.capture(f) == xg::CaptureStatus::Ok) std::this_thread::sleep_for(std::chrono::milliseconds(50));
  std::this_thread::sleep_for(std::chrono::milliseconds(1100));  // the safety re-read is due
  CHECK(cap.capture(f) == xg::CaptureStatus::NoChange);
  cap.stop();
}

TEST_CASE("X11 capture: a blanked display (DPMS off) pauses capture, and unblanking reads the whole screen") {
  if (!disposable_display()) { MESSAGE("no disposable display; skipped"); return; }
  Display* d = XOpenDisplay(nullptr);
  REQUIRE(d);
  int ev = 0, err = 0;
  if (!DPMSQueryExtension(d, &ev, &err) || !DPMSCapable(d)) { XCloseDisplay(d); MESSAGE("no DPMS; skipped"); return; }
  xg::X11Capture cap;
  if (!cap.initialize() || !cap.start()) { XCloseDisplay(d); MESSAGE("X11 capture not available; skipped"); return; }
  xg::Frame f;
  REQUIRE(cap.capture(f) == xg::CaptureStatus::Ok);
  DPMSEnable(d);
  DPMSForceLevel(d, DPMSModeOff);
  XSync(d, False);
  std::this_thread::sleep_for(std::chrono::milliseconds(300));  // past the 250 ms DPMS answer cache
  CHECK(cap.capture(f) == xg::CaptureStatus::SecureDesktop);
  DPMSForceLevel(d, DPMSModeOn);
  XSync(d, False);
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  REQUIRE(cap.capture(f) == xg::CaptureStatus::Ok);
  CHECK(f.full_dirty);
  DPMSDisable(d);
  XCloseDisplay(d);
  cap.stop();
}
