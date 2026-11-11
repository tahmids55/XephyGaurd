#if defined(_WIN32)
#include "win_gdi_capture.h"
#include <cstring>

namespace xg {

WinGdiCapture::WinGdiCapture() = default;

WinGdiCapture::~WinGdiCapture() {
  cleanup();
}

bool WinGdiCapture::initialize() {
  cleanup();

  hdc_screen_ = GetDC(nullptr);
  if (!hdc_screen_) return false;

  width_ = GetSystemMetrics(SM_CXSCREEN);
  height_ = GetSystemMetrics(SM_CYSCREEN);
  if (width_ <= 0 || height_ <= 0) {
    cleanup();
    return false;
  }

  hdc_mem_ = CreateCompatibleDC(hdc_screen_);
  if (!hdc_mem_) {
    cleanup();
    return false;
  }

  BITMAPINFO bmi{};
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = width_;
  bmi.bmiHeader.biHeight = -height_; // Negative for top-down rows
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;     // 32-bit BGRA
  bmi.bmiHeader.biCompression = BI_RGB;

  hbitmap_ = CreateDIBSection(hdc_screen_, &bmi, DIB_RGB_COLORS,
                              reinterpret_cast<void**>(&bits_), nullptr, 0);
  if (!hbitmap_ || !bits_) {
    cleanup();
    return false;
  }

  hold_bitmap_ = static_cast<HBITMAP>(SelectObject(hdc_mem_, hbitmap_));
  return true;
}

bool WinGdiCapture::start() {
  running_ = true;
  return true;
}

CaptureStatus WinGdiCapture::capture(Frame& out) {
  if (!running_ || !bits_ || !hdc_screen_ || !hdc_mem_) {
    return CaptureStatus::Error;
  }

  // Detect screen resolution changes
  int cur_w = GetSystemMetrics(SM_CXSCREEN);
  int cur_h = GetSystemMetrics(SM_CYSCREEN);
  if (cur_w != width_ || cur_h != height_) {
    return CaptureStatus::Lost;
  }

  BOOL ok = BitBlt(hdc_mem_, 0, 0, width_, height_,
                   hdc_screen_, 0, 0, SRCCOPY | CAPTUREBLT);
  if (!ok) {
    // Desktop switch / UAC / lock screen often causes BitBlt to fail with ACCESS_DENIED
    DWORD err = GetLastError();
    if (err == ERROR_ACCESS_DENIED) {
      return CaptureStatus::SecureDesktop;
    }
    return CaptureStatus::Error;
  }

  out.width = width_;
  out.height = height_;
  out.stride = width_ * 4;
  size_t total_bytes = static_cast<size_t>(out.stride * height_);
  out.bgra.resize(total_bytes);
  std::memcpy(out.bgra.data(), bits_, total_bytes);

  out.dirty.clear();
  out.full_dirty = true; // GDI BitBlt captures entire screen without hardware dirty rects
  out.secure_desktop = false;

  return CaptureStatus::Ok;
}

void WinGdiCapture::stop() {
  running_ = false;
}

void WinGdiCapture::cleanup() {
  running_ = false;
  if (hdc_mem_ && hold_bitmap_) {
    SelectObject(hdc_mem_, hold_bitmap_);
    hold_bitmap_ = nullptr;
  }
  if (hbitmap_) {
    DeleteObject(hbitmap_);
    hbitmap_ = nullptr;
  }
  if (hdc_mem_) {
    DeleteDC(hdc_mem_);
    hdc_mem_ = nullptr;
  }
  if (hdc_screen_) {
    ReleaseDC(nullptr, hdc_screen_);
    hdc_screen_ = nullptr;
  }
  bits_ = nullptr;
  width_ = 0;
  height_ = 0;
}

}  // namespace xg
#endif
