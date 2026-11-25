#if defined(__linux__)
#include "x11_capture.h"
#include "monitors.h"
#include <X11/extensions/dpms.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <unistd.h>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>

namespace xg {

namespace {
constexpr size_t kMaxDamageRects = 64;      // more than this in one capture: copy the whole screen instead
constexpr int64_t kSafetyRecheckMs = 1000;  // re-read the screen at least this often even without damage events
int64_t now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
}  // namespace

X11Capture::X11Capture() { wake_fd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC); }

X11Capture::~X11Capture() {
  cleanup();
  if (wake_fd_ >= 0) ::close(wake_fd_);
}

bool X11Capture::initialize() {
  cleanup();
  const char* display_name = std::getenv("DISPLAY");
  display_ = XOpenDisplay(display_name);
  if (!display_) return false;

  screen_ = DefaultScreen(display_);
  root_ = RootWindow(display_, screen_);

  XWindowAttributes attrs;
  if (!XGetWindowAttributes(display_, root_, &attrs)) {
    cleanup();
    return false;
  }
  width_ = attrs.width;
  height_ = attrs.height;

  // Check Shm extension
  if (!XShmQueryExtension(display_)) {
    cleanup();
    return false;
  }

  shm_image_ = XShmCreateImage(display_, DefaultVisual(display_, screen_),
                               DefaultDepth(display_, screen_), ZPixmap,
                               nullptr, &shminfo_, width_, height_);
  if (!shm_image_) {
    cleanup();
    return false;
  }

  shminfo_.shmid = shmget(IPC_PRIVATE, shm_image_->bytes_per_line * shm_image_->height,
                          IPC_CREAT | 0600);  // owner only: the segment holds screen pixels
  if (shminfo_.shmid < 0) {
    cleanup();
    return false;
  }

  shminfo_.shmaddr = shm_image_->data = static_cast<char*>(shmat(shminfo_.shmid, nullptr, 0));
  if (shminfo_.shmaddr == reinterpret_cast<char*>(-1)) {
    shmctl(shminfo_.shmid, IPC_RMID, nullptr);
    shminfo_.shmid = -1;
    cleanup();
    return false;
  }
  shminfo_.readOnly = False;

  if (!XShmAttach(display_, &shminfo_)) {
    cleanup();
    return false;
  }
  // Once the X server has attached, mark the segment for removal: it disappears when both sides
  // detach, so no other process can attach it later and nothing is left behind after a crash.
  XSync(display_, False);
  shmctl(shminfo_.shmid, IPC_RMID, nullptr);
  shm_removed_ = true;

  // Damage extension
  int damage_event, damage_error;
  if (XDamageQueryExtension(display_, &damage_event, &damage_error)) {
    damage_event_base_ = damage_event;
    damage_ = XDamageCreate(display_, root_, XDamageReportRawRectangles);
  }

  int dpms_event, dpms_error;
  dpms_ = DPMSQueryExtension(display_, &dpms_event, &dpms_error) && DPMSCapable(display_);
  blanked_ = false;
  dpms_checked_ms_ = -1000;

  // XRandR events for screen resize
  int r_event, r_error;
  if (XRRQueryExtension(display_, &r_event, &r_error)) {
    XRRSelectInput(display_, root_, RRScreenChangeNotifyMask);
  }

  // Monitors for the selector (XRandR 1.5). Primary first, then by position; a lone monitor offers no choice.
  {
    std::vector<MonitorInfo> found;
    int n = 0;
    XRRMonitorInfo* mons = XRRGetMonitors(display_, root_, True, &n);
    if (mons) {
      std::vector<std::pair<int, MonitorInfo>> tmp;
      for (int i = 0; i < n; ++i) {
        MonitorInfo m{mons[i].x, mons[i].y, mons[i].width, mons[i].height};
        if (m.x < 0 || m.y < 0 || m.w <= 0 || m.h <= 0 || m.x + m.w > width_ || m.y + m.h > height_) continue;
        tmp.push_back({mons[i].primary ? 0 : 1, m});
      }
      XRRFreeMonitors(mons);
      // A monitor that contains another one (a server's catch-all "screen" next to user-defined monitors) is just the
      // all-screens view again: leave it out.
      std::vector<std::pair<int, MonitorInfo>> kept;
      for (const auto& a : tmp) {
        bool contains_other = false;
        for (const auto& b : tmp) {
          const MonitorInfo &x = a.second, &y = b.second;
          if (&a != &b && y.x >= x.x && y.y >= x.y && y.x + y.w <= x.x + x.w && y.y + y.h <= x.y + x.h) contains_other = true;
        }
        if (!contains_other) kept.push_back(a);
      }
      tmp = std::move(kept);
      std::sort(tmp.begin(), tmp.end(), [](const auto& a, const auto& b) {
        if (a.first != b.first) return a.first < b.first;
        if (a.second.y != b.second.y) return a.second.y < b.second.y;
        return a.second.x < b.second.x;
      });
      if (tmp.size() >= 2 && tmp.size() <= 16)
        for (const auto& t : tmp) found.push_back(t.second);
    }
    std::lock_guard<std::mutex> lock(mon_mu_);
    monitors_ = std::move(found);
    if (selected_.load() > monitors_.size()) selected_.store(0);  // the layout shrank
  }

  XSync(display_, False);
  have_shadow_ = false;
  shadow_.assign(static_cast<size_t>(width_) * static_cast<size_t>(height_) * 4, 0);
  pending_.clear();
  damage_all_ = true;  // the first capture reads everything
  lost_ = false;
  return true;
}

