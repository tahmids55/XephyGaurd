#pragma once
// Stream framing for the helper -> service IPC (screen Step 59, DECISIONS #24).
//
// The stream carries two kinds of records:
//   JSON line:     one JSON object followed by '\n' (see ipc_messages.hpp), at most ipc::kMaxLine bytes.
//   binary record: 0x00, type (1 byte), payload length (4 bytes, big-endian), payload.
// A JSON line never starts with 0x00 (JSON text has no raw NUL), so the first byte tells them apart.
// The only binary type is kScreenFrame: one encoded screen-protocol message that the service forwards to the
// gateway as opaque bytes. The service checks type and length only and never decodes the payload.
// Any unknown type, empty payload or payload over kMaxScreenFrame is a protocol error: the reader stops and
// the connection must be closed (the stream cannot be resynchronised safely).
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace xg {
class IIpcClient;
}

namespace xg::ipc {

inline constexpr uint8_t kBinaryMarker = 0x00;
inline constexpr uint8_t kScreenFrame = 0x01;
inline constexpr size_t kBinaryHeader = 6;
inline constexpr size_t kMaxScreenFrame = 4 * 1024 * 1024;

struct Record {
    enum class Kind { line, screen_frame };
    Kind kind = Kind::line;
    std::string data;  // line without '\n', or the opaque frame payload
};

/// Incremental decoder for one connection. Not thread-safe; one reader per connection.
class StreamReader {
public:
    /// Appends `n` bytes and moves every complete record into `out`. Returns false on a protocol error; the
    /// caller must then close the connection (records decoded before the error are still in `out`).
    bool feed(const char* data, size_t n, std::vector<Record>& out);
    /// Bytes buffered for an incomplete record.
    size_t buffered() const { return buf_.size(); }

private:
    std::string buf_;
    bool failed_ = false;
};

/// Encodes one screen frame record; empty if the payload is empty or larger than kMaxScreenFrame.
std::string screen_frame_record(std::string_view payload);

/// Helper side: sends one encoded screen message to the service. False if too large or not sent.
bool send_screen_frame(IIpcClient& client, std::string_view payload);

}  // namespace xg::ipc
