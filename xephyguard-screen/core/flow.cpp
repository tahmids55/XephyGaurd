#include "flow.h"
#include <algorithm>
#include <cmath>

namespace xg {

FlowControl::FlowControl(int max_in_flight)
    : max_in_flight_(max_in_flight),
      smoothed_rtt_ms_(100.0),
      has_rtt_sample_(false),
      last_sent_ms_(0),
      quality_scale_(1.0),
      consecutive_clean_acks_(0),
      needs_keyframe_(true) {}

bool FlowControl::can_send(uint64_t now_ms) const {
  if (in_flight_times_.size() >= static_cast<size_t>(max_in_flight_)) {
    return false;
  }
  if (last_sent_ms_ > 0 && now_ms < last_sent_ms_ + target_interval_ms()) {
    return false;
  }
  return true;
}

void FlowControl::on_sent(uint32_t seq, uint64_t now_ms) {
  in_flight_times_[seq] = now_ms;
  last_sent_ms_ = now_ms;
  if (in_flight_times_.size() >= static_cast<size_t>(max_in_flight_)) {
    quality_scale_ = std::max(0.3, quality_scale_ - 0.1);
    consecutive_clean_acks_ = 0;
  }
}

void FlowControl::on_ack(uint32_t seq, uint64_t now_ms) {
  auto it = in_flight_times_.find(seq);
  if (it != in_flight_times_.end()) {
    uint64_t rtt = (now_ms >= it->second) ? (now_ms - it->second) : 0;
    if (!has_rtt_sample_) {
      smoothed_rtt_ms_ = static_cast<double>(rtt);
      has_rtt_sample_ = true;
    } else {
      smoothed_rtt_ms_ = 0.2 * static_cast<double>(rtt) + 0.8 * smoothed_rtt_ms_;
    }
    in_flight_times_.erase(it);

    ++consecutive_clean_acks_;
    if (consecutive_clean_acks_ >= 5) {
      quality_scale_ = std::min(1.0, quality_scale_ + 0.05);
      consecutive_clean_acks_ = 0;
    }
  }
}

uint64_t FlowControl::target_interval_ms() const {
  double interval = 2.0 * smoothed_rtt_ms_;
  if (interval < 200.0) interval = 200.0;
  if (interval > 1000.0) interval = 1000.0;
  return static_cast<uint64_t>(std::round(interval));
}

double FlowControl::quality_scale() const {
  return quality_scale_;
}

bool FlowControl::needs_keyframe() const {
  return needs_keyframe_;
}

void FlowControl::reset() {
  in_flight_times_.clear();
  needs_keyframe_ = true;
}

void FlowControl::note_keyframe_sent() {
  needs_keyframe_ = false;
}

}  // namespace xg
