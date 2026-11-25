// Streamer benchmark (screen Step 20, part a): the real AgentStateMachine loop, tile diff, encoder and flow
// control on synthetic 1080p workloads. The capture is synthetic and behaves like the current X11 capture
// (full copy per call, full_dirty = true), so its memcpy cost is included, as a real capture's would be.
// An immediate ACK stands in for the viewer, unless --stdio is given: then every wire message leaves on stdout
// as `u32 len | u64 capture time (unix ns) | wire` and messages from the gateway come in on stdin as
// `u32 len | wire` (max 4 KB), so gateway/cmd/screenbench can carry them over a real gateway (M3-3b); the row goes to stderr.
// Reports per phase: fps, bytes/s, ms from capture return to sent (tile diff + encode) p50/p95, keyframes,
// CPU seconds per second and per frame, thread wake-ups per second. Pin it with `taskset -c 0`.
#include "../agent/agent_state.h"
#include "../capture/capture.h"
#if defined(__linux__)
#include "../capture/x11_capture.h"
#include <signal.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#endif

#include <sys/resource.h>
#include <poll.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;
constexpr int kW = 1920, kH = 1080;

struct Rng {
  uint64_t s = 88172645463325252ull;
  uint32_t next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return static_cast<uint32_t>(s >> 16); }
};

double ms_since(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }

// Records when each capture returned, so the sent time minus this is the tile diff + encode cost.
class TimedCapture : public xg::ScreenCapture {
public:
  std::atomic<uint64_t> captures{0};
  Clock::time_point captured_at() { std::lock_guard<std::mutex> lk(tm_); return captured_at_; }
protected:
  void mark() { { std::lock_guard<std::mutex> lk(tm_); captured_at_ = Clock::now(); } captures.fetch_add(1); }
private:
  std::mutex tm_;
  Clock::time_point captured_at_;
};

// Mutates a 1080p BGRA buffer according to the workload, by wall-clock time.
class SyntheticCapture : public TimedCapture {
public:
  explicit SyntheticCapture(std::string workload) : workload_(std::move(workload)), buf_(size_t(kW) * kH * 4, 0xFF) {
    Rng r;  // text-like page: dark dashes on white, so the encoder sees realistic detail
    for (int y = 40; y < kH - 40; y += 20)
      for (int x = 40; x < kW - 40; x += 12)
        if (r.next() % 5) fill(x, y, 6 + int(r.next() % 4), 10, 0x30);
    t0_ = next_ = Clock::now();
  }
  bool initialize() override { return true; }
  bool start() override { t0_ = next_ = Clock::now(); return true; }
  void stop() override {}
  xg::CaptureStatus capture(xg::Frame& out) override {
    mark();
    advance();
    out.width = kW; out.height = kH; out.stride = kW * 4;
    out.bgra.assign(buf_.begin(), buf_.end());
    out.dirty.clear();
    out.full_dirty = true;
    out.secure_desktop = false;
    return xg::CaptureStatus::Ok;
  }
  const uint8_t* pixels() const { return buf_.data(); }
  /// Moves the workload on; true when the picture changed.
  bool advance() {
    const auto now = Clock::now();
    bool changed = false;
    auto due = [&](int period_ms) { if (now < next_) return false; next_ = now + std::chrono::milliseconds(period_ms); return changed = true; };
    ++tick_;
    if (workload_ == "typing") {          // one 16x24 glyph every 120 ms
      if (due(120)) fill(100 + (tick_ * 16) % 1200, 200, 16, 24, uint8_t(tick_ * 37));
    } else if (workload_ == "scrolling") {  // a 1920x900 text area shifts 24 px every 33 ms
      if (due(33)) {
        std::memmove(&buf_[size_t(100) * kW * 4], &buf_[size_t(124) * kW * 4], size_t(876) * kW * 4);
        for (int i = 0; i < 24; ++i) fill(40, 976 + i, 1800, 1, (tick_ + i) % 3 ? 0xFF : 0x30);
      }
    } else if (workload_ == "drag") {     // an 800x500 window moves 12 px every 16 ms
      if (due(16)) {
        const int x = 200 + (tick_ * 12) % 800;
        for (int j = 0; j < 500; ++j) std::memset(&buf_[(size_t(300 + j) * kW + x) * 4], uint8_t(0x80 + j % 64), size_t(800) * 4);
      }
    } else if (workload_ == "video") {    // a 1280x720 region changes every 33 ms (30 fps)
      if (due(33)) {
        Rng r; r.s += tick_;
        for (int j = 0; j < 720; ++j) {
          uint8_t* row = &buf_[(size_t(180 + j) * kW + 320) * 4];
          for (int i = 0; i < 1280; ++i) { uint32_t v = r.next(); row[i * 4] = uint8_t(v); row[i * 4 + 1] = uint8_t(j + tick_); row[i * 4 + 2] = uint8_t(i >> 2); }
        }
      }
    }  // "static" and "idle": nothing changes
    return changed;
  }

private:
  void fill(int x, int y, int w, int h, uint8_t v) {
    for (int j = y; j < y + h && j < kH; ++j) std::memset(&buf_[(size_t(j) * kW + x) * 4], v, size_t(std::min(w, kW - x)) * 4);
  }
  std::string workload_;
  std::vector<uint8_t> buf_;
  Clock::time_point t0_, next_;
  int tick_ = 0;
};