bool X11Capture::start() {
  running_ = true;
  return true;
}

void X11Capture::drain_events() {
  bool got_damage = false;
  while (XPending(display_) > 0) {
    XEvent ev;
    XNextEvent(display_, &ev);
    if (ev.type == RRScreenChangeNotify) {
      XRRUpdateConfiguration(&ev);
      lost_ = true;
    } else if (damage_event_base_ >= 0 && ev.type == damage_event_base_ + XDamageNotify) {
      const auto* d = reinterpret_cast<const XDamageNotifyEvent*>(&ev);
      got_damage = true;
      if (damage_all_) continue;
      const int x0 = std::max(0, static_cast<int>(d->area.x)), y0 = std::max(0, static_cast<int>(d->area.y));
      const int x1 = std::min(width_, d->area.x + d->area.width), y1 = std::min(height_, d->area.y + d->area.height);
      if (x1 <= x0 || y1 <= y0) continue;
      if (pending_.size() >= kMaxDamageRects) {
        damage_all_ = true;
        pending_.clear();
      } else {
        pending_.push_back({x0, y0, x1 - x0, y1 - y0});
      }
    }
  }
  if (got_damage && damage_) XDamageSubtract(display_, damage_, None, None);
}

bool X11Capture::wait_for_change(int timeout_ms) {
  if (!display_) return false;
  const int64_t due = last_full_ms_ + kSafetyRecheckMs - now_ms();
  if (!have_shadow_ || due <= 0 || XPending(display_) > 0) return true;
  pollfd fds[2] = {{ConnectionNumber(display_), POLLIN, 0}, {wake_fd_, POLLIN, 0}};
  const int wait = static_cast<int>(std::min<int64_t>(timeout_ms, due));
  const int n = ::poll(fds, wake_fd_ >= 0 ? 2 : 1, wait);
  if (n > 0 && wake_fd_ >= 0 && (fds[1].revents & POLLIN)) {
    uint64_t v;
    if (::read(wake_fd_, &v, sizeof v) < 0) {}
    return true;
  }
  return n > 0 || wait == due;  // an X event, or the safety re-check came due
}

void X11Capture::interrupt_wait() {
  if (wake_fd_ >= 0) {
    const uint64_t one = 1;
    if (::write(wake_fd_, &one, sizeof one) < 0) {}
  }
}

