#include "tiles.h"
#include "protocol.h"
#include <algorithm>
#include <unordered_set>

#define XXH_INLINE_ALL
#include "../third_party/xxhash.h"

namespace xg {

TileTracker::TileTracker(int width, int height)
    : width_(width), height_(height) {
  cols_ = (width + kTile - 1) / kTile;
  rows_ = (height + kTile - 1) / kTile;
  tile_hashes_.assign(static_cast<size_t>(cols_ * rows_), 0);
}

std::vector<uint32_t> TileTracker::update(const Frame& f) {
  // If frame dimensions changed, reconfigure
  if (f.width != width_ || f.height != height_) {
    width_ = f.width;
    height_ = f.height;
    cols_ = (width_ + kTile - 1) / kTile;
    rows_ = (height_ + kTile - 1) / kTile;
    tile_hashes_.assign(static_cast<size_t>(cols_ * rows_), 0);
    pending_indices_.clear();
    first_frame_ = true;
  }

  std::vector<uint32_t> changed;
  if (cols_ == 0 || rows_ == 0 || f.bgra.empty()) {
    return changed;
  }

  std::unordered_set<uint32_t> to_check;
  if (f.full_dirty || first_frame_) {
    const uint32_t total = static_cast<uint32_t>(cols_ * rows_);
    for (uint32_t i = 0; i < total; ++i) {
      to_check.insert(i);
    }
  } else {
    for (const auto& r : f.dirty) {
      if (r.w <= 0 || r.h <= 0) continue;
      int min_tx = std::max(0, r.x / kTile);
      int max_tx = std::min(cols_ - 1, (r.x + r.w - 1) / kTile);
      int min_ty = std::max(0, r.y / kTile);
      int max_ty = std::min(rows_ - 1, (r.y + r.h - 1) / kTile);

      for (int ty = min_ty; ty <= max_ty; ++ty) {
        for (int tx = min_tx; tx <= max_tx; ++tx) {
          to_check.insert(static_cast<uint32_t>(ty * cols_ + tx));
        }
      }
    }
  }

  if (to_check.empty()) {
    return changed;
  }

  XXH64_state_t* state = XXH64_createState();

  std::unordered_set<uint32_t> pending_set(pending_indices_.begin(), pending_indices_.end());

  for (uint32_t idx : to_check) {
    int tx = static_cast<int>(idx % cols_);
    int ty = static_cast<int>(idx / cols_);

    int start_x = tx * kTile;
    int start_y = ty * kTile;
    int tile_w = std::min(kTile, width_ - start_x);
    int tile_h = std::min(kTile, height_ - start_y);

    if (tile_w <= 0 || tile_h <= 0) continue;

    XXH64_reset(state, 0);
    for (int y = 0; y < tile_h; ++y) {
      const size_t offset = static_cast<size_t>((start_y + y) * f.stride + start_x * 4);
      if (offset + static_cast<size_t>(tile_w * 4) <= f.bgra.size()) {
        XXH64_update(state, f.bgra.data() + offset, static_cast<size_t>(tile_w * 4));
      }
    }
    uint64_t h = XXH64_digest(state);

    if (first_frame_ || h != tile_hashes_[idx]) {
      tile_hashes_[idx] = h;
      changed.push_back(idx);
      if (pending_set.insert(idx).second) {
        pending_indices_.push_back(idx);
      }
    }
  }

  XXH64_freeState(state);
  first_frame_ = false;

  std::sort(changed.begin(), changed.end());
  return changed;
}

const std::vector<uint32_t>& TileTracker::pending() const {
  return pending_indices_;
}

void TileTracker::mark_sent() {
  pending_indices_.clear();
}

void TileTracker::force_all_pending() {
  pending_indices_.clear();
  const uint32_t total = static_cast<uint32_t>(cols_ * rows_);
  pending_indices_.reserve(total);
  for (uint32_t i = 0; i < total; ++i) {
    pending_indices_.push_back(i);
  }
}

int TileTracker::cols() const {
  return cols_;
}

int TileTracker::rows() const {
  return rows_;
}

}  // namespace xg
