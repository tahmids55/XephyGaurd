#pragma once
#include "../capture/capture.h"
#include "../core/downscale.h"
#include "../core/encoder.h"
#include "../core/flow.h"
#include "../core/protocol.h"
#include "../core/tiles.h"
#include "privacy.h"
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace xg {

enum class AgentState {
  Idle,
  Armed,
  Streaming,
  PausedMasked,
  PausedSecure,
  PausedDisconnected
};

using FrameSender = std::function<void(const std::vector<uint8_t>& data)>;
using PrivacyChecker = std::function<bool()>;

class AgentStateMachine {
public:
  AgentStateMachine(std::shared_ptr<ScreenCapture> capture,
                    EncoderConfig enc_cfg = EncoderConfig{},
                    FrameSender sender = nullptr);
  ~AgentStateMachine();

  void set_frame_sender(FrameSender sender);
  void set_privacy_checker(PrivacyChecker checker);
  void set_privacy_filter(std::shared_ptr<PrivacyFilter> filter);

  AgentState state() const;
  const std::string& mode() const;

  // Control messages
  bool on_view_start(uint32_t stream_id, const std::string& mode = "live");
  bool on_view_stop();
  void on_network_disconnected();
  void on_network_connected();
  void on_privacy_mask_changed(bool masked);

  // Separate cursor messaging
  void send_cursor(uint16_t x, uint16_t y, const std::vector<uint8_t>& shape_data = {});

  // Incoming binary protocol messages (ACK, KeyframeReq)
  void on_incoming_message(const uint8_t* data, size_t len);

  // Status inspection
  uint32_t stream_id() const;
  uint32_t current_seq() const;
  bool is_thread_running() const;

private:
  void capture_loop();

  std::shared_ptr<ScreenCapture> capture_;
  EncoderConfig enc_cfg_;
  FrameSender sender_;
  PrivacyChecker privacy_checker_;
  std::shared_ptr<PrivacyFilter> privacy_filter_;

  mutable std::recursive_mutex mutex_;
  AgentState state_ = AgentState::Idle;
  AgentState pre_pause_state_ = AgentState::Idle;
  uint32_t stream_id_ = 0;
  std::string mode_ = "live";

  std::unique_ptr<TileTracker> tracker_;
  std::unique_ptr<FrameEncoder> encoder_;
  std::unique_ptr<FlowControl> flow_;

  uint32_t seq_ = 0;
  uint32_t last_sent_seq_ = 0;

  std::atomic<bool> running_{false};
  std::thread capture_thread_;
};

}  // namespace xg