void X11Capture::copy_rect(int x, int y, int w, int h) {
  const auto* src_base = reinterpret_cast<const uint8_t*>(shm_image_->data);
  const size_t bpl = static_cast<size_t>(shm_image_->bytes_per_line);
  for (int row = y; row < y + h; ++row) {
    const uint8_t* src = src_base + static_cast<size_t>(row) * bpl;
    uint8_t* dst = shadow_.data() + (static_cast<size_t>(row) * static_cast<size_t>(width_) + static_cast<size_t>(x)) * 4;
    if (shm_image_->bits_per_pixel == 32) {
      std::memcpy(dst, src + static_cast<size_t>(x) * 4, static_cast<size_t>(w) * 4);
      // 24-bit TrueColor leaves the padding byte 0: make it opaque (WebP encodes BGRA). One OR per pixel, only
      // over the changed rectangle, and the compiler vectorizes it.
      for (int i = 0; i < w; ++i) {
        uint32_t px;
        std::memcpy(&px, dst + i * 4, 4);
        px |= 0xFF000000u;  // alpha is the high byte on little-endian
        std::memcpy(dst + i * 4, &px, 4);
      }
    } else {
      for (int i = 0; i < w; ++i) {
        const uint8_t* p = src + static_cast<size_t>(x + i) * 3;
        dst[i * 4 + 0] = p[0];
        dst[i * 4 + 1] = p[1];
        dst[i * 4 + 2] = p[2];
        dst[i * 4 + 3] = 0xFF;
      }
    }
  }
}

bool X11Capture::row_differs(int row) const {
  const uint8_t* src = reinterpret_cast<const uint8_t*>(shm_image_->data) +
                       static_cast<size_t>(row) * static_cast<size_t>(shm_image_->bytes_per_line);
  const uint8_t* dst = shadow_.data() + static_cast<size_t>(row) * static_cast<size_t>(width_) * 4;
  uint32_t diff = 0;  // OR of all differences, no early exit, so the compiler vectorizes the loop
  if (shm_image_->bits_per_pixel == 32) {
    for (int i = 0; i < width_; ++i) {
      uint32_t s, d;
      std::memcpy(&s, src + i * 4, 4);
      std::memcpy(&d, dst + i * 4, 4);
      diff |= (s | 0xFF000000u) ^ d;
    }
  } else {
    for (int i = 0; i < width_; ++i) {
      diff |= static_cast<uint32_t>((src[i * 3] ^ dst[i * 4]) | (src[i * 3 + 1] ^ dst[i * 4 + 1]) |
                                    (src[i * 3 + 2] ^ dst[i * 4 + 2]));
    }
  }
  return diff != 0;
}

void X11Capture::add_changed_bands() {
  int start = -1;
  for (int row = 0; row <= height_; ++row) {
    const bool changed = row < height_ && row_differs(row);
    if (changed && start < 0) {
      start = row;
    } else if (!changed && start >= 0) {
      if (pending_.size() >= kMaxDamageRects) {
        damage_all_ = true;
        pending_.clear();
        return;
      }
      pending_.push_back({0, start, width_, row - start});
      start = -1;
    }
  }
}

bool X11Capture::display_blanked() {
  if (!dpms_) return false;
  const int64_t now = now_ms();
  if (now - dpms_checked_ms_ < 250) return blanked_;
  dpms_checked_ms_ = now;
  CARD16 level = DPMSModeOn;
  BOOL enabled = False;
  blanked_ = DPMSInfo(display_, &level, &enabled) && enabled && level != DPMSModeOn;
  return blanked_;
}

CaptureStatus X11Capture::capture(Frame& out) {
  if (!running_ || !display_ || !shm_image_) {
    return CaptureStatus::Error;
  }
  if (shm_image_->bits_per_pixel != 32 && shm_image_->bits_per_pixel != 24) {
    return CaptureStatus::Error;
  }

  drain_events();
  if (lost_) {
    return CaptureStatus::Lost;
  }
  if (selection_changed_.exchange(false)) damage_all_ = true;  // a new monitor is shown whole
  // Monitor off (DPMS standby/suspend/off): no screen read, and the first capture after it re-reads everything.
  if (display_blanked()) {
    pending_.clear();
    damage_all_ = true;
    return CaptureStatus::SecureDesktop;
  }

  // Cost follows change: no damage since the last capture means no screen read at all. Every kSafetyRecheckMs the
  // whole screen is re-read anyway, in case a compositor draws without reporting damage on the root window; that
  // read is compared with the shadow, and only rows that really differ are copied and reported (none: NoChange).
  const int64_t now = now_ms();
  const bool recheck = have_shadow_ && now - last_full_ms_ >= kSafetyRecheckMs;
  if (have_shadow_ && !recheck && !damage_all_ && pending_.empty()) {
    return CaptureStatus::NoChange;
  }

  if (!XShmGetImage(display_, root_, shm_image_, 0, 0, AllPlanes)) {
    return CaptureStatus::Error;
  }

  if (recheck) {
    last_full_ms_ = now;
    if (!damage_all_) add_changed_bands();
  }
  const bool full = !have_shadow_ || damage_all_;
  if (!full && pending_.empty()) {
    return CaptureStatus::NoChange;
  }
  out.dirty.clear();
  if (full) {
    copy_rect(0, 0, width_, height_);
    last_full_ms_ = now;
    out.full_dirty = true;
  } else {
    for (const auto& r : pending_) {
      copy_rect(r.x, r.y, r.w, r.h);
      out.dirty.push_back(r);
    }
    out.full_dirty = false;
  }
  pending_.clear();
  damage_all_ = false;
  have_shadow_ = true;

  out.width = width_;
  out.height = height_;
  out.stride = width_ * 4;
  out.bgra.assign(shadow_.begin(), shadow_.end());
  out.secure_desktop = false;
  out.monitor_id = 0;

  MonitorInfo mon;
  if (monitor_rect(mon)) {
    if (!crop_frame(out, mon)) return CaptureStatus::Error;
    out.monitor_id = selected_.load();
    if (!out.full_dirty && out.dirty.empty()) return CaptureStatus::NoChange;  // changes were on other monitors
  }
  return CaptureStatus::Ok;
}

