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

TEST_CASE("FrameEncoder: WebP encode/decode PSNR > 30 dB at every tested method") {
  xg::EncoderConfig cfg;
  cfg.codec = xg::Codec::Webp;
  cfg.quality_text = 85;
  SUBCASE("method 4 (default)") { cfg.webp_method = 4; }
  SUBCASE("method 0 (fastest)") { cfg.webp_method = 0; }
  SUBCASE("method 1") { cfg.webp_method = 1; }
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

TEST_CASE("FrameEncoder: BGRX input with zero alpha produces opaque WebP and Raw tiles") {
  xg::Frame f;
  f.width = 64;
  f.height = 64;
  f.stride = 64 * 4;
  f.bgra.resize(static_cast<size_t>(64 * 64 * 4));
  for (int i = 0; i < 64 * 64; ++i) {
    f.bgra[i * 4 + 0] = 0x40;
    f.bgra[i * 4 + 1] = 0x80;
    f.bgra[i * 4 + 2] = 0xC0;
    f.bgra[i * 4 + 3] = 0x00;  // Zero padding byte as returned by X11 XShmGetImage / GDI
  }

  xg::EncoderConfig webp_cfg;
  webp_cfg.codec = xg::Codec::Webp;
  webp_cfg.quality_text = 85;
  xg::FrameEncoder webp_enc(webp_cfg);
  auto mw = webp_enc.build(f, {0}, 1, 1, 0, true);
  REQUIRE(mw.tiles.size() == 1);
  int dw = 0, dh = 0;
  uint8_t* decoded = WebPDecodeBGRA(mw.tiles[0].payload.data(), mw.tiles[0].payload.size(), &dw, &dh);
  REQUIRE(decoded != nullptr);
  CHECK(decoded[3] == 0xFF);
  WebPFree(decoded);

  xg::EncoderConfig raw_cfg;
  raw_cfg.codec = xg::Codec::Raw;
  xg::FrameEncoder raw_enc(raw_cfg);
  auto mr = raw_enc.build(f, {0}, 1, 1, 0, true);
  REQUIRE(mr.tiles.size() == 1);
  REQUIRE(mr.tiles[0].payload.size() == 64 * 64 * 4);
  CHECK(mr.tiles[0].payload[3] == 0xFF);
}

#include <random>
#include <set>

TEST_CASE("merge_tiles: covers exactly the given tiles, no overlap, rectangles <= 256x256") {
  const int W = 1000, H = 700;  // 16 x 11 tiles, ragged right and bottom edges
  const int cols = 16, rows = 11;
  std::mt19937 rng(99);
  for (int round = 0; round < 300; ++round) {
    std::vector<uint32_t> dirty;
    const int density = 1 + static_cast<int>(rng() % 100);
    std::set<uint32_t> want;
    for (int i = 0; i < cols * rows; ++i) {
      if (static_cast<int>(rng() % 100) < density) { dirty.push_back(static_cast<uint32_t>(i)); want.insert(static_cast<uint32_t>(i)); }
    }
    auto regions = xg::FrameEncoder::merge_tiles(dirty, W, H);
    std::set<uint32_t> got;
    for (const auto& r : regions) {
      CHECK(r.w >= 1); CHECK(r.h >= 1);
      CHECK(r.w <= 256); CHECK(r.h <= 256);
      CHECK(r.x % xg::kTile == 0); CHECK(r.y % xg::kTile == 0);
      CHECK(r.x + r.w <= W); CHECK(r.y + r.h <= H);
      for (int ty = r.y / 64; ty < (r.y + r.h + 63) / 64; ++ty)
        for (int tx = r.x / 64; tx < (r.x + r.w + 63) / 64; ++tx)
          CHECK(got.insert(static_cast<uint32_t>(ty * cols + tx)).second);  // no tile twice
    }
    CHECK(got == want);
  }
}

TEST_CASE("merge_tiles: a full 1920x1080 frame is 40 rectangles, a lone tile stays one") {
  std::vector<uint32_t> all;
  for (uint32_t i = 0; i < 30 * 17; ++i) all.push_back(i);
  auto r = xg::FrameEncoder::merge_tiles(all, 1920, 1080);
  CHECK(r.size() == 8 * 5);
  CHECK(r[0].w == 256);
  CHECK(r[0].h == 256);
  CHECK(r.back().h == 1080 - 4 * 256);
  auto one = xg::FrameEncoder::merge_tiles({35}, 1920, 1080);  // tile (5,1)
  REQUIRE(one.size() == 1);
  CHECK(one[0].x == 320);
  CHECK(one[0].y == 64);
  CHECK(xg::FrameEncoder::merge_tiles({}, 1920, 1080).empty());
  CHECK(xg::FrameEncoder::merge_tiles({999999}, 1920, 1080).empty());  // out of range ignored
}

TEST_CASE("FrameEncoder: rectangles mode emits v2, one decodable image per merged rectangle") {
  xg::EncoderConfig cfg;
  cfg.rectangles = true;
  cfg.full_frame_threshold = 2.0;  // never FULL, so only the given tiles are sent
  SUBCASE("jpeg") { cfg.codec = xg::Codec::Jpeg; }
  SUBCASE("webp") { cfg.codec = xg::Codec::Webp; }
  SUBCASE("raw") { cfg.codec = xg::Codec::Raw; }
  xg::FrameEncoder encoder(cfg);

  xg::Frame f;
  f.width = 300;  // 5 x 4 tiles, ragged edges
  f.height = 200;
  f.stride = f.width * 4;
  f.bgra.assign(static_cast<size_t>(f.stride) * f.height, 0x80);

  // tiles (0,0)(1,0)(0,1)(1,1) -> one 128x128 rectangle; (4,3) -> the 44x8 corner
  auto msg = encoder.build(f, {0, 1, 5, 6, 19}, 3, 9, 8, false);
  CHECK(msg.version == xg::kVersion2);
  REQUIRE(msg.tiles.size() == 2);
  CHECK(msg.tiles[0].x == 0); CHECK(msg.tiles[0].y == 0);
  CHECK(msg.tiles[0].w == 128); CHECK(msg.tiles[0].h == 128);
  CHECK(msg.tiles[1].x == 256); CHECK(msg.tiles[1].y == 192);
  CHECK(msg.tiles[1].w == 44); CHECK(msg.tiles[1].h == 8);

  auto wire = xg::encode(msg);
  auto back = xg::decode(wire.data(), wire.size());
  REQUIRE(back.has_value());
  REQUIRE(back->tiles.size() == 2);
  for (const auto& t : back->tiles) {
    REQUIRE_FALSE(t.payload.empty());
    if (t.codec == xg::Codec::Webp) {
      int dw = 0, dh = 0;
      uint8_t* px = WebPDecodeBGRA(t.payload.data(), t.payload.size(), &dw, &dh);
      REQUIRE(px != nullptr);
      CHECK(dw == t.w); CHECK(dh == t.h);
      WebPFree(px);
    } else if (t.codec == xg::Codec::Jpeg) {
      tjhandle d = tjInitDecompress();
      int jw = 0, jh = 0, ss = 0, cs = 0;
      REQUIRE(tjDecompressHeader3(d, t.payload.data(), t.payload.size(), &jw, &jh, &ss, &cs) == 0);
      CHECK(jw == t.w); CHECK(jh == t.h);
      tjDestroy(d);
    } else {
      CHECK(t.payload.size() == static_cast<size_t>(t.w) * t.h * 4);
    }
  }
}

TEST_CASE("FrameEncoder: default stays v1 with 64 px tile indices") {
  xg::EncoderConfig cfg;
  xg::FrameEncoder encoder(cfg);
  xg::Frame f;
  f.width = 128; f.height = 128; f.stride = 512;
  f.bgra.assign(512u * 128u, 0x40);
  auto msg = encoder.build(f, {1, 2}, 1, 1, 0, false);
  CHECK(msg.version == xg::kVersion);
  REQUIRE(msg.tiles.size() == 2);
  CHECK(msg.tiles[0].x == 1); CHECK(msg.tiles[0].y == 0);
  CHECK(msg.tiles[1].x == 0); CHECK(msg.tiles[1].y == 1);
  CHECK(msg.tiles[0].w == 0);
}

namespace {
xg::Frame noisy_frame(int w, int h, uint32_t seed) {
  xg::Frame f;
  f.width = w; f.height = h; f.stride = w * 4;
  f.bgra.resize(static_cast<size_t>(f.stride) * h);
  uint32_t s = seed;
  for (size_t i = 0; i < f.bgra.size(); ++i) {
    s = s * 1664525u + 1013904223u;
    f.bgra[i] = static_cast<uint8_t>(((i / 4) % 97) * 2 + ((s >> 24) & 7));
  }
  return f;
}
std::vector<uint32_t> every_other_tile(int w, int h) {
  const int n = ((w + 63) / 64) * ((h + 63) / 64);
  std::vector<uint32_t> v;
  for (int i = 0; i < n; i += 2) v.push_back(static_cast<uint32_t>(i));
  return v;
}
}  // namespace

TEST_CASE("FrameEncoder: worker pool output is byte-identical to one thread, all codecs and both layouts") {
  const xg::Frame f = noisy_frame(700, 450, 7);
  const auto tiles = every_other_tile(f.width, f.height);
  for (xg::Codec codec : {xg::Codec::Jpeg, xg::Codec::Webp, xg::Codec::Raw}) {
    for (bool rects : {false, true}) {
      xg::EncoderConfig cfg;
      cfg.codec = codec;
      cfg.rectangles = rects;
      cfg.webp_method = 1;
      cfg.full_frame_threshold = 2.0;
      xg::FrameEncoder one(cfg);
      const auto want = xg::encode(one.build(f, tiles, 1, 5, 4, false));
      CHECK(one.workers() == 1);
      for (int n : {2, 4}) {
        cfg.workers = n;
        xg::FrameEncoder many(cfg);
        CHECK(many.workers() == n);
        CHECK(xg::encode(many.build(f, tiles, 1, 5, 4, false)) == want);
      }
    }
  }
}

TEST_CASE("FrameEncoder: reused state over many frames, set_workers toggles, worker CPU is counted") {
  xg::EncoderConfig cfg;
  cfg.codec = xg::Codec::Jpeg;
  cfg.rectangles = true;
  xg::FrameEncoder enc(cfg);
  xg::EncoderConfig ref_cfg = cfg;
  const int plan[] = {1, 2, 2, 1, 3, 2};
  for (int i = 0; i < 6; ++i) {
    // sizes change between frames so the reused buffers must grow and shrink correctly
    const xg::Frame f = noisy_frame(256 + 100 * i, 200 + 50 * (i % 3), 20 + i);
    const std::vector<uint32_t> tiles = every_other_tile(f.width, f.height);
    enc.set_workers(plan[i]);
    CHECK(enc.workers() == plan[i]);
    xg::FrameEncoder fresh(ref_cfg);  // a new encoder per frame: nothing reused
    CHECK(xg::encode(enc.build(f, tiles, 2, i + 1, 0, i == 0)) == xg::encode(fresh.build(f, tiles, 2, i + 1, 0, i == 0)));
  }
  enc.set_workers(99);
  CHECK(enc.workers() == 4);
  CHECK(enc.worker_cpu_seconds() >= 0.0);
  enc.set_workers(0);
  CHECK(enc.workers() == 1);
  xg::Frame empty;
  CHECK(enc.build(empty, {}, 1, 1, 0, false).tiles.empty());
}
