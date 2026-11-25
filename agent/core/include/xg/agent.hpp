#pragma once
#include <atomic>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "xg/agent_messages.hpp"
#include "xg/config.hpp"
#include "xg/discovery.hpp"
#include "xg/event_queue.hpp"
#include "xg/hit_dedup.hpp"
#include "xg/interfaces.hpp"
#include "xg/keystats.hpp"
#include "xg/mailbox.hpp"
#include "xg/policy.hpp"
#include "xg/process_tracker.hpp"
#include "xg/protocol.hpp"
#include "xg/rules.hpp"
#include "xg/screen_relay.hpp"
#include "xg/uplink.hpp"
#include "xg/worker.hpp"

namespace xg {

/// The agent core (actor). One thread, the one that calls run(), owns every member below. Platform
/// watchers, the uplink, the realtime channel and the worker only post AgentMessages; they read back
/// the published UplinkView. See DECISIONS #22.
///
/// The public scan_* / handle_* / apply_* methods are core-thread entry points; tests call them directly
/// on the test thread, which then acts as the core thread.
class Agent {
public:
    Agent(Config cfg, std::string config_path, ServicePlatform& platform);
    ~Agent();

    /// Main entry: blocks until `stop` becomes true.
    int run(std::atomic<bool>& stop);

    /// Thread-safe: hands a message to the core. Returns false if the mailbox is full.
    bool post(AgentMessage m) { return mailbox_.post(std::move(m)); }
    /// Processes every queued message now (core thread / tests).
    void drain();

    // ---- core-thread entry points (also used by unit tests) --------------------------------
    void apply_policy(const Policy& p);
    /// `envelope` = {"version","bundle":"<json text>","signature":"<b64>"} as served by the CCC.
    bool apply_signatures(const json& envelope, bool require_signature = true);
    void handle_command(const protocol::Command& cmd);
    bool isolated() const { return isolated_; }
    NetworkLock network_lock() const { return network_lock_; }
    void handle_rule_hit(const RuleHit& hit, const std::string& subject, json context, int64_t pid = 0);
    void scan_processes();
    void process_snapshot();
    void scan_ports();
    void scan_network();
    void scan_ai_traffic();
    /// Flags tunnels the proxy cannot see into, during a session only (practice / test).
    void scan_tunnels();
    void scan_devices();
    void scan_extensions();
    void on_helper_message(const IIpcServer::Peer& peer, const std::string& line);
    void on_process_started(const ProcessInfo& p);
    /// Re-evaluates everything already present (called automatically after a mode change).
    void reevaluate_all();
    json build_heartbeat();
    Policy policy() const { return policy_; }
    IEventSink& sink() { return *sink_; }
    /// Replace the persistent queue by another sink (tests, one-shot scan).
    void set_sink_for_tests(IEventSink* s) { sink_ = s; }
    RuleEngine& rules() { return rules_; }
    /// What the I/O threads currently see (tests).
    std::shared_ptr<const UplinkView> view() const { return view_.get(); }
    void publish_view();
    void check_helpers();
    void set_helper_grace_for_tests(int64_t ms) { helper_grace_ms_ = ms; }

private:
    void dispatch(AgentMessage& m);
    void on_policy_fetched(const json& body);
    void on_ack(const msg::AckReceived& a);
    void on_enroll_approved(const msg::EnrollApproved& e);
    void on_helper_conn(const IIpcServer::Peer& peer, bool connected);
    void on_device_event(const DeviceInfo& d);
    void emit_device(const DeviceInfo& d, const std::optional<RuleHit>& hit, bool raise_hit);
    void on_file_activity(const FileActivity& a);
    void on_key(int64_t t_ms, bool backspace, bool paste_chord);
    void on_artifacts_scanned(const ArtifactScanResult& res);
    void on_probe_done(const msg::ProbeDone& p);
    void report_port(const ListeningSocket& s, const RuleHit& hit, bool probed);
    void maybe_start_artifact_scan(int64_t now_steady);
    void command_result(const std::string& id, const std::string& status, const json& detail);
    void wake_uplink() { uplink_->wake(); }

    // periodic tasks
    void run_periodic(int64_t now_steady);
    void check_integrity(bool initial);
    /// Turns network isolation on/off (Internet cut, LAN + CCC kept), persists it and re-evaluates running apps.
    void apply_isolation(bool on);
    /// The lock the policy asks for: the session's network_lock in proxy_only mode, off otherwise.
    NetworkLock wanted_network_lock() const;
    /// Installs / removes the egress lock when it differs from what is installed (`force`: always).
    void apply_network_lock(bool force = false);
    void sample_gpu();
    void flush_keystats(int64_t now, bool force);
    void flush_file_activity();
    void push_helper_config();
    /// The policy's PAC URL if proxy_only and it is https:// on the CCC host; empty otherwise.
    std::string effective_pac_url() const;
    void apply_extension_blocklist();
    void notify_student(const std::string& title, const std::string& body, bool urgent);
    void restart_watchers();
    std::string active_user();
    std::vector<std::string> user_homes();

