#if defined(_WIN32)
#include "win_dxgi_capture.h"
#include <algorithm>
#include <cstring>

namespace xg {

WinDxgiCapture::WinDxgiCapture() = default;

WinDxgiCapture::~WinDxgiCapture() {
  cleanup();
}

bool WinDxgiCapture::initialize() {
  cleanup();

  IDXGIFactory1* factory = nullptr;
  HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory));
  if (FAILED(hr) || !factory) {
    return false;
  }

  IDXGIAdapter1* owning_adapter = nullptr;
  IDXGIOutput* target_output = nullptr;

  // Enumerate adapters and find first active output
  for (UINT a = 0; ; ++a) {
    IDXGIAdapter1* adapter = nullptr;
    if (factory->EnumAdapters1(a, &adapter) == DXGI_ERROR_NOT_FOUND) {
      break;
    }

    for (UINT o = 0; ; ++o) {
      IDXGIOutput* output = nullptr;
      if (adapter->EnumOutputs(o, &output) == DXGI_ERROR_NOT_FOUND) {
        break;
      }

      DXGI_OUTPUT_DESC desc;
      if (SUCCEEDED(output->GetDesc(&desc)) && desc.AttachedToDesktop) {
        owning_adapter = adapter;
        target_output = output;
        width_ = desc.DesktopCoordinates.right - desc.DesktopCoordinates.left;
        height_ = desc.DesktopCoordinates.bottom - desc.DesktopCoordinates.top;
        break;
      }
      output->Release();
    }

    if (target_output) break;
    adapter->Release();
  }

  factory->Release();

  if (!owning_adapter || !target_output || width_ <= 0 || height_ <= 0) {
    if (owning_adapter) owning_adapter->Release();
    if (target_output) target_output->Release();
    return false;
  }

  // Create D3D11 device on the OWNING adapter (critical for hybrid GPUs)
  D3D_FEATURE_LEVEL feature_levels[] = {
    D3D_FEATURE_LEVEL_11_1,
    D3D_FEATURE_LEVEL_11_0,
    D3D_FEATURE_LEVEL_10_1,
    D3D_FEATURE_LEVEL_10_0
  };
  D3D_FEATURE_LEVEL feature_level;

  hr = D3D11CreateDevice(
      owning_adapter,
      D3D_DRIVER_TYPE_UNKNOWN,
      nullptr,
      0,
      feature_levels,
      sizeof(feature_levels) / sizeof(feature_levels[0]),
      D3D11_SDK_VERSION,
      &d3d_device_,
      &feature_level,
      &d3d_context_);

  owning_adapter->Release();

  if (FAILED(hr) || !d3d_device_ || !d3d_context_) {
    target_output->Release();
    return false;
  }

  // Get IDXGIOutput1 for DuplicateOutput
  IDXGIOutput1* output1 = nullptr;
  hr = target_output->QueryInterface(__uuidof(IDXGIOutput1), reinterpret_cast<void**>(&output1));
  target_output->Release();

  if (FAILED(hr) || !output1) {
    cleanup();
    return false;
  }

  hr = output1->DuplicateOutput(d3d_device_, &duplication_);
  output1->Release();

  if (FAILED(hr) || !duplication_) {
    cleanup();
    return false;
  }

  // Create CPU-readable staging texture
  D3D11_TEXTURE2D_DESC staging_desc{};
  staging_desc.Width = static_cast<UINT>(width_);
  staging_desc.Height = static_cast<UINT>(height_);
  staging_desc.MipLevels = 1;
  staging_desc.ArraySize = 1;
  staging_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  staging_desc.SampleDesc.Count = 1;
  staging_desc.Usage = D3D11_USAGE_STAGING;
  staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

  hr = d3d_device_->CreateTexture2D(&staging_desc, nullptr, &staging_texture_);
  if (FAILED(hr) || !staging_texture_) {
    cleanup();
    return false;
  }

  return true;
}

bool WinDxgiCapture::start() {
  running_ = true;
  return true;
}

