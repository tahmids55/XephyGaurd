#include "encoder.h"
#include "thread_priority.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <turbojpeg.h>
#include <webp/encode.h>

namespace xg {

struct FrameEncoder::Ctx {
  tjhandle tj = nullptr;
  std::vector<uint8_t> bgra;  // the rectangle being encoded, opaque alpha
  std::vector<uint8_t> jpeg;  // TurboJPEG output buffer (tjBufSize), grows only
  WebPConfig wc{};
  bool wc_ok = false;
  Ctx() {
    wc_ok = WebPConfigInit(&wc) != 0;
    bgra.reserve(static_cast<size_t>(kMaxRectSide) * kMaxRectSide * 4);
  }
  ~Ctx() { if (tj != nullptr) tjDestroy(tj); }
};

struct FrameEncoder::Job {
  const Frame* frame;
  const std::vector<Region>* regions;
  std::vector<TileRec>* out;
  int eff_q;
};

FrameEncoder::FrameEncoder(EncoderConfig cfg) : cfg_(cfg), main_ctx_(new Ctx) {
  const int n = std::clamp(cfg_.workers, 1, 4);
  cfg_.workers = 1;
  set_workers(n);
}

FrameEncoder::~FrameEncoder() { stop_workers(); }

void FrameEncoder::stop_workers() {
  {
    std::lock_guard<std::mutex> lock(mu_);
    quit_ = true;
  }
  work_cv_.notify_all();
  for (auto& t : helpers_) t.join();
  helpers_.clear();
  helper_ctx_.clear();
  quit_ = false;
}

void FrameEncoder::set_workers(int n) {
  n = std::clamp(n, 1, 4);
  if (n == cfg_.workers) return;
  stop_workers();
  cfg_.workers = n;
  job_gen_ = 0;
  for (int i = 1; i < n; ++i) helper_ctx_.emplace_back(new Ctx);
  for (int i = 1; i < n; ++i) helpers_.emplace_back(&FrameEncoder::worker_main, this, static_cast<size_t>(i - 1));
}

void FrameEncoder::worker_main(size_t index) {
  set_current_thread_idle_priority();  // same rule as the capture thread: only CPU nobody else wants
  uint64_t seen = 0;
  double cpu0 = current_thread_cpu_seconds();
  for (;;) {
    const Job* job = nullptr;
    {
      std::unique_lock<std::mutex> lock(mu_);
      work_cv_.wait(lock, [&] { return quit_ || (job_ != nullptr && job_gen_ != seen); });
      if (quit_) return;
      seen = job_gen_;
      job = job_;
    }
    run_job(*helper_ctx_[index], *job);
    const double cpu = current_thread_cpu_seconds();
    worker_cpu_us_.fetch_add(static_cast<uint64_t>((cpu - cpu0) * 1e6));
    cpu0 = cpu;
    {
      std::lock_guard<std::mutex> lock(mu_);
      --helpers_busy_;
    }
    done_cv_.notify_all();
  }
}

void FrameEncoder::run_job(Ctx& c, const Job& j) {
  for (;;) {
    const size_t i = next_.fetch_add(1);
    if (i >= j.regions->size()) return;
    encode_region(c, *j.frame, (*j.regions)[i], j.eff_q, (*j.out)[i]);
  }
}

void FrameEncoder::set_quality_scale(double s) {
  quality_scale_ = std::clamp(s, 0.3, 1.0);
}

std::vector<FrameEncoder::Region> FrameEncoder::merge_tiles(const std::vector<uint32_t>& tile_indices,
                                                            int width, int height) {
  constexpr int kSpan = kMaxRectSide / kTile;  // tiles per rectangle side
  std::vector<Region> out;
  const int cols = (width + kTile - 1) / kTile;
  const int rows = (height + kTile - 1) / kTile;
  if (cols <= 0 || rows <= 0) return out;
  // 0 = clean, 1 = dirty and not yet covered
  std::vector<uint8_t> grid(static_cast<size_t>(cols) * rows, 0);
  for (uint32_t idx : tile_indices) {
    if (idx < grid.size()) grid[idx] = 1;
  }
  for (int ty = 0; ty < rows; ++ty) {
    for (int tx = 0; tx < cols; ++tx) {
      if (!grid[static_cast<size_t>(ty) * cols + tx]) continue;
      int tw = 1;
      while (tw < kSpan && tx + tw < cols && grid[static_cast<size_t>(ty) * cols + tx + tw]) ++tw;
      int th = 1;
      while (th < kSpan && ty + th < rows) {
        bool whole = true;
        for (int i = 0; i < tw; ++i) {
          if (!grid[static_cast<size_t>(ty + th) * cols + tx + i]) { whole = false; break; }
        }
        if (!whole) break;
        ++th;
      }
      for (int j = 0; j < th; ++j)
        for (int i = 0; i < tw; ++i) grid[static_cast<size_t>(ty + j) * cols + tx + i] = 0;
      Region r;
      r.x = tx * kTile;
      r.y = ty * kTile;
      r.w = std::min(tw * kTile, width - r.x);
      r.h = std::min(th * kTile, height - r.y);
      out.push_back(r);
    }
  }
  return out;
}

Message FrameEncoder::build(const Frame& f, const std::vector<uint32_t>& tile_indices,
                            uint32_t stream_id, uint32_t seq, uint32_t base_seq, bool keyframe) {
  Message m;
  m.version = cfg_.rectangles ? kVersion2 : kVersion;
  m.type = MsgType::Frame;
  m.monitor_id = f.monitor_id;
  m.stream_id = stream_id;
  m.seq = seq;
  m.width = static_cast<uint16_t>(f.width);
  m.height = static_cast<uint16_t>(f.height);

  const int cols = (f.width + kTile - 1) / kTile;
  const int rows = (f.height + kTile - 1) / kTile;
  const uint32_t total_tiles = static_cast<uint32_t>(cols * rows);

  double fraction = total_tiles > 0 ? (static_cast<double>(tile_indices.size()) / total_tiles) : 0.0;
  bool is_full = keyframe || (fraction >= cfg_.full_frame_threshold);

  if (keyframe) {
    m.flags = Flags::KEY | Flags::FULL;
    m.base_seq = 0;
  } else if (is_full) {
    m.flags = Flags::FULL;
    m.base_seq = 0;
  } else {
    m.flags = Flags::DELTA;
    m.base_seq = base_seq;
  }

  if (f.secure_desktop) {
    m.flags |= Flags::SECURE;
  }

  // Determine list of tiles to send
  std::vector<uint32_t> targets;
  if (is_full) {
    targets.reserve(total_tiles);
    for (uint32_t i = 0; i < total_tiles; ++i) {
      targets.push_back(i);
    }
  } else {
    targets = tile_indices;
  }

  int eff_q = static_cast<int>(std::round(cfg_.quality_text * quality_scale_));
  eff_q = std::clamp(eff_q, 10, 100);

  // v1: one region per 64 px tile. v2: dirty tiles merged into rectangles.
  std::vector<Region> regions;
  if (cfg_.rectangles) {
    regions = merge_tiles(targets, f.width, f.height);
  } else {
    regions.reserve(targets.size());
    for (uint32_t idx : targets) {
      Region r;
      r.x = static_cast<int>(idx % cols) * kTile;
      r.y = static_cast<int>(idx / cols) * kTile;
      r.w = std::min(kTile, f.width - r.x);
      r.h = std::min(kTile, f.height - r.y);
      regions.push_back(r);
    }
  }
  regions.erase(std::remove_if(regions.begin(), regions.end(), [](const Region& r) { return r.w <= 0 || r.h <= 0; }),
                regions.end());

  m.tiles.resize(regions.size());  // slot i belongs to region i, whichever thread encodes it
  const Job job{&f, &regions, &m.tiles, eff_q};
  if (helpers_.empty() || regions.size() < 2) {
    for (size_t i = 0; i < regions.size(); ++i) encode_region(*main_ctx_, f, regions[i], eff_q, m.tiles[i]);
  } else {
    next_.store(0);
    {
      std::lock_guard<std::mutex> lock(mu_);
      job_ = &job;
      ++job_gen_;
      helpers_busy_ = helpers_.size();
    }
    work_cv_.notify_all();
    run_job(*main_ctx_, job);  // the calling thread takes its share
    std::unique_lock<std::mutex> lock(mu_);
    done_cv_.wait(lock, [&] { return helpers_busy_ == 0; });
    job_ = nullptr;
  }
  return m;
}

void FrameEncoder::encode_region(Ctx& c, const Frame& f, const Region& rg, int eff_q, TileRec& rec) const {
  const int tile_w = rg.w;
  const int tile_h = rg.h;

  c.bgra.resize(static_cast<size_t>(tile_w * tile_h * 4));
  for (int y = 0; y < tile_h; ++y) {
    const size_t src_off = static_cast<size_t>((rg.y + y) * f.stride + rg.x * 4);
    uint8_t* dst_row = c.bgra.data() + static_cast<size_t>(y * tile_w * 4);
    std::memcpy(dst_row, f.bgra.data() + src_off, static_cast<size_t>(tile_w * 4));
    // Desktop captures (X11 TrueColor, Win32 GDI) often emit 0x00 in byte 3; ensure opaque pixels.
    for (int x = 0; x < tile_w; ++x) {
      dst_row[x * 4 + 3] = 0xFF;
    }
  }

  if (cfg_.rectangles) {
    rec.x = static_cast<uint16_t>(rg.x);
    rec.y = static_cast<uint16_t>(rg.y);
    rec.w = static_cast<uint16_t>(tile_w);
    rec.h = static_cast<uint16_t>(tile_h);
  } else {
    rec.x = static_cast<uint16_t>(rg.x / kTile);
    rec.y = static_cast<uint16_t>(rg.y / kTile);
  }
  rec.codec = cfg_.codec;

  if (cfg_.codec == Codec::Webp) {
    // Advanced API: same as WebPEncodeBGRA (default preset) but with a configurable method (speed/size trade).
    WebPPicture pic;
    if (c.wc_ok && WebPPictureInit(&pic)) {
      c.wc.quality = static_cast<float>(eff_q);
      c.wc.method = std::clamp(cfg_.webp_method, 0, 6);
      pic.width = tile_w;
      pic.height = tile_h;
      WebPMemoryWriter wr;
      WebPMemoryWriterInit(&wr);
      pic.writer = WebPMemoryWrite;
      pic.custom_ptr = &wr;
      if (WebPPictureImportBGRA(&pic, c.bgra.data(), tile_w * 4) && WebPEncode(&c.wc, &pic) && wr.size > 0) {
        rec.payload.assign(wr.mem, wr.mem + wr.size);
      }
      WebPMemoryWriterClear(&wr);
      WebPPictureFree(&pic);
    }
  } else if (cfg_.codec == Codec::Jpeg) {
    if (c.tj == nullptr) c.tj = tjInitCompress();
    const long cap = c.tj != nullptr ? static_cast<long>(tjBufSize(tile_w, tile_h, TJSAMP_420)) : -1;
    if (cap > 0) {
      if (c.jpeg.size() < static_cast<size_t>(cap)) c.jpeg.resize(static_cast<size_t>(cap));
      unsigned char* out = c.jpeg.data();  // NOREALLOC: TurboJPEG writes into our buffer, never reallocates
      unsigned long out_size = static_cast<unsigned long>(c.jpeg.size());
      if (tjCompress2(c.tj, c.bgra.data(), tile_w, tile_w * 4, tile_h, TJPF_BGRA, &out, &out_size, TJSAMP_420,
                      eff_q, TJFLAG_FASTDCT | TJFLAG_NOREALLOC) == 0) {
        rec.payload.assign(out, out + out_size);
      }
    }
  } else {
    rec.codec = Codec::Raw;
    rec.payload = c.bgra;
  }

  // A compressed rectangle must fit the wire limit; the raw one always does (w*h*4 <= 256*256*4).
  if (cfg_.rectangles && rec.payload.size() > kMaxRectPayload) {
    rec.codec = Codec::Raw;
    rec.payload = c.bgra;
  }
}

}  // namespace xg
