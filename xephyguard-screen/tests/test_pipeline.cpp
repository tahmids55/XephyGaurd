#include "../third_party/doctest.h"
#include "../capture/fake_capture.h"
#include "../core/protocol.h"
#include "../core/tiles.h"
#include "../core/encoder.h"
#include "../core/flow.h"
#include <cmath>
#include <cstring>
#include <fstream>
#include <turbojpeg.h>
#include <webp/decode.h>

namespace {

double compute_psnr(const uint8_t* a, const uint8_t* b, int w, int h) {
  double sse = 0.0;
  for (int i = 0; i < w * h; ++i) {
    double b_diff = static_cast<double>(a[i * 4 + 0]) - static_cast<double>(b[i * 4 + 0]);
    double g_diff = static_cast<double>(a[i * 4 + 1]) - static_cast<double>(b[i * 4 + 1]);
    double r_diff = static_cast<double>(a[i * 4 + 2]) - static_cast<double>(b[i * 4 + 2]);
    sse += (b_diff * b_diff) + (g_diff * g_diff) + (r_diff * r_diff);
  }
  double mse = sse / (w * h * 3);
  if (mse <= 1e-10) return 99.0;
  return 10.0 * std::log10((255.0 * 255.0) / mse);
}

struct ViewerState {
  int width = 0;
  int height = 0;
  uint32_t last_applied_seq = 0;
  std::vector<uint8_t> canvas_bgra;
  bool keyframe_requested = false;

  void apply(const xg::Message& msg) {
    if (msg.flags & xg::Flags::KEY) {
      width = msg.width;
      height = msg.height;
      canvas_bgra.assign(static_cast<size_t>(width * height * 4), 0);
      last_applied_seq = msg.seq;
      keyframe_requested = false;
    } else if (msg.flags & xg::Flags::FULL) {
      width = msg.width;
      height = msg.height;
      if (canvas_bgra.size() != static_cast<size_t>(width * height * 4)) {
        canvas_bgra.resize(static_cast<size_t>(width * height * 4), 0);
      }
      last_applied_seq = msg.seq;
    } else if (msg.flags & xg::Flags::DELTA) {
      if (msg.base_seq != last_applied_seq) {
        keyframe_requested = true;
        return;
      }
      last_applied_seq = msg.seq;
    }

    for (const auto& tile : msg.tiles) {
      int tx = tile.x;
      int ty = tile.y;
      int tile_w = std::min(xg::kTile, width - tx * xg::kTile);
      int tile_h = std::min(xg::kTile, height - ty * xg::kTile);
      if (tile_w <= 0 || tile_h <= 0) continue;

      std::vector<uint8_t> decoded(static_cast<size_t>(tile_w * tile_h * 4), 0);
      if (tile.codec == xg::Codec::Webp) {
        int dw = 0, dh = 0;
        uint8_t* dec = WebPDecodeBGRA(tile.payload.data(), tile.payload.size(), &dw, &dh);
        if (dec) {
          if (dw == tile_w && dh == tile_h) {
            std::memcpy(decoded.data(), dec, decoded.size());
          }
          WebPFree(dec);
        }
      } else if (tile.codec == xg::Codec::Jpeg) {
        tjhandle tj = tjInitDecompress();
        if (tj) {
          tjDecompress2(tj, tile.payload.data(), static_cast<unsigned long>(tile.payload.size()),
                        decoded.data(), tile_w, tile_w * 4, tile_h,
                        TJPF_BGRA, TJFLAG_FASTDCT);
          tjDestroy(tj);
        }
      } else if (tile.codec == xg::Codec::Raw) {
        if (tile.payload.size() >= decoded.size()) {
          std::memcpy(decoded.data(), tile.payload.data(), decoded.size());
        }
      }

      int start_x = tx * xg::kTile;
      int start_y = ty * xg::kTile;
      for (int r = 0; r < tile_h; ++r) {
        int dst_row = start_y + r;
        if (dst_row >= height) break;
        uint8_t* dst = canvas_bgra.data() + (dst_row * width + start_x) * 4;
        const uint8_t* src = decoded.data() + r * tile_w * 4;
        std::memcpy(dst, src, static_cast<size_t>(tile_w * 4));
      }
    }
  }
};

void save_fixture(const std::string& filename, const std::vector<uint8_t>& data) {
  std::vector<std::string> candidates = {
    "xephyguard-screen/tests/fixtures/" + filename,
    "tests/fixtures/" + filename,
    "../tests/fixtures/" + filename
  };
  bool written = false;
  for (const auto& path : candidates) {
    std::ofstream out(path, std::ios::binary);
    if (out.is_open()) {
      out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
      if (out.good()) {
        written = true;
        break;
      }
    }
  }
  REQUIRE(written);
}

}  // namespace