CaptureStatus WinDxgiCapture::capture(Frame& out) {
  if (!running_ || !duplication_ || !d3d_context_ || !staging_texture_) {
    return CaptureStatus::Error;
  }

  DXGI_OUTDUPL_FRAME_INFO frame_info{};
  IDXGIResource* desktop_resource = nullptr;

  HRESULT hr = duplication_->AcquireNextFrame(100, &frame_info, &desktop_resource);

  if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
    return CaptureStatus::NoChange;
  }
  if (hr == DXGI_ERROR_ACCESS_LOST) {
    cleanup();
    return CaptureStatus::Lost;
  }
  if (hr == DXGI_ERROR_ACCESS_DENIED) {
    return CaptureStatus::SecureDesktop;
  }
  if (FAILED(hr) || !desktop_resource) {
    return CaptureStatus::Error;
  }

  ID3D11Texture2D* desktop_texture = nullptr;
  hr = desktop_resource->QueryInterface(__uuidof(ID3D11Texture2D),
                                        reinterpret_cast<void**>(&desktop_texture));
  desktop_resource->Release();

  if (FAILED(hr) || !desktop_texture) {
    duplication_->ReleaseFrame();
    return CaptureStatus::Error;
  }

  // Copy GPU desktop texture to staging texture
  d3d_context_->CopyResource(staging_texture_, desktop_texture);
  desktop_texture->Release();

  // Retrieve dirty rects metadata
  out.dirty.clear();
  out.full_dirty = false;

  UINT dirty_bytes = 0;
  duplication_->GetFrameDirtyRects(0, nullptr, &dirty_bytes);
  if (dirty_bytes > 0) {
    UINT rect_count = dirty_bytes / sizeof(RECT);
    dirty_rects_buf_.resize(rect_count);
    hr = duplication_->GetFrameDirtyRects(dirty_bytes, dirty_rects_buf_.data(), &dirty_bytes);
    if (SUCCEEDED(hr)) {
      for (const auto& r : dirty_rects_buf_) {
        out.dirty.push_back({
            r.left,
            r.top,
            r.right - r.left,
            r.bottom - r.top
        });
      }
    }
  } else {
    out.full_dirty = true;
  }

  // Map staging texture to CPU memory
  D3D11_MAPPED_SUBRESOURCE mapped{};
  hr = d3d_context_->Map(staging_texture_, 0, D3D11_MAP_READ, 0, &mapped);
  if (FAILED(hr)) {
    duplication_->ReleaseFrame();
    return CaptureStatus::Error;
  }

  out.width = width_;
  out.height = height_;
  out.stride = width_ * 4;
  size_t total_bytes = static_cast<size_t>(out.stride * height_);
  out.bgra.resize(total_bytes);

  const uint8_t* src_row = static_cast<const uint8_t*>(mapped.pData);
  uint8_t* dst_row = out.bgra.data();
  size_t row_bytes = static_cast<size_t>(width_ * 4);

  for (int y = 0; y < height_; ++y) {
    std::memcpy(dst_row, src_row, row_bytes);
    src_row += mapped.RowPitch;
    dst_row += out.stride;
  }

  d3d_context_->Unmap(staging_texture_, 0);
  duplication_->ReleaseFrame();

  out.secure_desktop = false;
  return CaptureStatus::Ok;
}

void WinDxgiCapture::stop() {
  running_ = false;
}

void WinDxgiCapture::cleanup() {
  running_ = false;
  if (staging_texture_) {
    staging_texture_->Release();
    staging_texture_ = nullptr;
  }
  if (duplication_) {
    duplication_->Release();
    duplication_ = nullptr;
  }
  if (d3d_context_) {
    d3d_context_->Release();
    d3d_context_ = nullptr;
  }
  if (d3d_device_) {
    d3d_device_->Release();
    d3d_device_ = nullptr;
  }
  width_ = 0;
  height_ = 0;
  dirty_rects_buf_.clear();
}

}  // namespace xg
#endif
