#include "../capture/fake_capture.h"
#include "../core/encoder.h"
#include "../core/flow.h"
#include "../core/protocol.h"
#include "../core/tiles.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <numeric>
#include <random>
#include <string>
#include <thread>
#include <vector>

struct Metrics {
  std::atomic<uint64_t> total_bytes{0};
  std::atomic<uint64_t> total_frames{0};
  std::atomic<uint64_t> total_keyframes{0};
  std::atomic<uint64_t> dropped_frames{0};
  std::mutex rtt_mutex;
  std::vector<double> rtt_samples;

  void record_rtt(double rtt_ms) {
    std::lock_guard<std::mutex> lock(rtt_mutex);
    rtt_samples.push_back(rtt_ms);
  }

  double p95_rtt() {
    std::lock_guard<std::mutex> lock(rtt_mutex);
    if (rtt_samples.empty()) return 0.0;
    std::sort(rtt_samples.begin(), rtt_samples.end());
    size_t idx = static_cast<size_t>(rtt_samples.size() * 0.95);
    if (idx >= rtt_samples.size()) idx = rtt_samples.size() - 1;
    double val = rtt_samples[idx];
    rtt_samples.clear(); // Clear for next reporting window
    return val;
  }
};

int main(int argc, char* argv[]) {
  int num_agents = 40;
  int num_viewers = 5;
  int duration_sec = 60; // Default 1 minute for test runs, can be 7200 for 2-hour soak
  double packet_loss_pct = 0.0; // 0..100

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--agents" && i + 1 < argc) {
      num_agents = std::stoi(argv[++i]);
    } else if (arg == "--viewers" && i + 1 < argc) {
      num_viewers = std::stoi(argv[++i]);
    } else if (arg == "--duration" && i + 1 < argc) {
      duration_sec = std::stoi(argv[++i]);
    } else if (arg == "--loss" && i + 1 < argc) {
      packet_loss_pct = std::stod(argv[++i]);
    }
  }

  std::cout << "========================================================\n"
            << " XephyGuard Screen View Benchmark & Soak Simulator\n"
            << "========================================================\n"
            << "  Simulated Agents  : " << num_agents << "\n"
            << "  Simulated Viewers : " << num_viewers << "\n"
            << "  Duration          : " << duration_sec << " seconds\n"
            << "  Packet Loss       : " << packet_loss_pct << " %\n"
            << "========================================================\n"
            << std::endl;

  std::atomic<bool> running{true};
  Metrics metrics;

  std::vector<std::thread> agent_threads;
  agent_threads.reserve(num_agents);

  for (int agent_idx = 0; agent_idx < num_agents; ++agent_idx) {
    agent_threads.emplace_back([&, agent_idx]() {
      xg::FakeCapture capture(1280, 720);
      capture.initialize();
      capture.start();

      xg::TileTracker tracker(1280, 720);
      xg::EncoderConfig enc_cfg{};
      enc_cfg.quality_text = 85;
      enc_cfg.codec = xg::Codec::Webp;
      xg::FrameEncoder encoder(enc_cfg);
      xg::FlowControl flow(3);

      std::mt19937 rng(1337 + agent_idx);
      std::uniform_real_distribution<double> dist_loss(0.0, 100.0);
      std::uniform_real_distribution<double> dist_rtt(15.0, 45.0);

      uint32_t seq = 0;
      uint32_t last_sent_seq = 0;
      uint64_t now_ms = 1000;

      while (running.load()) {
        xg::Frame frame;
        xg::CaptureStatus st = capture.capture(frame);
        if (st == xg::CaptureStatus::Ok) {
          tracker.update(frame);
          const auto& pending = tracker.pending();

          if (!pending.empty() || flow.needs_keyframe()) {
            bool is_key = flow.needs_keyframe();
            ++seq;
            uint32_t base = is_key ? 0 : last_sent_seq;

            encoder.set_quality_scale(flow.quality_scale());
            auto msg = encoder.build(frame, pending, static_cast<uint32_t>(agent_idx + 1), seq, base, is_key);
            auto wire = xg::encode(msg);

            // Packet loss injection
            if (packet_loss_pct > 0.0 && dist_loss(rng) < packet_loss_pct) {
              metrics.dropped_frames.fetch_add(1);
              // Viewer missed base_seq -> will send keyframe request
              flow.reset();
              tracker.force_all_pending();
            } else {
              metrics.total_bytes.fetch_add(wire.size());
              metrics.total_frames.fetch_add(1);
              if (is_key) {
                metrics.total_keyframes.fetch_add(1);
                flow.note_keyframe_sent();
              }
              tracker.mark_sent();
              last_sent_seq = seq;

              double sim_rtt = dist_rtt(rng);
              metrics.record_rtt(sim_rtt);
              flow.on_ack(seq, now_ms + static_cast<uint64_t>(sim_rtt));
            }
          }
        }

        now_ms += 200; // ~5 FPS
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
      }
    });
  }

  // Reporter loop
  int elapsed = 0;
  uint64_t prev_bytes = 0;

  while (elapsed < duration_sec) {
    std::this_thread::sleep_for(std::chrono::seconds(5));
    elapsed += 5;

    uint64_t cur_bytes = metrics.total_bytes.load();
    double mbps = static_cast<double>(cur_bytes - prev_bytes) * 8.0 / (5.0 * 1024.0 * 1024.0);
    prev_bytes = cur_bytes;

    double p95 = metrics.p95_rtt();
    std::cout << "[T+" << std::setw(4) << elapsed << "s] "
              << "Throughput: " << std::fixed << std::setprecision(2) << mbps << " Mbps | "
              << "Total Frames: " << metrics.total_frames.load() << " | "
              << "Keyframes: " << metrics.total_keyframes.load() << " | "
              << "Dropped: " << metrics.dropped_frames.load() << " | "
              << "p95 RTT: " << p95 << " ms"
              << std::endl;
  }

  running.store(false);
  for (auto& t : agent_threads) {
    if (t.joinable()) t.join();
  }

  std::cout << "\nBenchmark complete. Total data transferred: "
            << std::fixed << std::setprecision(2)
            << (static_cast<double>(metrics.total_bytes.load()) / (1024.0 * 1024.0)) << " MB.\n";
  return 0;
}
