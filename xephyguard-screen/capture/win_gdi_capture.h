#pragma once
#if defined(_WIN32)
#include "capture.h"
#include <windows.h>

namespace xg {

class WinGdiCapture : public ScreenCapture {
public:
  WinGdiCapture();
  ~WinGdiCapture() override;

  bool initialize() override;
  bool start() override;
  CaptureStatus capture(Frame& out) override;
  void stop() override;

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
};

}  // namespace xg
#endif