    Config cfg_;
    std::string config_path_;
    ServicePlatform& plat_;
    std::unique_ptr<EventQueue> queue_;
    IEventSink* sink_ = nullptr;

    // Actor plumbing. Declared before the I/O objects that reference them.
    Mailbox<AgentMessage> mailbox_{8192};
    Snapshot<UplinkView> view_;
    AgentStatus status_;
    std::unique_ptr<Uplink> uplink_;
    std::unique_ptr<Realtime> realtime_;
    std::unique_ptr<ScreenRelay> screen_relay_;  // helper frames -> gateway over mTLS (DECISIONS #24)
    Worker worker_;        // artifact walks
    Worker probe_worker_;  // LLM API port probes (F74)
    std::vector<std::thread> threads_;
    bool running_ = false;

    // Core-owned state (no locks: only the core thread touches it).
    RuleEngine rules_;
    Policy policy_;
    json realtime_cfg_;
    bool isolated_ = false;
    NetworkLock network_lock_ = NetworkLock::off;  // what set_network_lock() last installed successfully
    int64_t clock_offset_ms_ = 0;
    int64_t last_ack_seq_ = 0;
    HitDeduper dedup_;
    ProcessTracker tracker_;
    std::map<int64_t, std::string> rule_pids_;  // pid -> rule id, for stop events
    uint64_t suppressed_hits_ = 0;
    bool input_running_ = false;
    bool files_running_ = false;
    uint32_t paste_chords_ = 0;
    std::set<std::string> probed_ports_;
    std::string last_ext_hash_;
    json screen_caps_;
    double last_gpu_mem_ = -1;
    std::map<std::string, std::string> reported_tamper_;
    std::set<std::string> known_ports_, known_adapters_, known_extensions_;
    std::map<std::string, AdapterInfo> adapters_;
    std::vector<std::pair<int64_t, OutboundAiFlow>> recent_ai_flows_;
    int default_routes_ = -1;
    bool network_dirty_ = true;
    // Signature bundle the agent refused (bad signature, parse error, no domains). While the server keeps
    // announcing exactly this version, on_ack() does not re-trigger a fetch until the retry interval passes.
    std::string rejected_sig_version_;
    int64_t rejected_sig_at_ms_ = 0;
    bool rescan_extensions_ = false, rescan_artifacts_ = false;
    int64_t next_artifact_scan_ = 0;
    KeyStatsAggregator keys_;
    std::map<std::string, json> file_agg_;
    std::map<uint64_t, IIpcServer::Peer> helpers_;
    /// Token bucket per helper connection: helper data is user-controlled (I-AGENT-16, I-AGENT-23).
    struct PeerBudget { double tokens = 0; int64_t last_ms = 0; uint64_t dropped = 0; int64_t last_warn_ms = 0; };
    std::map<uint64_t, PeerBudget> peer_budget_;
    bool allow_helper_message(const IIpcServer::Peer& peer);
    struct LostHelper { int64_t since = 0; std::string user; };
    std::map<uint32_t, LostHelper> lost_helpers_;  // uid -> helper disconnect, pending the grace period
    int64_t helper_grace_ms_ = 90000;
    int64_t helper_missing_since_ = 0;
    bool helper_missing_reported_ = false;
    std::map<std::string, std::string> file_hashes_;
    int64_t wall_minus_steady_ = 0;
    int64_t started_steady_ = 0;
    uint64_t last_cpu_ms_ = 0;
    int64_t last_cpu_sample_ = 0;
    double cpu_pct_ = 0;
    // Screen streaming cost (screen Step 57): relay counters at the last heartbeat, the source helper's governor.
    uint64_t last_screen_frames_ = 0, last_screen_bytes_ = 0;
    int64_t last_screen_sample_ = 0;
    double screen_cpu_pct_ = 0;
    int64_t screen_level_ = 0, screen_cost_at_ = 0;
    std::map<std::string, int64_t> next_run_;
    bool reevaluate_ = false;
};

/// `xephyguard-agent enroll`: obtains the CA, generates a key + CSR and registers as Pending.
struct EnrollOptions {
    std::string key;
    std::string ccc_host;
    std::string ca_fingerprint;
    bool wait = true;
    int wait_timeout_s = 0;     // 0 = forever
    bool trust_on_first_use = false;
};
int enroll(Config& cfg, const std::string& config_path, const EnrollOptions& opts, ISystemInfo& sys);
/// Takes the CCC address from a discovery reply; never its CA fingerprint.
void apply_discovery(Config& cfg, const net::DiscoveryResult& d);

}  // namespace xg
