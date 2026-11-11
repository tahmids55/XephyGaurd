#include "../third_party/doctest.h"
#include "../core/encoder.h"
#include <cmath>
#include <turbojpeg.h>
#include <webp/decode.h>

static double calculate_psnr(const uint8_t* src, const uint8_t* dst, int w, int h) {
  double sse = 0.0;
  for (int i = 0; i < w * h; ++i) {
    double b_diff = static_cast<double>(src[i * 4 + 0]) - static_cast<double>(dst[i * 4 + 0]);
    double g_diff = static_cast<double>(src[i * 4 + 1]) - static_cast<double>(dst[i * 4 + 1]);
    double r_diff = static_cast<double>(src[i * 4 + 2]) - static_cast<double>(dst[i * 4 + 2]);
    sse += (b_diff * b_diff) + (g_diff * g_diff) + (r_diff * r_diff);
  }
  double mse = sse / (w * h * 3);
  if (mse <= 1e-10) return 99.0;
  return 10.0 * std::log10((255.0 * 255.0) / mse);
}

TEST_CASE("FrameEncoder: WebP encode/decode PSNR > 30 dB") {
  xg::EncoderConfig cfg;
  cfg.codec = xg::Codec::Webp;
  cfg.quality_text = 85;
  xg::FrameEncoder encoder(cfg);

  xg::Frame f;
  f.width = 64;
  f.height = 64;
  f.stride = 64 * 4;
  f.bgra.resize(static_cast<size_t>(64 * 64 * 4));
  for (int y = 0; y < 64; ++y) {
    for (int x = 0; x < 64; ++x) {
      f.bgra[(y * 64 + x) * 4 + 0] = static_cast<uint8_t>((x * 4) % 256);
      f.bgra[(y * 64 + x) * 4 + 1] = static_cast<uint8_t>((y * 4) % 256);
      f.bgra[(y * 64 + x) * 4 + 2] = static_cast<uint8_t>((x + y) % 256);
      f.bgra[(y * 64 + x) * 4 + 3] = 0xFF;
    }
  }

  auto msg = encoder.build(f, {0}, 1, 1, 0, false);
  REQUIRE(msg.tiles.size() == 1);
  CHECK(msg.tiles[0].codec == xg::Codec::Webp);
  REQUIRE_FALSE(msg.tiles[0].payload.empty());

  int dw = 0, dh = 0;
  uint8_t* decoded = WebPDecodeBGRA(msg.tiles[0].payload.data(), msg.tiles[0].payload.size(), &dw, &dh);
  REQUIRE(decoded != nullptr);
  CHECK(dw == 64);
  CHECK(dh == 64);

  double psnr = calculate_psnr(f.bgra.data(), decoded, 64, 64);
  WebPFree(decoded);

  CHECK(psnr > 30.0);
}

TEST_CASE("FrameEncoder: JPEG encode/decode PSNR > 30 dB") {
  xg::EncoderConfig cfg;
  cfg.codec = xg::Codec::Jpeg;
  cfg.quality_text = 85;
  xg::FrameEncoder encoder(cfg);

  xg::Frame f;
  f.width = 64;
  f.height = 64;
  f.stride = 64 * 4;
  f.bgra.resize(static_cast<size_t>(64 * 64 * 4));
  for (int y = 0; y < 64; ++y) {
    for (int x = 0; x < 64; ++x) {
      f.bgra[(y * 64 + x) * 4 + 0] = static_cast<uint8_t>((x * 3) % 256);
      f.bgra[(y * 64 + x) * 4 + 1] = static_cast<uint8_t>((y * 3) % 256);
      f.bgra[(y * 64 + x) * 4 + 2] = static_cast<uint8_t>(200);
      f.bgra[(y * 64 + x) * 4 + 3] = 0xFF;
    }
  }

  auto msg = encoder.build(f, {0}, 1, 1, 0, false);
  REQUIRE(msg.tiles.size() == 1);
  CHECK(msg.tiles[0].codec == xg::Codec::Jpeg);
  REQUIRE_FALSE(msg.tiles[0].payload.empty());

  tjhandle tj = tjInitDecompress();
  REQUIRE(tj != nullptr);

  std::vector<uint8_t> decoded(64 * 64 * 4);
  int res = tjDecompress2(tj, msg.tiles[0].payload.data(), static_cast<unsigned long>(msg.tiles[0].payload.size()),
                          decoded.data(), 64, 64 * 4, 64, TJPF_BGRA, 0);
  tjDestroy(tj);
  REQUIRE(res == 0);

  double psnr = calculate_psnr(f.bgra.data(), decoded.data(), 64, 64);
  CHECK(psnr > 30.0);
}

TEST_CASE("FrameEncoder: Threshold triggers FULL flag exactly") {
  xg::EncoderConfig cfg;
  cfg.codec = xg::Codec::Webp;
  cfg.full_frame_threshold = 0.5;  // 50%
  xg::FrameEncoder encoder(cfg);

  xg::Frame f;
  f.width = 128;
  f.height = 128;
  f.stride = 128 * 4;
  f.bgra.resize(static_cast<size_t>(128 * 128 * 4), 0);
  // Total tiles = 4

  // Case 1: 1 of 4 (25%) < 50% -> DELTA
  auto m1 = encoder.build(f, {0}, 1, 10, 9, false);
  CHECK((m1.flags & xg::Flags::FULL) == 0);
  CHECK((m1.flags & xg::Flags::DELTA) != 0);
  CHECK(m1.tiles.size() == 1);
  CHECK(m1.base_seq == 9);

  // Case 2: 2 of 4 (50%) == 50% -> FULL
  auto m2 = encoder.build(f, {0, 1}, 1, 11, 10, false);
  CHECK((m2.flags & xg::Flags::FULL) != 0);
  CHECK((m2.flags & xg::Flags::DELTA) == 0);
  CHECK(m2.tiles.size() == 4);  // All tiles sent
  CHECK(m2.base_seq == 0);
}

TEST_CASE("FrameEncoder: Keyframe sets KEY, FULL, and base_seq = 0") {
  xg::EncoderConfig cfg;
  cfg.codec = xg::Codec::Webp;
  xg::FrameEncoder encoder(cfg);

  xg::Frame f;
  f.width = 64;
  f.height = 64;
  f.stride = 64 * 4;
  f.bgra.resize(static_cast<size_t>(64 * 64 * 4), 0xFF);

  auto m = encoder.build(f, {0}, 1, 1, 999, true);
  CHECK((m.flags & xg::Flags::KEY) != 0);
  CHECK((m.flags & xg::Flags::FULL) != 0);
  CHECK(m.base_seq == 0);
  CHECK(m.seq == 1);
}
