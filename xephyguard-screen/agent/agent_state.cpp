#include "agent_state.h"
#include "thread_priority.h"
#include <algorithm>
#include <chrono>
#include <iterator>

namespace xg {

static uint64_t get_now_ms() {
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

// The monitor list for the viewer's selector (Step 35): geometry and the active monitor only.
static std::vector<uint8_t> monitors_wire(const std::vector<MonitorInfo>& mons, uint16_t active, bool all_view,
                                          uint32_t stream_id) {
  Message m{};
  m.type = MsgType::Monitors;
  m.flags = all_view ? Flags::ALL_SCREENS : 0;
  m.monitor_id = active;
  m.stream_id = stream_id;
  for (const auto& r : mons) {
    if (m.monitors.size() >= kMaxMonitors) break;
    auto u16 = [](int v) { return static_cast<uint16_t>(std::max(0, std::min(v, 65535))); };
    m.monitors.push_back({u16(r.x), u16(r.y), u16(std::max(1, r.w)), u16(std::max(1, r.h))});
  }
  return encode(m);
}

AgentStateMachine::AgentStateMachine(std::shared_ptr<ScreenCapture> capture,
                                     EncoderConfig enc_cfg,
                                     FrameSender sender)
    : capture_(std::move(capture)),
      enc_cfg_(enc_cfg),
      sender_(std::move(sender)) {}

AgentStateMachine::~AgentStateMachine() {
  on_view_stop();
}

void AgentStateMachine::set_frame_sender(FrameSender sender) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  sender_ = std::move(sender);
}

void AgentStateMachine::set_privacy_checker(PrivacyChecker checker) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  privacy_checker_ = std::move(checker);
}

void AgentStateMachine::set_privacy_filter(std::shared_ptr<PrivacyFilter> filter) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  privacy_filter_ = std::move(filter);
}

AgentState AgentStateMachine::state() const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return state_;
}

const std::string& AgentStateMachine::mode() const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return mode_;
}

uint32_t AgentStateMachine::stream_id() const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return stream_id_;
}

uint32_t AgentStateMachine::current_seq() const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return seq_;
}

bool AgentStateMachine::is_thread_running() const {
  return running_.load();
}

void AgentStateMachine::set_wall_interval_ms(int ms) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  wall_interval_ms_ = ms > 0 ? ms : 1;
}

void AgentStateMachine::set_cpu_budget_pct(double pct) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  governor_.set_budget_pct(pct);
}

StreamStats AgentStateMachine::stream_stats() const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return {governor_.fps(), governor_.quality_scale(), governor_.cpu_pct(), governor_.budget_pct(), governor_.level(), governor_.workers()};
}

void AgentStateMachine::wall_pause() {
  int ms = 0;
  {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    ms = wall_interval_ms_;
  }
  pause_for(ms);
}

void AgentStateMachine::pause_for(int ms) {
  std::unique_lock<std::recursive_mutex> lock(mutex_);
  wake_cv_.wait_for(lock, std::chrono::milliseconds(ms), [&] { return !running_.load() || wall_wake_; });
  wall_wake_ = false;
}

std::vector<uint8_t> AgentStateMachine::wall_unchanged_locked() const {
  // A frame header without KEY/FULL/DELTA and without tiles: "the thumbnail you have is still current".
  Message same{};
  same.type = MsgType::Frame;
  same.stream_id = stream_id_;
  same.seq = last_sent_seq_;
  same.base_seq = last_sent_seq_;
  return encode(same);
}

bool AgentStateMachine::on_view_start(uint32_t stream_id, const std::string& mode) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (running_.load()) {
    return false;
  }

  stream_id_ = stream_id;
  mode_ = mode;
  seq_ = 0;
  last_sent_seq_ = 0;
  wall_sent_ = false;
  wall_wake_ = false;
  wall_last_.clear();
  governor_ = CpuGovernor(governor_.budget_pct());
  last_frame_ms_ = 0;
  gov_frames_ = 0;

  if (!capture_ || !capture_->initialize() || !capture_->start()) {
    return false;
  }

  EncoderConfig cfg = enc_cfg_;
  if (mode == "snapshot") {
    cfg.quality_text = 95;
    cfg.full_frame_threshold = 0.0; // Force full frame
  }

  encoder_ = std::make_unique<FrameEncoder>(cfg);
  flow_ = std::make_unique<FlowControl>();
  tracker_.reset();

  staged_.reset();
  enc_cpu_us_.store(0);
  state_ = AgentState::Streaming;
  running_.store(true);

  if (mode == "live") {
    encode_thread_ = std::thread(&AgentStateMachine::encode_loop, this);
    cursor_thread_ = std::thread(&AgentStateMachine::cursor_loop, this);
  }
  capture_thread_ = std::thread(&AgentStateMachine::capture_loop, this);
  return true;
}

