#pragma once
#include <atomic>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "xg/config.hpp"
#include "xg/event_queue.hpp"
#include "xg/hit_dedup.hpp"
#include "xg/http.hpp"
#include "xg/interfaces.hpp"
#include "xg/keystats.hpp"
#include "xg/policy.hpp"
#include "xg/process_tracker.hpp"
#include "xg/protocol.hpp"
#include "xg/rules.hpp"

namespace xg {

/// Runtime state shared between the agent threads and reported in heartbeats.
struct AgentStatus {
    std::atomic<bool> online{false};
    std::atomic<bool> realtime_connected{false};
    std::atomic<bool> isolated{false};
    std::atomic<int64_t> clock_offset_ms{0};
    std::atomic<int64_t> last_ack_seq{0};
    std::atomic<uint64_t> bytes_up{0};
    std::atomic<int> helpers_connected{0};
};

class Agent {
public:
    Agent(Config cfg, std::string config_path, ServicePlatform& platform);
    ~Agent();

    /// Main entry: blocks until `stop` becomes true.
    int run(std::atomic<bool>& stop);

    // ---- pieces exposed for unit tests ----------------------------------------------------
    void apply_policy(const Policy& p);
    /// `envelope` = {"version","bundle":"<json text>","signature":"<b64>"} as served by the CCC.
    bool apply_signatures(const json& envelope, bool require_signature = true);
    void handle_command(const protocol::Command& cmd);
    void handle_rule_hit(const RuleHit& hit, const std::string& subject, json context, int64_t pid = 0);
    void scan_processes();
    void scan_ports();
    void scan_network();
    void scan_extensions();
    void on_helper_message(const IIpcServer::Peer& peer, const std::string& line);
    void on_process_started(const ProcessInfo& p);
    /// Re-evaluates everything already present (called automatically after a mode change).
    void reevaluate_all();
    json build_heartbeat();
    Policy policy() const;
    IEventSink& sink() { return *sink_; }
    /// Replace the persistent queue by another sink (tests).
    void set_sink_for_tests(IEventSink* s) { sink_ = s; }
    RuleEngine& rules() { return rules_; }

private:
    // threads
    void uplink_loop(std::atomic<bool>& stop);
    void realtime_loop(std::atomic<bool>& stop);
    void artifact_loop(std::atomic<bool>& stop);
    bool flush_once();
    bool fetch_policy();
    bool poll_enrollment();
    void command_result(const std::string& id, const std::string& status, const json& detail);

    // periodic tasks
    void run_periodic(int64_t now_steady);
    void check_integrity(bool initial);
    void check_helpers();
    void process_snapshot();
    void sample_gpu();
    void flush_keystats(int64_t now, bool force);
    void flush_file_activity();
    void push_helper_config();
    void notify_student(const std::string& title, const std::string& body, bool urgent);
    void restart_watchers();
    std::string active_user();
    std::vector<std::string> user_homes();
    std::unique_ptr<net::HttpsClient> make_api_client(bool with_client_cert);
    net::TlsOptions tls_options(bool with_client_cert) const;

    Config cfg_;
    std::string config_path_;
    ServicePlatform& plat_;
    std::unique_ptr<EventQueue> queue_;
    IEventSink* sink_ = nullptr;
    RuleEngine rules_;
    mutable std::mutex policy_mu_;
    Policy policy_;
    json realtime_cfg_;
    AgentStatus status_;
    HitDeduper dedup_;
    ProcessTracker tracker_;
    std::mutex tracker_mu_;
    std::map<int64_t, std::string> rule_pids_;               // pid -> rule id, for stop events
    std::mutex hit_mu_;
    uint64_t suppressed_hits_ = 0;
    std::unique_ptr<net::HttpsClient> api_;
    std::mutex api_mu_;
    bool input_running_ = false;
    bool files_running_ = false;
    std::atomic<uint32_t> paste_chords_{0};
    std::set<std::string> probed_ports_;
    std::string last_ext_hash_;
    double last_gpu_mem_ = -1;
    bool warned_not_enrolled_ = false;
    std::map<std::string, std::string> reported_tamper_;
    std::set<std::string> known_ports_, known_adapters_, known_extensions_;
    std::map<std::string, AdapterInfo> adapters_;
    int default_routes_ = -1;
    std::atomic<bool> network_dirty_{true};
    std::atomic<bool> policy_dirty_{true};
    std::atomic<bool> rescan_extensions_{false}, rescan_artifacts_{false};
    std::mutex keys_mu_;
    KeyStatsAggregator keys_;
    std::mutex files_mu_;
    std::map<std::string, json> file_agg_;
    std::mutex helpers_mu_;
    std::map<uint64_t, IIpcServer::Peer> helpers_;
    int64_t helper_missing_since_ = 0;
    bool helper_missing_reported_ = false;
    std::map<std::string, std::string> file_hashes_;
    int64_t wall_minus_steady_ = 0;
    int64_t started_steady_ = 0;
    uint64_t last_cpu_ms_ = 0;
    int64_t last_cpu_sample_ = 0;
    double cpu_pct_ = 0;
    std::map<std::string, int64_t> next_run_;
    std::condition_variable flush_cv_;
    std::atomic<bool> wake_uplink_{false};
    std::atomic<bool> reevaluate_{false};
    void wake_uplink() { wake_uplink_ = true; flush_cv_.notify_all(); }
    std::mutex flush_mu_;
    std::vector<std::thread> threads_;
    std::atomic<bool>* stop_ = nullptr;
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

}  // namespace xg
