#pragma once
#include "../capture/capture.h"
#include <cstdint>
#include <vector>

namespace xg {
class TileTracker {
public:
  TileTracker(int width, int height);
  // Hash tiles (xxh64) touched by frame.dirty (or all if full_dirty).
  // Returns indices (ty*cols+tx) of tiles whose hash changed since last call.
  // Updates internal stored hashes.
  std::vector<uint32_t> update(const Frame& f);
  // Accumulate: tiles stay "pending" until mark_sent(). Returns all pending.
  const std::vector<uint32_t>& pending() const;
  void mark_sent();            // clears pending
  void force_all_pending();    // for keyframes
  int cols() const; int rows() const;

private:
  int width_ = 0;
  int height_ = 0;
  int cols_ = 0;
  int rows_ = 0;
  std::vector<uint64_t> tile_hashes_;
  std::vector<uint32_t> pending_indices_;
  bool first_frame_ = true;
};
}
