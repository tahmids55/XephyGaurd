#if defined(_WIN32)
#include "win_dxgi_capture.h"
#include <algorithm>
#include <cstring>

namespace xg {

WinDxgiCapture::WinDxgiCapture(int output_index) : output_index_(output_index < 0 ? 0 : output_index) {}

std::vector<RECT> WinDxgiCapture::list_outputs() {
  std::vector<RECT> rects;
  IDXGIFactory1* factory = nullptr;
  if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory))) || !factory) return rects;
  for (UINT a = 0; ; ++a) {
    IDXGIAdapter1* adapter = nullptr;
    if (factory->EnumAdapters1(a, &adapter) == DXGI_ERROR_NOT_FOUND) break;
    for (UINT o = 0; ; ++o) {
      IDXGIOutput* output = nullptr;
      if (adapter->EnumOutputs(o, &output) == DXGI_ERROR_NOT_FOUND) break;
      DXGI_OUTPUT_DESC desc;
      if (SUCCEEDED(output->GetDesc(&desc)) && desc.AttachedToDesktop && rects.size() < 16) rects.push_back(desc.DesktopCoordinates);
      output->Release();
    }
    adapter->Release();
  }
  factory->Release();
  return rects;
}

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

  // Enumerate adapters and find the output_index_-th active output (0 = the first)
  int attached = 0;
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
      if (SUCCEEDED(output->GetDesc(&desc)) && desc.AttachedToDesktop && attached++ == output_index_) {
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

  shadow_.assign(static_cast<size_t>(width_) * static_cast<size_t>(height_) * 4, 0);
  return true;
}

bool WinDxgiCapture::start() {
  running_ = true;
  return true;
}

bool WinDxgiCapture::clamp(RECT& r) const {
  r.left = std::clamp<LONG>(r.left, 0, width_);
  r.right = std::clamp<LONG>(r.right, 0, width_);
  r.top = std::clamp<LONG>(r.top, 0, height_);
  r.bottom = std::clamp<LONG>(r.bottom, 0, height_);
  return r.right > r.left && r.bottom > r.top;
}

void WinDxgiCapture::copy_to_shadow(const uint8_t* src, size_t pitch, const RECT& r) {
  const size_t row_bytes = static_cast<size_t>(r.right - r.left) * 4;
  for (LONG y = r.top; y < r.bottom; ++y) {
    const uint8_t* s = src + static_cast<size_t>(y) * pitch + static_cast<size_t>(r.left) * 4;
    uint8_t* d = shadow_.data() + (static_cast<size_t>(y) * static_cast<size_t>(width_) + static_cast<size_t>(r.left)) * 4;
    std::memcpy(d, s, row_bytes);
    // The desktop image's alpha byte is undefined: make it opaque (the encoders read BGRA).
    for (size_t i = 3; i < row_bytes; i += 4) d[i] = 0xFF;
  }
}

