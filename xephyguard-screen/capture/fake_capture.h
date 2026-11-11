#pragma once
#include "capture.h"

namespace xg {
class FakeCapture : public ScreenCapture {
public:
  FakeCapture(int width = 1280, int height = 720);
  bool initialize() override;
  bool start() override;
  CaptureStatus capture(Frame& out) override;
  void stop() override;

  void trigger_desktop_switch(bool secure);
  void trigger_lost();

private:
  int width_ = 1280;
  int height_ = 720;
  int stride_ = 0;
  int frame_idx_ = 0;
  bool running_ = false;
  bool secure_desktop_ = false;
  bool lost_ = false;
  std::vector<uint8_t> base_bgra_;
};
}
