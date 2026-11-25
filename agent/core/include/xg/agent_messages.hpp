#pragma once
// Typed messages into the agent core, and the read-only view the core publishes for the I/O threads.
// Producers never touch agent state: they post one of these (DECISIONS #22).
#include <cstdint>
#include <optional>
#include <string>
#include <variant>

#include "xg/artifact_scan.hpp"
#include "xg/event.hpp"
#include "xg/interfaces.hpp"
#include "xg/protocol.hpp"
#include "xg/rules.hpp"
#include "xg/types.hpp"

namespace xg {

namespace msg {
/// Exec event from the platform process watcher (netlink proc connector / ETW).
struct ProcessExec { ProcessInfo p; };
/// Links, addresses or routes changed.
struct NetworkChanged {};
/// USB / device hotplug.
struct DeviceEvent { DeviceInfo d; };
/// One JSON line from a session helper (user-controlled data).
struct HelperLine { IIpcServer::Peer peer; std::string line; };
struct HelperConn { IIpcServer::Peer peer; bool connected = false; };
struct FileEvent { FileActivity a; };
/// Key-down timing only: no key identity exists anywhere in the agent.
struct KeyEvent { int64_t t_ms = 0; bool backspace = false; bool paste_chord = false; };
/// Validated command from the realtime channel.
struct CommandMsg { protocol::Command cmd; };
/// Body of GET /policy (an object whose "policy" member is an object).
struct PolicyFetched { json body; };
/// Parsed batch ack; the uplink already acknowledged the queue.
struct AckReceived { protocol::BatchAck ack; int64_t t0_ms = 0; int64_t t1_ms = 0; };
/// Enrollment approved by the CCC (all fields type-checked by the uplink).
struct EnrollApproved { std::string cert_pem, ca_pem, token; int64_t pc_id = 0; };
struct ArtifactsScanned { ArtifactScanResult result; };
/// A local port answered an LLM API path (probe worker, F74). `rule` is the port rule that required the probe,
/// if any; otherwise the generic LLM API rule applies.
struct ProbeDone { ListeningSocket sock; std::string path; std::optional<RuleHit> rule; };
}  // namespace msg

using AgentMessage = std::variant<msg::ProcessExec, msg::NetworkChanged, msg::DeviceEvent, msg::HelperLine,
                                  msg::HelperConn, msg::FileEvent, msg::KeyEvent, msg::CommandMsg, msg::PolicyFetched,
                                  msg::AckReceived, msg::EnrollApproved, msg::ArtifactsScanned, msg::ProbeDone>;

/// Everything the uplink and realtime threads may read. Published by the core, never mutated in place.
struct UplinkView {
    std::string ccc_host;
    uint16_t api_port = 8443;
    std::string ca_file, cert_file, key_file;
    std::string token, pc_id, enrollment_id, poll_token;
    bool enrolled = false;
    int heartbeat_s = 30;
    json heartbeat = json::object();  // latest heartbeat built by the core
    json realtime;                    // realtime channel config from the CCC (null until known)
};

}  // namespace xg