TEST_CASE("Pipeline: End-to-end capture to viewer with PSNR > 30 dB") {
  xg::FakeCapture capture(1280, 720);
  REQUIRE(capture.initialize());
  REQUIRE(capture.start());

  xg::TileTracker tracker(1280, 720);
  xg::EncoderConfig enc_cfg;
  enc_cfg.codec = xg::Codec::Webp;
  enc_cfg.quality_text = 85;
  xg::FrameEncoder encoder(enc_cfg);
  xg::FlowControl flow(3);

  ViewerState viewer;

  uint32_t stream_id = 42;
  uint32_t last_sent_seq = 0;
  uint64_t simulated_time = 1000;

  for (int frame = 0; frame < 10; ++frame) {
    xg::Frame f;
    REQUIRE(capture.capture(f) == xg::CaptureStatus::Ok);

    auto changed_tiles = tracker.update(f);
    const auto& pending = tracker.pending();

    bool is_keyframe = flow.needs_keyframe();
    uint32_t seq = frame + 1;
    uint32_t base_seq = is_keyframe ? 0 : last_sent_seq;

    auto msg = encoder.build(f, pending, stream_id, seq, base_seq, is_keyframe);
    auto wire_bytes = xg::encode(msg);
    REQUIRE_FALSE(wire_bytes.empty());

    auto decoded_msg = xg::decode(wire_bytes.data(), wire_bytes.size());
    REQUIRE(decoded_msg.has_value());

    viewer.apply(*decoded_msg);
    tracker.mark_sent();
    flow.on_sent(seq, simulated_time);
    if (is_keyframe) {
      flow.note_keyframe_sent();
    }
    last_sent_seq = seq;

    // Simulate ack
    flow.on_ack(seq, simulated_time + 40);
    simulated_time += 100;

    double psnr = compute_psnr(f.bgra.data(), viewer.canvas_bgra.data(), f.width, f.height);
    CHECK(psnr >= 30.0);
  }

  capture.stop();
}

