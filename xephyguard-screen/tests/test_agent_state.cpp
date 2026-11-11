#include "../third_party/doctest.h"
#include "../agent/agent_state.h"
#include "../capture/fake_capture.h"
#include <atomic>
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

  // Trigger privacy mask
  is_masked.store(true);
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  CHECK(agent.state() == xg::AgentState::PausedMasked);
  CHECK(masked_placeholders.load() > 0);

  // Unmask
  is_masked.store(false);
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  CHECK(agent.state() == xg::AgentState::Streaming);

  // Trigger Secure Desktop
  capture->trigger_desktop_switch(true);
  std::this_thread::sleep_for(std::chrono::milliseconds(150));

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
