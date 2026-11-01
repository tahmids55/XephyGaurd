#include "xg/websocket.hpp"

#include <cstring>

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

bool WebSocketClient::connect(const std::string& host, uint16_t port, const std::string& path, const TlsOptions& opts,
                              std::string* err) {
    open_ = false;
    buf_.clear();
    partial_.clear();
    if (!conn_.connect(host, port, opts, err)) return false;
    const std::string key = crypto::base64_encode(crypto::random_bytes(16));
    std::string req = "GET " + path + " HTTP/1.1\r\nHost: " + host + ":" + std::to_string(port) +
                      "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: " + key +
                      "\r\nSec-WebSocket-Version: 13\r\nUser-Agent: xephyguard-agent\r\n\r\n";
    if (!conn_.write_all(req.data(), req.size())) { if (err) *err = "ws: write failed"; return false; }
    std::string raw;
    char b[4096];
    while (raw.find("\r\n\r\n") == std::string::npos) {
        if (!conn_.wait_readable(opts.timeout_ms)) { if (err) *err = "ws: handshake timeout"; return false; }
        long n = conn_.read_some(b, sizeof b);
        if (n <= 0) { if (err) *err = "ws: handshake read failed"; return false; }
        raw.append(b, size_t(n));
        if (raw.size() > 65536) { if (err) *err = "ws: handshake too large"; return false; }
    }
    const size_t he = raw.find("\r\n\r\n");
    HttpResponse r;
    std::string hdrs = raw.substr(0, he) + "\r\nContent-Length: 0\r\n\r\n";
    if (!parse_http_response(hdrs, r, nullptr) || r.status != 101) {
        if (err) *err = "ws: upgrade refused (status " + std::to_string(r.status) + ")";
        return false;
    }
    if (r.headers["sec-websocket-accept"] != ws_accept_key(key)) { if (err) *err = "ws: bad accept key"; return false; }
    buf_ = raw.substr(he + 4);
    open_ = true;
    return true;
}

bool WebSocketClient::send_frame(WsOpcode op, const std::string& payload) {
    uint8_t mk[4];
    auto rb = crypto::random_bytes(4);
    std::memcpy(mk, rb.data(), 4);
    auto f = ws_encode_frame(op, payload, true, mk);
    if (!conn_.write_all(f.data(), f.size())) { open_ = false; return false; }
    return true;
}

bool WebSocketClient::send_text(const std::string& text) { return open_ && send_frame(WsOpcode::text, text); }

std::optional<std::string> WebSocketClient::receive(int timeout_ms) {
    const int64_t deadline = steady_ms() + timeout_ms;
    char b[16384];
    while (open_) {
        size_t consumed = 0;
        bool perr = false;
        if (auto fr = ws_decode_frame(buf_, &consumed, &perr)) {
            buf_.erase(0, consumed);
            switch (fr->opcode) {
                case WsOpcode::ping: send_frame(WsOpcode::pong, fr->payload); continue;
                case WsOpcode::pong: continue;
                case WsOpcode::close: send_frame(WsOpcode::close, {}); open_ = false; conn_.close(); return std::nullopt;
                case WsOpcode::text:
                case WsOpcode::binary:
                case WsOpcode::cont:
                    partial_ += fr->payload;
                    if (fr->fin) { std::string m = std::move(partial_); partial_.clear(); return m; }
                    if (partial_.size() > 16 * 1024 * 1024) { open_ = false; return std::nullopt; }
                    continue;
                default: continue;
            }
        }
        if (perr) { open_ = false; conn_.close(); return std::nullopt; }
        const int64_t left = deadline - steady_ms();
        if (left <= 0) return std::nullopt;
        if (!conn_.wait_readable(int(left))) return std::nullopt;
        long n = conn_.read_some(b, sizeof b);
        if (n <= 0) { open_ = false; conn_.close(); return std::nullopt; }
        buf_.append(b, size_t(n));
    }
    return std::nullopt;
}

void WebSocketClient::close() {
    if (open_) send_frame(WsOpcode::close, {});
    open_ = false;
    conn_.close();
}

}  // namespace xg::net
