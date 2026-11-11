#pragma once
#if defined(_WIN32)
#include "capture.h"
#include <d3d11.h>
#include <dxgi1_2.h>
#include <vector>

namespace xg {

class WinDxgiCapture : public ScreenCapture {
public:
  WinDxgiCapture();
  ~WinDxgiCapture() override;

  bool initialize() override;
  bool start() override;
  CaptureStatus capture(Frame& out) override;
  void stop() override;

private:
  void cleanup();

  ID3D11Device* d3d_device_ = nullptr;
  ID3D11DeviceContext* d3d_context_ = nullptr;
  IDXGIOutputDuplication* duplication_ = nullptr;
  ID3D11Texture2D* staging_texture_ = nullptr;

  int width_ = 0;
  int height_ = 0;
  bool running_ = false;
  std::vector<RECT> dirty_rects_buf_;
};

}  // namespace xg
#endif
