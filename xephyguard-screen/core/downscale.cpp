#include "downscale.h"
#include <algorithm>

namespace xg {

std::vector<uint8_t> downscale_box(const Frame& src, int out_w, int* out_h) {
  if (src.width <= 0 || src.height <= 0 || out_w <= 0 || src.bgra.empty()) {
    if (out_h) *out_h = 0;
    return {};
  }

  // Preserve aspect ratio
  int calculated_h = (src.height * out_w) / src.width;
  if (calculated_h <= 0) {
    calculated_h = 1;
  }
  if (out_h) {
    *out_h = calculated_h;
  }

  int dst_stride = out_w * 4;
  std::vector<uint8_t> out(static_cast<size_t>(dst_stride * calculated_h), 0);

  double x_scale = static_cast<double>(src.width) / static_cast<double>(out_w);
  double y_scale = static_cast<double>(src.height) / static_cast<double>(calculated_h);

  for (int dy = 0; dy < calculated_h; ++dy) {
    int sy_start = static_cast<int>(dy * y_scale);
    int sy_end = static_cast<int>((dy + 1) * y_scale);
    if (sy_end <= sy_start) sy_end = sy_start + 1;
    if (sy_end > src.height) sy_end = src.height;

    uint8_t* dst_row = out.data() + dy * dst_stride;

    for (int dx = 0; dx < out_w; ++dx) {
      int sx_start = static_cast<int>(dx * x_scale);
      int sx_end = static_cast<int>((dx + 1) * x_scale);
      if (sx_end <= sx_start) sx_end = sx_start + 1;
      if (sx_end > src.width) sx_end = src.width;

      uint64_t sum_b = 0;
      uint64_t sum_g = 0;
      uint64_t sum_r = 0;
      uint64_t sum_a = 0;
      uint64_t count = 0;

      for (int sy = sy_start; sy < sy_end; ++sy) {
        const uint8_t* row_ptr = src.bgra.data() + sy * src.stride;
        for (int sx = sx_start; sx < sx_end; ++sx) {
          const uint8_t* px = row_ptr + sx * 4;
          sum_b += px[0];
          sum_g += px[1];
          sum_r += px[2];
          sum_a += px[3];
          count++;
        }
      }

      if (count > 0) {
        dst_row[dx * 4 + 0] = static_cast<uint8_t>((sum_b + count / 2) / count);
        dst_row[dx * 4 + 1] = static_cast<uint8_t>((sum_g + count / 2) / count);
        dst_row[dx * 4 + 2] = static_cast<uint8_t>((sum_r + count / 2) / count);
        dst_row[dx * 4 + 3] = static_cast<uint8_t>((sum_a + count / 2) / count);
      }
    }
  }

  return out;
}

}  // namespace xg
