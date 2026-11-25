#include "xg/websocket.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <poll.h>
#include <sys/socket.h>
#endif

#include "xg/crypto.hpp"
#include "xg/http.hpp"
#include "xg/util.hpp"

namespace xg::net {

static const char* kWsGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

std::string ws_accept_key(const std::string& key) { return crypto::base64_encode(crypto::sha1_raw(key + kWsGuid)); }

std::string ws_encode_frame(WsOpcode op, const std::string& payload, bool mask, const uint8_t mk[4]) {
    std::string f;
    f.push_back(char(0x80 | uint8_t(op)));
    const uint64_t n = payload.size();
    const uint8_t mbit = mask ? 0x80 : 0;
    if (n < 126) f.push_back(char(mbit | uint8_t(n)));
    else if (n <= 0xFFFF) {
        f.push_back(char(mbit | 126));
        f.push_back(char((n >> 8) & 0xFF));
        f.push_back(char(n & 0xFF));
    } else {
        f.push_back(char(mbit | 127));
        for (int i = 7; i >= 0; --i) f.push_back(char((n >> (8 * i)) & 0xFF));
    }
    if (mask) {
        f.append(reinterpret_cast<const char*>(mk), 4);
        for (uint64_t i = 0; i < n; ++i) f.push_back(char(uint8_t(payload[size_t(i)]) ^ mk[i % 4]));
    } else {
        f += payload;
    }
    return f;
}

std::optional<WsFrame> ws_decode_frame(const std::string& b, size_t* consumed, bool* perr) {
    if (perr) *perr = false;
    if (b.size() < 2) return std::nullopt;
    const auto* p = reinterpret_cast<const uint8_t*>(b.data());
    WsFrame fr;
    fr.fin = p[0] & 0x80;
    if (p[0] & 0x70) { if (perr) *perr = true; return std::nullopt; }  // RSV bits without extension
    fr.opcode = WsOpcode(p[0] & 0x0F);
    const bool masked = p[1] & 0x80;
    uint64_t len = p[1] & 0x7F;
    size_t pos = 2;
    if (len == 126) {
        if (b.size() < 4) return std::nullopt;
        len = (uint64_t(p[2]) << 8) | p[3];
        pos = 4;
    } else if (len == 127) {
        if (b.size() < 10) return std::nullopt;
        len = 0;
        for (int i = 0; i < 8; ++i) len = (len << 8) | p[2 + i];
        pos = 10;
    }
    if (len > 16 * 1024 * 1024) { if (perr) *perr = true; return std::nullopt; }
    uint8_t mk[4] = {0, 0, 0, 0};
    if (masked) {
        if (b.size() < pos + 4) return std::nullopt;
        std::memcpy(mk, p + pos, 4);
        pos += 4;
    }
    if (b.size() < pos + len) return std::nullopt;
    fr.payload.assign(b, pos, size_t(len));
    if (masked)
        for (size_t i = 0; i < fr.payload.size(); ++i) fr.payload[i] = char(uint8_t(fr.payload[i]) ^ mk[i % 4]);
    if (consumed) *consumed = pos + size_t(len);
    return fr;
}

namespace {
std::string sanitize_hdr(const std::string& v) {
    std::string out;
    out.reserve(v.size());
    for (char c : v) {
        if (c != '\r' && c != '\n' && c != '\0') out.push_back(c);
    }
    return out;
}
}  // namespace

WebSocketClient::~WebSocketClient() { close(); }

void WebSocketClient::raw_close_locked() {
    open_.store(false);
    conn_.close();
    if (plain_sock_ >= 0) {
        socket_close(plain_sock_);
        plain_sock_ = -1;
    }
}

bool WebSocketClient::raw_write_all(const void* data, size_t len) {
    if (use_tls_) return conn_.write_all(data, len);
    if (plain_sock_ < 0) return false;
    const char* p = static_cast<const char*>(data);
    while (len > 0) {
#ifdef _WIN32
        int n = ::send(static_cast<SOCKET>(plain_sock_), p, int(len), 0);
        if (n <= 0) {
            if (n < 0 && WSAGetLastError() == WSAEINTR) continue;
            return false;
        }
#else
        ssize_t n = ::send(int(plain_sock_), p, len, MSG_NOSIGNAL);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            return false;
        }
#endif
        p += size_t(n);
        len -= size_t(n);
    }
    return true;
}

