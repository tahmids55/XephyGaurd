#pragma once
#include <cstdint>
#include <vector>

namespace xg {
struct Frame {
  int width = 0, height = 0, stride = 0;     // stride in bytes
  std::vector<uint8_t> bgra;                  // size = stride * height
  struct Rect { int x, y, w, h; };
  std::vector<Rect> dirty;                    // empty + full_dirty=false => unchanged
  bool full_dirty = false;                    // true => treat everything as changed
  bool secure_desktop = false;
};

enum class CaptureStatus { Ok, NoChange, SecureDesktop, Lost, Error };

class ScreenCapture {
public:
  virtual ~ScreenCapture() = default;
  virtual bool initialize() = 0;
  virtual bool start() = 0;
  virtual CaptureStatus capture(Frame& out) = 0;  // fills out when Ok
  virtual void stop() = 0;
};
}
