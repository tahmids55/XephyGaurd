#include "agent_state.h"
#include "../capture/fake_capture.h"

#if defined(_WIN32)
#include "../capture/win_dxgi_capture.h"
#include "../capture/win_gdi_capture.h"
#include <windows.h>
#include <winhttp.h>
#else
#include "../capture/x11_capture.h"
#endif

#include <csignal>
#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

static std::atomic<bool> g_running{true};

static void sig_handler(int) {
  g_running.store(false);
}

#if defined(_WIN32)
class WinHttpWsClient {
public:
  WinHttpWsClient() = default;
  ~WinHttpWsClient() { close(); }

  bool connect(const std::wstring& host, INTERNET_PORT port, const std::wstring& path) {
    session_ = WinHttpOpen(L"XephyGuardAgent/1.0",
                           WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                           WINHTTP_NO_PROXY_NAME,
                           WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session_) return false;

    conn_ = WinHttpConnect(session_, host.c_str(), port, 0);
    if (!conn_) return false;

    request_ = WinHttpOpenRequest(conn_, L"GET", path.c_str(), NULL,
                                 WINHTTP_NO_REFERER,
                                 WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    if (!request_) return false;

    if (!WinHttpSetOption(request_, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, NULL, 0)) {
      return false;
    }

    if (!WinHttpSendRequest(request_, WINHTTP_NO_ADDITIONAL_HEADERS, 0, NULL, 0, 0, 0)) {
      return false;
    }

    if (!WinHttpReceiveResponse(request_, NULL)) {
      return false;
    }

    ws_ = WinHttpWebSocketCompleteUpgrade(request_, 0);
    WinHttpCloseHandle(request_);
    request_ = NULL;

    return ws_ != NULL;
  }

  bool send_binary(const uint8_t* data, size_t len) {
    if (!ws_) return false;
    DWORD res = WinHttpWebSocketSend(ws_, WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE,
                                     const_cast<uint8_t*>(data), static_cast<DWORD>(len));
    return res == ERROR_SUCCESS;
  }

  bool receive(std::vector<uint8_t>& out) {
    if (!ws_) return false;
    out.resize(4096);
    DWORD bytes_read = 0;
    WINHTTP_WEB_SOCKET_BUFFER_TYPE buf_type;
    DWORD res = WinHttpWebSocketReceive(ws_, out.data(), static_cast<DWORD>(out.size()),
                                       &bytes_read, &buf_type);
    if (res == ERROR_SUCCESS && bytes_read > 0) {
      out.resize(bytes_read);
      return true;
    }
    return false;
  }

  void close() {
    if (ws_) {
      WinHttpWebSocketClose(ws_, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, NULL, 0);
      WinHttpCloseHandle(ws_);
      ws_ = NULL;
    }
    if (request_) {
      WinHttpCloseHandle(request_);
      request_ = NULL;
    }
    if (conn_) {
      WinHttpCloseHandle(conn_);
      conn_ = NULL;
    }
    if (session_) {
      WinHttpCloseHandle(session_);
      session_ = NULL;
    }
  }

  bool is_connected() const { return ws_ != NULL; }

private:
  HINTERNET session_ = NULL;
  HINTERNET conn_ = NULL;
  HINTERNET request_ = NULL;
  HINTERNET ws_ = NULL;
};
#endif

int main(int argc, char* argv[]) {
  std::signal(SIGINT, sig_handler);
  std::signal(SIGTERM, sig_handler);

  bool use_fake = false;
  bool standalone = false;
  uint32_t stream_id = 1;
  std::string mode = "live";
  std::string gateway_host = "127.0.0.1";
  uint16_t gateway_port = 8082;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--fake") {
      use_fake = true;
    } else if (arg == "--standalone") {
      standalone = true;
    } else if (arg == "--snapshot") {
      mode = "snapshot";
    } else if (arg == "--wall") {
      mode = "wall";
    } else if (arg == "--stream" && i + 1 < argc) {
      stream_id = static_cast<uint32_t>(std::stoul(argv[++i]));
    } else if (arg == "--gateway" && i + 1 < argc) {
      std::string gw = argv[++i];
      size_t colon = gw.find(':');
      if (colon != std::string::npos) {
        gateway_host = gw.substr(0, colon);
        gateway_port = static_cast<uint16_t>(std::stoi(gw.substr(colon + 1)));
      } else {
        gateway_host = gw;
      }
    }
  }

  std::cout << "========================================================\n"
            << " XephyGuard Screen Capture Agent\n"
            << "========================================================\n"
            << " Stream ID : " << stream_id << "\n"
            << " Mode      : " << mode << "\n"
            << " Backend   : " << (use_fake ? "FakeCapture" : "Real Desktop Capture (DXGI/GDI)") << "\n"
            << " Gateway   : " << (standalone ? "None (standalone)" : (gateway_host + ":" + std::to_string(gateway_port))) << "\n"
            << "========================================================\n"
            << std::endl;

  std::shared_ptr<xg::ScreenCapture> capture;

  if (use_fake) {
    std::cout << "[Agent] Using synthetic FakeCapture (1280x720)..." << std::endl;
    capture = std::make_shared<xg::FakeCapture>(1280, 720);
  } else {
#if defined(_WIN32)
    std::cout << "[Agent] Probing DXGI Desktop Duplication on active monitor..." << std::endl;
    auto dxgi = std::make_shared<xg::WinDxgiCapture>();
    if (dxgi->initialize()) {
      capture = dxgi;
      std::cout << "[Agent] Direct3D11 / DXGI Desktop Duplication ready." << std::endl;
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

#if defined(_WIN32)
  auto ws_client = std::make_shared<WinHttpWsClient>();
  if (!standalone) {
    std::wstring w_host(gateway_host.begin(), gateway_host.end());
    std::wstring w_path = L"/agent?stream_id=" + std::to_wstring(stream_id);

    std::cout << "[Agent] Connecting to Gateway ws://" << gateway_host << ":" << gateway_port << "..." << std::endl;
    if (ws_client->connect(w_host, gateway_port, w_path)) {
      std::cout << "[Agent] Connected to Gateway successfully!" << std::endl;
    } else {
      std::cerr << "[Agent] Warning: Could not connect to Gateway at "
                << gateway_host << ":" << gateway_port
                << ". Run gateway first, or use --standalone.\n"
                << "       Continuing in capture test mode." << std::endl;
    }
  }
#endif

  std::atomic<uint64_t> frames_sent{0};
  std::atomic<uint64_t> bytes_sent{0};

  xg::AgentStateMachine state_machine(
      capture, cfg,
      [&](const std::vector<uint8_t>& data) {
#if defined(_WIN32)
        if (ws_client && ws_client->is_connected()) {
          if (ws_client->send_binary(data.data(), data.size())) {
            frames_sent++;
            bytes_sent += data.size();
          }
        }
#else
        (void)data;
#endif
      });

#if defined(_WIN32)
  // Background thread to receive ACKs / Keyframe requests from Gateway
  std::thread rx_thread([&]() {
    while (g_running.load() && ws_client && ws_client->is_connected()) {
      std::vector<uint8_t> rx_data;
      if (ws_client->receive(rx_data)) {
        state_machine.on_incoming_message(rx_data.data(), rx_data.size());
      } else {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
      }
    }
  });
#endif

  std::cout << "[Agent] Starting screen stream (id: " << stream_id << ", mode: " << mode << ")..." << std::endl;
  if (!state_machine.on_view_start(stream_id, mode)) {
    std::cerr << "[Agent] Failed to start capture stream." << std::endl;
    g_running.store(false);
  }

  std::cout << "[Agent] Running! Screen capture active. Press Ctrl+C to terminate.\n" << std::endl;

  auto last_stat = std::chrono::steady_clock::now();
  while (g_running.load() && state_machine.is_thread_running()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::seconds>(now - last_stat).count() >= 5) {
      last_stat = now;
      std::cout << "[Agent Stats] State: " << static_cast<int>(state_machine.state())
                << " | Seq: " << state_machine.current_seq()
                << " | Frames Sent: " << frames_sent.load()
                << " | Data: " << (bytes_sent.load() / 1024) << " KB"
                << std::endl;
    }
  }

  std::cout << "\n[Agent] Stopping capture..." << std::endl;
  state_machine.on_view_stop();

#if defined(_WIN32)
  if (ws_client) {
    ws_client->close();
  }
  if (rx_thread.joinable()) {
    rx_thread.join();
  }
#endif

  std::cout << "[Agent] Stopped cleanly. Total frames: " << frames_sent.load() << std::endl;
  return 0;
}