TEST_CASE("Pipeline: Intermediate frame drop triggers base_seq mismatch and keyframe recovery") {
  xg::FakeCapture capture(1280, 720);
  REQUIRE(capture.initialize());
  REQUIRE(capture.start());

  xg::TileTracker tracker(1280, 720);
  xg::EncoderConfig enc_cfg;
  enc_cfg.codec = xg::Codec::Webp;
  enc_cfg.quality_text = 85;
  xg::FrameEncoder encoder(enc_cfg);
  xg::FlowControl flow(3);

  ViewerState viewer;
  uint32_t stream_id = 101;
  uint64_t simulated_time = 2000;

  // Frame 1: Keyframe
  xg::Frame f1;
  REQUIRE(capture.capture(f1) == xg::CaptureStatus::Ok);
  tracker.update(f1);
  auto msg1 = encoder.build(f1, tracker.pending(), stream_id, 1, 0, true);
  auto bytes1 = xg::encode(msg1);
  viewer.apply(*xg::decode(bytes1.data(), bytes1.size()));
  tracker.mark_sent();
  flow.on_sent(1, simulated_time);
  flow.note_keyframe_sent();
  flow.on_ack(1, simulated_time + 30);
  CHECK(viewer.last_applied_seq == 1);
  CHECK_FALSE(viewer.keyframe_requested);

  // Frame 2: Delta (DROPPED over network)
  simulated_time += 100;
  xg::Frame f2;
  REQUIRE(capture.capture(f2) == xg::CaptureStatus::Ok);
  tracker.update(f2);
  auto msg2 = encoder.build(f2, tracker.pending(), stream_id, 2, 1, false);
  auto bytes2 = xg::encode(msg2);
  tracker.mark_sent();
  flow.on_sent(2, simulated_time);
  // Do NOT deliver bytes2 to viewer! (Simulate packet drop)

  // Frame 3: Delta (Received by viewer, but base_seq is 2 while last_applied is 1)
  simulated_time += 100;
  xg::Frame f3;
  REQUIRE(capture.capture(f3) == xg::CaptureStatus::Ok);
  tracker.update(f3);
  auto msg3 = encoder.build(f3, tracker.pending(), stream_id, 3, 2, false);
  auto bytes3 = xg::encode(msg3);
  tracker.mark_sent();
  flow.on_sent(3, simulated_time);

  viewer.apply(*xg::decode(bytes3.data(), bytes3.size()));
  // Viewer detects base_seq mismatch: base_seq(2) != last_applied(1)
  CHECK(viewer.keyframe_requested);
  CHECK(viewer.last_applied_seq == 1);

  // Viewer sends KEYFRAME_REQ -> Sender handles it
  flow.reset();
  CHECK(flow.needs_keyframe());
  tracker.force_all_pending();

  // Frame 4: Sender sends recovering Keyframe
  simulated_time += 100;
  xg::Frame f4;
  REQUIRE(capture.capture(f4) == xg::CaptureStatus::Ok);
  tracker.update(f4);
  auto msg4 = encoder.build(f4, tracker.pending(), stream_id, 4, 0, true);
  CHECK((msg4.flags & xg::Flags::KEY) != 0);
  CHECK(msg4.base_seq == 0);

  auto bytes4 = xg::encode(msg4);
  viewer.apply(*xg::decode(bytes4.data(), bytes4.size()));
  tracker.mark_sent();
  flow.on_sent(4, simulated_time);
  flow.note_keyframe_sent();

  // Recovery verified
  CHECK_FALSE(viewer.keyframe_requested);
  CHECK(viewer.last_applied_seq == 4);

  double psnr = compute_psnr(f4.bgra.data(), viewer.canvas_bgra.data(), f4.width, f4.height);
  CHECK(psnr >= 30.0);

  capture.stop();
}

TEST_CASE("Pipeline: Dump fixtures keyframe.bin, delta.bin, full.bin") {
  xg::FakeCapture capture(1280, 720);
  REQUIRE(capture.initialize());
  REQUIRE(capture.start());

  xg::TileTracker tracker(1280, 720);
  xg::EncoderConfig enc_cfg;
  enc_cfg.codec = xg::Codec::Webp;
  enc_cfg.quality_text = 85;
  enc_cfg.full_frame_threshold = 0.6;
  xg::FrameEncoder encoder(enc_cfg);

  // 1. Dump keyframe.bin
  xg::Frame f1;
  REQUIRE(capture.capture(f1) == xg::CaptureStatus::Ok);
  tracker.update(f1);
  auto keyframe_msg = encoder.build(f1, tracker.pending(), 1001, 1, 0, true);
  auto keyframe_bytes = xg::encode(keyframe_msg);
  REQUIRE_FALSE(keyframe_bytes.empty());
  save_fixture("keyframe.bin", keyframe_bytes);
  tracker.mark_sent();

  // 2. Dump delta.bin
  xg::Frame f2;
  REQUIRE(capture.capture(f2) == xg::CaptureStatus::Ok);
  tracker.update(f2);
  auto delta_msg = encoder.build(f2, tracker.pending(), 1001, 2, 1, false);
  CHECK((delta_msg.flags & xg::Flags::DELTA) != 0);
  auto delta_bytes = xg::encode(delta_msg);
  REQUIRE_FALSE(delta_bytes.empty());
  save_fixture("delta.bin", delta_bytes);
  tracker.mark_sent();

  // 3. Dump full.bin (trigger threshold >= 0.6 without KEY flag)
  xg::Frame f3;
  REQUIRE(capture.capture(f3) == xg::CaptureStatus::Ok);
  tracker.update(f3);
  tracker.force_all_pending();  // 100% of tiles > 60% threshold
  auto full_msg = encoder.build(f3, tracker.pending(), 1001, 3, 0, false);
  CHECK((full_msg.flags & xg::Flags::FULL) != 0);
  auto full_bytes = xg::encode(full_msg);
  REQUIRE_FALSE(full_bytes.empty());
  save_fixture("full.bin", full_bytes);

  capture.stop();
}
