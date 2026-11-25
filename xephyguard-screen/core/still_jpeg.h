#pragma once
#include <cstdint>
#include <string>

namespace xg {
/// Encodes one whole BGRA picture as a baseline JPEG (4:2:0, fast DCT). `stride` is in bytes (>= width * 4),
/// `quality` is clamped to 1..100. Empty if the input is invalid (null, non-positive or > 16384 px sides) or the
/// encoder fails. Used for the one-off evidence screenshot on an alert (F123, DECISIONS #32).
std::string encode_still_jpeg(const uint8_t* bgra, int width, int height, int stride, int quality);
}  // namespace xg
