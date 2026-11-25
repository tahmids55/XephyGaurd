#include "xg/ipc_framing.hpp"

#include "xg/interfaces.hpp"
#include "xg/ipc_messages.hpp"

namespace xg::ipc {

bool StreamReader::feed(const char* data, size_t n, std::vector<Record>& out) {
    if (failed_) return false;
    buf_.append(data, n);
    size_t pos = 0;
    while (pos < buf_.size()) {
        if (static_cast<uint8_t>(buf_[pos]) == kBinaryMarker) {
            if (buf_.size() - pos < kBinaryHeader) break;
            const auto* h = reinterpret_cast<const uint8_t*>(buf_.data() + pos);
            const size_t len = (size_t(h[2]) << 24) | (size_t(h[3]) << 16) | (size_t(h[4]) << 8) | size_t(h[5]);
            const bool screen = h[1] == kScreenFrame, evidence = h[1] == kEvidenceJpeg;
            if (!(screen || evidence) || len == 0 || len > (screen ? kMaxScreenFrame : kMaxEvidenceJpeg)) {
                failed_ = true;
                buf_.clear();
                return false;
            }
            if (buf_.size() - pos - kBinaryHeader < len) break;
            out.push_back({screen ? Record::Kind::screen_frame : Record::Kind::evidence_jpeg,
                           buf_.substr(pos + kBinaryHeader, len)});
            pos += kBinaryHeader + len;
            continue;
        }
        const size_t nl = buf_.find('\n', pos);
        if (nl == std::string::npos) {
            if (buf_.size() - pos > kMaxLine) pos = buf_.size();  // oversized garbage line: dropped (as before)
            break;
        }
        out.push_back({Record::Kind::line, buf_.substr(pos, nl - pos)});
        pos = nl + 1;
    }
    buf_.erase(0, pos);
    return true;
}

namespace {
std::string binary_record(uint8_t type, std::string_view payload, size_t max) {
    if (payload.empty() || payload.size() > max) return {};
    const auto len = uint32_t(payload.size());
    std::string r;
    r.reserve(kBinaryHeader + payload.size());
    r.push_back(char(kBinaryMarker));
    r.push_back(char(type));
    r.push_back(char((len >> 24) & 0xff));
    r.push_back(char((len >> 16) & 0xff));
    r.push_back(char((len >> 8) & 0xff));
    r.push_back(char(len & 0xff));
    r.append(payload);
    return r;
}
}  // namespace

std::string screen_frame_record(std::string_view payload) { return binary_record(kScreenFrame, payload, kMaxScreenFrame); }

std::string evidence_jpeg_record(std::string_view jpeg) { return binary_record(kEvidenceJpeg, jpeg, kMaxEvidenceJpeg); }

bool send_screen_frame(IIpcClient& client, std::string_view payload) {
    const std::string r = screen_frame_record(payload);
    return !r.empty() && client.send(r);
}

bool send_evidence_jpeg(IIpcClient& client, std::string_view jpeg) {
    const std::string r = evidence_jpeg_record(jpeg);
    return !r.empty() && client.send(r);
}

}  // namespace xg::ipc
