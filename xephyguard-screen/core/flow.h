#pragma once
#include <cstddef>
#include <cstdint>
#include <map>

namespace xg {
// Live-stream back-pressure (screen Step 24). No pacing floor here: the frame interval comes from the configured fps
// (the CPU governor in live mode). This only limits what is unacknowledged: at most `max_in_flight` frames and
// `max_in_flight_bytes` bytes, but one frame may always go out when nothing is in flight. Frames unacknowledged for
// kAckTimeoutMs count as lost, so a dropped Ack cannot stall the stream.
class FlowControl {
public:
  static constexpr size_t kDefaultWindowBytes = 2 * 1024 * 1024;
  static constexpr uint64_t kAckTimeoutMs = 2000;

  explicit FlowControl(int max_in_flight = 8, size_t max_in_flight_bytes = kDefaultWindowBytes);
  bool can_send(uint64_t now_ms);                // false while the window is full (expires lost frames first)
  void on_sent(uint32_t seq, uint64_t now_ms, size_t bytes = 0);
  void on_ack(uint32_t seq, uint64_t now_ms);    // updates the RTT estimate
  double smoothed_rtt_ms() const { return smoothed_rtt_ms_; }
  size_t in_flight_bytes() const { return in_flight_bytes_; }
  size_t in_flight() const { return in_flight_.size(); }
  double quality_scale() const;                  // 0.3..1.0, falls when the window fills
  bool needs_keyframe() const;                   // true after reset()
  void reset();                                  // viewer joined / reconnected
  void note_keyframe_sent();

private:
  bool full() const;
  struct Sent { uint64_t at_ms; size_t bytes; };
  int max_in_flight_;
  size_t max_bytes_;
  size_t in_flight_bytes_ = 0;
  double smoothed_rtt_ms_ = 100.0;
  bool has_rtt_sample_ = false;
  double quality_scale_ = 1.0;
  int consecutive_clean_acks_ = 0;
  bool needs_keyframe_ = true;
  std::map<uint32_t, Sent> in_flight_;
};
}
