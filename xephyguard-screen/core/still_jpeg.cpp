#include "still_jpeg.h"

#include <algorithm>
#include <turbojpeg.h>

namespace xg {

std::string encode_still_jpeg(const uint8_t* bgra, int width, int height, int stride, int quality) {
  constexpr int kMaxSide = 16384;
  if (bgra == nullptr || width <= 0 || height <= 0 || width > kMaxSide || height > kMaxSide || stride < width * 4)
    return {};
  tjhandle tj = tjInitCompress();
  if (tj == nullptr) return {};
  unsigned char* out = nullptr;  // TurboJPEG allocates; freed below
  unsigned long out_size = 0;
  std::string jpeg;
  if (tjCompress2(tj, bgra, width, stride, height, TJPF_BGRA, &out, &out_size, TJSAMP_420,
                  std::clamp(quality, 1, 100), TJFLAG_FASTDCT) == 0 && out != nullptr && out_size > 0) {
    jpeg.assign(reinterpret_cast<const char*>(out), out_size);
  }
  tjFree(out);
  tjDestroy(tj);
  return jpeg;
}

}  // namespace xg