std::vector<MonitorInfo> X11Capture::monitors() {
  std::lock_guard<std::mutex> lock(mon_mu_);
  return monitors_;
}

bool X11Capture::select_monitor(uint16_t id) {
  {
    std::lock_guard<std::mutex> lock(mon_mu_);
    if (id == kAllScreens) id = 0;
    else if (id < 1 || id > monitors_.size()) return false;
  }
  if (selected_.exchange(id) != id) {
    selection_changed_.store(true);
    interrupt_wait();  // the capture thread may be waiting for damage
  }
  return true;
}

bool X11Capture::monitor_rect(MonitorInfo& out) {
  const uint16_t sel = selected_.load();
  if (sel == 0) return false;
  std::lock_guard<std::mutex> lock(mon_mu_);
  if (sel > monitors_.size()) return false;
  out = monitors_[sel - 1];
  return true;
}

void X11Capture::stop() {
  running_ = false;
}

bool X11Capture::cursor_position(int& x, int& y) {
  std::lock_guard<std::mutex> lock(cursor_mu_);
  if (width_ <= 0 || height_ <= 0) return false;  // not initialised
  if (!cursor_display_) {
    cursor_display_ = XOpenDisplay(std::getenv("DISPLAY"));
    if (!cursor_display_) return false;
  }
  Window root_ret, child_ret;
  int rx = 0, ry = 0, wx = 0, wy = 0;
  unsigned int mask = 0;
  if (!XQueryPointer(cursor_display_, DefaultRootWindow(cursor_display_), &root_ret, &child_ret, &rx, &ry, &wx, &wy,
                     &mask))
    return false;
  MonitorInfo mon;
  if (monitor_rect(mon)) {  // relative to the selected monitor; off it = no position
    if (rx < mon.x || ry < mon.y || rx >= mon.x + mon.w || ry >= mon.y + mon.h) return false;
    rx -= mon.x;
    ry -= mon.y;
  }
  x = std::max(0, std::min(rx, width_ - 1));
  y = std::max(0, std::min(ry, height_ - 1));
  return true;
}

void X11Capture::cleanup() {
  {
    std::lock_guard<std::mutex> lock(cursor_mu_);
    if (cursor_display_) {
      XCloseDisplay(cursor_display_);
      cursor_display_ = nullptr;
    }
  }
  running_ = false;
  if (display_) {
    if (damage_) {
      XDamageDestroy(display_, damage_);
      damage_ = 0;
    }
    if (shminfo_.shmaddr && shminfo_.shmaddr != reinterpret_cast<char*>(-1)) {
      XShmDetach(display_, &shminfo_);
      shmdt(shminfo_.shmaddr);
      // Remove only if not already marked: a removed id may be reused by another segment.
      if (!shm_removed_ && shminfo_.shmid >= 0) shmctl(shminfo_.shmid, IPC_RMID, nullptr);
      shm_removed_ = false;
      shminfo_.shmaddr = nullptr;
    }
    if (shm_image_) {
      XDestroyImage(shm_image_);
      shm_image_ = nullptr;
    }
    XCloseDisplay(display_);
    display_ = nullptr;
  }
}

}  // namespace xg
#endif
