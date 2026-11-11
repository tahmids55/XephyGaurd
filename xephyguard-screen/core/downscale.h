#pragma once
#include "../capture/capture.h"
#include <cstdint>
#include <vector>

namespace xg {

// Downscales a BGRA frame using an area box filter, preserving aspect ratio.
// Returns raw BGRA bytes (out_w * out_h * 4). If out_h is non-null, writes calculated height.
// If src is invalid or out_w <= 0, returns an empty vector and sets *out_h = 0.
std::vector<uint8_t> downscale_box(const Frame& src, int out_w, int* out_h = nullptr);

}  // namespace xg
