#include "agent_state.h"
#include "../capture/fake_capture.h"

#if defined(_WIN32)
#include "../capture/win_dxgi_capture.h"
#include "../capture/win_gdi_capture.h"
#else
#include "../capture/x11_capture.h"
#endif

#include <csignal>
#include <atomic>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

static std::atomic<bool> g_running{true};

static void sig_handler(int) {
  g_running.store(false);
}

int main(int argc, char* argv[]) {
  std::signal(SIGINT, sig_handler);
  std::signal(SIGTERM, sig_handler);

  bool use_fake = false;
  uint32_t stream_id = 1001;
  std::string mode = "live";

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--fake") {
      use_fake = true;
    } else if (arg == "--snapshot") {
      mode = "snapshot";
    } else if (arg == "--wall") {
      mode = "wall";
    } else if (arg == "--stream" && i + 1 < argc) {
      stream_id = static_cast<uint32_t>(std::stoul(argv[++i]));
    }
  }

  std::shared_ptr<xg::ScreenCapture> capture;

  if (use_fake) {
    std::cout << "[Agent] Using synthetic FakeCapture (1280x720)" << std::endl;
    capture = std::make_shared<xg::FakeCapture>(1280, 720);
  } else {
#if defined(_WIN32)
    std::cout << "[Agent] Probing DXGI Desktop Duplication capture..." << std::endl;
    auto dxgi = std::make_shared<xg::WinDxgiCapture>();
    if (dxgi->initialize()) {
      capture = dxgi;
      std::cout << "[Agent] DXGI capture initialized successfully." << std::endl;
    } else {
      std::cout << "[Agent] DXGI unavailable, falling back to Win32 GDI BitBlt." << std::endl;
      capture = std::make_shared<xg::WinGdiCapture>();
    }
#else
    std::cout << "[Agent] Initializing Linux X11 capture..." << std::endl;
    capture = std::make_shared<xg::X11Capture>();
#endif
  }

  xg::EncoderConfig cfg{};
  cfg.quality_text = 85;
  cfg.codec = xg::Codec::Webp;

  xg::AgentStateMachine state_machine(
      capture, cfg, [](const std::vector<uint8_t>& data) {
        // In standalone daemon mode, data is routed to gateway socket or IPC pipe
        (void)data;
      });

  std::cout << "[Agent] Starting screen stream (id: " << stream_id << ", mode: " << mode << ")..." << std::endl;
  if (!state_machine.on_view_start(stream_id, mode)) {
    std::cerr << "[Agent] Failed to start capture stream." << std::endl;
    return 1;
  }

  std::cout << "[Agent] Running. Press Ctrl+C to terminate." << std::endl;
  while (g_running.load() && state_machine.is_thread_running()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  std::cout << "[Agent] Stopping capture..." << std::endl;
  state_machine.on_view_stop();
  std::cout << "[Agent] Clean exit." << std::endl;
  return 0;
}