long WebSocketClient::raw_read_some(void* buf, size_t len) {
    if (use_tls_) return conn_.read_some(buf, len);
    if (plain_sock_ < 0) return -1;
#ifdef _WIN32
    int n = ::recv(static_cast<SOCKET>(plain_sock_), static_cast<char*>(buf), int(len), 0);
    if (n < 0) {
        int e = WSAGetLastError();
        if (e == WSAEINTR || e == WSAEWOULDBLOCK || e == WSAETIMEDOUT) return -2;
        return -1;
    }
    return long(n);
#else
    ssize_t n = ::recv(int(plain_sock_), buf, len, 0);
    if (n < 0) {
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) return -2;
        return -1;
    }
    return long(n);
#endif
}

bool WebSocketClient::raw_wait_readable(int timeout_ms) {
    long long fd = -1;
    {
        std::lock_guard lk(io_mu_);
        if (!open_.load()) return false;
        if (use_tls_) {
            if (conn_.has_buffered()) return true;
            fd = conn_.native_handle();
        } else {
            fd = plain_sock_;
        }
    }
    if (fd < 0) return false;
#ifdef _WIN32
    WSAPOLLFD pfd{static_cast<SOCKET>(fd), POLLRDNORM, 0};
    return WSAPoll(&pfd, 1, timeout_ms) > 0;
#else
    pollfd pfd{int(fd), POLLIN, 0};
    return ::poll(&pfd, 1, timeout_ms) > 0;
#endif
}

bool WebSocketClient::connect(const std::string& host, uint16_t port, const std::string& path, const TlsOptions& opts,
                              std::string* err,
                              const std::vector<std::pair<std::string, std::string>>& extra_headers,
                              bool use_tls) {
    std::lock_guard lk(io_mu_);
    raw_close_locked();
    use_tls_ = use_tls;
    buf_.clear();
    partial_.clear();
    in_message_ = false;
    if (use_tls_) {
        if (!conn_.connect(host, port, opts, err)) return false;
    } else {
        plain_sock_ = tcp_connect(host, port, opts.timeout_ms, err);
        if (plain_sock_ < 0) return false;
    }
    const std::string key = crypto::base64_encode(crypto::random_bytes(16));
    const bool std_port = (use_tls_ && port == 443) || (!use_tls_ && port == 80);
    std::string req = "GET " + path + " HTTP/1.1\r\nHost: " + host + (std_port ? "" : ":" + std::to_string(port)) +
                      "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: " + key +
                      "\r\nSec-WebSocket-Version: 13\r\nUser-Agent: xephyguard-agent\r\n";
    for (const auto& [hk, hv] : extra_headers) {
        const std::string sk = sanitize_hdr(hk);
        const std::string sv = sanitize_hdr(hv);
        if (!sk.empty()) req += sk + ": " + sv + "\r\n";
    }
    req += "\r\n";
    if (!raw_write_all(req.data(), req.size())) {
        if (err) *err = "ws: write failed";
        raw_close_locked();
        return false;
    }
    std::string raw;
    char b[4096];
    const int64_t deadline = steady_ms() + opts.timeout_ms;
    while (raw.find("\r\n\r\n") == std::string::npos) {
        const int64_t left = deadline - steady_ms();
        if (left <= 0) { if (err) *err = "ws: handshake timeout"; raw_close_locked(); return false; }
        bool readable = false;
        if (use_tls_) {
            readable = conn_.wait_readable(int(left));
        } else {
#ifdef _WIN32
            WSAPOLLFD pfd{static_cast<SOCKET>(plain_sock_), POLLRDNORM, 0};
            readable = WSAPoll(&pfd, 1, int(left)) > 0;
#else
            pollfd pfd{int(plain_sock_), POLLIN, 0};
            readable = ::poll(&pfd, 1, int(left)) > 0;
#endif
        }
        if (!readable) { if (err) *err = "ws: handshake timeout"; raw_close_locked(); return false; }
        long n = raw_read_some(b, sizeof b);
        if (n == -2) continue;
        if (n <= 0) { if (err) *err = "ws: handshake read failed"; raw_close_locked(); return false; }
        raw.append(b, size_t(n));
        if (raw.size() > 65536) { if (err) *err = "ws: handshake too large"; raw_close_locked(); return false; }
    }
    const size_t he = raw.find("\r\n\r\n");
    HttpResponse r;
    std::string hdrs = raw.substr(0, he) + "\r\nContent-Length: 0\r\n\r\n";
    if (!parse_http_response(hdrs, r, nullptr) || r.status != 101) {
        if (err) *err = "ws: upgrade refused (status " + std::to_string(r.status) + ")";
        raw_close_locked();
        return false;
    }
    if (r.headers["sec-websocket-accept"] != ws_accept_key(key)) {
        if (err) *err = "ws: bad accept key";
        raw_close_locked();
        return false;
    }
    buf_ = raw.substr(he + 4);
    open_.store(true);
    return true;
}

