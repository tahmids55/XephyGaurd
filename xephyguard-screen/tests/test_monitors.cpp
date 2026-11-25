#include "../third_party/doctest.h"
#include "../agent/agent_state.h"
#include "../capture/fake_capture.h"
#include "../capture/monitors.h"
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

using namespace xg;

TEST_CASE("Monitors message: round trip and malformed input") {
  Message m{};
  m.type = MsgType::Monitors;
  m.flags = Flags::ALL_SCREENS;
  m.monitor_id = 2;
  m.stream_id = 7;
  m.monitors = {{0, 0, 1920, 1080}, {1920, 0, 1280, 1024}};
  auto wire = encode(m);
  REQUIRE(wire.size() == kHeaderSize + 2 * kMonitorRecSize);
  auto d = decode(wire.data(), wire.size());
  REQUIRE(d.has_value());
  CHECK(d->type == MsgType::Monitors);
  CHECK(d->monitor_id == 2);
  CHECK((d->flags & Flags::ALL_SCREENS) != 0);
  REQUIRE(d->monitors.size() == 2);
  CHECK(d->monitors[1].x == 1920);
  CHECK(d->monitors[1].h == 1024);

  auto cut = wire;
  cut.pop_back();
  CHECK_FALSE(decode(cut.data(), cut.size()).has_value());  // length must match the record count exactly
  auto extra = wire;
  extra.push_back(0);
  CHECK_FALSE(decode(extra.data(), extra.size()).has_value());
  auto zero = wire;
  zero[kHeaderSize + 4] = zero[kHeaderSize + 5] = 0;  // w = 0
  CHECK_FALSE(decode(zero.data(), zero.size()).has_value());
  auto v2 = wire;
  v2[2] = kVersion2;
  CHECK_FALSE(decode(v2.data(), v2.size()).has_value());
  Message many = m;
  many.monitors.assign(kMaxMonitors + 1, MonitorRec{0, 0, 10, 10});
  auto big = encode(many);
  CHECK_FALSE(decode(big.data(), big.size()).has_value());
}

TEST_CASE("Protocol: a frame may carry up to kMaxTiles tiles, not more") {
  Message m{};
  m.type = MsgType::Frame;
  m.width = 8192;
  m.height = 8192;
  for (int i = 0; i < 5000; ++i) m.tiles.push_back(TileRec{static_cast<uint16_t>(i % 128), static_cast<uint16_t>(i / 128), Codec::Raw, {0}});
  auto wire = encode(m);
  auto d = decode(wire.data(), wire.size());
  REQUIRE(d.has_value());
  CHECK(d->tiles.size() == 5000);
  // A header that claims more tiles than the limit, or than the bytes can hold, is refused without a big allocation.
  wire[24] = static_cast<uint8_t>((kMaxTiles + 1) & 0xFF);
  wire[25] = static_cast<uint8_t>((kMaxTiles + 1) >> 8);
  CHECK_FALSE(decode(wire.data(), wire.size()).has_value());
  Message tiny{};
  tiny.type = MsgType::Frame;
  auto hdr = encode(tiny);
  hdr[24] = 0x00;
  hdr[25] = 0x20;  // 8192 tiles claimed, none present
  CHECK_FALSE(decode(hdr.data(), hdr.size()).has_value());
}

namespace {
Frame make_frame(int w, int h) {
  Frame f;
  f.width = w;
  f.height = h;
  f.stride = w * 4;
  f.bgra.resize(static_cast<size_t>(w) * h * 4);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      f.bgra[(static_cast<size_t>(y) * w + x) * 4] = static_cast<uint8_t>(x);
      f.bgra[(static_cast<size_t>(y) * w + x) * 4 + 1] = static_cast<uint8_t>(y);
    }
  return f;
}
}  // namespace

TEST_CASE("crop_frame: pixels, dirty rectangles and size follow the monitor") {
  Frame f = make_frame(8, 4);
  f.dirty = {{0, 0, 2, 2}, {3, 1, 4, 2}, {6, 3, 2, 1}};
  REQUIRE(crop_frame(f, {4, 0, 4, 4}));
  CHECK(f.width == 4);
  CHECK(f.height == 4);
  CHECK(f.stride == 16);
  REQUIRE(f.bgra.size() == 4u * 4 * 4);
  CHECK(f.bgra[0] == 4);        // old column 4 is the new column 0
  CHECK(f.bgra[1] == 0);
  CHECK(f.bgra[(3 * 4 + 3) * 4] == 7);
  REQUIRE(f.dirty.size() == 2);  // the first rect lies on the other monitor
  CHECK(f.dirty[0].x == 0);      // {3,1,4,2} clipped to x 4..7 -> {0,1,3,2}
  CHECK(f.dirty[0].w == 3);
  CHECK(f.dirty[1].x == 2);      // {6,3,2,1} -> {2,3,2,1}
  CHECK(f.dirty[1].y == 3);
}

