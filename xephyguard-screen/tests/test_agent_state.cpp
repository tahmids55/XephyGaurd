#include "../third_party/doctest.h"
#include "../agent/agent_state.h"
#include "../capture/fake_capture.h"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <chrono>
#include <thread>
#include <vector>

TEST_CASE("AgentStateMachine: Start, receive 10 frames, stop, confirm thread terminated") {
  auto capture = std::make_shared<xg::FakeCapture>(1280, 720);
  std::atomic<int> frames_received{0};
  std::vector<xg::Message> messages;
  std::mutex msg_mutex;

  xg::EncoderConfig cfg;
  cfg.codec = xg::Codec::Webp;
  cfg.quality_text = 85;

  xg::AgentStateMachine agent(capture, cfg, nullptr);
  agent.set_frame_sender([&](const std::vector<uint8_t>& data) {
    auto msg = xg::decode(data.data(), data.size());
    if (msg.has_value()) {
      std::lock_guard<std::mutex> lock(msg_mutex);
      messages.push_back(*msg);
      frames_received++;

      // Simulate viewer sending ACK
      if (msg->seq > 0) {
        xg::Message ack{};
        ack.type = xg::MsgType::Ack;
        ack.seq = msg->seq;
        ack.stream_id = msg->stream_id;
        auto ack_bytes = xg::encode(ack);
        agent.on_incoming_message(ack_bytes.data(), ack_bytes.size());
      }
    }
  });

  CHECK(agent.state() == xg::AgentState::Idle);
  CHECK_FALSE(agent.is_thread_running());

  // Start view
  REQUIRE(agent.on_view_start(101, "live"));
  CHECK(agent.state() == xg::AgentState::Streaming);
  CHECK(agent.is_thread_running());

  // Wait until at least 10 frames are received
  auto start_time = std::chrono::steady_clock::now();
  while (frames_received.load() < 10) {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - start_time);
    if (elapsed.count() > 5) break;
  }

  CHECK(frames_received.load() >= 10);

  // Stop view
  REQUIRE(agent.on_view_stop());
  CHECK(agent.state() == xg::AgentState::Idle);
  CHECK_FALSE(agent.is_thread_running());

  // First frame was keyframe
  std::lock_guard<std::mutex> lock(msg_mutex);
  REQUIRE_FALSE(messages.empty());
  CHECK((messages[0].flags & xg::Flags::KEY) != 0);
  CHECK(messages[0].base_seq == 0);
}