// Cost follows change: only the dirty and move-destination rectangles are copied from the GPU into the staging
// texture and from there into the persistent shadow frame; the rest of the shadow stays as it was. A pointer-only
// update (LastPresentTime 0) is NoChange. A full copy happens on the first frame, after request_full() and when the
// driver gives no rectangle metadata.
CaptureStatus WinDxgiCapture::capture(Frame& out) {
  if (!running_ || !duplication_ || !d3d_context_ || !staging_texture_) {
    return CaptureStatus::Error;
  }

  auto emit = [&](bool full) {
    out.width = width_;
    out.height = height_;
    out.stride = width_ * 4;
    out.bgra.assign(shadow_.begin(), shadow_.end());
    out.dirty.clear();
    out.full_dirty = full;
    if (!full) {
      for (const auto& r : changed_) out.dirty.push_back({r.left, r.top, r.right - r.left, r.bottom - r.top});
    }
    out.secure_desktop = false;
    full_pending_ = false;
    return CaptureStatus::Ok;
  };

  DXGI_OUTDUPL_FRAME_INFO frame_info{};
  IDXGIResource* desktop_resource = nullptr;
  HRESULT hr = duplication_->AcquireNextFrame(100, &frame_info, &desktop_resource);

  if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
    // Nothing changed; a viewer that needs a picture of a static screen gets the shadow.
    return (full_pending_ && have_shadow_) ? emit(true) : CaptureStatus::NoChange;
  }
  if (hr == DXGI_ERROR_ACCESS_LOST) {
    cleanup();
    return CaptureStatus::Lost;
  }
  if (hr == DXGI_ERROR_ACCESS_DENIED) {
    full_pending_ = true;
    return CaptureStatus::SecureDesktop;
  }
  if (FAILED(hr) || !desktop_resource) {
    return CaptureStatus::Error;
  }

  if (frame_info.LastPresentTime.QuadPart == 0 && have_shadow_) {
    desktop_resource->Release();
    duplication_->ReleaseFrame();
    return full_pending_ ? emit(true) : CaptureStatus::NoChange;
  }

  ID3D11Texture2D* desktop_texture = nullptr;
  hr = desktop_resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&desktop_texture));
  desktop_resource->Release();
  if (FAILED(hr) || !desktop_texture) {
    duplication_->ReleaseFrame();
    return CaptureStatus::Error;
  }

  // Changed rectangles: dirty rects plus the destination of every move rect (the desktop texture already holds the
  // moved pixels, so a move is copied like a dirty rect). Metadata missing or unreadable => full copy.
  changed_.clear();
  bool full = !have_shadow_ || full_pending_ || frame_info.TotalMetadataBufferSize == 0;
  if (!full) {
    UINT bytes = 0;
    move_rects_buf_.resize(frame_info.TotalMetadataBufferSize / sizeof(DXGI_OUTDUPL_MOVE_RECT) + 1);
    hr = duplication_->GetFrameMoveRects(static_cast<UINT>(move_rects_buf_.size() * sizeof(DXGI_OUTDUPL_MOVE_RECT)),
                                         move_rects_buf_.data(), &bytes);
    if (FAILED(hr)) {
      full = true;
    } else {
      for (UINT i = 0; i < bytes / sizeof(DXGI_OUTDUPL_MOVE_RECT); ++i) changed_.push_back(move_rects_buf_[i].DestinationRect);
    }
  }
  if (!full) {
    UINT bytes = 0;
    dirty_rects_buf_.resize(frame_info.TotalMetadataBufferSize / sizeof(RECT) + 1);
    hr = duplication_->GetFrameDirtyRects(static_cast<UINT>(dirty_rects_buf_.size() * sizeof(RECT)),
                                          dirty_rects_buf_.data(), &bytes);
    if (FAILED(hr)) {
      full = true;
    } else {
      for (UINT i = 0; i < bytes / sizeof(RECT); ++i) changed_.push_back(dirty_rects_buf_[i]);
    }
  }
  if (!full) {
    size_t kept = 0;
    for (RECT r : changed_) {
      if (clamp(r)) changed_[kept++] = r;
    }
    changed_.resize(kept);
    if (changed_.empty()) {
      desktop_texture->Release();
      duplication_->ReleaseFrame();
      return CaptureStatus::NoChange;
    }
  }

  if (full) {
    d3d_context_->CopyResource(staging_texture_, desktop_texture);
  } else {
    for (const auto& r : changed_) {
      D3D11_BOX box{static_cast<UINT>(r.left), static_cast<UINT>(r.top), 0,
                    static_cast<UINT>(r.right), static_cast<UINT>(r.bottom), 1};
      d3d_context_->CopySubresourceRegion(staging_texture_, 0, static_cast<UINT>(r.left), static_cast<UINT>(r.top), 0,
                                          desktop_texture, 0, &box);
    }
  }
  desktop_texture->Release();

  D3D11_MAPPED_SUBRESOURCE mapped{};
  hr = d3d_context_->Map(staging_texture_, 0, D3D11_MAP_READ, 0, &mapped);
  if (FAILED(hr)) {
    duplication_->ReleaseFrame();
    return CaptureStatus::Error;
  }
  const auto* src = static_cast<const uint8_t*>(mapped.pData);
  if (full) {
    copy_to_shadow(src, mapped.RowPitch, RECT{0, 0, width_, height_});
  } else {
    for (const auto& r : changed_) copy_to_shadow(src, mapped.RowPitch, r);
  }
  d3d_context_->Unmap(staging_texture_, 0);
  duplication_->ReleaseFrame();

  have_shadow_ = true;
  return emit(full);
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
  have_shadow_ = false;
  full_pending_ = true;
  shadow_.clear();
  dirty_rects_buf_.clear();
  move_rects_buf_.clear();
  changed_.clear();
}

bool WinDxgiCapture::cursor_position(int& x, int& y) {
  POINT pt;
  if (!GetCursorPos(&pt)) return false;
  x = std::max(0, std::min(static_cast<int>(pt.x), width_ - 1));
  y = std::max(0, std::min(static_cast<int>(pt.y), height_ - 1));
  return true;
}

}  // namespace xg
#endif
