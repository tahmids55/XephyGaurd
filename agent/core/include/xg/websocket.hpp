#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "xg/tls.hpp"

namespace xg::net {

enum class WsOpcode : uint8_t { cont = 0x0, text = 0x1, binary = 0x2, close = 0x8, ping = 0x9, pong = 0xA };

struct WsFrame {
    bool fin = true;
    WsOpcode opcode = WsOpcode::text;
    std::string payload;
};

/// RFC 6455 frame codec (exposed for tests).
std::string ws_encode_frame(WsOpcode op, const std::string& payload, bool mask, const uint8_t mask_key[4]);
/// Decodes one frame from the front of `buf`. Returns nullopt if more bytes are needed.
std::optional<WsFrame> ws_decode_frame(const std::string& buf, size_t* consumed, bool* protocol_error);
std::string ws_accept_key(const std::string& client_key_b64);

/// Blocking WebSocket client over TlsConnection. Handles ping/pong and message reassembly.
class WebSocketClient {
public:
    bool connect(const std::string& host, uint16_t port, const std::string& path, const TlsOptions& opts,
                 std::string* err);
    bool send_text(const std::string& text);
    /// Waits up to `timeout_ms` for a complete text message. Returns nullopt on timeout (check open()).
    std::optional<std::string> receive(int timeout_ms);
    void close();
    bool open() const { return open_; }

private:
    bool send_frame(WsOpcode op, const std::string& payload);
    TlsConnection conn_;
    std::string buf_, partial_;
    bool open_ = false;
};

}  // namespace xg::net
