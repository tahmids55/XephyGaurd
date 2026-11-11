#pragma once
#include "protocol.h"
#include "../capture/capture.h"

namespace xg {
struct EncoderConfig {
  int quality_text = 85;     // 1..100
  int quality_video = 60;
  Codec codec = Codec::Webp;
  double full_frame_threshold = 0.6;  // fraction of tiles that triggers FULL
};

class FrameEncoder {
public:
  explicit FrameEncoder(EncoderConfig cfg);
  // Builds a Frame message from the frame and a list of tile indices.
  // Sets FULL flag and sends all tiles if tiles.size()/total >= threshold.
  Message build(const Frame& f, const std::vector<uint32_t>& tile_indices,
                uint32_t stream_id, uint32_t seq, uint32_t base_seq, bool keyframe);
  void set_quality_scale(double s);  // 0.3..1.0, used by flow control

private:
  EncoderConfig cfg_;
  double quality_scale_ = 1.0;
};
}
