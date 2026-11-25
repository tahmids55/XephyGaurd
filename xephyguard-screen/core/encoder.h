#pragma once
#include "protocol.h"
#include "../capture/capture.h"
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

namespace xg {
struct EncoderConfig {
  int quality_text = 85;     // 1..100
  int quality_video = 60;
  Codec codec = Codec::Webp;
  int webp_method = 4;       // 0 (fastest) .. 6 (smallest); 4 is libwebp's default
  double full_frame_threshold = 0.6;  // fraction of tiles that triggers FULL
  int workers = 1;           // threads that encode one frame's rectangles (1 = the calling thread only, max 4)
  bool rectangles = false;   // true: protocol v2, dirty tiles merged into rectangles (max 256x256), one image each
};

class FrameEncoder {
public:
  explicit FrameEncoder(EncoderConfig cfg);
  // Builds a Frame message from the frame and a list of tile indices.
  // Sets FULL flag and sends all tiles if tiles.size()/total >= threshold.
  Message build(const Frame& f, const std::vector<uint32_t>& tile_indices,
                uint32_t stream_id, uint32_t seq, uint32_t base_seq, bool keyframe);
  ~FrameEncoder();
  FrameEncoder(const FrameEncoder&) = delete;
  FrameEncoder& operator=(const FrameEncoder&) = delete;
  void set_quality_scale(double s);  // 0.3..1.0, used by flow control
  // Number of threads that encode one frame (1..4); helper threads start/stop here, at idle priority. The output is
  // byte-identical for every value. Call from the thread that calls build() (not while a build runs).
  void set_workers(int n);
  int workers() const { return cfg_.workers; }
  // CPU seconds used by the helper threads so far (not the calling thread); the CPU governor adds it to its own.
  double worker_cpu_seconds() const { return static_cast<double>(worker_cpu_us_.load()) / 1e6; }

  struct Region { int x, y, w, h; };  // pixels, inside the frame
  // Merges the given tile indices (ty*cols+tx) into rectangles of whole tiles, at most 4x4 tiles (256x256 px),
  // clipped to width x height. Only tiles in the list are covered, none twice. Order: row-major by first tile.
  static std::vector<Region> merge_tiles(const std::vector<uint32_t>& tile_indices, int width, int height);

private:
  struct Ctx;  // per-thread codec state kept between frames (TurboJPEG handle, pixel and output buffers)
  struct Job;
  void encode_region(Ctx& c, const Frame& f, const Region& rg, int eff_q, TileRec& rec) const;
  void run_job(Ctx& c, const Job& j);
  void worker_main(size_t index);
  void stop_workers();

  EncoderConfig cfg_;
  double quality_scale_ = 1.0;
  std::unique_ptr<Ctx> main_ctx_;
  std::vector<std::unique_ptr<Ctx>> helper_ctx_;
  std::vector<std::thread> helpers_;
  std::mutex mu_;
  std::condition_variable work_cv_, done_cv_;
  const Job* job_ = nullptr;  // set while a build waits for its helpers
  uint64_t job_gen_ = 0;
  size_t helpers_busy_ = 0;
  bool quit_ = false;
  std::atomic<size_t> next_{0};
  std::atomic<uint64_t> worker_cpu_us_{0};
};
}
