#include "xg/protocol.hpp"

#include <regex>

#include "xg/json_read.hpp"

namespace xg::protocol {

json build_batch(const std::string& pc_id, const std::vector<Event>& events, const json& heartbeat, size_t* included) {
    json arr = json::array();
    size_t bytes = 256 + heartbeat.dump().size();
    size_t n = 0;
    for (const auto& e : events) {
        if (n >= kMaxBatchEvents) break;
        json ej = e.to_json();
        const size_t sz = ej.dump().size();
        if (n > 0 && bytes + sz > kMaxBatchBytes) break;
        bytes += sz;
        arr.push_back(std::move(ej));
        ++n;
    }
    if (included) *included = n;
    return json{{"v", kVersion}, {"pc_id", pc_id}, {"heartbeat", heartbeat}, {"events", std::move(arr)}};
}

std::optional<BatchAck> parse_batch_ack(const std::string& body, std::string* err) {
    auto j = json::parse(body, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        if (err) *err = "ack is not a JSON object";
        return std::nullopt;
    }
    if (!j.contains("ack_seq") || !j["ack_seq"].is_number_integer()) {
        if (err) *err = "ack_seq missing";
        return std::nullopt;
    }
    BatchAck a;
    a.ack_seq = j["ack_seq"].get<int64_t>();
    a.server_time_ms = jr::i64(j, "server_time_ms");
    a.policy_version = jr::i64(j, "policy_version");
    a.signatures_version = jr::str(j, "signatures_version");
    a.isolated = jr::boolean(j, "isolated");
    return a;
}

std::optional<Command> parse_command(const json& j, std::string* err) {
    static const std::regex id_re("^[A-Za-z0-9_-]{1,64}$");
    static const std::vector<std::string> known = {"policy_updated", "message", "kill", "isolate_notice",
                                                   "rescan", "ping", "flush", "screen_lease"};
    if (!j.is_object()) { if (err) *err = "command not an object"; return std::nullopt; }
    Command c;
    c.id = j.contains("id") ? (j["id"].is_string() ? j["id"].get<std::string>() : j["id"].dump()) : "";
    c.type = jr::str(j, "type");
    c.args = j.contains("args") ? j["args"] : json::object();
    if (!std::regex_match(c.id, id_re)) { if (err) *err = "bad command id"; return std::nullopt; }
    if (std::find(known.begin(), known.end(), c.type) == known.end()) {
        if (err) *err = "unknown command type: " + c.type;
        return std::nullopt;
    }
    if (!c.args.is_object()) { if (err) *err = "args must be an object"; return std::nullopt; }
    if (c.type == "message") {
        if (!c.args.contains("text") || !c.args["text"].is_string() || c.args["text"].get<std::string>().size() > 2000) {
            if (err) *err = "message.text missing or too long";
            return std::nullopt;
        }
    }
    if (c.type == "kill" && !(c.args.contains("pid") || c.args.contains("exe"))) {
        if (err) *err = "kill needs pid or exe";
        return std::nullopt;
    }
    return c;
}

}  // namespace xg::protocol