TEST_CASE("crop_frame: outside the frame is refused, no change inside gives an empty dirty list") {
  Frame f = make_frame(8, 4);
  CHECK_FALSE(crop_frame(f, {8, 0, 4, 4}));
  CHECK_FALSE(crop_frame(f, {0, 0, 0, 4}));
  CHECK(f.width == 8);
  f.dirty = {{0, 0, 2, 2}};
  f.full_dirty = false;
  REQUIRE(crop_frame(f, {4, 0, 4, 4}));
  CHECK(f.dirty.empty());
  Frame g = make_frame(8, 4);  // a monitor sticking out of the frame is clamped
  REQUIRE(crop_frame(g, {6, 2, 10, 10}));
  CHECK(g.width == 2);
  CHECK(g.height == 2);
}

namespace {
// Two 640x720 monitors side by side inside the fake 1280x720 desktop.
class TwoMonitors : public FakeCapture {
public:
  TwoMonitors() : FakeCapture(1280, 720) {}
  std::vector<MonitorInfo> monitors() override { return {{0, 0, 640, 720}, {640, 0, 640, 720}}; }
  bool select_monitor(uint16_t id) override {
    if (id == kAllScreens) id = 0;
    else if (id < 1 || id > 2) return false;
    selected_.store(id);
    return true;
  }
  uint16_t selected_monitor() const override { return selected_.load(); }
  CaptureStatus capture(Frame& out) override {
    const CaptureStatus st = FakeCapture::capture(out);
    const uint16_t sel = selected_.load();
    if (st == CaptureStatus::Ok && sel != 0) {
      crop_frame(out, monitors()[sel - 1]);
      out.monitor_id = sel;
      out.full_dirty = true;  // the fake has no damage source: send whole monitors
    }
    return st;
  }
private:
  std::atomic<uint16_t> selected_{0};
};
}  // namespace

TEST_CASE("AgentStateMachine: a KeyframeReq with a monitor id switches the picture and announces the monitors") {
  auto capture = std::make_shared<TwoMonitors>();
  std::mutex mu;
  std::vector<Message> seen;
  AgentStateMachine agent(capture, EncoderConfig{}, nullptr);
  agent.set_frame_sender([&](const std::vector<uint8_t>& data) {
    auto m = decode(data.data(), data.size());
    if (!m) return;
    {
      std::lock_guard<std::mutex> lock(mu);
      seen.push_back(*m);
    }
    if (m->type == MsgType::Frame && m->seq > 0) {
      Message ack{};
      ack.type = MsgType::Ack;
      ack.seq = m->seq;
      ack.stream_id = m->stream_id;
      auto b = encode(ack);
      agent.on_incoming_message(b.data(), b.size());
    }
  });
  auto request = [&](uint16_t monitor) {
    Message k{};
    k.type = MsgType::KeyframeReq;
    k.monitor_id = monitor;
    auto b = encode(k);
    agent.on_incoming_message(b.data(), b.size());
  };
  auto wait_for = [&](auto pred) {
    for (int i = 0; i < 250; ++i) {
      {
        std::lock_guard<std::mutex> lock(mu);
        for (const auto& m : seen)
          if (pred(m)) return true;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return false;
  };
  auto full_frame = [](uint16_t mon, int w) {
    return [=](const Message& m) {
      return m.type == MsgType::Frame && m.monitor_id == mon && m.width == w && (m.flags & (Flags::KEY | Flags::FULL));
    };
  };

  REQUIRE(agent.on_view_start(9, "live"));
  CHECK(wait_for(full_frame(0, 1280)));  // starts on the whole desktop

  request(2);
  CHECK(wait_for(full_frame(2, 640)));
  CHECK(capture->selected_monitor() == 2);
  CHECK(wait_for([](const Message& m) { return m.type == MsgType::Monitors && m.monitor_id == 2 && m.monitors.size() == 2; }));

  request(0);  // 0 = keep
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  CHECK(capture->selected_monitor() == 2);
  request(7);  // not a monitor: ignored
  CHECK(capture->selected_monitor() == 2);

  request(kAllScreens);
  CHECK(capture->selected_monitor() == 0);
  {
    std::lock_guard<std::mutex> lock(mu);
    seen.clear();
  }
  request(kAllScreens);
  CHECK(wait_for(full_frame(0, 1280)));
  agent.on_view_stop();
}