TEST_CASE("AgentStateMachine: Snapshot mode sends exactly one frame then transitions to Armed") {
  auto capture = std::make_shared<xg::FakeCapture>(1280, 720);
  std::atomic<int> frames_received{0};

  xg::AgentStateMachine agent(capture, xg::EncoderConfig{}, [&](const std::vector<uint8_t>&) {
    frames_received++;
  });

  REQUIRE(agent.on_view_start(202, "snapshot"));

  // Wait for snapshot completion
  for (int i = 0; i < 50; ++i) {
    if (frames_received.load() >= 1) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  CHECK(frames_received.load() == 1);
  CHECK(agent.state() == xg::AgentState::Armed);
  CHECK_FALSE(agent.is_thread_running());

  agent.on_view_stop();
}

TEST_CASE("AgentStateMachine: Privacy mask and Secure Desktop state transitions") {
  auto capture = std::make_shared<xg::FakeCapture>(1280, 720);
  std::atomic<bool> is_masked{false};
  std::atomic<int> masked_placeholders{0};
  std::atomic<int> secure_placeholders{0};

  xg::AgentStateMachine agent(capture, xg::EncoderConfig{}, nullptr);
  agent.set_frame_sender([&](const std::vector<uint8_t>& data) {
    auto msg = xg::decode(data.data(), data.size());
    if (msg.has_value()) {
      if (msg->flags & xg::Flags::MASKED) masked_placeholders++;
      if (msg->flags & xg::Flags::SECURE) secure_placeholders++;

      // Simulate ACK
      if (msg->seq > 0) {
        xg::Message ack{};
        ack.type = xg::MsgType::Ack;
        ack.seq = msg->seq;
        ack.stream_id = msg->stream_id;
        auto ack_bytes = xg::encode(ack);
        agent.on_incoming_message(ack_bytes.data(), ack_bytes.size());
      }
    }
  });

  agent.set_privacy_checker([&]() {
    return is_masked.load();
  });

  REQUIRE(agent.on_view_start(303, "live"));
  CHECK(agent.state() == xg::AgentState::Streaming);

  // Poll instead of a fixed sleep: under load the capture thread can take longer than 100 ms to notice.
  auto wait_state = [&](xg::AgentState want) {
    for (int i = 0; i < 200 && agent.state() != want; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(25));
  };

  // Trigger privacy mask
  is_masked.store(true);
  wait_state(xg::AgentState::PausedMasked);

  CHECK(agent.state() == xg::AgentState::PausedMasked);
  CHECK(masked_placeholders.load() > 0);

  // Unmask
  is_masked.store(false);
  wait_state(xg::AgentState::Streaming);

  CHECK(agent.state() == xg::AgentState::Streaming);

  // Trigger Secure Desktop
  capture->trigger_desktop_switch(true);
  wait_state(xg::AgentState::PausedSecure);

  CHECK(agent.state() == xg::AgentState::PausedSecure);
  CHECK(secure_placeholders.load() > 0);

  // Switch back
  capture->trigger_desktop_switch(false);
  for (int i = 0; i < 20; ++i) {
    if (agent.state() == xg::AgentState::Streaming) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
  }

  CHECK(agent.state() == xg::AgentState::Streaming);

  agent.on_view_stop();
}

TEST_CASE("AgentStateMachine: Network disconnection and reconnection") {
  auto capture = std::make_shared<xg::FakeCapture>(1280, 720);
  xg::AgentStateMachine agent(capture);

  REQUIRE(agent.on_view_start(404, "live"));
  CHECK(agent.state() == xg::AgentState::Streaming);

  agent.on_network_disconnected();
  CHECK(agent.state() == xg::AgentState::PausedDisconnected);

  agent.on_network_connected();
  CHECK(agent.state() == xg::AgentState::Streaming);

  agent.on_view_stop();
}

TEST_CASE("AgentStateMachine: callers are not starved while the screen is masked") {
  auto capture = std::make_shared<xg::FakeCapture>(640, 480);
  xg::AgentStateMachine agent(capture, xg::EncoderConfig{}, nullptr);
  agent.set_privacy_checker([] { return true; });
  REQUIRE(agent.on_view_start(505, "live"));
  for (int i = 0; i < 200 && agent.state() != xg::AgentState::PausedMasked; ++i)
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
  REQUIRE(agent.state() == xg::AgentState::PausedMasked);

  // The capture loop used to sleep while holding the state mutex, so these could wait many seconds.
  const auto t0 = std::chrono::steady_clock::now();
  for (int i = 0; i < 20; ++i) {
    agent.set_privacy_filter(nullptr);
    agent.set_privacy_checker([] { return true; });
    (void)agent.state();
  }
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
  CHECK(ms < 1500);
  agent.on_view_stop();
}

namespace {
// A capture of a screen that never changes: the first capture is Ok, then NoChange until the machine asks for the
// whole screen. wait_for_change blocks like the X11 one (until its timeout or interrupt_wait).
class StaticScreenCapture : public xg::ScreenCapture {
public:
  bool initialize() override { return true; }
  bool start() override { first_ = true; return true; }
  void stop() override {}
  xg::CaptureStatus capture(xg::Frame& out) override {
    captures++;
    if (!first_ && !full_) return xg::CaptureStatus::NoChange;
    first_ = full_ = false;
    out.width = 128; out.height = 128; out.stride = 512;
    out.bgra.assign(size_t(512) * 128, 0xFF);
    out.dirty.clear();
    out.full_dirty = true;
    out.secure_desktop = false;
    return xg::CaptureStatus::Ok;
  }
  bool wait_for_change(int timeout_ms) override {
    waits++;
    std::unique_lock<std::mutex> lk(m_);
    cv_.wait_for(lk, std::chrono::milliseconds(timeout_ms), [&] { return woken_; });
    const bool w = woken_;
    woken_ = false;
    return w;
  }
  void interrupt_wait() override { { std::lock_guard<std::mutex> lk(m_); woken_ = true; } cv_.notify_all(); }
  void request_full() override { full_ = true; }
  std::atomic<int> captures{0}, waits{0};

private:
  bool first_ = true, full_ = false, woken_ = false;
  std::mutex m_;
  std::condition_variable cv_;
};
}  // namespace

TEST_CASE("AgentStateMachine: a static screen blocks in wait_for_change and a keyframe request still gets a picture") {
  auto capture = std::make_shared<StaticScreenCapture>();
  std::atomic<int> frames{0}, keyframes{0};
  xg::AgentStateMachine agent(capture, xg::EncoderConfig{}, nullptr);
  agent.set_frame_sender([&](const std::vector<uint8_t>& data) {
    auto msg = xg::decode(data.data(), data.size());
    if (!msg.has_value() || msg->type != xg::MsgType::Frame) return;
    ++frames;
    if (msg->flags & xg::Flags::KEY) ++keyframes;
    xg::Message ack{};
    ack.type = xg::MsgType::Ack; ack.seq = msg->seq; ack.stream_id = msg->stream_id;
    auto b = xg::encode(ack);
    agent.on_incoming_message(b.data(), b.size());
  });
  REQUIRE(agent.on_view_start(5, "live"));
  std::this_thread::sleep_for(std::chrono::milliseconds(1200));
  CHECK(frames.load() == 1);          // the first picture only: nothing changed since
  CHECK(capture->captures.load() < 15);  // the old 20 ms sleep would have captured about 60 times
  CHECK(capture->waits.load() >= 1);

  xg::Message req{};
  req.type = xg::MsgType::KeyframeReq; req.stream_id = 5;
  auto rb = xg::encode(req);
  agent.on_incoming_message(rb.data(), rb.size());
  for (int i = 0; i < 100 && frames.load() < 2; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
  CHECK(frames.load() == 2);
  CHECK(keyframes.load() == 2);

  const auto t0 = std::chrono::steady_clock::now();
  REQUIRE(agent.on_view_stop());      // interrupt_wait ends the blocked wait at once
  CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(200));
}

TEST_CASE("AgentStateMachine: wall mode sends a static screen's thumbnail once, then 28-byte unchanged messages") {
  auto capture = std::make_shared<StaticScreenCapture>();
  std::atomic<int> keyframes{0}, unchanged{0}, other{0};
  xg::AgentStateMachine agent(capture, xg::EncoderConfig{}, nullptr);
  agent.set_wall_interval_ms(50);
  agent.set_frame_sender([&](const std::vector<uint8_t>& data) {
    auto msg = xg::decode(data.data(), data.size());
    if (!msg.has_value() || msg->type != xg::MsgType::Frame) return;
    if (msg->flags & xg::Flags::KEY) ++keyframes;
    else if (msg->flags == 0 && msg->tiles.empty() && data.size() == xg::kHeaderSize) ++unchanged;
    else ++other;
  });
  REQUIRE(agent.on_view_start(6, "wall"));
  std::this_thread::sleep_for(std::chrono::milliseconds(400));
  CHECK(keyframes.load() == 1);
  CHECK(unchanged.load() >= 3);
  CHECK(other.load() == 0);

  xg::Message req{};
  req.type = xg::MsgType::KeyframeReq; req.stream_id = 6;
  auto rb = xg::encode(req);
  agent.on_incoming_message(rb.data(), rb.size());  // a new wall viewer needs the picture again
  for (int i = 0; i < 100 && keyframes.load() < 2; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
  CHECK(keyframes.load() == 2);

  agent.set_wall_interval_ms(60000);
  std::this_thread::sleep_for(std::chrono::milliseconds(100));  // now inside a long pause
  const auto t0 = std::chrono::steady_clock::now();
  REQUIRE(agent.on_view_stop());  // the pause ends at once
  CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(200));
}

TEST_CASE("AgentStateMachine: a secure-desktop pause sends its placeholder once, again only for a keyframe request") {
  auto capture = std::make_shared<xg::FakeCapture>(128, 128);
  std::atomic<int> secure{0};
  xg::AgentStateMachine agent(capture, xg::EncoderConfig{}, nullptr);
  agent.set_frame_sender([&](const std::vector<uint8_t>& data) {
    auto msg = xg::decode(data.data(), data.size());
    if (msg.has_value() && msg->type == xg::MsgType::Frame && (msg->flags & xg::Flags::SECURE)) ++secure;
  });
  REQUIRE(agent.on_view_start(7, "live"));  // initialize() clears the fake's secure flag: switch after it
  capture->trigger_desktop_switch(true);
  std::this_thread::sleep_for(std::chrono::milliseconds(600));
  CHECK(agent.state() == xg::AgentState::PausedSecure);
  CHECK(secure.load() == 1);  // was one every 100 ms

  xg::Message req{};
  req.type = xg::MsgType::KeyframeReq; req.stream_id = 7;
  auto rb = xg::encode(req);
  agent.on_incoming_message(rb.data(), rb.size());
  CHECK(secure.load() == 2);  // a new viewer sees the banner too
  const auto t0 = std::chrono::steady_clock::now();
  REQUIRE(agent.on_view_stop());
  CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(200));
}

