#include "agent_state.h"
#include "../capture/fake_capture.h"

#include <fstream>
#include <cctype>

#if defined(_WIN32)
#include "../capture/win_dxgi_capture.h"
#include "../capture/win_gdi_capture.h"
#include <windows.h>
#include <winhttp.h>
#include <io.h>
#else
#include "../capture/x11_capture.h"
#endif

#include <csignal>
#include <cstdlib>
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
    close();
    host_ = host;
    port_ = port;
    path_ = path;

    session_ = WinHttpOpen(L"XephyGuardAgent/1.0",
                           WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                           WINHTTP_NO_PROXY_NAME,
                           WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session_) return false;

    conn_ = WinHttpConnect(session_, host.c_str(), port, 0);
    if (!conn_) { close(); return false; }

    request_ = WinHttpOpenRequest(conn_, L"GET", path.c_str(), NULL,
                                 WINHTTP_NO_REFERER,
                                 WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    if (!request_) { close(); return false; }

    if (!WinHttpSetOption(request_, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, NULL, 0)) {
      close(); return false;
    }

    if (!WinHttpSendRequest(request_, WINHTTP_NO_ADDITIONAL_HEADERS, 0, NULL, 0, 0, 0)) {
      close(); return false;
    }

    if (!WinHttpReceiveResponse(request_, NULL)) {
      close(); return false;
    }

    ws_ = WinHttpWebSocketCompleteUpgrade(request_, 0);
    WinHttpCloseHandle(request_);
    request_ = NULL;

    return ws_ != NULL;
  }

  bool reconnect() {
    if (host_.empty()) return false;
    return connect(host_, port_, path_);
  }

  bool send_binary(const uint8_t* data, size_t len) {
    if (!ws_) return false;
    DWORD res = WinHttpWebSocketSend(ws_, WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE,
                                     const_cast<uint8_t*>(data), static_cast<DWORD>(len));
    if (res != ERROR_SUCCESS) {
      close();
      return false;
    }
    return true;
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
    if (res != ERROR_SUCCESS) {
      close();
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
  std::wstring host_;
  INTERNET_PORT port_ = 0;
  std::wstring path_;
};
#endif

static bool try_read_agent_toml(std::string& host_out, uint32_t& stream_out) {
  std::vector<std::string> search_paths;
#if defined(_WIN32)
  if (const char* pd = std::getenv("PROGRAMDATA"); pd && *pd) {
    search_paths.push_back(std::string(pd) + "\\XephyGuard\\agent.toml");
  }
#else
  if (const char* cfg_dir = std::getenv("XG_CONFIG_DIR"); cfg_dir && *cfg_dir) {
    search_paths.push_back(std::string(cfg_dir) + "/agent.toml");
  }
  search_paths.push_back("/etc/xephyguard/agent.toml");
#endif
  search_paths.push_back("agent.toml");
  search_paths.push_back("../agent.toml");
  for (const auto& path : search_paths) {
    std::ifstream file(path);
    if (!file.is_open()) continue;
    std::string line;
    while (std::getline(file, line)) {
      auto trim = [](std::string s) {
        while (!s.empty() && isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
        while (!s.empty() && isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
        return s;
      };
      line = trim(line);
      if (line.rfind("host", 0) == 0) {
        auto eq = line.find('=');
        if (eq != std::string::npos) {
          std::string val = trim(line.substr(eq + 1));
          if (val.size() >= 2 && val.front() == '"' && val.back() == '"') {
            val = val.substr(1, val.size() - 2);
          }
          if (!val.empty() && val != "127.0.0.1" && val != "localhost") host_out = val;
        }
      } else if (line.rfind("pc_id", 0) == 0) {
        auto eq = line.find('=');
        if (eq != std::string::npos) {
          std::string val = trim(line.substr(eq + 1));
          if (val.size() >= 2 && val.front() == '"' && val.back() == '"') {
            val = val.substr(1, val.size() - 2);
          }
          try {
            if (!val.empty()) stream_out = static_cast<uint32_t>(std::stoul(val));
          } catch (...) {}
        }
      }
    }
    return true;
  }
  return false;
}

int main(int argc, char* argv[]) {
  std::signal(SIGINT, sig_handler);
  std::signal(SIGTERM, sig_handler);

  bool use_fake = false;
  bool standalone = false;
  uint32_t stream_id = 1;
  if (const char* env_sid = std::getenv("XG_STREAM_ID"); env_sid && *env_sid) {
    try { stream_id = static_cast<uint32_t>(std::stoul(env_sid)); } catch (...) {}
  }
  std::string mode = "live";
  std::string gateway_host;
  if (const char* env_host = std::getenv("XG_CCC_HOST"); env_host && *env_host) {
    gateway_host = env_host;
  }
  uint16_t gateway_port = 8082;
  if (const char* env_port = std::getenv("XG_SCREEN_PORT"); env_port && *env_port) {
    try { gateway_port = static_cast<uint16_t>(std::stoul(env_port)); } catch (...) {}
  }
  std::string agent_token;
  if (const char* env_tok = std::getenv("XG_SCREEN_TOKEN"); env_tok && *env_tok) {
    agent_token = env_tok;
  }
  bool cli_gw_set = !gateway_host.empty();
  bool cli_stream_set = false;

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
    } else if ((arg == "--stream" || arg == "--stream-id") && i + 1 < argc) {
      stream_id = static_cast<uint32_t>(std::stoul(argv[++i]));
      cli_stream_set = true;
    } else if (arg == "--host" && i + 1 < argc) {
      gateway_host = argv[++i];
      cli_gw_set = true;
    } else if (arg == "--port" && i + 1 < argc) {
      gateway_port = static_cast<uint16_t>(std::stoi(argv[++i]));
    } else if (arg == "--agent-token" && i + 1 < argc) {
      agent_token = argv[++i];
    } else if (arg == "--gateway" && i + 1 < argc) {
      std::string gw = argv[++i];
      size_t colon = gw.find(':');
      if (colon != std::string::npos) {
        gateway_host = gw.substr(0, colon);
        gateway_port = static_cast<uint16_t>(std::stoi(gw.substr(colon + 1)));
      } else {
        gateway_host = gw;
      }
      cli_gw_set = true;
    } else if (arg.rfind("--", 0) != 0) {
      // Positional argument support: xgagent.exe [gateway_host[:port]] [stream_id]
      if (!cli_gw_set) {
        size_t colon = arg.find(':');
        if (colon != std::string::npos) {
          gateway_host = arg.substr(0, colon);
          gateway_port = static_cast<uint16_t>(std::stoi(arg.substr(colon + 1)));
        } else {
          gateway_host = arg;
        }
        cli_gw_set = true;
      } else if (!cli_stream_set) {
        try {
          stream_id = static_cast<uint32_t>(std::stoul(arg));
          cli_stream_set = true;
        } catch (...) {}
      }
    }
  }

  // Attempt auto-detection from agent.toml if not explicitly specified on CLI
  if (!cli_gw_set || !cli_stream_set) {
    std::string toml_host;
    uint32_t toml_stream = 0;
    if (try_read_agent_toml(toml_host, toml_stream)) {
      if (!cli_gw_set && !toml_host.empty()) {
        gateway_host = toml_host;
        std::cout << "[Agent] Detected Gateway Host from agent.toml: " << gateway_host << std::endl;
      }
      if (!cli_stream_set && toml_stream > 0) {
        stream_id = toml_stream;
        std::cout << "[Agent] Detected Stream ID from agent.toml: " << stream_id << std::endl;
      }
    }
  }

  // If still unresolved, prompt interactively (no hardcoded IP fallback)
  if (!standalone && (gateway_host.empty() || (!cli_gw_set && (gateway_host == "127.0.0.1" || gateway_host == "localhost")))) {
#if defined(_WIN32)
    if (_isatty(_fileno(stdin))) {
      std::cout << "Enter CCC Server Host/IP (e.g. 192.168.1.10): ";
      std::string input;
      if (std::getline(std::cin, input)) {
        while (!input.empty() && isspace(static_cast<unsigned char>(input.front()))) input.erase(input.begin());
        while (!input.empty() && isspace(static_cast<unsigned char>(input.back()))) input.pop_back();
        if (!input.empty()) {
          size_t colon = input.find(':');
          if (colon != std::string::npos) {
            gateway_host = input.substr(0, colon);
            gateway_port = static_cast<uint16_t>(std::stoi(input.substr(colon + 1)));
          } else {
            gateway_host = input;
          }
        } else {
          if (const char* env_host = std::getenv("XG_CCC_HOST"); env_host && *env_host) {
            gateway_host = env_host;
          } else {
            std::cerr << "Error: CCC Server host is required (pass --host or configure agent.toml).\n";
            return 1;
          }
        }
      }
      if (!cli_stream_set) {
        std::cout << "Enter PC / Stream ID [default: " << stream_id << "]: ";
        std::string s_input;
        if (std::getline(std::cin, s_input)) {
          while (!s_input.empty() && isspace(static_cast<unsigned char>(s_input.front()))) s_input.erase(s_input.begin());
          while (!s_input.empty() && isspace(static_cast<unsigned char>(s_input.back()))) s_input.pop_back();
          if (!s_input.empty()) {
            try { stream_id = static_cast<uint32_t>(std::stoul(s_input)); } catch (...) {}
          }
        }
      }
    }
#endif
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
    if (gateway_host.empty()) {
      std::cerr << "[Agent] Error: Gateway host is required (--host, XG_CCC_HOST, or agent.toml).\n";
      return 1;
    }
    std::wstring w_host(gateway_host.begin(), gateway_host.end());
    std::wstring w_path = L"/agent?stream_id=" + std::to_wstring(stream_id);
    if (!agent_token.empty()) {
      w_path += L"&agent_token=" + std::wstring(agent_token.begin(), agent_token.end());
    }

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
  auto last_reconnect = std::chrono::steady_clock::now();

  xg::AgentStateMachine state_machine(
      capture, cfg,
      [&](const std::vector<uint8_t>& data) {
#if defined(_WIN32)
        if (standalone) return;
        if (ws_client) {
          if (!ws_client->is_connected()) {
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::seconds>(now - last_reconnect).count() >= 2) {
              last_reconnect = now;
              std::cout << "[Agent] Attempting reconnect to Gateway ws://" << gateway_host << ":" << gateway_port << "..." << std::endl;
              if (ws_client->reconnect()) {
                std::cout << "[Agent] Reconnected to Gateway successfully!" << std::endl;
              }
            }
          }
          if (ws_client->is_connected()) {
            if (ws_client->send_binary(data.data(), data.size())) {
              frames_sent++;
              bytes_sent += data.size();
            }
          }
        }
#else
        (void)data;
#endif
      });

#if defined(_WIN32)
  // Background thread to receive ACKs / Keyframe requests from Gateway
  std::thread rx_thread([&]() {
    while (g_running.load()) {
      if (ws_client && ws_client->is_connected()) {
        std::vector<uint8_t> rx_data;
        if (ws_client->receive(rx_data)) {
          state_machine.on_incoming_message(rx_data.data(), rx_data.size());
        } else {
          std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
      } else {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
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