bool AgentStateMachine::on_view_stop() {
  running_.store(false);
  if (capture_) capture_->interrupt_wait();  // the capture thread may be blocked waiting for a screen change
  { std::lock_guard<std::recursive_mutex> lock(mutex_); }  // a wall_pause() between its check and its wait sees it
  wake_cv_.notify_all();
  slot_cv_.notify_all();

  if (capture_thread_.joinable()) {
    capture_thread_.join();
  }
  if (encode_thread_.joinable()) {
    encode_thread_.join();
  }
  if (cursor_thread_.joinable()) {
    cursor_thread_.join();
  }
  staged_.reset();

  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (capture_) {
    capture_->stop();
  }
  tracker_.reset();
  encoder_.reset();
  flow_.reset();
  state_ = AgentState::Idle;
  return true;
}

void AgentStateMachine::on_network_disconnected() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (state_ == AgentState::Streaming || state_ == AgentState::PausedMasked ||
      state_ == AgentState::PausedSecure) {
    pre_pause_state_ = state_;
    state_ = AgentState::PausedDisconnected;
  }
}

void AgentStateMachine::on_network_connected() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (state_ == AgentState::PausedDisconnected) {
    state_ = (pre_pause_state_ != AgentState::Idle) ? pre_pause_state_
                                                    : AgentState::Streaming;
    if (flow_) {
      flow_->reset();
    }
    if (tracker_) {
      tracker_->force_all_pending();
    }
    wall_sent_ = false;
  }
}

void AgentStateMachine::on_privacy_mask_changed(bool masked) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (masked) {
    if (state_ == AgentState::Streaming) {
      state_ = AgentState::PausedMasked;
      if (sender_) {
        Message placeholder{};
        placeholder.type = MsgType::Frame;
        placeholder.flags = Flags::MASKED;
        placeholder.stream_id = stream_id_;
        sender_(encode(placeholder));
      }
    }
  } else {
    if (state_ == AgentState::PausedMasked) {
      state_ = AgentState::Streaming;
      if (flow_) {
        flow_->reset();
      }
      if (tracker_) {
        tracker_->force_all_pending();
      }
      wall_sent_ = false;
    }
  }
}

void AgentStateMachine::send_cursor(uint16_t x, uint16_t y, const std::vector<uint8_t>& shape_data) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  // Privacy: the pointer is shown only while the picture is (not while masked, on the secure desktop or paused).
  if (!sender_ || state_ != AgentState::Streaming || masked_locked(get_now_ms())) return;

  Message msg{};
  msg.type = MsgType::Cursor;
  msg.flags = Flags::CURSOR_F;
  msg.stream_id = stream_id_;
  msg.seq = ++seq_;
  msg.width = x;
  msg.height = y;

  if (!shape_data.empty()) {
    TileRec rec;
    rec.x = 0;
    rec.y = 0;
    rec.codec = Codec::Raw;
    rec.payload = shape_data;
    msg.tiles.push_back(std::move(rec));
  }

  sender_(encode(msg));
}

void AgentStateMachine::on_incoming_message(const uint8_t* data, size_t len) {
  auto opt = decode(data, len);
  if (!opt.has_value()) return;

  const auto& msg = *opt;
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!flow_) return;

  uint64_t now_ms = get_now_ms();

  if (msg.type == MsgType::Ack) {
    flow_->on_ack(msg.seq, now_ms);
    if (mode_ != "wall") {  // a live loop waiting for window space continues at once (wall pauses stay)
      wall_wake_ = true;
      wake_cv_.notify_all();
    }
  } else if (msg.type == MsgType::KeyframeReq) {
    // A viewer picked a monitor (1..N) or all screens (0xFFFF); 0 means "keep what is shown". The picture changes
    // size, so the tracker starts over (the next frame is a full one) and tiles queued for the old one are dropped.
    if (msg.monitor_id != 0 && capture_) {
      const uint16_t want = msg.monitor_id == kAllScreens ? 0 : msg.monitor_id;
      if (want != capture_->selected_monitor() && capture_->select_monitor(msg.monitor_id)) {
        staged_.reset();
        refine_.clear();
        tracker_.reset();
      }
    }
    flow_->reset();
    if (tracker_) {
      tracker_->force_all_pending();
    }
    wall_sent_ = false;
    wall_wake_ = true;
    if (sender_ && (state_ == AgentState::PausedMasked || state_ == AgentState::PausedSecure)) {
      Message placeholder{};  // the placeholder went out once on entry; a new viewer needs it too
      placeholder.type = MsgType::Frame;
      placeholder.flags = state_ == AgentState::PausedMasked ? Flags::MASKED : Flags::SECURE;
      placeholder.stream_id = stream_id_;
      sender_(encode(placeholder));
    }
    if (sender_ && capture_) {  // the selector's list goes out before the picture it describes
      const auto mons = capture_->monitors();
      if (mons.size() >= 2)
        sender_(monitors_wire(mons, capture_->selected_monitor(), capture_->all_screens_view(), stream_id_));
    }
    wake_cv_.notify_all();
    if (capture_) capture_->interrupt_wait();  // answer a new viewer now, not at the next wait timeout
  }
}

