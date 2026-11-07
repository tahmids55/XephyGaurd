#pragma once
#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>

namespace xg {

using json = nlohmann::json;

/// Event types (also the "type" field on the wire). Keep in sync with docs/PROTOCOL.md.
namespace ev {
inline constexpr const char* heartbeat = "heartbeat";
inline constexpr const char* agent_started = "agent_started";
inline constexpr const char* agent_stopping = "agent_stopping";
inline constexpr const char* process_start = "process_start";
inline constexpr const char* process_stop = "process_stop";
inline constexpr const char* process_snapshot = "process_snapshot";
inline constexpr const char* rule_hit = "rule_hit";
inline constexpr const char* listening_port = "listening_port";
inline constexpr const char* window_focus = "window_focus";
inline constexpr const char* paste = "paste";
inline constexpr const char* copy = "copy";
inline constexpr const char* key_stats = "key_stats";
inline constexpr const char* net_adapter = "net_adapter";
inline constexpr const char* device = "device";
inline constexpr const char* extension_scan = "extension_scan";
inline constexpr const char* artifact_scan = "artifact_scan";
inline constexpr const char* file_activity = "file_activity";
inline constexpr const char* gpu_sample = "gpu_sample";
inline constexpr const char* tamper = "tamper";
inline constexpr const char* capabilities = "capabilities";
inline constexpr const char* command_result = "command_result";
inline constexpr const char* helper_status = "helper_status";
}  // namespace ev

struct Event {
    int64_t seq = 0;      // monotonic per agent install, assigned by the queue
    int64_t ts_ms = 0;    // agent wall clock (CCC corrects with the reported offset)
    std::string type;
    json data;

    json to_json() const { return json{{"seq", seq}, {"ts", ts_ms}, {"type", type}, {"data", data}}; }
};

/// Anything that accepts events (the persistent queue in production, a vector in tests).
class IEventSink {
public:
    virtual ~IEventSink() = default;
    virtual void emit(const std::string& type, json data) = 0;
};

}  // namespace xg
