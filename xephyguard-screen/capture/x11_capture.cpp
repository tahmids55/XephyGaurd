#if defined(__linux__)
#include "x11_capture.h"
#include <sys/ipc.h>
#include <sys/shm.h>
#include <cstdlib>
#include <cstring>

namespace xg {

X11Capture::~X11Capture() { cleanup(); }

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
                          IPC_CREAT | 0777);
  if (shminfo_.shmid < 0) {
    cleanup();
    return false;
  }

  shminfo_.shmaddr = shm_image_->data = static_cast<char*>(shmat(shminfo_.shmid, nullptr, 0));
  if (shminfo_.shmaddr == reinterpret_cast<char*>(-1)) {
    cleanup();
    return false;
  }
  shminfo_.readOnly = False;

  if (!XShmAttach(display_, &shminfo_)) {
    cleanup();
    return false;
  }

  // Damage extension
  int damage_event, damage_error;
  if (XDamageQueryExtension(display_, &damage_event, &damage_error)) {
    damage_ = XDamageCreate(display_, root_, XDamageReportRawRectangles);
  }

  // XRandR events for screen resize
  int r_event, r_error;
  if (XRRQueryExtension(display_, &r_event, &r_error)) {
    XRRSelectInput(display_, root_, RRScreenChangeNotifyMask);
  }

  XSync(display_, False);
  return true;
}

bool X11Capture::start() {
  running_ = true;
  first_ = true;
  return true;
}

CaptureStatus X11Capture::capture(Frame& out) {
  if (!running_ || !display_ || !shm_image_) {
    return CaptureStatus::Error;
  }

  // Check for resize events via XRandR
  while (XPending(display_) > 0) {
    XEvent ev;
    XNextEvent(display_, &ev);
    if (ev.type == RRScreenChangeNotify) {
      XRRUpdateConfiguration(&ev);
      return CaptureStatus::Lost;
    }
  }

  if (!XShmGetImage(display_, root_, shm_image_, 0, 0, AllPlanes)) {
    return CaptureStatus::Error;
  }

  out.width = width_;
  out.height = height_;
  out.stride = shm_image_->bytes_per_line;
  size_t size = static_cast<size_t>(out.stride * height_);
  out.bgra.resize(size);
  std::memcpy(out.bgra.data(), shm_image_->data, size);

  out.dirty.clear();
  out.full_dirty = first_;
  first_ = false;
  out.secure_desktop = false;

  return CaptureStatus::Ok;
}

void X11Capture::stop() {
  running_ = false;
}

void X11Capture::cleanup() {
  running_ = false;
  if (display_) {
    if (damage_) {
      XDamageDestroy(display_, damage_);
      damage_ = 0;
    }
    if (shminfo_.shmaddr && shminfo_.shmaddr != reinterpret_cast<char*>(-1)) {
      XShmDetach(display_, &shminfo_);
      shmdt(shminfo_.shmaddr);
      shmctl(shminfo_.shmid, IPC_RMID, nullptr);
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
