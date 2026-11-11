#pragma once
#include <cstdint>
#include <map>

namespace xg {
class FlowControl {
public:
  explicit FlowControl(int max_in_flight = 3);
  bool can_send(uint64_t now_ms) const;  // false if in-flight >= max or too soon
  void on_sent(uint32_t seq, uint64_t now_ms);
  void on_ack(uint32_t seq, uint64_t now_ms);   // updates RTT estimate
  uint64_t target_interval_ms() const;          // 200..1000 depending on RTT/pressure
  double quality_scale() const;                 // 0.3..1.0, falls when slow
  bool needs_keyframe() const;                  // true after reset()
  void reset();                                 // viewer joined / reconnected
  void note_keyframe_sent();

private:
  int max_in_flight_ = 3;
  double smoothed_rtt_ms_ = 100.0;
  bool has_rtt_sample_ = false;
  uint64_t last_sent_ms_ = 0;
  double quality_scale_ = 1.0;
  int consecutive_clean_acks_ = 0;
  bool needs_keyframe_ = true;
  std::map<uint32_t, uint64_t> in_flight_times_;
};
}