bool AgentStateMachine::masked_locked(uint64_t now_ms) {
  if (privacy_filter_) return privacy_filter_->check_masked(now_ms);
  if (privacy_checker_) {
    try {
      return privacy_checker_();
    } catch (...) {
      return true;
    }
  }
  return false;
}

void AgentStateMachine::capture_loop() {
  // Never sleep while holding mutex_: std::recursive_mutex is not fair, so the loop would re-take it at once and
  // starve state(), on_incoming_message() and set_privacy_filter() for seconds while masked or paused.
  bool asked_full = false;  // request_full() sent and no frame has come back yet
  set_current_thread_idle_priority();  // capture + encode run only on CPU time nobody else wants (best effort)
  // The budget covers this thread, the encode thread and the encoder's helper threads (worker_cpu_seconds).
  auto gov_cpu_now = [this] {
    return current_thread_cpu_seconds() + static_cast<double>(enc_cpu_us_.load()) / 1e6 +
           (encoder_ ? encoder_->worker_cpu_seconds() : 0.0);
  };
  double gov_cpu0 = gov_cpu_now();
  uint64_t gov_t0 = get_now_ms();
  while (running_.load()) {
    uint64_t now_ms = get_now_ms();
    int pause_ms = 0;

    {
      std::lock_guard<std::recursive_mutex> lock(mutex_);
      if (now_ms - gov_t0 >= 1000) {  // the governor measures this thread's own CPU time once per second
        const double cpu = gov_cpu_now();
        governor_.update(cpu - gov_cpu0, static_cast<double>(now_ms - gov_t0) / 1000.0, gov_frames_);
        gov_frames_ = 0;
        gov_cpu0 = cpu;
        gov_t0 = now_ms;
      }
      if (state_ == AgentState::PausedDisconnected) {
        pause_ms = 50;
      } else {
        const bool masked = masked_locked(now_ms);

        if (masked) {
          if (state_ != AgentState::PausedMasked) {
            state_ = AgentState::PausedMasked;
            if (sender_) {
              Message placeholder{};
              placeholder.type = MsgType::Frame;
              placeholder.flags = Flags::MASKED;
              placeholder.stream_id = stream_id_;
              sender_(encode(placeholder));
            }
          }
          pause_ms = 50;
        } else {
          if (state_ == AgentState::PausedMasked) {
            state_ = AgentState::Streaming;
            if (flow_) flow_->reset();
            if (tracker_) tracker_->force_all_pending();
            wall_sent_ = false;
          }
          if (mode_ != "wall" && flow_ && !flow_->can_send(now_ms)) {
            pause_ms = 100;  // an Ack, a keyframe request or stop ends it early; lost frames expire after 2 s
          } else if (mode_ == "live" && last_frame_ms_ != 0) {
            const uint64_t interval = static_cast<uint64_t>(governor_.min_interval_ms());
            if (now_ms - last_frame_ms_ < interval) pause_ms = static_cast<int>(interval - (now_ms - last_frame_ms_));
          }
        }
      }
    }
    if (pause_ms > 0) {
      pause_for(pause_ms);  // stop and keyframe requests end it early
      continue;
    }

    if (mode_ == "live") {  // one frame may wait for the encoder; capture the next one only after it was taken
      std::unique_lock<std::recursive_mutex> lock(mutex_);
      slot_cv_.wait(lock, [&] { return !running_.load() || !staged_.has_value(); });
      if (!running_.load()) break;
    }

    Frame frame;
    CaptureStatus status = capture_->capture(frame);

    if (status == CaptureStatus::Lost) {
      {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        capture_->stop();
        capture_->initialize();
        capture_->start();
        if (flow_) flow_->reset();
        if (tracker_) tracker_->force_all_pending();
        wall_sent_ = false;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      continue;
    }

    if (status == CaptureStatus::SecureDesktop) {
      {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        const bool entering = state_ != AgentState::PausedSecure;
        state_ = AgentState::PausedSecure;
        wall_sent_ = false;
        if (sender_ && entering) {  // once per pause (was every 100 ms)
          Message placeholder{};
          placeholder.type = MsgType::Frame;
          placeholder.flags = Flags::SECURE;
          placeholder.stream_id = stream_id_;
          sender_(encode(placeholder));
        }
      }
      pause_for(250);  // secure desktop or blanked display: check again 4 times per second, stop ends it
      continue;
    }

    if (status == CaptureStatus::NoChange) {
      // Nothing on screen changed. A viewer that still needs a picture (keyframe request, lifted mask, reconnect)
      // gets one by asking the capture for the whole screen; otherwise block until the screen changes. The wait
      // is bounded so the privacy check at the top of the loop keeps running a few times per second.
      bool need_picture = false;
      std::vector<uint8_t> unchanged;
      {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        if (mode_ == "wall") {
          need_picture = !wall_sent_;
          if (wall_sent_) unchanged = wall_unchanged_locked();
        } else {
          // Progressive refinement (Step 30): tiles sent at reduced quality go out again at full quality, only while
          // nothing else is pending, the stream is not throttled and the window has room.
          if (mode_ == "live" && tracker_ && flow_ && !refine_.empty() && tracker_->pending().empty() &&
              governor_.level() == 0 && flow_->quality_scale() >= 0.999 && flow_->can_send(get_now_ms()))
            tracker_->add_pending(refine_);
          need_picture = !tracker_ || (flow_ && flow_->needs_keyframe()) || !tracker_->pending().empty();
        }
      }
      if (!unchanged.empty()) {  // wall: nothing changed since the last thumbnail, send 28 bytes and wait
        if (sender_) sender_(unchanged);
        wall_pause();
        continue;
      }
      if (need_picture && !asked_full) {
        asked_full = true;
        capture_->request_full();
        continue;
      }
      capture_->wait_for_change(250);
      continue;
    }
    asked_full = false;

    if (status == CaptureStatus::Ok) {
      std::vector<uint8_t> wire_data;
      bool is_snapshot = false;
      bool is_wall = false;

      {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        if (state_ == AgentState::PausedSecure) {
          state_ = AgentState::Streaming;
        }

        if (mode_ == "wall") {
          is_wall = true;
          int out_h = 0;
          auto downscaled = downscale_box(frame, 320, &out_h);

          if (wall_sent_ && downscaled == wall_last_) {
            wire_data = wall_unchanged_locked();  // the screen changed but not the thumbnail: no encode
          } else {
            Frame wall_frame;
            wall_frame.width = 320;
            wall_frame.height = out_h;
            wall_frame.stride = 320 * 4;
            wall_frame.bgra = std::move(downscaled);
            wall_frame.full_dirty = true;

            TileTracker wall_tracker(wall_frame.width, wall_frame.height);
            wall_tracker.update(wall_frame);

            ++seq_;
            // Wall mode: always full keyframes, never deltas
            auto msg = encoder_->build(wall_frame, wall_tracker.pending(), stream_id_, seq_, 0, true);
            wire_data = encode(msg);
            last_sent_seq_ = seq_;
            wall_last_ = std::move(wall_frame.bgra);
            wall_sent_ = true;
          }
        } else {
          if (!tracker_) {
            tracker_ = std::make_unique<TileTracker>(frame.width, frame.height);
          }

          tracker_->update(frame);
          const auto& pending = tracker_->pending();

          if (pending.empty() && !flow_->needs_keyframe()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
          }

          if (mode_ == "live") {  // the encode thread builds and sends it while the next frame is captured
            staged_ = std::move(frame);
            slot_cv_.notify_all();
            continue;
          }

          bool keyframe = flow_->needs_keyframe();
          ++seq_;
          uint32_t base = keyframe ? 0 : last_sent_seq_;

          encoder_->set_quality_scale(std::min(flow_->quality_scale(), 1.0));
          auto msg = encoder_->build(frame, pending, stream_id_, seq_, base, keyframe);
          wire_data = encode(msg);

          tracker_->mark_sent();
          flow_->on_sent(seq_, now_ms, wire_data.size());
          if (keyframe) {
            flow_->note_keyframe_sent();
          }
          last_sent_seq_ = seq_;

          if (mode_ == "snapshot") {
            is_snapshot = true;
          }
        }
      }

      if (sender_ && !wire_data.empty()) {
        sender_(wire_data);
      }

      if (is_snapshot) {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        running_.store(false);
        state_ = AgentState::Armed;
        break;
      }

      if (is_wall) {
        wall_pause();  // one wait per thumbnail period (was 50 wake-ups of 100 ms)
        continue;
      }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(30));
  }
}

void AgentStateMachine::cursor_loop() {
  set_current_thread_idle_priority();
  int last_x = -1, last_y = -1;
  uint64_t last_move_ms = 0;
  while (running_.load()) {
    int x = 0, y = 0;
    if (capture_ && capture_->cursor_position(x, y) && (x != last_x || y != last_y)) {
      send_cursor(static_cast<uint16_t>(x), static_cast<uint16_t>(y));
      last_x = x;
      last_y = y;
      last_move_ms = get_now_ms();
    }
    // 33 ms (30 Hz) while the pointer moves, 100 ms once it has been still for a second.
    const auto wait = std::chrono::milliseconds(get_now_ms() - last_move_ms < 1000 ? 33 : 100);
    std::unique_lock<std::recursive_mutex> lock(mutex_);
    wake_cv_.wait_for(lock, wait, [&] { return !running_.load(); });
  }
}

void AgentStateMachine::encode_loop() {
  set_current_thread_idle_priority();  // same rule as the capture thread
  while (true) {
    Frame frame;
    std::vector<uint32_t> pending;
    uint32_t seq = 0, base = 0;
    bool keyframe = false;
    double scale = 1.0;
    int workers = 1;
    {
      std::unique_lock<std::recursive_mutex> lock(mutex_);
      slot_cv_.wait(lock, [&] { return !running_.load() || staged_.has_value(); });
      if (!running_.load()) return;
      frame = std::move(*staged_);
      staged_.reset();
      slot_cv_.notify_all();  // the capture thread may capture the next frame now
      if (!tracker_ || !flow_ || !encoder_) continue;
      if (state_ != AgentState::Streaming) {  // masked, secure or disconnected since it was captured: send nothing
        flow_->reset();
        tracker_->force_all_pending();
        continue;
      }
      keyframe = flow_->needs_keyframe();
      if (tracker_->pending().empty() && !keyframe) continue;
      pending = tracker_->pending();
      tracker_->mark_sent();
      seq = ++seq_;
      base = keyframe ? 0 : last_sent_seq_;
      last_sent_seq_ = seq;
      if (keyframe) flow_->note_keyframe_sent();
      scale = std::min(flow_->quality_scale(), governor_.quality_scale());
      if (keyframe) refine_.clear();
      if (scale < 0.999) {  // sent blurry: remember for refinement
        refine_.insert(refine_.end(), pending.begin(), pending.end());
        std::sort(refine_.begin(), refine_.end());
        refine_.erase(std::unique(refine_.begin(), refine_.end()), refine_.end());
      } else if (!refine_.empty()) {  // sent at full quality: no longer blurry
        std::vector<uint32_t> sorted = pending, rest;
        std::sort(sorted.begin(), sorted.end());
        std::set_difference(refine_.begin(), refine_.end(), sorted.begin(), sorted.end(), std::back_inserter(rest));
        refine_.swap(rest);
      }
      workers = governor_.workers();
      last_frame_ms_ = get_now_ms();
      ++gov_frames_;
    }

    // No lock while encoding: this is the slow part the capture thread now overlaps.
    encoder_->set_workers(workers);  // only this thread calls build(), as set_workers() requires
    encoder_->set_quality_scale(scale);
    const std::vector<uint8_t> wire_data = encode(encoder_->build(frame, pending, stream_id_, seq, base, keyframe));

    FrameSender sender;
    {
      std::lock_guard<std::recursive_mutex> lock(mutex_);
      const uint64_t now_ms = get_now_ms();
      // Privacy: a mask that began while this frame was encoding must stop it; the picture is dropped and the
      // viewer gets a keyframe after the mask lifts.
      if (!flow_ || !tracker_ || state_ != AgentState::Streaming || masked_locked(now_ms)) {
        if (flow_) flow_->reset();
        if (tracker_) tracker_->force_all_pending();
        continue;
      }
      flow_->on_sent(seq, now_ms, wire_data.size());
      sender = sender_;
    }
    if (sender && !wire_data.empty()) sender(wire_data);
    enc_cpu_us_.store(static_cast<uint64_t>(current_thread_cpu_seconds() * 1e6));
  }
}

}  // namespace xg
