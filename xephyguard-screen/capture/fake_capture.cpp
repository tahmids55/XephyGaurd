#include "fake_capture.h"
#include <algorithm>
#include <cstring>

namespace xg {

FakeCapture::FakeCapture(int width, int height)
    : width_(width), height_(height), stride_(width * 4) {
  base_bgra_.resize(stride_ * height_, 0xFF);
  // Fill background with a soft gradient
  for (int y = 0; y < height_; ++y) {
    uint8_t* row = base_bgra_.data() + y * stride_;
    for (int x = 0; x < width_; ++x) {
      row[x * 4 + 0] = static_cast<uint8_t>((x * 255) / std::max(1, width_));      // B
      row[x * 4 + 1] = static_cast<uint8_t>((y * 255) / std::max(1, height_));     // G
      row[x * 4 + 2] = static_cast<uint8_t>(128);                                   // R
      row[x * 4 + 3] = 0xFF;                                                        // A
    }
  }
}

bool FakeCapture::initialize() {
  lost_ = false;
  secure_desktop_ = false;
  return true;
}

bool FakeCapture::start() {
  running_ = true;
  frame_idx_ = 0;
  return true;
}

CaptureStatus FakeCapture::capture(Frame& out) {
  if (!running_) return CaptureStatus::Error;
  if (lost_) {
    lost_ = false;
    return CaptureStatus::Lost;
  }
  if (secure_desktop_) {
    return CaptureStatus::SecureDesktop;
  }

  out.width = width_;
  out.height = height_;
  out.stride = stride_;
  out.bgra = base_bgra_;
  out.dirty.clear();
  out.full_dirty = (frame_idx_ == 0);
  out.secure_desktop = false;

  if (frame_idx_ == 0) {
    // Initial keyframe / full dirty
    ++frame_idx_;
    return CaptureStatus::Ok;
  }

  // Draw moving rectangle (simulates moving cursor / progress bar)
  int rect_x = (frame_idx_ * 16) % std::max(1, width_ - 64);
  int rect_y = 100;
  int rect_w = 48;
  int rect_h = 32;

  // Clear previous rectangle area in dirty list
  int prev_x = ((frame_idx_ - 1) * 16) % std::max(1, width_ - 64);
  out.dirty.push_back({prev_x, rect_y, rect_w, rect_h});
  out.dirty.push_back({rect_x, rect_y, rect_w, rect_h});

  // Paint the new rectangle
  for (int y = rect_y; y < rect_y + rect_h && y < height_; ++y) {
    uint8_t* row = out.bgra.data() + y * stride_;
    for (int x = rect_x; x < rect_x + rect_w && x < width_; ++x) {
      row[x * 4 + 0] = 0x00;  // B
      row[x * 4 + 1] = 0xFF;  // G
      row[x * 4 + 2] = 0xFF;  // R (Yellow)
      row[x * 4 + 3] = 0xFF;  // A
    }
  }

  // Simulating typing in an editor region (x: 200..400, y: 300..350)
  int type_x = 200 + ((frame_idx_ * 8) % 180);
  int type_y = 300;
  out.dirty.push_back({type_x, type_y, 16, 24});
  for (int y = type_y; y < type_y + 24 && y < height_; ++y) {
    uint8_t* row = out.bgra.data() + y * stride_;
    for (int x = type_x; x < type_x + 16 && x < width_; ++x) {
      row[x * 4 + 0] = 0xFF;  // Blueish text
      row[x * 4 + 1] = 0x20;
      row[x * 4 + 2] = 0x20;
      row[x * 4 + 3] = 0xFF;
    }
  }

  ++frame_idx_;
  return CaptureStatus::Ok;
}

void FakeCapture::stop() {
  running_ = false;
}

void FakeCapture::trigger_desktop_switch(bool secure) {
  secure_desktop_ = secure;
}

void FakeCapture::trigger_lost() {
  lost_ = true;
}

}  // namespace xg