#if defined(__linux__)
// The real X11 capture (SHM + Damage) on the display in $DISPLAY, timed like the synthetic one.
class TimedX11Capture : public TimedCapture {
public:
  bool initialize() override { return inner_.initialize(); }
  bool start() override { return inner_.start(); }
  void stop() override { inner_.stop(); }
  xg::CaptureStatus capture(xg::Frame& out) override { mark(); return inner_.capture(out); }
  bool wait_for_change(int timeout_ms) override { return inner_.wait_for_change(timeout_ms); }
  void interrupt_wait() override { inner_.interrupt_wait(); }
  void request_full() override { inner_.request_full(); }
private:
  xg::X11Capture inner_;
};

// Forked painter: draws the workload onto the root window of $DISPLAY from another process, so its CPU does
// not count in the streamer's numbers (getrusage RUSAGE_SELF). Dies with the bench.
pid_t start_painter(const std::string& workload) {
  const pid_t pid = fork();
  if (pid != 0) return pid;
  prctl(PR_SET_PDEATHSIG, SIGKILL);
  Display* d = XOpenDisplay(nullptr);
  if (!d) _exit(1);
  const int scr = DefaultScreen(d);
  Window root = RootWindow(d, scr);
  GC gc = XCreateGC(d, root, 0, nullptr);
  SyntheticCapture src(workload);
  std::vector<uint8_t> px(size_t(kW) * kH * 4);
  XImage* img = XCreateImage(d, DefaultVisual(d, scr), unsigned(DefaultDepth(d, scr)), ZPixmap, 0,
                             reinterpret_cast<char*>(px.data()), kW, kH, 32, 0);
  if (!img) _exit(1);
  for (bool first = true;; first = false) {
    if (src.advance() || first) {
      std::memcpy(px.data(), src.pixels(), px.size());
      XPutImage(d, root, gc, img, 0, 0, 0, 0, kW, kH);
      XFlush(d);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(4));
  }
}
#endif

xg::EncoderConfig g_enc;  // --codec, --quality, --webp-method, --rects, --workers

double pct(std::vector<double>& v, double p) {
  if (v.empty()) return 0;
  std::sort(v.begin(), v.end());
  return v[std::min(v.size() - 1, size_t(v.size() * p))];
}

struct Usage { double cpu_s; long wakeups; };
Usage usage() {
  rusage ru{};
  getrusage(RUSAGE_SELF, &ru);
  return {ru.ru_utime.tv_sec + ru.ru_utime.tv_usec / 1e6 + ru.ru_stime.tv_sec + ru.ru_stime.tv_usec / 1e6, ru.ru_nvcsw};
}

uint64_t unix_ns() {
  return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
}

