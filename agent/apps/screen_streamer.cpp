#include "screen_streamer.hpp"

#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "agent_state.h"
#include "fake_capture.h"

#if defined(_WIN32)
#include "win_dxgi_capture.h"
#include "win_gdi_capture.h"
#include <windows.h>
#include <winhttp.h>
#else
#include "x11_capture.h"
#endif

namespace xg {

#if defined(_WIN32)
class HelperWsClient {
public:
    HelperWsClient() = default;
    ~HelperWsClient() { close(); }

    bool connect(const std::wstring& host,
                 INTERNET_PORT port,
                 const std::wstring& path,
                 bool use_tls = false,
                 const std::wstring& agent_token = {}) {
        close();
        host_ = host;
        port_ = port;
        path_ = path;
        use_tls_ = use_tls;
        agent_token_ = agent_token;

        session_ = WinHttpOpen(L"XephyGuardHelper/1.0",
                               WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                               WINHTTP_NO_PROXY_NAME,
                               WINHTTP_NO_PROXY_BYPASS, 0);
        if (!session_) return false;

        conn_ = WinHttpConnect(session_, host.c_str(), port, 0);
        if (!conn_) { close(); return false; }

        DWORD flags = use_tls ? WINHTTP_FLAG_SECURE : 0;
        request_ = WinHttpOpenRequest(conn_, L"GET", path.c_str(), NULL,
                                     WINHTTP_NO_REFERER,
                                     WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
        if (!request_) { close(); return false; }

        if (!WinHttpSetOption(request_, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, NULL, 0)) {
            close(); return false;
        }

        std::wstring headers;
        if (!agent_token.empty()) {
            headers = L"X-Agent-Token: " + agent_token + L"\r\n";
        }
        LPCWSTR hdr_ptr = headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str();
        DWORD hdr_len = headers.empty() ? 0 : static_cast<DWORD>(headers.size());
        if (!WinHttpSendRequest(request_, hdr_ptr, hdr_len, NULL, 0, 0, 0)) {
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
        return connect(host_, port_, path_, use_tls_, agent_token_);
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
    bool use_tls_ = false;
    std::wstring agent_token_;
};
#endif

struct ScreenStreamer::Impl {
    std::string host;
    uint32_t stream_id = 0;
    uint16_t port = 8082;
    std::string path = "/agent";
    bool use_tls = false;
    std::string agent_token;
    std::atomic<bool> running{false};

    std::thread worker_thread;
    std::thread rx_thread;

#if defined(_WIN32)
    std::shared_ptr<HelperWsClient> ws_client;
#endif

    void run() {
        std::shared_ptr<xg::ScreenCapture> capture;
#if defined(_WIN32)
        auto dxgi = std::make_shared<xg::WinDxgiCapture>();
        if (dxgi->initialize()) {
            capture = dxgi;
        } else {
            capture = std::make_shared<xg::WinGdiCapture>();
        }
#else
        capture = std::make_shared<xg::X11Capture>();
#endif

        xg::EncoderConfig cfg{};
        cfg.quality_text = 85;
        cfg.codec = xg::Codec::Webp;

#if defined(_WIN32)
        ws_client = std::make_shared<HelperWsClient>();
        std::wstring w_host(host.begin(), host.end());
        std::string base_path = path.empty() ? "/agent" : path;
        if (base_path.front() != '/') base_path.insert(base_path.begin(), '/');
        std::string full_path = base_path + (base_path.find('?') == std::string::npos ? '?' : '&') +
                                "stream_id=" + std::to_string(stream_id);
        std::wstring w_path(full_path.begin(), full_path.end());
        std::wstring w_tok(agent_token.begin(), agent_token.end());
        ws_client->connect(w_host, port, w_path, use_tls, w_tok);
#endif

#if defined(_WIN32)
        auto last_reconnect = std::chrono::steady_clock::now();
#endif

        xg::AgentStateMachine state_machine(
            capture, cfg,
            [&](const std::vector<uint8_t>& data) {
#if defined(_WIN32)
                if (ws_client) {
                    if (!ws_client->is_connected()) {
                        auto now = std::chrono::steady_clock::now();
                        if (std::chrono::duration_cast<std::chrono::seconds>(now - last_reconnect).count() >= 2) {
                            last_reconnect = now;
                            ws_client->reconnect();
                        }
                    }
                    if (ws_client->is_connected()) {
                        ws_client->send_binary(data.data(), data.size());
                    }
                }
#else
                (void)data;
#endif
            });

#if defined(_WIN32)
        rx_thread = std::thread([this, &state_machine]() {
            auto last_rx_reconnect = std::chrono::steady_clock::now();
            while (running.load()) {
                if (ws_client && ws_client->is_connected()) {
                    std::vector<uint8_t> rx_data;
                    if (ws_client->receive(rx_data)) {
                        state_machine.on_incoming_message(rx_data.data(), rx_data.size());
                    } else {
                        std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    }
                } else if (running.load()) {
                    auto now = std::chrono::steady_clock::now();
                    if (std::chrono::duration_cast<std::chrono::seconds>(now - last_rx_reconnect).count() >= 2) {
                        last_rx_reconnect = now;
                        if (ws_client && ws_client->reconnect()) {
                            state_machine.on_view_start(stream_id, "live");
                        }
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                }
            }
        });
#endif

        if (!state_machine.on_view_start(stream_id, "live")) {
            running.store(false);
            return;
        }

        while (running.load() && state_machine.is_thread_running()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        state_machine.on_view_stop();

#if defined(_WIN32)
        if (ws_client) {
            ws_client->close();
        }
        if (rx_thread.joinable()) {
            rx_thread.join();
        }
#endif
    }
};

ScreenStreamer::ScreenStreamer() : impl_(std::make_unique<Impl>()) {}

ScreenStreamer::~ScreenStreamer() {
    stop();
}

bool ScreenStreamer::start(const std::string& ccc_host,
                           uint32_t stream_id,
                           uint16_t gateway_port,
                           const std::string& gateway_path,
                           bool use_tls,
                           const std::string& agent_token) {
    if (ccc_host.empty() || stream_id == 0) return false;
    const std::string norm_path = gateway_path.empty() ? std::string("/agent") : gateway_path;

    if (impl_->running.load()) {
        if (impl_->host == ccc_host && impl_->stream_id == stream_id && impl_->port == gateway_port &&
            impl_->path == norm_path && impl_->use_tls == use_tls && impl_->agent_token == agent_token) {
            return true; // Already streaming with identical parameters
        }
        stop();
    }

    impl_->host = ccc_host;
    impl_->stream_id = stream_id;
    impl_->port = gateway_port;
    impl_->path = norm_path;
    impl_->use_tls = use_tls;
    impl_->agent_token = agent_token;
    impl_->running.store(true);

    impl_->worker_thread = std::thread([this]() {
        impl_->run();
    });

    return true;
}

void ScreenStreamer::stop() {
    if (!impl_ || !impl_->running.load()) return;

    impl_->running.store(false);
#if defined(_WIN32)
    if (impl_->ws_client) {
        impl_->ws_client->close();
    }
#endif
    if (impl_->worker_thread.joinable()) {
        impl_->worker_thread.join();
    }
}

bool ScreenStreamer::is_running() const {
    return impl_ && impl_->running.load();
}

} // namespace xg