bool WebSocketClient::send_frame(WsOpcode op, const std::string& payload) {
    std::lock_guard lk(io_mu_);
    if (!open_.load()) return false;
    uint8_t mk[4];
    auto rb = crypto::random_bytes(4);
    std::memcpy(mk, rb.data(), 4);
    auto f = ws_encode_frame(op, payload, true, mk);
    if (!raw_write_all(f.data(), f.size())) {
        raw_close_locked();
        return false;
    }
    return true;
}

bool WebSocketClient::send_text(const std::string& text) { return open_.load() && send_frame(WsOpcode::text, text); }

bool WebSocketClient::send_binary(const void* data, size_t len) {
    if (!open_.load()) return false;
    std::string payload(static_cast<const char*>(data), len);
    return send_frame(WsOpcode::binary, payload);
}

bool WebSocketClient::send_binary(const std::string& data) {
    return open_.load() && send_frame(WsOpcode::binary, data);
}

void WebSocketClient::interrupt() {
    std::lock_guard lk(io_mu_);
    if (!open_.load()) return;
    const long long fd = use_tls_ ? conn_.native_handle() : plain_sock_;
    if (fd < 0) return;
#ifdef _WIN32
    ::shutdown(static_cast<SOCKET>(fd), SD_BOTH);
#else
    ::shutdown(int(fd), SHUT_RDWR);
#endif
}

std::optional<std::string> WebSocketClient::receive(int timeout_ms, bool interruptible) {
    const int64_t deadline = steady_ms() + timeout_ms;
    char b[16384];
    while (open_.load()) {
        size_t consumed = 0;
        bool perr = false;
        if (auto fr = ws_decode_frame(buf_, &consumed, &perr)) {
            buf_.erase(0, consumed);
            switch (fr->opcode) {
                case WsOpcode::ping: send_frame(WsOpcode::pong, fr->payload); continue;
                case WsOpcode::pong: continue;
                case WsOpcode::close: {
                    std::lock_guard lk(io_mu_);
                    if (open_.load()) {
                        uint8_t mk[4] = {0, 0, 0, 0};
                        auto cf = ws_encode_frame(WsOpcode::close, {}, true, mk);
                        raw_write_all(cf.data(), cf.size());
                    }
                    raw_close_locked();
                    return std::nullopt;
                }
                case WsOpcode::text:
                case WsOpcode::binary:
                case WsOpcode::cont: {
                    // RFC 6455 5.4: a continuation needs an open message, a new message needs none open.
                    const bool bad_order = (fr->opcode == WsOpcode::cont) != in_message_;
                    if (bad_order || partial_.size() + fr->payload.size() > max_message_) {
                        std::lock_guard lk(io_mu_);
                        raw_close_locked();
                        return std::nullopt;
                    }
                    partial_ += fr->payload;
                    in_message_ = !fr->fin;
                    if (fr->fin) { std::string m = std::move(partial_); partial_.clear(); return m; }
                    continue;
                }
                default: continue;
            }
        }
        if (perr) {
            std::lock_guard lk(io_mu_);
            raw_close_locked();
            return std::nullopt;
        }
        const int64_t left = deadline - steady_ms();
        if (left <= 0) return std::nullopt;
        const int slice = interruptible ? int(left) : int(std::min<int64_t>(left, 50));
        if (!raw_wait_readable(slice)) continue;
        long n = 0;
        {
            std::lock_guard lk(io_mu_);
            if (!open_.load()) return std::nullopt;
            n = raw_read_some(b, sizeof b);
            if (n == -2) continue;
            if (n <= 0) {
                raw_close_locked();
                return std::nullopt;
            }
        }
        buf_.append(b, size_t(n));
    }
    return std::nullopt;
}

void WebSocketClient::close() {
    std::lock_guard lk(io_mu_);
    if (open_.load()) {
        uint8_t mk[4] = {0, 0, 0, 0};
        auto cf = ws_encode_frame(WsOpcode::close, {}, true, mk);
        raw_write_all(cf.data(), cf.size());
    }
    raw_close_locked();
}

}  // namespace xg::net