bool read_full(int fd, void* buf, size_t n, const std::atomic<bool>& keep) {
  auto* p = static_cast<uint8_t*>(buf);
  while (n) {
    pollfd pf{fd, POLLIN, 0};
    if (!keep.load()) return false;
    if (::poll(&pf, 1, 100) <= 0) continue;  // wake 10x/s to notice the end of the phase
    const ssize_t r = ::read(fd, p, n);
    if (r <= 0) return false;
    p += r; n -= size_t(r);
  }
  return true;
}

void run_phase(const std::string& workload, int seconds, bool json, bool use_stdio, bool use_x11) {
  FILE* res = use_stdio ? stderr : stdout;
  std::shared_ptr<TimedCapture> cap;
#if defined(__linux__)
  pid_t painter = -1;
  if (use_x11) {
    cap = std::make_shared<TimedX11Capture>();
    if (workload != "idle") painter = start_painter(workload);
  }
#endif
  if (!cap) cap = std::make_shared<SyntheticCapture>(workload);
  std::vector<double> lat;
  uint64_t frames = 0, bytes = 0, keys = 0;
  std::mutex m;
  xg::AgentStateMachine* sm_ptr = nullptr;
  xg::AgentStateMachine sm(cap, g_enc, [&](const std::vector<uint8_t>& wire) {
    const double ms = ms_since(cap->captured_at());
    std::lock_guard<std::mutex> lk(m);
    ++frames; bytes += wire.size();
    if (use_stdio) {  // to the gateway bridge; the viewer's ACKs come back on stdin
      std::vector<uint8_t> rec(12 + wire.size());
      const uint32_t len = uint32_t(8 + wire.size());
      const uint64_t t = unix_ns() - uint64_t(ms * 1e6);
      std::memcpy(&rec[0], &len, 4);
      std::memcpy(&rec[4], &t, 8);
      std::memcpy(&rec[12], wire.data(), wire.size());
      if (::write(STDOUT_FILENO, rec.data(), rec.size()) < 0) {}
      if (wire.size() >= 28 && wire[3] == 1) {
        lat.push_back(ms);
        if (wire[4] & 1) ++keys;
      }
      return;
    }
    if (wire.size() >= 28 && wire[3] == 1) {
      lat.push_back(ms);
      if (wire[4] & 1) ++keys;
      if (sm_ptr) {  // the viewer's ACK, with no network delay
        xg::Message ack{}; ack.type = xg::MsgType::Ack; ack.stream_id = 1;
        ack.seq = uint32_t(wire[12]) | uint32_t(wire[13]) << 8 | uint32_t(wire[14]) << 16 | uint32_t(wire[15]) << 24;
        auto a = xg::encode(ack);
        sm_ptr->on_incoming_message(a.data(), a.size());
      }
    }
  });
  sm_ptr = &sm;
  if (workload != "idle" && !sm.on_view_start(1, "live")) { std::cerr << "streamer did not start\n"; return; }

  std::atomic<bool> reading{use_stdio};
  std::thread reader;
  if (use_stdio) reader = std::thread([&] {
    uint32_t len = 0;
    std::vector<uint8_t> msg;
    while (reading.load() && read_full(STDIN_FILENO, &len, 4, reading) && len >= 28 && len <= 4096) {
      msg.resize(len);
      if (!read_full(STDIN_FILENO, msg.data(), len, reading)) break;
      sm.on_incoming_message(msg.data(), msg.size());
    }
  });

  const uint64_t c0 = cap->captures.load();
  const Usage u0 = usage();
  const auto t0 = Clock::now();
  std::this_thread::sleep_for(std::chrono::seconds(seconds));
  const double secs = ms_since(t0) / 1000.0;
  const Usage u1 = usage();
  const double caps = double(cap->captures.load() - c0);
  const xg::StreamStats gov = sm.stream_stats();
  sm.on_view_stop();
#if defined(__linux__)
  if (painter > 0) { kill(painter, SIGKILL); waitpid(painter, nullptr, 0); }
#endif
  reading = false;
  if (reader.joinable()) reader.join();

  std::lock_guard<std::mutex> lk(m);
  const double cpu = u1.cpu_s - u0.cpu_s;
  const double fps = frames / secs, wake = double(u1.wakeups - u0.wakeups) / secs;
  const double p50 = pct(lat, 0.5), p95 = pct(lat, 0.95);
  if (json) {
    std::fprintf(res, "{\"workload\":\"%s\",\"seconds\":%.1f,\"fps\":%.2f,\"bytes_per_s\":%.0f,\"build_ms_p50\":%.2f,\"build_ms_p95\":%.2f,"
                "\"keyframes\":%llu,\"cpu_s_per_s\":%.4f,\"cpu_ms_per_frame\":%.2f,\"captures_per_s\":%.1f,\"cpu_ms_per_capture\":%.2f,\"wakeups_per_s\":%.1f,\"gov_fps\":%.1f,\"gov_quality\":%.2f,\"gov_level\":%d}\n",
                workload.c_str(), secs, fps, bytes / secs, p50, p95, (unsigned long long)keys, cpu / secs,
                frames ? cpu * 1000 / frames : 0.0, caps / secs, caps ? cpu * 1000 / caps : 0.0, wake, gov.fps_cap,
                gov.quality_scale, gov.level);
  } else {
    std::fprintf(res, "%-10s fps %6.2f  %9.0f B/s  build p50/p95 %6.2f/%6.2f ms  key %3llu  cpu %5.1f%% of a core  %7.2f ms/frame  %5.1f cap/s %6.2f ms/cap  wake-ups %6.1f/s  gov fps %4.1f q %.2f lvl %d\n",
                workload.c_str(), fps, bytes / secs, p50, p95, (unsigned long long)keys, 100 * cpu / secs,
                frames ? cpu * 1000 / frames : 0.0, caps / secs, caps ? cpu * 1000 / caps : 0.0, wake, gov.fps_cap,
                gov.quality_scale, gov.level);
  }
  std::fflush(res);
}
}  // namespace

