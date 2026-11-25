#include "monitors.h"
#include <algorithm>
#include <cstring>

namespace xg {

bool crop_frame(Frame& f, const MonitorInfo& r) {
  const int x0 = std::max(r.x, 0), y0 = std::max(r.y, 0);
  const int x1 = std::min(r.x + r.w, f.width), y1 = std::min(r.y + r.h, f.height);
  if (x1 <= x0 || y1 <= y0 || f.stride < f.width * 4 ||
      f.bgra.size() < static_cast<size_t>(f.stride) * static_cast<size_t>(f.height))
    return false;
  const int w = x1 - x0, h = y1 - y0;
  if (x0 != 0 || y0 != 0 || w != f.width || h != f.height) {
    std::vector<uint8_t> out(static_cast<size_t>(w) * h * 4);
    for (int row = 0; row < h; ++row)
      std::memcpy(out.data() + static_cast<size_t>(row) * w * 4,
                  f.bgra.data() + static_cast<size_t>(y0 + row) * f.stride + static_cast<size_t>(x0) * 4,
                  static_cast<size_t>(w) * 4);
    f.bgra = std::move(out);
  }
  std::vector<Frame::Rect> dirty;
  for (const auto& d : f.dirty) {
    const int dx0 = std::max(d.x, x0), dy0 = std::max(d.y, y0);
    const int dx1 = std::min(d.x + d.w, x1), dy1 = std::min(d.y + d.h, y1);
    if (dx1 > dx0 && dy1 > dy0) dirty.push_back({dx0 - x0, dy0 - y0, dx1 - dx0, dy1 - dy0});
  }
  f.dirty = std::move(dirty);
  f.width = w;
  f.height = h;
  f.stride = w * 4;
  return true;
}

}  // namespace xg
