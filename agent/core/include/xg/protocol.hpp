#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "xg/event.hpp"

namespace xg::protocol {

inline constexpr int kVersion = 1;
inline constexpr size_t kMaxBatchEvents = 500;
inline constexpr size_t kMaxBatchBytes = 512 * 1024;

// REST paths on the CCC agent endpoint (port 8443, mTLS).
inline constexpr const char* kEnrollPath = "/api/agent/v1/enroll";
inline constexpr const char* kPolicyPath = "/api/agent/v1/policy";
inline constexpr const char* kEventsPath = "/api/agent/v1/events";
inline constexpr const char* kBroadcastAuthPath = "/api/agent/v1/broadcasting/auth";
inline constexpr const char* kCommandAckPrefix = "/api/agent/v1/commands/";

struct BatchAck {
    int64_t ack_seq = 0;
    int64_t server_time_ms = 0;
    int64_t policy_version = 0;
    std::string signatures_version;
    bool isolated = false;
};

/// Builds the body of POST /events. Stops adding events once kMaxBatchBytes would be exceeded
/// and reports how many were included through `included`.
json build_batch(const std::string& pc_id, const std::vector<Event>& events, const json& heartbeat,
                 size_t* included = nullptr);
std::optional<BatchAck> parse_batch_ack(const std::string& body, std::string* err = nullptr);

struct Command {
    std::string id;     // server-generated, used for ack
    std::string type;   // policy_updated, message, kill, set_indicator, rescan, isolate_notice, ping
    json args;
};
/// Validates and parses a command received over the WebSocket channel.
std::optional<Command> parse_command(const json& j, std::string* err = nullptr);

/// Clock offset estimate: server time minus local midpoint of the request.
inline int64_t clock_offset(int64_t local_send_ms, int64_t local_recv_ms, int64_t server_ms) {
    return server_ms - (local_send_ms + (local_recv_ms - local_send_ms) / 2);
}

}  // namespace xg::protocol