int main(int argc, char** argv) {
  int seconds = 10;
  bool json = false, use_stdio = false, use_x11 = false;
  std::vector<std::string> phases;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--seconds" && i + 1 < argc) seconds = std::max(1, std::atoi(argv[++i]));
    else if (a == "--workload" && i + 1 < argc) phases.push_back(argv[++i]);
    else if (a == "--json") json = true;
    else if (a == "--codec" && i + 1 < argc) {
      const std::string c = argv[++i];
      if (c == "jpeg") g_enc.codec = xg::Codec::Jpeg;
      else if (c == "webp") g_enc.codec = xg::Codec::Webp;
      else { std::cerr << "--codec webp|jpeg\n"; return 2; }
    }
    else if (a == "--quality" && i + 1 < argc) g_enc.quality_text = std::clamp(std::atoi(argv[++i]), 1, 100);
    else if (a == "--rects") g_enc.rectangles = true;
    else if (a == "--workers" && i + 1 < argc) g_enc.workers = std::clamp(std::atoi(argv[++i]), 1, 4);
    else if (a == "--webp-method" && i + 1 < argc) g_enc.webp_method = std::clamp(std::atoi(argv[++i]), 0, 6);
    else if (a == "--stdio") use_stdio = true;
    else if (a == "--capture" && i + 1 < argc) {
      const std::string c = argv[++i];
      if (c == "x11") use_x11 = true;
      else if (c != "synthetic") { std::cerr << "--capture synthetic|x11\n"; return 2; }
    }
    else { std::cerr << "usage: xgstreambench [--seconds N] [--workload idle|static|typing|scrolling|drag|video]... [--json] [--stdio] [--capture synthetic|x11] [--codec webp|jpeg] [--quality N] [--webp-method 0-6] [--rects] [--workers 1-4]\n"; return 2; }
  }
  if (use_stdio && phases.size() != 1) { std::cerr << "--stdio needs exactly one --workload\n"; return 2; }
  if (phases.empty()) phases = {"idle", "static", "typing", "scrolling", "drag", "video"};
  for (const auto& p : phases) run_phase(p, seconds, json, use_stdio, use_x11);
  return 0;
}