TEST_CASE("AgentStateMachine: an idle live view gets the second encoder worker from the governor") {
  auto capture = std::make_shared<StaticScreenCapture>();
  xg::AgentStateMachine agent(capture, xg::EncoderConfig{}, nullptr);
  agent.set_frame_sender([&](const std::vector<uint8_t>&) {});
  CHECK(agent.stream_stats().workers == 1);
  REQUIRE(agent.on_view_start(7, "live"));
  int workers = 0;
  for (int i = 0; i < 400 && workers != 2; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    workers = agent.stream_stats().workers;
  }
  CHECK(workers == 2);  // near 0 % CPU, not throttled
  REQUIRE(agent.on_view_stop());
}

TEST_CASE("AgentStateMachine: the encode thread keeps the delta chain ordered (seq +1, base_seq = previous seq)") {
  auto capture = std::make_shared<xg::FakeCapture>(640, 360);
  std::mutex m;
  std::vector<xg::Message> frames;
  xg::AgentStateMachine agent(capture, xg::EncoderConfig{}, nullptr);
  agent.set_frame_sender([&](const std::vector<uint8_t>& data) {
    auto msg = xg::decode(data.data(), data.size());
    if (!msg.has_value() || msg->type != xg::MsgType::Frame) return;
    { std::lock_guard<std::mutex> lock(m); frames.push_back(*msg); }
    xg::Message ack{};
    ack.type = xg::MsgType::Ack; ack.seq = msg->seq; ack.stream_id = msg->stream_id;
    auto b = xg::encode(ack);
    agent.on_incoming_message(b.data(), b.size());
  });
  REQUIRE(agent.on_view_start(11, "live"));
  for (int i = 0; i < 300; ++i) {
    { std::lock_guard<std::mutex> lock(m); if (frames.size() >= 8) break; }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  REQUIRE(agent.on_view_stop());
  std::lock_guard<std::mutex> lock(m);
  REQUIRE(frames.size() >= 8);
  CHECK((frames[0].flags & xg::Flags::KEY) != 0);
  for (size_t i = 1; i < frames.size(); ++i) {
    CHECK(frames[i].seq == frames[i - 1].seq + 1);
    if (!(frames[i].flags & xg::Flags::KEY)) CHECK(frames[i].base_seq == frames[i - 1].seq);
  }
}

TEST_CASE("AgentStateMachine: no picture is sent once the mask is on, even if one was being encoded") {
  auto capture = std::make_shared<xg::FakeCapture>(1280, 720);
  std::atomic<bool> masked{false};
  std::atomic<int> pictures_while_masked{0}, pictures{0};
  xg::AgentStateMachine agent(capture, xg::EncoderConfig{}, nullptr);
  agent.set_privacy_checker([&] { return masked.load(); });
  agent.set_frame_sender([&](const std::vector<uint8_t>& data) {
    auto msg = xg::decode(data.data(), data.size());
    if (!msg.has_value() || msg->type != xg::MsgType::Frame) return;
    const bool placeholder = (msg->flags & (xg::Flags::MASKED | xg::Flags::SECURE)) != 0;
    if (!placeholder) {
      ++pictures;
      if (masked.load()) ++pictures_while_masked;
    }
    xg::Message ack{};
    ack.type = xg::MsgType::Ack; ack.seq = msg->seq; ack.stream_id = msg->stream_id;
    auto b = xg::encode(ack);
    agent.on_incoming_message(b.data(), b.size());
  });
  REQUIRE(agent.on_view_start(12, "live"));
  for (int i = 0; i < 200 && pictures.load() < 3; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
  masked.store(true);
  std::this_thread::sleep_for(std::chrono::milliseconds(600));
  CHECK(pictures_while_masked.load() == 0);
  REQUIRE(agent.on_view_stop());
}
