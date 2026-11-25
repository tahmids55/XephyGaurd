#pragma once
#include "../capture/capture.h"
#include "../core/downscale.h"
#include "../core/encoder.h"
#include "../core/flow.h"
#include "../core/governor.h"
#include "../core/protocol.h"
#include "../core/tiles.h"
#include "privacy.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
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

// What the CPU governor chose for the live stream, and the CPU it measured (numbers only, nothing from the screen).
struct StreamStats {
  double fps_cap = 0, quality_scale = 1, cpu_pct = 0, budget_pct = 0;
  int level = 0;
  int workers = 1;  // encoder threads the governor allows now
};
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

  // Wall mode: time between thumbnails (default 5 s). Tests use a short one.
  void set_wall_interval_ms(int ms);
  // Live mode: the capture + encode thread's CPU budget in percent of one core (default 8, floor 2 fps).
  void set_cpu_budget_pct(double pct);
  StreamStats stream_stats() const;

private:
  void capture_loop();
  void encode_loop();  // live mode: encodes the staged frame while capture_loop() captures the next one
  bool masked_locked(uint64_t now_ms);  // privacy check; caller holds mutex_
  void wall_pause();  // waits wall_interval_ms_ unless stopped or a keyframe request arrives
  void pause_for(int ms);  // waits ms unless stopped or a keyframe request arrives
  std::vector<uint8_t> wall_unchanged_locked() const;

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
  CpuGovernor governor_;
  uint64_t last_frame_ms_ = 0;  // live: when the last frame went out (governor pacing)
  int gov_frames_ = 0;          // live frames sent since the governor's last update

  // Wall mode: the last thumbnail sent, so an unchanged one is answered with a 28-byte header only.
  std::vector<uint8_t> wall_last_;
  bool wall_sent_ = false;  // false = the next thumbnail is sent in full (new viewer, unmask, reconnect)
  bool wall_wake_ = false;
  int wall_interval_ms_ = 5000;
  std::condition_variable_any wake_cv_;

  // Live mode pipeline: capture_loop() stages at most one captured frame (its tiles are already in tracker_); the
  // encode thread takes it, builds and sends it while the capture thread captures the next one. Guarded by mutex_.
  std::optional<Frame> staged_;
  std::vector<uint32_t> refine_;  // tiles last sent below full quality; re-sent sharp when the screen is static and the governor has budget (Step 30)
  std::condition_variable_any slot_cv_;  // staged_ filled or taken, or stop
  std::atomic<uint64_t> enc_cpu_us_{0};  // the encode thread's own CPU time, for the governor

  std::atomic<bool> running_{false};
  std::thread capture_thread_;
  std::thread encode_thread_;
  std::thread cursor_thread_;  // live mode: pointer position up to 30 Hz (Step 34)
  void cursor_loop();
};

}  // namespace xg
