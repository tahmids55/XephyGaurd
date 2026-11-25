#include "flow.h"
#include <algorithm>

namespace xg {

FlowControl::FlowControl(int max_in_flight, size_t max_in_flight_bytes)
    : max_in_flight_(std::max(1, max_in_flight)), max_bytes_(std::max<size_t>(1, max_in_flight_bytes)) {}

bool FlowControl::full() const {
  return in_flight_.size() >= static_cast<size_t>(max_in_flight_) || in_flight_bytes_ >= max_bytes_;
}

bool FlowControl::can_send(uint64_t now_ms) {
  for (auto it = in_flight_.begin(); it != in_flight_.end();) {
    if (now_ms >= it->second.at_ms + kAckTimeoutMs) {
      in_flight_bytes_ -= it->second.bytes;
      it = in_flight_.erase(it);
    } else {
      ++it;
    }
  }
  return in_flight_.empty() || !full();
}

void FlowControl::on_sent(uint32_t seq, uint64_t now_ms, size_t bytes) {
  auto old = in_flight_.find(seq);
  if (old != in_flight_.end()) in_flight_bytes_ -= old->second.bytes;
  in_flight_[seq] = Sent{now_ms, bytes};
  in_flight_bytes_ += bytes;
  if (full()) {
    quality_scale_ = std::max(0.3, quality_scale_ - 0.1);
    consecutive_clean_acks_ = 0;
  }
}

void FlowControl::on_ack(uint32_t seq, uint64_t now_ms) {
  auto it = in_flight_.find(seq);
  if (it == in_flight_.end()) return;
  const uint64_t rtt = now_ms >= it->second.at_ms ? now_ms - it->second.at_ms : 0;
  if (!has_rtt_sample_) {
    smoothed_rtt_ms_ = static_cast<double>(rtt);
    has_rtt_sample_ = true;
  } else {
    smoothed_rtt_ms_ = 0.2 * static_cast<double>(rtt) + 0.8 * smoothed_rtt_ms_;
  }
  in_flight_bytes_ -= it->second.bytes;
  in_flight_.erase(it);
  if (++consecutive_clean_acks_ >= 5) {
    quality_scale_ = std::min(1.0, quality_scale_ + 0.05);
    consecutive_clean_acks_ = 0;
  }
}

double FlowControl::quality_scale() const { return quality_scale_; }

bool FlowControl::needs_keyframe() const { return needs_keyframe_; }

void FlowControl::reset() {
  in_flight_.clear();
  in_flight_bytes_ = 0;
  needs_keyframe_ = true;
}

void FlowControl::note_keyframe_sent() { needs_keyframe_ = false; }

}  // namespace xg
