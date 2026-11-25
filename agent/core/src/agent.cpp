#include "xg/agent.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <thread>

#include "xg/artifact_scan.hpp"
#include "xg/crypto.hpp"
#include "xg/ipc_messages.hpp"
#include "xg/json_read.hpp"
#include "xg/log.hpp"
#include "xg/pusher.hpp"
#include "xg/util.hpp"
#include "xg/worker.hpp"

namespace xg {

namespace fs = std::filesystem;

namespace {
std::string join(const std::string& dir, const std::string& name) { return (fs::path(dir) / name).string(); }

const char* mode_label(Mode m) {
    switch (m) {
        case Mode::always_on: return "Always-on";
        case Mode::practice: return "Practice";
        case Mode::test: return "Test";
    }
    return "Always-on";
}

std::string limit(const std::string& s, size_t n) { return s.size() > n ? s.substr(0, n) : s; }
}  // namespace

Agent::Agent(Config cfg, std::string config_path, ServicePlatform& plat)
    : cfg_(std::move(cfg)), config_path_(std::move(config_path)), plat_(plat) {
    // Files from older versions may still carry inherited (user-readable) permissions (I-AGENT-2).
    for (const auto& secret : {config_path_, cfg_.key_file}) {
        std::error_code ec;
        if (!secret.empty() && fs::exists(secret, ec) && !protect_secret_file(secret))
            log::warn("cannot restrict access to {}", secret);
    }
    queue_ = std::make_unique<EventQueue>(join(cfg_.data_dir, "queue.db"));
    sink_ = queue_.get();
    uplink_ = std::make_unique<Uplink>(queue_.get(), view_, mailbox_, status_);
    probe_worker_.start();  // idle until scan_ports hands it candidates
    realtime_ = std::make_unique<Realtime>(view_, mailbox_, status_);
    screen_relay_ = std::make_unique<ScreenRelay>(view_, plat_.ipc.get(), mtls_screen_link_factory());
    policy_ = Policy::default_for(Mode::always_on);

    // Fail-safe: start from the last known policy and signatures, then refresh from the CCC.
    bool have_sigs = false;
    if (auto s = queue_->kv_get("signatures")) {
        auto j = json::parse(*s, nullptr, false);
        if (!j.is_discarded()) have_sigs = apply_signatures(j, false);
    }
    if (!have_sigs) {
        // Seed bundle installed by the package (root-owned). Used only until the CCC sends a signed one.
        auto seed = join(fs::path(config_path_).parent_path().string(), "signatures.json");
        bool ok = false;
        auto txt = read_file(seed, &ok);
        if (ok) {
            auto j = json::parse(txt, nullptr, false);
            if (!j.is_discarded()) {
                rules_.load(SignatureSet::from_json(j));
                if (plat_.network) {
        plat_.network->set_ai_domains(rules_.ai_domains());
        plat_.network->set_web_hints(rules_.web());
    }
                log::info("signatures: loaded seed bundle {} ({} rules)", rules_.version(), SignatureSet::from_json(j).rule_count());
            }
        }
    }
    if (auto p = queue_->kv_get("policy")) {
        auto j = json::parse(*p, nullptr, false);
        if (!j.is_discarded()) apply_policy(Policy::from_json(j));
    }
    if (auto r = queue_->kv_get("realtime")) {
        auto j = json::parse(*r, nullptr, false);
        if (!j.is_discarded()) realtime_cfg_ = j;
    }
    publish_view();
}

Agent::~Agent() = default;

// ------------------------------------------------------------------------------------- policy
void Agent::apply_policy(const Policy& p) {
    const Mode old_mode = policy_.mode;
    policy_ = p;
    rules_.set_disabled(p.disabled_rules);
    keys_.set_window(p.intervals.keystats_window_s);
    if (old_mode != p.mode) {
        log::info("policy: mode {} -> {} (policy v{}, profile '{}')", to_string(old_mode), to_string(p.mode), p.version,
                  p.profile_name);
        dedup_.reset();  // a new session should re-report what is already running
        reevaluate_ = true;
    }
    next_run_.clear();
    if (plat_.system) {
        plat_.system->set_exam_mode(p.mode == Mode::test);
        apply_extension_blocklist();
        plat_.system->set_browser_ai_overview_suppressed(p.suppress_ai_overview, p.google_search_url);
        plat_.system->set_browser_proxy_pac(effective_pac_url());
        const std::string dns_target =
            (p.proxy_mode == "gateway" && p.mode != Mode::always_on) ? cfg_.ccc_host : std::string{};
        plat_.system->set_lab_dns(dns_target);
    }
    apply_network_lock();
    push_helper_config();
    if (running_) restart_watchers();
}

bool Agent::apply_signatures(const json& env, bool require_signature) {
    if (!env.is_object() || !env.contains("bundle") || !env["bundle"].is_string()) return false;
    const std::string bundle = env["bundle"].get<std::string>();
    const std::string env_version = jr::str(env, "version");
    // Remember a refused version so the uplink loop does not refetch it in a tight loop.
    auto reject = [&] {
        rejected_sig_version_ = env_version;
        rejected_sig_at_ms_ = now_ms();
        return false;
    };
    if (require_signature) {
        bool ok = false;
        const std::string ca = read_file(cfg_.ca_file, &ok);
        if (!ok || !crypto::verify_signature(bundle, jr::str(env, "signature"), ca)) {
            log::error("signatures: bundle {} has an invalid signature; keeping {}", env_version, rules_.version());
            sink_->emit(ev::tamper, {{"kind", "signature_invalid"}, {"bundle_version", env_version}});
            return reject();
        }
    }
    auto j = json::parse(bundle, nullptr, false);
    if (j.is_discarded()) return reject();
    std::string err;
    auto set = SignatureSet::from_json(j, &err);
    if (!err.empty()) {
        log::error("signatures: parse error: {}", err);
        return reject();
    }
    // Rollback guard (I-AGENT-30): a validly signed but older bundle must not replace a newer one.
    if (require_signature) {
        if (auto c = compare_bundle_versions(set.version, rules_.version()); c && *c < 0) {
            log::warn("signatures: bundle {} is older than {}; keeping {}", set.version, rules_.version(), rules_.version());
            return reject();
        }
    }
    // A bundle without any AI/IDE domain would blind domain monitoring on this PC. Keep the last-known-good
    // bundle instead (a first load with nothing to lose is still accepted).
    if (set.ai_domains.empty() && set.ide_domains.empty() && !rules_.ai_domains().empty()) {
        log::warn("signatures: bundle {} has no domains; keeping {}", env_version, rules_.version());
        return reject();
    }
    rejected_sig_version_.clear();
    rejected_sig_at_ms_ = 0;
    log::info("signatures: loaded {} ({} rules)", set.version, set.rule_count());
    rules_.load(std::move(set));
    if (plat_.network) {
        plat_.network->set_ai_domains(rules_.ai_domains());
        plat_.network->set_web_hints(rules_.web());
    }
    if (queue_) queue_->kv_set("signatures", env.dump());
    dedup_.reset();
    known_extensions_.clear();
    last_ext_hash_.clear();
    rescan_extensions_ = true;
    push_helper_config();   // the helper filters WSL extensions with the bundle's ids; refresh them now
    apply_extension_blocklist();
    return true;
}

// ------------------------------------------------------------------------------------ rule hits
void Agent::handle_rule_hit(const RuleHit& hit, const std::string& subject, json context, int64_t pid) {
    const Policy& pol = policy_;
    if (!pol.rule_enabled(hit.rule_id)) return;
    auto actions = pol.actions_for(hit.rule_id, hit.severity);
    // While a PC is isolated, anything high-severity that tries to run is closed, whatever the mode.
    if (isolated_ && hit.severity >= Severity::high) {
        for (auto a : {Action::warn, Action::kill})
            if (std::find(actions.begin(), actions.end(), a) == actions.end()) actions.push_back(a);
    }
    const bool wants_kill = std::find(actions.begin(), actions.end(), Action::kill) != actions.end();

    auto do_kill = [&]() -> json {
        if (!wants_kill || pid <= 0 || !plat_.processes) return nullptr;
        const auto exe = normalize_exe(jr::str(context, "exe"));
        if (pol.kill_exempt.contains(exe)) return json{{"pid", pid}, {"killed", false}, {"reason", "exempt"}};
        const auto* known = tracker_.get(pid);
        const bool ok = plat_.processes->kill(pid, known ? known->start_time : 0);
        log::info("action: kill pid {} ({}) for {} -> {}", pid, exe, hit.rule_id, ok ? "ok" : "failed");
        return json{{"pid", pid}, {"killed", ok}};
    };

    if (!dedup_.should_report(hit.rule_id, subject, steady_ms())) {
        ++suppressed_hits_;
        do_kill();  // enforcement still applies to repeated launches; only the report is suppressed
        return;
    }

    json taken = json::array();
    json result = json::object();
    for (auto a : actions) {
        taken.push_back(to_string(a));
        switch (a) {
            case Action::warn:
                notify_student("XephyGuard notice",
                               hit.description + (wants_kill && pid > 0
                                                      ? " is not allowed right now and was closed. This has been recorded for your instructor."
                                                      : " was detected. This has been recorded for your instructor."),
                               hit.severity >= Severity::high);
                break;
            case Action::kill:
                result["kill"] = do_kill();
                break;
            case Action::log:
            case Action::alert:
            case Action::isolate:
                break;  // alert/isolate are executed by the CCC when it ingests the event
        }
    }
    sink_->emit(ev::rule_hit, {{"rule_id", hit.rule_id},
                               {"severity", to_string(hit.severity)},
                               {"category", hit.category},
                               {"description", hit.description},
                               {"subject", limit(subject, 512)},
                               {"actions", taken},
                               {"result", result},
                               {"context", std::move(context)},
                               {"mode", to_string(pol.mode)},
                               {"session_id", pol.session_id},
                               {"user", active_user()}});
    wake_uplink();  // alerts should reach the CCC within seconds
}

// ------------------------------------------------------------------------------------ processes
void Agent::on_process_started(const ProcessInfo& p) {
    auto hits = rules_.match_process(p);
    if (hits.empty()) return;
    // Prefer the binary name; fall back to the process name for versioned binaries ("2.1.316").
    auto exe = normalize_exe(p.exe_path.empty() ? p.name : p.exe_path);
    if (std::none_of(exe.begin(), exe.end(), [](unsigned char c) { return std::isalpha(c); }) && !p.name.empty())
        exe = normalize_exe(p.name);
    rule_pids_[p.pid] = hits.front().rule_id;
    sink_->emit(ev::process_start, {{"pid", p.pid}, {"ppid", p.ppid}, {"exe", exe}, {"user", p.user},
                                    {"rss_mb", p.rss_bytes / (1024 * 1024)}, {"rule_id", hits.front().rule_id}});
    for (const auto& h : hits)
        handle_rule_hit(h, exe, json{{"exe", exe}, {"pid", p.pid}, {"user", p.user}}, p.pid);
}

void Agent::scan_processes() {
    if (!plat_.processes) return;
    auto snap = plat_.processes->snapshot();
    const ProcessTracker::Diff d = tracker_.update(snap);
    std::vector<std::pair<int64_t, std::string>> stopped_hits;
    for (const auto& p : d.stopped)
        if (auto it = rule_pids_.find(p.pid); it != rule_pids_.end()) {
            stopped_hits.emplace_back(p.pid, it->second);
            rule_pids_.erase(it);
        }
    for (const auto& p : d.started) on_process_started(p);
    for (const auto& [pid, rule] : stopped_hits) sink_->emit(ev::process_stop, {{"pid", pid}, {"rule_id", rule}});
}

void Agent::process_snapshot() {
    ProcessTracker::NameSnapshot s = tracker_.name_snapshot();
    std::vector<ProcessInfo> procs = tracker_.running();
    const std::map<int64_t, std::string>& rules_by_pid = rule_pids_;
    auto is_sys_user = [](const std::string& u) {
        auto l = to_lower(u);
        return l.empty() || l == "system" || l == "local service" || l == "network service" || l == "root" ||
               l.starts_with("dwm-") || l.starts_with("umfd-");
    };
    std::sort(procs.begin(), procs.end(), [&](const ProcessInfo& a, const ProcessInfo& b) {
        const bool a_hit = rules_by_pid.contains(a.pid);
        const bool b_hit = rules_by_pid.contains(b.pid);
        if (a_hit != b_hit) return a_hit > b_hit;
        const bool a_active = (a.cpu_pct > 0.1 || a.net_kbps > 0.1);
        const bool b_active = (b.cpu_pct > 0.1 || b.net_kbps > 0.1);
        if (a_active != b_active) return a_active > b_active;
        const bool a_user = !is_sys_user(a.user);
        const bool b_user = !is_sys_user(b.user);
        if (a_user != b_user) return a_user > b_user;
        if (a.rss_bytes != b.rss_bytes) return a.rss_bytes > b.rss_bytes;
        return a.pid < b.pid;
    });
    json list = json::array();
    const size_t max_items = std::min<size_t>(procs.size(), 250);
    for (size_t i = 0; i < max_items; ++i) {
        const auto& p = procs[i];
        auto exe = normalize_exe(p.exe_path.empty() ? p.name : p.exe_path);
        if (exe.empty()) exe = normalize_exe(p.name);
        auto it = rules_by_pid.find(p.pid);
        const std::string rule_id = it != rules_by_pid.end() ? it->second : "";
        const double rss_mb = std::round((double(p.rss_bytes) / (1024.0 * 1024.0)) * 10.0) / 10.0;
        list.push_back({{"pid", p.pid},
                        {"ppid", p.ppid},
                        {"exe", limit(exe, 64)},
                        {"name", limit(p.name, 64)},
                        {"user", limit(p.user, 32)},
                        {"rss_mb", rss_mb},
                        {"cpu_pct", p.cpu_pct},
                        {"net_kbps", p.net_kbps},
                        {"net_conns", p.net_conns},
                        {"start", p.start_time},
                        {"rule_id", rule_id}});
    }
    // Only names (no command lines) and bounded process table; the hash lets the CCC detect divergence.
    if (s.added.size() > 200) s.added.resize(200);
    if (s.removed.size() > 200) s.removed.resize(200);
    sink_->emit(ev::process_snapshot, {{"hash", s.hash},
                                       {"count", s.count},
                                       {"total_running", procs.size()},
                                       {"added", s.added},
                                       {"removed", s.removed},
                                       {"list", std::move(list)}});
}

// ----------------------------------------------------------------------------------------- ports
void Agent::scan_ports() {
    if (!plat_.network) return;
    const auto socks = plat_.network->listening_sockets();
    const auto probe = rules_.probe();
    std::set<std::string> now_keys;
    struct ProbeJob { ListeningSocket sock; std::optional<RuleHit> rule; };
    std::vector<ProbeJob> to_probe;
    for (const auto& s : socks) {
        const std::string key = std::to_string(s.port) + "/" + std::to_string(s.pid);
        now_keys.insert(key);
        if (known_ports_.contains(key)) continue;
        const std::optional<RuleHit> hit = rules_.match_port(s.port);
        const auto needs_probe = rules_.match_port(s.port, /*probe_required=*/true);
        const bool candidate = needs_probe || (s.port > 1024 && s.rss_bytes >= probe.min_rss_mb * 1024 * 1024);
        if (hit) report_port(s, *hit, false);
        else if (probe.enabled && candidate && !probed_ports_.contains(key)) to_probe.push_back({s, needs_probe});
    }
    known_ports_ = std::move(now_keys);
    if (probed_ports_.size() > 1024) probed_ports_.clear();
    // Probes talk to servers any local user can run: they go to the probe worker with their own copies, and
    // results come back as ProbeDone messages, so the core thread never waits on them (F74).
    if (to_probe.empty()) return;
    const bool queued = probe_worker_.submit([this, jobs = to_probe, paths = probe.paths](const std::atomic<bool>& stop) {
        for (const auto& j : jobs) {
            if (stop) return;
            if (auto path = net::probe_llm_api(j.sock.address, j.sock.port, paths); !path.empty())
                post(msg::ProbeDone{j.sock, path, j.rule});
        }
    });
    if (queued)  // busy worker: the same candidates are offered again on the next scan
        for (const auto& j : to_probe) probed_ports_.insert(std::to_string(j.sock.port) + "/" + std::to_string(j.sock.pid));
}

void Agent::report_port(const ListeningSocket& s, const RuleHit& hit, bool probed) {
    sink_->emit(ev::listening_port, {{"port", s.port}, {"address", s.address}, {"process", s.process},
                                     {"pid", s.pid}, {"rule_id", hit.rule_id}, {"probed", probed}});
    handle_rule_hit(hit, std::to_string(s.port), json{{"port", s.port}, {"exe", s.process}, {"pid", s.pid}}, s.pid);
}

void Agent::on_probe_done(const msg::ProbeDone& p) {
    const std::string key = std::to_string(p.sock.port) + "/" + std::to_string(p.sock.pid);
    if (!known_ports_.contains(key)) return;  // the listener went away meanwhile
    const RuleHit hit = p.rule ? *p.rule
                               : RuleHit{"port.generic_llm_api", "local_llm",
                                         "Local server answering an LLM API path (" + p.path + ")", rules_.probe().severity};
    report_port(p.sock, hit, true);
}

// --------------------------------------------------------------------------------------- network
void Agent::scan_network() {
    if (!plat_.network) return;
    const auto list = plat_.network->adapters();
    const bool first = default_routes_ < 0;
    std::map<std::string, AdapterInfo> now;
    int routes = 0;
    for (const auto& a : list) {
        now[a.name] = a;
        if (a.is_default_route) ++routes;
    }
    auto emit_change = [&](const char* change, const AdapterInfo& a) {
        auto hit = rules_.classify_adapter(a);
        sink_->emit(ev::net_adapter, {{"change", change}, {"name", a.name}, {"description", limit(a.description, 128)},
                                      {"driver", a.driver}, {"mac", a.mac}, {"addresses", a.addresses}, {"up", a.up},
                                      {"wireless", a.is_wireless}, {"virtual", a.is_virtual},
                                      {"default_route", a.is_default_route}, {"kind", hit ? hit->rule_id : ""},
                                      {"initial", first}});
        if (hit && a.up && std::string(change) != "removed" && std::string(change) != "down")
            handle_rule_hit(*hit, a.name, json{{"adapter", a.name}, {"driver", a.driver}, {"initial", first}});
    };
    for (const auto& [name, a] : now) {
        auto it = adapters_.find(name);
        if (it == adapters_.end()) {
            if (!first || a.up) emit_change(first ? "present" : "added", a);
        } else if (it->second.up != a.up) {
            emit_change(a.up ? "up" : "down", a);
        } else if (it->second.addresses != a.addresses || it->second.is_default_route != a.is_default_route) {
            emit_change("changed", a);
        }
    }
    for (const auto& [name, a] : adapters_)
        if (!now.contains(name)) emit_change("removed", a);
    if (routes > 1 && (first || default_routes_ <= 1))
        if (auto hit = rules_.second_route_rule())
            handle_rule_hit(*hit, "default_route", json{{"default_routes", routes}});
    adapters_ = std::move(now);
    default_routes_ = routes;
}

// --------------------------------------------------------------------------------------- devices
void Agent::emit_device(const DeviceInfo& d, const std::optional<RuleHit>& hit, bool raise_hit) {
    sink_->emit(ev::device, {{"action", d.action}, {"subsystem", d.subsystem}, {"class", d.devclass},
                             {"vendor", d.vendor_id}, {"product", d.product_id}, {"model", limit(d.model, 128)},
                             {"serial_hash", d.serial_hash}, {"rule_id", hit ? hit->rule_id : ""}});
    if (hit && raise_hit)
        handle_rule_hit(*hit, d.vendor_id + ":" + d.product_id + ":" + d.serial_hash,
                        json{{"class", d.devclass}, {"vendor", d.vendor_id}, {"model", d.model}});
}

void Agent::scan_devices() {
    if (!plat_.devices || !policy_.collect.devices) return;
    for (const auto& d : plat_.devices->scan()) {
        auto hit = rules_.match_device(d);
        if (!hit && d.devclass != "usb_storage") continue;
        emit_device(d, hit, true);
    }
}

// ------------------------------------------------------------------------------ outbound AI traffic
namespace {
bool is_extension_host_proc(const std::string& p) {
    return p == "code" || p == "code - insiders" || p == "vscodium" || p == "codium" || p == "copilot-runtime" ||
           p == "copilot-language-server" || p == "copilot" || p == "cursor" || p == "windsurf" ||
           p == "antigravity";
}


double round_kb(uint64_t bytes) { return std::round((double(bytes) / 1024.0) * 10.0) / 10.0; }

/// Rule-hit context for one outbound AI flow (domain lowercased by the caller).
json flow_context(const OutboundAiFlow& f, const std::string& dom) {
    return json{{"process", f.process}, {"exe", f.process},          {"pid", f.pid},
                {"domain", dom},        {"remote_ip", f.remote_ip},   {"sent_kb", round_kb(f.bytes_sent)},
                {"recv_kb", round_kb(f.bytes_recv)}};
}

std::tuple<std::string, std::string, std::string> classify_ai_flow(const RuleEngine& rules,
                                                                   const std::string& dom,
                                                                   const std::string& proc) {
    if (auto hit = rules.match_ai_domain(dom)) {
        const std::string cat = (hit->category == "ide_ai_backend") ? "ai_extension" : "browser_ai";
        return {hit->rule_id, cat, hit->description};
    }
    if (is_extension_host_proc(proc)) {
        return {"net.ide_ai", "ai_extension", "IDE AI backend stream (" + dom + ")"};
    }
    return {"net.ai_traffic", "browser_ai", "Outbound AI network stream (" + dom + ")"};
}
}  // namespace

void Agent::scan_ai_traffic() {
    if (!plat_.network) return;
    const int64_t now = steady_ms();
    auto flows = plat_.network->active_ai_flows();
    recent_ai_flows_.erase(std::remove_if(recent_ai_flows_.begin(), recent_ai_flows_.end(),
                                          [now](const auto& kv) { return (now - kv.first) > 30000; }),
                           recent_ai_flows_.end());
    for (const auto& f : flows) recent_ai_flows_.emplace_back(now, f);
    if (flows.empty()) return;
    rescan_extensions_ = true;

    const bool ext_scan_active = plat_.extensions && policy_.collect.extensions;
    for (const auto& f : flows) {
        const auto dom = to_lower(f.domain);
        auto [rule_id, category, desc] = classify_ai_flow(rules_, dom, f.process);
        // Browsers and standalone AI apps / CLIs emit immediately; IDE extension-host AI streams defer to
        // scan_extensions() when extension scanning is enabled (which still falls back if no extension matches).
        if (category == "browser_ai" || rules_.is_browser(f.process) || !is_extension_host_proc(f.process) ||
            !ext_scan_active) {
            RuleHit hit{rule_id, category, desc, Severity::high};
            // A standalone program talking to an AI service (not a browser / IDE host) can be closed in Test mode.
            const bool closable = !rules_.is_browser(f.process) && !is_extension_host_proc(f.process);
            handle_rule_hit(hit, f.process + ":" + dom, flow_context(f, dom), closable ? f.pid : 0);
        }
    }
}

void Agent::scan_tunnels() {
    const auto& t = rules_.web().tunnel;
    if (!plat_.network || t.id.empty() || policy_.mode == Mode::always_on) return;
    for (const auto& f : plat_.network->active_tunnels()) {
        if (!rules_.web().is_tunnel(f.remote_ip, f.remote_port)) continue;  // never trust the adapter's filter alone
        RuleHit hit{t.id, "tunnel", t.description, t.severity};
        const std::string dest = f.remote_ip + ":" + std::to_string(f.remote_port);
        // pid 0: a tunnel is flagged for review, never closed automatically.
        handle_rule_hit(hit, f.process + ":" + dest,
                        json{{"process", f.process}, {"exe", f.process}, {"pid", f.pid}, {"remote_ip", f.remote_ip},
                             {"remote_port", f.remote_port}, {"signal", "tcp_destination"}});
    }
}

// ------------------------------------------------------------------------------------ extensions
void Agent::scan_extensions() {
    if (!plat_.extensions) return;
    scan_ai_traffic();
    auto exts = plat_.extensions->scan();
    std::vector<ProcessInfo> procs = tracker_.running();
    if (procs.empty() && plat_.processes) procs = plat_.processes->snapshot();

    std::vector<OutboundAiFlow> flows;
    const int64_t flows_now = steady_ms();
    for (const auto& [ts, f] : recent_ai_flows_)
        if (flows_now - ts <= 30000) flows.push_back(f);

    std::map<int64_t, ProcessInfo> proc_by_pid;
    std::set<std::string> running_exes;
    for (const auto& p : procs) {
        proc_by_pid[p.pid] = p;
        running_exes.insert(normalize_exe(p.exe_path.empty() ? p.name : p.exe_path));
        running_exes.insert(normalize_exe(p.name));
    }

    auto is_jetbrains_exe = [](const std::string& exe) {
        return exe == "idea64" || exe == "idea" || exe == "pycharm64" || exe == "pycharm" || exe == "clion64" ||
               exe == "clion" || exe == "webstorm64" || exe == "webstorm" || exe == "goland64" || exe == "goland" ||
               exe == "rider64" || exe == "rider" || exe == "phpstorm64" || exe == "phpstorm" || exe == "studio64" ||
               exe == "rustrover64" || exe == "datagrip64" || exe == "rubymine64";
    };

    auto ide_is_running = [&](const std::string& ide) {
        if (procs.empty()) return true;  // allow unit tests that omit process list
        if (ide == "vscode") return running_exes.contains("code");
        if (ide == "vscode-insiders") return running_exes.contains("code - insiders");
        if (ide == "vscodium") return running_exes.contains("vscodium") || running_exes.contains("codium");
        if (ide == "cursor") return running_exes.contains("cursor");
        if (ide == "windsurf") return running_exes.contains("windsurf");
        if (ide == "antigravity") return running_exes.contains("antigravity");
        if (ide == "jetbrains") {
            return std::any_of(running_exes.begin(), running_exes.end(), is_jetbrains_exe);
        }
        if (ide == "chrome") return running_exes.contains("chrome");
        if (ide == "edge") return running_exes.contains("msedge");
        if (ide == "brave") return running_exes.contains("brave");
        if (ide == "firefox") return running_exes.contains("firefox");
        return false;
    };

    // Determine which IDE (if any) owns the process/parent-chain of `f`, so VS Code traffic never flags JetBrains plugins.
    auto flow_belongs_to_ide = [&](const std::string& ide, const OutboundAiFlow& f) {
        if (!ide_is_running(ide)) return false;
        std::set<std::string> chain_exes;
        std::string chain_paths;
        chain_exes.insert(normalize_exe(f.process));
        int64_t cur = f.pid;
        for (int depth = 0; depth < 6 && cur > 4; ++depth) {
            auto it = proc_by_pid.find(cur);
            if (it == proc_by_pid.end()) break;
            chain_exes.insert(normalize_exe(it->second.exe_path.empty() ? it->second.name : it->second.exe_path));
            chain_exes.insert(normalize_exe(it->second.name));
            chain_paths += " " + to_lower(it->second.exe_path) + " " + to_lower(it->second.cmdline);
            if (it->second.ppid == cur) break;
            cur = it->second.ppid;
        }

        const bool is_vscode_chain = chain_exes.contains("code") || chain_paths.find(".vscode\\") != std::string::npos ||
                                     chain_paths.find(".vscode/") != std::string::npos ||
                                     chain_paths.find("microsoft vs code") != std::string::npos;
        const bool is_insiders_chain = chain_exes.contains("code - insiders") ||
                                       chain_paths.find(".vscode-insiders") != std::string::npos;
        const bool is_vscodium_chain = chain_exes.contains("vscodium") || chain_exes.contains("codium") ||
                                       chain_paths.find(".vscode-oss") != std::string::npos;
        const bool is_cursor_chain = chain_exes.contains("cursor") || chain_paths.find(".cursor\\") != std::string::npos ||
                                     chain_paths.find(".cursor/") != std::string::npos;
        const bool is_windsurf_chain = chain_exes.contains("windsurf") ||
                                       chain_paths.find(".windsurf\\") != std::string::npos ||
                                       chain_paths.find(".windsurf/") != std::string::npos;
        const bool is_antigravity_chain = chain_exes.contains("antigravity") ||
                                          chain_paths.find(".antigravity\\") != std::string::npos ||
                                          chain_paths.find(".antigravity/") != std::string::npos;
        const bool is_jb_chain = std::any_of(chain_exes.begin(), chain_exes.end(), is_jetbrains_exe) ||
                                 chain_paths.find("jetbrains") != std::string::npos ||
                                 chain_paths.find("github-copilot-intellij") != std::string::npos;

        const bool any_specific_ide = is_vscode_chain || is_insiders_chain || is_vscodium_chain || is_cursor_chain ||
                                      is_windsurf_chain || is_antigravity_chain || is_jb_chain;
        if (any_specific_ide) {
            if (ide == "vscode") return is_vscode_chain;
            if (ide == "vscode-insiders") return is_insiders_chain;
            if (ide == "vscodium") return is_vscodium_chain;
            if (ide == "cursor") return is_cursor_chain;
            if (ide == "windsurf") return is_windsurf_chain;
            if (ide == "antigravity") return is_antigravity_chain;
            if (ide == "jetbrains") return is_jb_chain;
            return false;
        }
        return true;
    };

    auto fmt_kb = [](uint64_t b) {
        double kb = std::round((double(b) / 1024.0) * 10.0) / 10.0;
        std::string s = std::to_string(kb);
        if (auto dot = s.find('.'); dot != std::string::npos && dot + 2 < s.size()) s.resize(dot + 2);
        return s;
    };

    auto flow_matches_ext = [&](const ExtensionInfo& e, const std::string& rule_id, const OutboundAiFlow& f) {
        if (!flow_belongs_to_ide(e.ide, f)) return false;
        // Backend domains come from the bundle (extension `domains` / `id_domains`, I-AGENT-19).
        const std::string dom = to_lower(f.domain);
        for (const auto& d : rules_.extension_domains(e, rule_id))
            if (dom == d || iends_with(dom, "." + d)) return true;
        return false;
    };

    std::set<std::string> ids;
    json ai = json::array();
    struct ActiveExtHit {
        RuleHit hit;
        ExtensionInfo ext;
        std::optional<OutboundAiFlow> flow;
    };
    std::vector<ActiveExtHit> active_hits;
    struct InactiveExtHit {
        RuleHit hit;
        ExtensionInfo ext;
    };
    std::vector<InactiveExtHit> inactive_test_hits;
    const bool test_mode = policy_.mode == Mode::test;
    std::vector<bool> flow_claimed(flows.size(), false);
    for (auto& e : exts) {
        auto h = rules_.match_extension(e);
        std::optional<OutboundAiFlow> matched_flow;
        if (h) {
            for (size_t fi = 0; fi < flows.size(); ++fi) {
                const auto& f = flows[fi];
                if (flow_matches_ext(e, h->rule_id, f)) {
                    if (!e.active) {
                        e.active = true;
                        e.activity =
                            f.domain + " (+" + fmt_kb(f.bytes_sent) + " KB sent, +" + fmt_kb(f.bytes_recv) + " KB recv)";
                        matched_flow = f;
                    }
                    flow_claimed[fi] = true;
                }
            }
        }

        ids.insert(e.ide + ":" + e.id + ":" + e.user + ":" + (e.active ? "1" : "0"));
        if (h) {
            ai.push_back({{"ide", e.ide},
                          {"id", e.id},
                          {"version", e.version},
                          {"user", e.user},
                          {"rule_id", h->rule_id},
                          {"description", h->description},
                          {"active", e.active},
                          {"activity", e.activity}});
            if (e.active) {
                active_hits.push_back({*h, e, matched_flow});
            } else if (test_mode) {
                inactive_test_hits.push_back({*h, e});
            }
        }
    }
    std::string joined;
    for (const auto& i : ids) joined += i + "\n";
    const auto hash = crypto::sha256_hex(joined);
    if (hash != last_ext_hash_) {
        std::vector<std::string> added, removed;
        std::set_difference(ids.begin(), ids.end(), known_extensions_.begin(), known_extensions_.end(),
                            std::back_inserter(added));
        std::set_difference(known_extensions_.begin(), known_extensions_.end(), ids.begin(), ids.end(),
                            std::back_inserter(removed));
        if (added.size() > 300) added.resize(300);
        if (removed.size() > 300) removed.resize(300);
        sink_->emit(ev::extension_scan, {{"count", ids.size()}, {"hash", hash}, {"ai", ai}, {"added", added},
                                         {"removed", removed}, {"initial", last_ext_hash_.empty()}});
        last_ext_hash_ = hash;
        known_extensions_ = std::move(ids);
    }
    for (const auto& item : active_hits) {
        RuleHit live_hit = item.hit;
        live_hit.description = item.hit.description + " (actively used in " + item.ext.ide + ")";
        json ctx{{"ide", item.ext.ide},
                 {"extension", item.ext.id},
                 {"version", item.ext.version},
                 {"user", item.ext.user},
                 {"activity", item.ext.activity}};
        if (item.flow) {
            ctx["domain"] = item.flow->domain;
            ctx["process"] = item.flow->process;
            ctx["remote_ip"] = item.flow->remote_ip;
            ctx["sent_kb"] = round_kb(item.flow->bytes_sent);
            ctx["recv_kb"] = round_kb(item.flow->bytes_recv);
        }
        handle_rule_hit(live_hit, item.ext.ide + ":" + item.ext.id + ":" + item.ext.user, std::move(ctx));
    }
    for (const auto& item : inactive_test_hits) {
        RuleHit hit = item.hit;
        hit.severity = Severity::low;
        hit.description = item.hit.description + " (installed in " + item.ext.ide + ")";
        handle_rule_hit(hit, "installed:" + item.ext.ide + ":" + item.ext.id + ":" + item.ext.user,
                        json{{"ide", item.ext.ide},
                             {"extension", item.ext.id},
                             {"version", item.ext.version},
                             {"user", item.ext.user},
                             {"activity", ""}});
    }
    // Any extension-host AI stream that did not match an installed extension (e.g., built-in Copilot Chat)
    // must still be emitted rather than silently dropped.
    for (size_t fi = 0; fi < flows.size(); ++fi) {
        if (flow_claimed[fi]) continue;
        const auto& f = flows[fi];
        if (!is_extension_host_proc(f.process) || rules_.is_browser(f.process)) continue;
        const auto dom = to_lower(f.domain);
        auto [rule_id, category, desc] = classify_ai_flow(rules_, dom, f.process);
        if (category != "ai_extension") continue;
        RuleHit hit{rule_id, category, desc + " (" + f.process + ")", Severity::high};
        handle_rule_hit(hit, f.process + ":" + dom, flow_context(f, dom));
    }
}

void Agent::on_device_event(const DeviceInfo& d) {
    if (!policy_.collect.devices) return;
    emit_device(d, rules_.match_device(d), d.action == "add");
}

void Agent::on_helper_conn(const IIpcServer::Peer& peer, bool connected) {
    if (connected) helpers_[peer.conn_id] = peer;
    else {
        helpers_.erase(peer.conn_id);
        peer_budget_.erase(peer.conn_id);
        if (screen_relay_->source() == peer.conn_id) screen_relay_->set_source(0);
    }
    if (!connected) {
        sink_->emit(ev::helper_status, {{"connected", false}, {"user", peer.user}, {"pid", peer.pid}});
        // Logoff also disconnects the helper: only a session that is still there after the grace period
        // without a helper counts as tampering (I-AGENT-17). check_helpers() decides.
        lost_helpers_.try_emplace(peer.uid, LostHelper{steady_ms(), peer.user});
        wake_uplink();
        check_helpers();
    } else {
        push_helper_config();
    }
}

// ----------------------------------------------------------------------------------- helper IPC
bool Agent::allow_helper_message(const IIpcServer::Peer& peer) {
    constexpr double kBurst = 200, kPerSecond = 50;
    const int64_t now = steady_ms();
    auto [it, fresh] = peer_budget_.try_emplace(peer.conn_id);
    auto& b = it->second;
    if (fresh) b = {kBurst, now, 0, 0};
    b.tokens = std::min(kBurst, b.tokens + double(now - b.last_ms) * kPerSecond / 1000.0);
    b.last_ms = now;
    if (b.tokens >= 1) {
        b.tokens -= 1;
        return true;
    }
    ++b.dropped;
    if (now - b.last_warn_ms > 60000) {
        b.last_warn_ms = now;
        log::warn("ipc: helper {} (pid {}) exceeds {} msg/s, {} messages dropped", peer.user, peer.pid, kPerSecond, b.dropped);
    }
    return false;
}

void Agent::on_helper_message(const IIpcServer::Peer& peer, const std::string& line) {
    if (line.size() > ipc::kMaxLine) return;
    if (!allow_helper_message(peer)) return;
    auto j = json::parse(line, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return;
    const std::string t = jr::str(j, "t");
    const Policy& pol = policy_;
    const std::string user = peer.user;
    if (t == "hello") {
        sink_->emit(ev::helper_status, {{"connected", true}, {"user", user}, {"session", limit(jr::str(j, "session"), 64)},
                                        {"display", limit(jr::str(j, "display"), 64)},
                                        {"caps", jr::obj(j, "caps")}, {"helper_version", jr::i64(j, "v")}});
        push_helper_config();
    } else if (t == "window") {
        if (!pol.collect.window) return;
        const auto app = normalize_exe(jr::str(j, "app"));
        const std::string raw_title = jr::str(j, "title");
        const std::string filtered_title = limit(pol.filter_title(app, raw_title), 256);


        // Browser windows only, patterns from the bundle (web.ai_sites, I-AGENT-18/19). A corroborating signal:
        // the proxy's domain decision is the authority.
        const std::string ai_site = rules_.match_ai_site(app, raw_title).value_or("");
        json wf{{"app", limit(app, 64)}, {"title", filtered_title}, {"user", user}};
        if (!ai_site.empty()) wf["ai_site"] = ai_site;
        sink_->emit(ev::window_focus, std::move(wf));

        if (!ai_site.empty()) {
            // Severity from the bundle (default medium): the title only corroborates the proxy's domain decision.
            RuleHit hit{"web.ai_tab", "browser_ai", "Active AI browser tab: " + ai_site, rules_.web().ai_tab_severity};
            handle_rule_hit(hit, app + ":" + ai_site,
                            // Title follows title_capture like window_focus (ETHICS: no raw titles by default).
                            json{{"app", app}, {"title", filtered_title}, {"ai_site", ai_site}, {"user", user},
                                 {"signal", "window_title"}});
        }
    } else if (t == "paste") {
        if (!pol.collect.clipboard) return;
        const bool is_copy = jr::boolean(j, "copy");
        // "app (file.cpp)" keeps the file part only with title_capture=full (I-AGENT-9).
        const bool keep_file = pol.title_capture == TitleCapture::full;
        auto norm_target = [keep_file](const std::string& s) -> std::string {
            auto pos = s.find(" (");
            if (pos != std::string::npos && s.ends_with(')'))
                return keep_file ? normalize_exe(s.substr(0, pos)) + s.substr(pos) : normalize_exe(s.substr(0, pos));
            return normalize_exe(s);
        };
        sink_->emit(is_copy ? ev::copy : ev::paste,
                    {{"chars", jr::i64(j, "chars", 0, 0, int64_t(1) << 40)}, {"lines", jr::i64(j, "lines", 0, 0, int64_t(1) << 40)},
                     {"code", jr::boolean(j, "code")}, {"score", jr::num(j, "score", 0.0, 0.0, 1.0)},
                     {"src", limit(norm_target(jr::str(j, "src")), 64)},
                     {"dst", limit(norm_target(jr::str(j, "dst")), 64)}, {"user", user}});
        wake_uplink();
    } else if (t == "keys") {
        if (!pol.collect.keys) return;
        // Rebuilt from a numeric allow-list: a user-level peer cannot smuggle other fields to the CCC (I-AGENT-10).
        json k = keystats_from_helper(j);
        k["user"] = user;
        k["source"] = "helper";
        sink_->emit(ev::key_stats, k);
    } else if (t == "paste_chord") {
        ++paste_chords_;
    } else if (t == "screen_cost") {
        // Numbers only, and only from the helper that streams: the governor's CPU % of one core and throttle level.
        if (screen_relay_->source() != peer.conn_id) return;
        screen_cpu_pct_ = jr::num(j, "cpu_pct", 0.0, 0.0, 1000.0);
        screen_level_ = jr::i64(j, "level", 0, 0, 1000);
        screen_cost_at_ = steady_ms();
    } else if (t == "screen_caps") {
        screen_caps_ = {
            {"session_type", limit(jr::str(j, "session_type", "x11"), 16)},
            {"snapshot", jr::boolean(j, "snapshot")},
            {"live", jr::boolean(j, "live")},
            {"delta_tiles", jr::boolean(j, "delta_tiles")},
            {"monitors", jr::i64(j, "monitors", 1, 1, 16)},
            {"secure_desktop", jr::boolean(j, "secure_desktop")},
        };
        // A helper that streams over IPC (screen Step 61) becomes the relay source, but only for the active
        // session: another logged-in user's helper must not feed this PC's stream.
        uint32_t active_uid = 0;
        bool have_active = false;
        if (plat_.system)
            for (const auto& s : plat_.system->sessions())
                if (s.active) { active_uid = s.uid; have_active = true; break; }
        const bool may_stream = cfg_.screen_enabled && jr::boolean(j, "ipc_stream") && have_active && peer.uid == active_uid;
        if (may_stream) screen_relay_->set_source(peer.conn_id);
        else if (screen_relay_->source() == peer.conn_id) screen_relay_->set_source(0);
    } else if (t == "ping") {
        if (plat_.ipc) plat_.ipc->send(peer.conn_id, ipc::line({{"t", "pong"}}));
    }
}

void Agent::push_helper_config() {
    if (!plat_.ipc) return;
    const Policy& p = policy_;
    const std::string pac_url = effective_pac_url();
    json mask_rules = json::array();
    for (const auto& m : p.screen_mask_rules) mask_rules.push_back({{"process", m.process}, {"title_regex", m.title_regex}});
    plat_.ipc->broadcast(ipc::line({{"t", "config"},
                                    {"mode", to_string(p.mode)},
                                    {"title_capture", to_string(p.title_capture)},
                                    {"ccc_host", cfg_.ccc_host},
                                    {"pc_id", cfg_.pc_id},
                                    {"ext_ids", rules_.extension_ids()},
                                    {"proxy_pac_url", pac_url},
                                    // No gateway address or credential: the helper streams through the service
                                    // (DECISIONS #24), so the student's session never holds a screen token.
                                    {"screen", {{"enabled", cfg_.screen_enabled}, {"privacy_rules", mask_rules}, {"codec", p.screen_codec},
                                                {"quality", p.screen_quality}, {"webp_method", p.screen_webp_method},
                                                {"cpu_budget_pct", p.screen_cpu_budget_pct}}},
                                    {"collect", {{"window", p.collect.window}, {"clipboard", p.collect.clipboard},
                                                 {"keys", p.collect.keys}}}}));
    plat_.ipc->broadcast(ipc::line({{"t", "indicator"},
                                    {"mode", to_string(p.mode)},
                                    {"text", std::string("Monitored by XephyGuard · mode: ") + mode_label(p.mode) +
                                                 (isolated_ ? " · network isolated" : "")},
                                    {"notice", p.notice_text}}));
}

void Agent::apply_extension_blocklist() {
    // Detection backed by enforcement: during a Test, AI browser extensions from the bundle are blocked by
    // managed browser policy; outside a Test the agent's entries are removed again (F80).
    if (!plat_.system) return;
    plat_.system->set_browser_extension_blocklist(policy_.mode == Mode::test ? rules_.browser_extension_ids()
                                                                             : std::vector<std::string>{});
}

std::string Agent::effective_pac_url() const {
    if (policy_.proxy_mode != "proxy_only" || policy_.proxy_pac_url.empty()) return {};
    // Written into browser policies and HKCU for every user: only an HTTPS PAC on the CCC itself, so neither a
    // bad policy value nor a LAN host serving a forged PAC over plain HTTP can redirect the lab's traffic (F97).
    if (!trusted_pac_url(policy_.proxy_pac_url, cfg_.ccc_host)) {
        log::warn("policy: ignoring proxy_pac_url {} (must be https on {})", limit(policy_.proxy_pac_url, 200),
                  cfg_.ccc_host);
        return {};
    }
    return policy_.proxy_pac_url;
}

void Agent::notify_student(const std::string& title, const std::string& body, bool urgent) {
    if (!plat_.ipc) return;
    plat_.ipc->broadcast(ipc::line({{"t", "notify"}, {"title", title}, {"body", body}, {"urgent", urgent}}));
}

void Agent::check_helpers() {
    if (!plat_.system) return;
    const auto sessions = plat_.system->sessions();
    std::vector<uint32_t> connected;
    for (const auto& [id, p] : helpers_) connected.push_back(p.uid);
    if (plat_.helper_launcher) plat_.helper_launcher->ensure_helpers(sessions, connected);
    const bool graphical = std::any_of(sessions.begin(), sessions.end(), [](const UserSession& s) {
        return s.session_type == "x11" || s.session_type == "wayland" || s.session_type == "console" ||
               s.session_type == "rdp";
    });
    const int64_t now = steady_ms();
    for (auto it = lost_helpers_.begin(); it != lost_helpers_.end();) {
        const uint32_t uid = it->first;
        const bool back = std::find(connected.begin(), connected.end(), uid) != connected.end();
        const bool session_left = std::none_of(sessions.begin(), sessions.end(), [&](const UserSession& s) {
            return s.uid == uid && (s.session_type == "x11" || s.session_type == "wayland" ||
                                    s.session_type == "console" || s.session_type == "rdp");
        });
        if (back || session_left) {
            it = lost_helpers_.erase(it);
        } else if (now - it->second.since >= helper_grace_ms_) {
            sink_->emit(ev::tamper, {{"kind", "helper_terminated"}, {"user", it->second.user},
                                     {"detail", "session helper process was disconnected or terminated"}});
            wake_uplink();
            it = lost_helpers_.erase(it);
        } else {
            ++it;
        }
    }
    if (graphical && connected.empty()) {
        if (helper_missing_since_ == 0) helper_missing_since_ = now;
        if (!helper_missing_reported_ && now - helper_missing_since_ > helper_grace_ms_) {
            helper_missing_reported_ = true;
            sink_->emit(ev::tamper, {{"kind", "helper_missing"}, {"user", active_user()},
                                     {"detail", "a user is logged in but the session helper is not running"}});
        }
    } else {
        helper_missing_since_ = 0;
        helper_missing_reported_ = false;
    }
}

// ------------------------------------------------------------------------------- isolation
void Agent::apply_isolation(bool on) {
    isolated_ = on;
    if (queue_) queue_->kv_set("isolated", on ? "1" : "0");  // survives agent restarts and reboots
    bool ok = true;
    if (plat_.isolator) {
        ok = on ? plat_.isolator->isolate({cfg_.ccc_host}) : plat_.isolator->restore();
        if (!ok) log::error("isolation: could not {} the network block", on ? "apply" : "remove");
    }
    sink_->emit(ev::net_adapter, {{"change", on ? "isolated" : "restored"}, {"name", "Internet"},
                                  {"description", on ? "Outbound Internet blocked (LAN still reachable)"
                                                     : "Outbound Internet restored"},
                                  {"driver", ""}, {"mac", ""}, {"addresses", json::array()}, {"up", !on},
                                  {"wireless", false}, {"virtual", false}, {"default_route", false},
                                  {"kind", on ? "net.isolated" : ""}, {"initial", false}});
    if (on) {
        dedup_.reset();
        reevaluate_ = true;  // close AI apps that are already running
    }
    push_helper_config();
}

NetworkLock Agent::wanted_network_lock() const {
    if (policy_.proxy_mode != "proxy_only" || policy_.mode == Mode::always_on) return NetworkLock::off;
    return policy_.network_lock;
}

void Agent::apply_network_lock(bool force) {
    if (!plat_.isolator) return;
    const NetworkLock want = wanted_network_lock();
    if (!force && want == network_lock_) return;
    if (plat_.isolator->set_network_lock(want, {cfg_.ccc_host})) {
        if (want != network_lock_) log::info("network lock: {} -> {}", to_string(network_lock_), to_string(want));
        network_lock_ = want;
    } else {
        // Retried by run_periodic; the heartbeat keeps reporting the lock that is really installed.
        log::error("network lock: could not apply '{}'", to_string(want));
    }
}

// ------------------------------------------------------------------------------- watchers, misc
void Agent::restart_watchers() {
    const Policy& p = policy_;
    if (plat_.files) {
        if (files_running_) { plat_.files->stop(); files_running_ = false; }
        if (p.collect.files && !p.work_dirs.empty()) {
            std::vector<std::string> dirs;
            for (const auto& h : user_homes())
                for (const auto& d : p.work_dirs) dirs.push_back(expand_path(d, h));
            files_running_ =
                plat_.files->watch(dirs, [this](const FileActivity& a) { post(msg::FileEvent{a}); });
        }
    }
    if (plat_.input) {
        if (p.collect.keys && !input_running_) {
            input_running_ = plat_.input->start(
                [this](int64_t t, bool bs, bool paste_chord) { post(msg::KeyEvent{t, bs, paste_chord}); });
            if (input_running_) log::info("input: key-timing monitor started (timing only, no key identities)");
        } else if (!p.collect.keys && input_running_) {
            plat_.input->stop();
            input_running_ = false;
            keys_.flush(now_ms(), true);  // discard
        }
    }
}

void Agent::on_file_activity(const FileActivity& a) {
    auto& agg = file_agg_[a.dir];
    if (agg.is_null())
        agg = {{"dir", a.dir}, {"saves", 0}, {"created", 0}, {"deleted", 0}, {"bytes_delta", 0}, {"exts", json::array()},
               {"first_ts", now_ms()}};
    if (a.action == "created") agg["created"] = agg["created"].get<int>() + 1;
    else if (a.action == "deleted") agg["deleted"] = agg["deleted"].get<int>() + 1;
    else agg["saves"] = agg["saves"].get<int>() + 1;
    agg["bytes_delta"] = agg["bytes_delta"].get<int64_t>() + a.size_delta;
    agg["total_bytes"] = a.total_bytes;
    auto& exts = agg["exts"];
    if (!a.file_ext.empty() && exts.size() < 10 && std::find(exts.begin(), exts.end(), a.file_ext) == exts.end())
        exts.push_back(a.file_ext);
}

void Agent::on_key(int64_t t, bool backspace, bool paste_chord) {
    if (paste_chord) {
        // Ask the user's helper to measure the clipboard being pasted (size/lines/code-likeness only).
        ++paste_chords_;
        if (plat_.ipc && policy_.collect.clipboard) plat_.ipc->broadcast(ipc::line({{"t", "paste_probe"}}));
        return;
    }
    if (input_running_) keys_.on_key(t, backspace);
}

void Agent::flush_keystats(int64_t now, bool force) {
    const std::optional<KeyStatsWindow> w = keys_.flush(now, force);
    const uint32_t chords = std::exchange(paste_chords_, 0u);
    if (!w && chords == 0) return;
    if (!policy_.collect.keys) return;
    json j = w ? w->to_json() : json{{"window_start", now}, {"keys", 0}};
    j["paste_chords"] = chords;
    j["user"] = active_user();
    j["source"] = "service";
    sink_->emit(ev::key_stats, j);
}

void Agent::flush_file_activity() {
    std::map<std::string, json> agg;
    agg.swap(file_agg_);
    for (auto& [dir, j] : agg) {
        j["user"] = active_user();
        sink_->emit(ev::file_activity, j);
    }
}

void Agent::sample_gpu() {
    if (!plat_.gpu) return;
    auto s = plat_.gpu->sample();
    if (!s) return;
    const bool spike = s->util_pct >= 30.0 || (last_gpu_mem_ >= 0 && std::abs(s->mem_used_mb - last_gpu_mem_) >= 512.0);
    last_gpu_mem_ = s->mem_used_mb;
    if (spike)
        sink_->emit(ev::gpu_sample, {{"util_pct", s->util_pct}, {"mem_used_mb", s->mem_used_mb},
                                     {"mem_total_mb", s->mem_total_mb}, {"gpu", s->name}});
}

std::string Agent::active_user() {
    if (!plat_.system) return {};
    for (const auto& s : plat_.system->sessions())
        if (s.active) return s.user;
    auto ss = plat_.system->sessions();
    return ss.empty() ? std::string{} : ss.front().user;
}

std::vector<std::string> Agent::user_homes() {
    std::vector<std::string> homes;
    if (!plat_.system) return homes;
    for (const auto& s : plat_.system->sessions())
        if (!s.home.empty() && std::find(homes.begin(), homes.end(), s.home) == homes.end()) homes.push_back(s.home);
    return homes;
}

// ---------------------------------------------------------------------------------- integrity
void Agent::check_integrity(bool initial) {
    auto report = [&](const std::string& kind, json detail) {
        const std::string key = kind + detail.dump();
        if (reported_tamper_[kind] == key) return;  // once per distinct state
        reported_tamper_[kind] = key;
        detail["kind"] = kind;
        sink_->emit(ev::tamper, detail);
        log::warn("tamper: {} {}", kind, detail.dump());
    };
    // Binaries vs. install manifest ("<sha256>  <path>" lines written by the package).
    std::map<std::string, std::string> manifest;
    bool mok = false;
    for (const auto& line : split(read_file(cfg_.manifest_file, &mok), '\n')) {
        auto parts = split(trim(line), ' ');
        if (parts.size() >= 2) manifest[parts.back()] = to_lower(parts.front());
    }
    std::vector<std::string> watched = {cfg_.helper_path};
    if (plat_.system) watched.insert(watched.begin(), plat_.system->self_exe_path());
    for (const auto& path : watched) {
        if (path.empty()) continue;
        auto h = crypto::sha256_file_hex(path);
        if (!h) {
            if (path == cfg_.helper_path && !initial) report("helper_binary_missing", {{"path", path}});
            continue;
        }
        if (auto it = manifest.find(path); it != manifest.end() && it->second != *h)
            report("binary_hash_mismatch", {{"path", path}, {"expected", it->second}, {"actual", *h}});
        auto& prev = file_hashes_[path];
        if (!initial && !prev.empty() && prev != *h) report("binary_changed", {{"path", path}});
        prev = *h;
    }
    // Configuration file (the agent updates its own expected hash after it saves).
    if (auto h = crypto::sha256_file_hex(config_path_)) {
        auto& prev = file_hashes_[config_path_];
        if (!initial && !prev.empty() && prev != *h) report("config_changed", {{"path", config_path_}});
        prev = *h;
    }
    // CA certificate presence / identity.
    bool ca_ok = false;
    const auto ca = read_file(cfg_.ca_file, &ca_ok);
    if (cfg_.enrolled()) {
        if (!ca_ok) report("ca_removed", {{"path", cfg_.ca_file}});
        else if (auto fp = crypto::cert_fingerprint(ca)) {
            auto& prev = file_hashes_[cfg_.ca_file];
            if (!initial && !prev.empty() && prev != *fp) report("ca_changed", {{"path", cfg_.ca_file}});
            prev = *fp;
            // The CA pinned at enrollment (config ca_fingerprint) must still be the one on disk; unlike the
            // check above this also catches a swap made while the agent was not running.
            if (!cfg_.ca_fingerprint.empty() && to_lower(cfg_.ca_fingerprint) != *fp)
                report("ca_changed", {{"path", cfg_.ca_file}, {"pinned", true}});
        }
    }
    // Clock: wall time moved relative to the monotonic clock.
    const int64_t wms = now_ms() - steady_ms();
    if (!initial && std::abs(wms - wall_minus_steady_) > 5000)
        report("clock_change", {{"delta_ms", wms - wall_minus_steady_}, {"at", now_ms()}});
    wall_minus_steady_ = wms;
}

// ------------------------------------------------------------------------------------ heartbeat
json Agent::build_heartbeat() {
    const Policy& p = policy_;
    json caps = json::object();
    caps["processes"] = plat_.processes != nullptr;
    caps["network"] = plat_.network != nullptr;
    caps["devices"] = plat_.devices != nullptr;
    caps["extensions"] = plat_.extensions != nullptr;
    caps["files"] = plat_.files != nullptr;
    caps["keys"] = plat_.input != nullptr && plat_.input->available();
    caps["gpu"] = plat_.gpu != nullptr && plat_.gpu->available();
    std::vector<UserSession> sess_list = plat_.system ? plat_.system->sessions() : std::vector<UserSession>{};
    {
        if (screen_caps_.is_object() && !screen_caps_.empty()) caps["screen"] = screen_caps_;
        else if (plat_.system) caps["screen"] = plat_.system->fallback_screen_caps(sess_list, cfg_.screen_enabled);
    }
    json usage = json::object();
    if (plat_.system) {
        auto u = plat_.system->self_usage();
        const int64_t now = steady_ms();
        if (last_cpu_sample_ > 0 && now > last_cpu_sample_)
            cpu_pct_ = 100.0 * double(u.cpu_time_ms - last_cpu_ms_) / double(now - last_cpu_sample_);
        last_cpu_ms_ = u.cpu_time_ms;
        last_cpu_sample_ = now;
        usage = {{"rss_kb", u.rss_bytes / 1024}, {"cpu_pct", std::round(cpu_pct_ * 100.0) / 100.0},
                 {"bytes_up", status_.bytes_up.load()}};
    }
    {
        // Streaming cost since the last heartbeat; present only while frames went up (nothing from the screen).
        const int64_t now = steady_ms();
        const uint64_t frames = screen_relay_->sent_frames(), bytes = screen_relay_->sent_bytes();
        if (last_screen_sample_ > 0 && now > last_screen_sample_ && frames > last_screen_frames_) {
            const double secs = double(now - last_screen_sample_) / 1000.0;
            json sc = {{"fps", std::round(double(frames - last_screen_frames_) / secs * 10.0) / 10.0},
                       {"kbps", std::round(double(bytes - last_screen_bytes_) * 8.0 / 1000.0 / secs)}};
            if (screen_cost_at_ > 0 && now - screen_cost_at_ <= 30000) {
                sc["cpu_pct"] = std::round(screen_cpu_pct_ * 10.0) / 10.0;
                sc["level"] = screen_level_;
            }
            usage["screen"] = sc;
        }
        last_screen_frames_ = frames;
        last_screen_bytes_ = bytes;
        last_screen_sample_ = now;
    }
    std::vector<std::string> users;
    if (plat_.system)
        for (const auto& s : plat_.system->sessions()) users.push_back(s.user + "@" + s.session_type);
    const int helpers = int(helpers_.size());
    return json{{"agent_version", XG_VERSION},
                {"uptime_s", plat_.system ? plat_.system->uptime_s() : 0},
                {"agent_uptime_s", (steady_ms() - started_steady_) / 1000},
                {"mode", to_string(p.mode)},
                {"session_id", p.session_id},
                {"policy_version", p.version},
                {"signatures_version", rules_.version()},
                {"user", active_user()},
                {"sessions", users},
                {"clock_offset_ms", clock_offset_ms_},
                {"local_time_ms", now_ms()},
                {"queue_depth", queue_ ? queue_->size() : 0},
                {"helpers", helpers},
                {"realtime", status_.realtime_connected.load()},
                {"suppressed_hits", suppressed_hits_},
                {"core_dropped", mailbox_.dropped()},
                {"network_lock", to_string(network_lock_)},
                {"capabilities", caps},
                {"usage", usage}};
}

// ------------------------------------------------------------------------- messages from the uplink
void Agent::on_policy_fetched(const json& j) {
    if (!j.is_object() || !j.contains("policy") || !j["policy"].is_object()) return;
    if (j.contains("signatures") && j["signatures"].is_object() && jr::str(j["signatures"], "version") != rules_.version())
        apply_signatures(j["signatures"], true);
    std::string err;
    Policy p = Policy::from_json(j["policy"], &err);
    if (!err.empty()) {
        log::error("policy: invalid policy from CCC: {}", err);
        return;
    }
    apply_policy(p);
    queue_->kv_set("policy", j["policy"].dump());
    if (j.contains("realtime") && j["realtime"].is_object()) {
        realtime_cfg_ = j["realtime"];
        queue_->kv_set("realtime", realtime_cfg_.dump());
    }
    publish_view();
}

void Agent::on_ack(const msg::AckReceived& a) {
    const auto& ack = a.ack;
    last_ack_seq_ = ack.ack_seq;
    if (ack.server_time_ms > 0) clock_offset_ms_ = protocol::clock_offset(a.t0_ms, a.t1_ms, ack.server_time_ms);
    if (ack.policy_version != 0 && ack.policy_version != policy_.version) uplink_->request_policy();
    if (!ack.signatures_version.empty() && ack.signatures_version != rules_.version()) {
        // Do not spin on a bundle we already refused: only refetch when the server announces a different
        // version, or when the retry interval (15 min) has elapsed.
        constexpr int64_t kRejectedSigRetryMs = 15 * 60 * 1000;
        const int64_t now = now_ms();
        if (ack.signatures_version != rejected_sig_version_) {
            uplink_->request_policy();
        } else if (now - rejected_sig_at_ms_ >= kRejectedSigRetryMs) {
            rejected_sig_at_ms_ = now;
            uplink_->request_policy();
        }
    }
    if (ack.isolated != isolated_) apply_isolation(ack.isolated);
}

void Agent::on_enroll_approved(const msg::EnrollApproved& e) {
    if (cfg_.enrolled()) return;
    write_file_atomic(cfg_.cert_file, e.cert_pem);
    if (!e.ca_pem.empty() && write_file_atomic(cfg_.ca_file, e.ca_pem))
        if (auto fp = crypto::cert_fingerprint(e.ca_pem)) cfg_.ca_fingerprint = *fp;  // keep the pin in step
    cfg_.pc_id = std::to_string(e.pc_id);
    cfg_.token = e.token;
    cfg_.poll_token.clear();
    cfg_.save(config_path_);
    if (auto h = crypto::sha256_file_hex(config_path_)) file_hashes_[config_path_] = *h;
    log::info("enrollment approved: this PC is #{}", cfg_.pc_id);
    publish_view();
    uplink_->request_policy();
    push_helper_config();
}

void Agent::command_result(const std::string& id, const std::string& status, const json& detail) {
    sink_->emit(ev::command_result, {{"command_id", id}, {"status", status}, {"detail", detail}});
    wake_uplink();
}

void Agent::handle_command(const protocol::Command& cmd) {
    log::info("command {} ({})", cmd.type, cmd.id);
    if (cmd.type == "policy_updated") {
        uplink_->request_policy();
        command_result(cmd.id, "ok", json::object());
    } else if (cmd.type == "message") {
        notify_student(limit(jr::str(cmd.args, "title", "Message from your instructor"), 200),
                       jr::str(cmd.args, "text"), jr::boolean(cmd.args, "urgent"));
        command_result(cmd.id, "ok", {{"helpers", helpers_.size()}});
    } else if (cmd.type == "kill") {
        json killed = json::array();
        const Policy& pol = policy_;
        // (pid, start time, exe): the start time pins the exact process the CCC saw (F35); a pid that was reused
        // since the instructor's process list was taken is left alone.
        struct Target { int64_t pid; int64_t start; std::string exe; };
        std::vector<Target> targets;
        auto exe_of = [](const ProcessInfo& p) { return normalize_exe(p.exe_path.empty() ? p.name : p.exe_path); };
        if (const int64_t pid = jr::i64(cmd.args, "pid", 0, 1); pid > 0) {
            const auto* p = tracker_.get(pid);
            targets.push_back({pid, jr::i64(cmd.args, "start_time", p ? p->start_time : 0), p ? exe_of(*p) : ""});
        }
        if (const auto want = normalize_exe(jr::str(cmd.args, "exe")); !want.empty() && plat_.processes)
            for (const auto& p : plat_.processes->snapshot())
                if (exe_of(p) == want) targets.push_back({p.pid, p.start_time, want});
        for (const auto& t : targets) {
            const bool ok = !pol.kill_exempt.contains(t.exe) && plat_.processes && plat_.processes->kill(t.pid, t.start);
            killed.push_back({{"pid", t.pid}, {"exe", t.exe}, {"killed", ok}});
        }
        notify_student("XephyGuard", "A program was closed by your instructor.", false);
        next_run_.erase("proc");
        next_run_.erase("snap");
        command_result(cmd.id, "ok", {{"processes", killed}});
    } else if (cmd.type == "isolate_notice") {
        const bool on = jr::boolean(cmd.args, "isolated");
        apply_isolation(on);
        notify_student("XephyGuard", on ? "Internet access for this PC was paused by the instructor. The local network still works."
                                        : "Internet access for this PC was restored.",
                       true);
        command_result(cmd.id, "ok", {{"isolated", isolated_}});
    } else if (cmd.type == "rescan") {
        const auto what = jr::str(cmd.args, "what", "all");
        if (what == "extensions" || what == "installed" || what == "all") rescan_extensions_ = true;
        if (what == "artifacts" || what == "extensions" || what == "installed" || what == "all") rescan_artifacts_ = true;
        if (what == "processes" || what == "all") {
            scan_processes();
            process_snapshot();
            next_run_.erase("proc");
            next_run_.erase("snap");
        }
        if (what == "network" || what == "ports" || what == "browser" || what == "all") {
            known_ports_.clear();
            scan_ports();
            scan_network();
            scan_ai_traffic();
        }
        if (what == "devices" || what == "network" || what == "all") {
            scan_devices();
        }
        command_result(cmd.id, "ok", json::object());
    } else if (cmd.type == "screen_lease") {
        // The CCC watches this PC's screen for ttl_s more seconds (sent with every screen token and renewal).
        const int64_t ttl = jr::i64(cmd.args, "ttl_s", 0, 0, 3600);
        if (ttl > 0 && cfg_.screen_enabled) screen_relay_->grant_lease(int(ttl));
        command_result(cmd.id, ttl > 0 && cfg_.screen_enabled ? "ok" : "ignored", json::object());
    } else if (cmd.type == "flush" || cmd.type == "ping") {
        wake_uplink();
        command_result(cmd.id, "ok", {{"pong", now_ms()}});
    } else {
        command_result(cmd.id, "ignored", json::object());
    }
}

// --------------------------------------------------------------------------------- artifacts
void Agent::maybe_start_artifact_scan(int64_t now) {
    if (!policy_.collect.artifacts || worker_.busy()) return;
    if (now < next_artifact_scan_ && !rescan_artifacts_) return;
    // The walk runs on the worker with its own copies; results come back as a message.
    auto rules = std::make_shared<const RuleEngine>(rules_);
    auto homes = user_homes();
    const bool queued = worker_.submit([this, rules, homes](const std::atomic<bool>& stop) {
        auto res = scan_artifacts(*rules, homes, stop);
        if (!stop) post(msg::ArtifactsScanned{std::move(res)});
    });
    if (!queued) return;
    rescan_artifacts_ = false;
    next_artifact_scan_ = now + int64_t(policy_.intervals.artifact_scan_s) * 1000;
}

void Agent::on_artifacts_scanned(const ArtifactScanResult& res) {
    json found = json::array();
    for (const auto& a : res.found) {
        std::string desc = "Local AI model file / store";
        Severity sev = Severity::medium;
        for (const auto& r : rules_.artifact_rules()) {
            if (r.id == a.rule_id) {
                desc = r.description;
                sev = r.severity;
                break;
            }
        }
        const double size_mb = std::round((double(a.size_bytes) / (1024.0 * 1024.0)) * 10.0) / 10.0;
        found.push_back({{"path", a.path}, {"size_mb", size_mb}, {"rule_id", a.rule_id}, {"description", desc}});
        handle_rule_hit(RuleHit{a.rule_id, "local_model", desc, sev}, a.path,
                        json{{"path", a.path}, {"size_mb", size_mb}, {"rule_id", a.rule_id}});
    }
    sink_->emit(ev::artifact_scan, {{"count", res.found.size()}, {"files_visited", res.files_visited},
                                    {"truncated", res.truncated}, {"found", found}});
    wake_uplink();
}

// ------------------------------------------------------------------------------------ main loop
void Agent::reevaluate_all() {
    // Processes, ports, adapters and extensions that were already present when the mode changed
    // are evaluated again under the new policy (e.g. an AI tool left open when a Test starts).
    std::vector<ProcessInfo> running;
    if (plat_.processes) running = plat_.processes->snapshot();
    tracker_.update(running);
    for (const auto& p : running) on_process_started(p);
    known_ports_.clear();
    adapters_.clear();
    default_routes_ = -1;
    last_ext_hash_.clear();
    known_extensions_.clear();
    network_dirty_ = true;
    rescan_extensions_ = true;
    next_run_.erase("ports");
    scan_devices();
}

void Agent::run_periodic(int64_t now) {
    if (std::exchange(reevaluate_, false)) reevaluate_all();
    const Policy& p = policy_;
    auto due = [&](const char* k, int s) {
        auto& t = next_run_[k];
        if (now >= t) {
            t = now + int64_t(s) * 1000;
            return true;
        }
        return false;
    };
    if (p.collect.processes && due("proc", p.intervals.process_scan_s)) scan_processes();
    if (p.collect.processes && due("snap", p.intervals.snapshot_s)) process_snapshot();
    if (p.collect.ports && due("ports", p.intervals.port_scan_s)) scan_ports();
    if (p.collect.network && (std::exchange(network_dirty_, false) || due("net", 30))) scan_network();
    if (p.collect.network && due("ai_net", 4)) scan_ai_traffic();
    if (p.collect.network && due("tunnels", 10)) scan_tunnels();
    if (p.collect.extensions && (std::exchange(rescan_extensions_, false) || due("ext", p.intervals.extension_scan_s)))
        scan_extensions();
    flush_keystats(now_ms(), false);
    if (due("files", 10)) flush_file_activity();
    if (p.collect.gpu && due("gpu", p.intervals.gpu_sample_s)) sample_gpu();
    if (due("integrity", p.intervals.integrity_check_s)) check_integrity(false);
    if (due("helpers", 2)) check_helpers();
    if (running_) maybe_start_artifact_scan(now);
    // Anti-tamper: if the isolation rule disappears while the PC is isolated, put it back and report it.
    if (isolated_ && plat_.isolator && due("isolation", 20) && !plat_.isolator->active()) {
        log::warn("isolation: block rule missing, re-applying");
        sink_->emit(ev::tamper, {{"kind", "isolation_rule_removed"}, {"user", active_user()},
                                 {"detail", "the network isolation rule was removed locally and has been re-applied"}});
        plat_.isolator->isolate({cfg_.ccc_host});
    }
    if (network_lock_ != NetworkLock::off && plat_.isolator && due("netlock", 20) &&
        plat_.isolator->network_lock() != network_lock_) {
        log::warn("network lock: rules missing, re-applying");
        sink_->emit(ev::tamper, {{"kind", "network_lock_removed"}, {"user", active_user()},
                                 {"detail", "the session network lock was removed locally and has been re-applied"}});
        apply_network_lock(true);
    } else if (plat_.isolator && wanted_network_lock() != network_lock_ && due("netlock_retry", 30)) {
        apply_network_lock();
    }
}

namespace {
template <class... F>
struct overloaded : F... {
    using F::operator()...;
};
template <class... F>
overloaded(F...) -> overloaded<F...>;
}  // namespace

void Agent::dispatch(AgentMessage& m) {
    std::visit(overloaded{
                   [&](msg::ProcessExec& e) {
                       if (tracker_.note_started(e.p) && policy_.collect.processes) on_process_started(e.p);
                   },
                   [&](msg::NetworkChanged&) { network_dirty_ = true; },
                   [&](msg::DeviceEvent& e) { on_device_event(e.d); },
                   [&](msg::HelperLine& e) { on_helper_message(e.peer, e.line); },
                   [&](msg::HelperConn& e) { on_helper_conn(e.peer, e.connected); },
                   [&](msg::FileEvent& e) { on_file_activity(e.a); },
                   [&](msg::KeyEvent& e) { on_key(e.t_ms, e.backspace, e.paste_chord); },
                   [&](msg::CommandMsg& e) { handle_command(e.cmd); },
                   [&](msg::PolicyFetched& e) { on_policy_fetched(e.body); },
                   [&](msg::AckReceived& e) { on_ack(e); },
                   [&](msg::EnrollApproved& e) { on_enroll_approved(e); },
                   [&](msg::ArtifactsScanned& e) { on_artifacts_scanned(e.result); },
                   [&](msg::ProbeDone& e) { on_probe_done(e); },
               },
               m);
}

void Agent::drain() {
    for (auto& m : mailbox_.take_all()) {
        try {
            dispatch(m);
        } catch (const std::exception& e) {
            log::error("core: message {} failed: {}", m.index(), e.what());
        }
    }
}

void Agent::publish_view() {
    UplinkView v;
    v.ccc_host = cfg_.ccc_host;
    v.api_port = cfg_.api_port;
    v.ca_file = cfg_.ca_file;
    v.cert_file = cfg_.cert_file;
    v.key_file = cfg_.key_file;
    v.token = cfg_.token;
    v.pc_id = cfg_.pc_id;
    v.enrollment_id = cfg_.enrollment_id;
    v.poll_token = cfg_.poll_token;
    v.enrolled = cfg_.enrolled();
    v.heartbeat_s = policy_.intervals.heartbeat_s;
    v.heartbeat = build_heartbeat();
    v.realtime = realtime_cfg_;
    view_.set(std::move(v));
}

int Agent::run(std::atomic<bool>& stop) {
    running_ = true;
    started_steady_ = steady_ms();
    wall_minus_steady_ = now_ms() - steady_ms();
    const bool clean = queue_->kv_get("clean_shutdown").value_or("1") == "1";
    queue_->kv_set("clean_shutdown", "0");
    sink_->emit(ev::agent_started, {{"version", XG_VERSION},
                                    {"previous_exit", clean ? "clean" : "unclean"},
                                    {"os", plat_.system ? plat_.system->os_name() + " " + plat_.system->os_version() : ""},
                                    {"hostname", plat_.system ? plat_.system->hostname() : ""}});
    check_integrity(true);

    // Platform callbacks run on platform threads: they only post to the core.
    if (plat_.ipc) {
        // Frames bypass the core (up to 4 MiB each): the IPC thread hands them straight to the relay queue.
        plat_.ipc->set_frame_handler(
            [this](const IIpcServer::Peer& peer, std::string&& f) { screen_relay_->push(peer.conn_id, std::move(f)); });
        const bool ok = plat_.ipc->start(
            cfg_.ipc_path,
            [this](const IIpcServer::Peer& peer, const std::string& line) { post(msg::HelperLine{peer, line}); },
            [this](const IIpcServer::Peer& peer, bool connected) { post(msg::HelperConn{peer, connected}); });
        if (!ok) log::error("ipc: cannot listen on {}", cfg_.ipc_path);
    }
    if (plat_.processes) plat_.processes->watch([this](const ProcessInfo& p) { post(msg::ProcessExec{p}); });
    if (plat_.network) plat_.network->watch([this] { post(msg::NetworkChanged{}); });
    if (plat_.devices) {
        scan_devices();
        plat_.devices->start([this](const DeviceInfo& d) { post(msg::DeviceEvent{d}); });
    }
    restart_watchers();
    push_helper_config();
    if (queue_->kv_get("isolated").value_or("0") == "1") {
        log::warn("isolation: PC was isolated before the agent (re)started; re-applying the network block");
        apply_isolation(true);  // the CCC lifts it with its next ack if the instructor already restored this PC
    }
    apply_network_lock(true);  // also removes a lock left behind by a crash when the policy no longer wants one

    publish_view();
    next_artifact_scan_ = steady_ms() + 5000;  // keep service startup fast
    worker_.start();
    threads_.emplace_back([this, &stop] { uplink_->run(stop); });
    threads_.emplace_back([this, &stop] { realtime_->run(stop); });
    threads_.emplace_back([this, &stop] { screen_relay_->run(stop); });
    if (plat_.service_host) plat_.service_host->notify_ready();
    log::info("agent {} running (pc {}, mode {})", XG_VERSION, cfg_.pc_id.empty() ? "unenrolled" : cfg_.pc_id,
              to_string(policy_.mode));

    int64_t next_tick = 0, next_view = 0;
    while (!stop) {
        const int64_t wait = std::clamp<int64_t>(next_tick - steady_ms(), 0, 250);
        for (auto& m : mailbox_.wait_take_all(std::chrono::milliseconds(wait))) {
            try {
                dispatch(m);
            } catch (const std::exception& e) {
                log::error("core: message {} failed: {}", m.index(), e.what());
            }
        }
        const int64_t now = steady_ms();
        if (now >= next_tick) {
            try {
                run_periodic(now);
            } catch (const std::exception& e) {
                log::error("periodic task failed: {}", e.what());
            }
            if (plat_.service_host) plat_.service_host->watchdog_ping();
            next_tick = now + 250;
        }
        if (now >= next_view) {
            try {
                publish_view();
            } catch (const std::exception& e) {
                log::error("core: publishing state failed: {}", e.what());
            }
            next_view = now + 1000;
        }
    }

    // Clean shutdown: announce it so the CCC can distinguish it from a kill or a network cut.
    log::info("agent stopping");
    if (plat_.service_host) plat_.service_host->notify_stopping();
    sink_->emit(ev::agent_stopping, {{"reason", "service_stop"}, {"uptime_s", (steady_ms() - started_steady_) / 1000}});
    flush_keystats(now_ms(), true);
    flush_file_activity();
    // Stop producers first, so nothing posts into a core that no longer runs.
    if (plat_.processes) plat_.processes->stop();
    if (plat_.network) plat_.network->stop();
    if (plat_.devices) plat_.devices->stop();
    if (plat_.files) plat_.files->stop();
    if (plat_.input) plat_.input->stop();
    wake_uplink();
    screen_relay_->request_stop();
    worker_.stop();
    probe_worker_.stop();
    for (auto& t : threads_) t.join();
    threads_.clear();
    mailbox_.close();
    publish_view();
    if (cfg_.enrolled()) uplink_->flush_once();
    queue_->kv_set("clean_shutdown", "1");
    if (plat_.system) {
        plat_.system->set_exam_mode(false);
        plat_.system->set_browser_extension_blocklist({});
        plat_.system->set_browser_ai_overview_suppressed(false);
        plat_.system->set_browser_proxy_pac({});
        plat_.system->set_lab_dns({});
    }
    if (plat_.ipc) {
        plat_.ipc->broadcast(ipc::line({{"t", "clear_proxy_pac"}}));
        plat_.ipc->stop();
    }
    running_ = false;
    return 0;
}

}  // namespace xg
