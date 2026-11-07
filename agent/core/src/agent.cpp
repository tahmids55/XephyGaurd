#include "xg/agent.hpp"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <cstdio>
#include <cstdlib>
#endif

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <sstream>
#include <thread>

#include "xg/artifact_scan.hpp"
#include "xg/crypto.hpp"
#include "xg/ipc_messages.hpp"
#include "xg/log.hpp"
#include "xg/pusher.hpp"
#include "xg/util.hpp"

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

#ifdef _WIN32
std::wstring utf8_to_wide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}

void run_cmd_silent(const std::string& cmd, DWORD timeout_ms = 8000) {
    std::wstring wcmd = L"cmd.exe /C " + utf8_to_wide(cmd);
    STARTUPINFOW si{};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    if (CreateProcessW(nullptr, wcmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, timeout_ms);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}

std::string run_cmd_capture(const std::string& cmd, DWORD timeout_ms = 5000) {
    SECURITY_ATTRIBUTES sa{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE r_pipe = nullptr, w_pipe = nullptr;
    if (!CreatePipe(&r_pipe, &w_pipe, &sa, 0)) return {};
    SetHandleInformation(r_pipe, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si{};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput = w_pipe;
    si.hStdError = w_pipe;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    std::wstring wcmd = L"cmd.exe /C " + utf8_to_wide(cmd);
    std::string out;
    if (CreateProcessW(nullptr, wcmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        CloseHandle(w_pipe);
        w_pipe = nullptr;
        char buf[1024];
        DWORD n = 0;
        while (ReadFile(r_pipe, buf, sizeof buf, &n, nullptr) && n > 0 && out.size() < 65536) {
            out.append(buf, n);
        }
        WaitForSingleObject(pi.hProcess, timeout_ms);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    if (w_pipe) CloseHandle(w_pipe);
    CloseHandle(r_pipe);
    return out;
}
#else
void run_cmd_silent(const std::string& cmd) {
    [[maybe_unused]] int rc = std::system((cmd + " >/dev/null 2>&1").c_str());
}

std::string run_cmd_capture(const std::string& cmd) {
    std::string out;
    FILE* fp = popen((cmd + " 2>/dev/null").c_str(), "r");
    if (!fp) return out;
    char buf[512];
    while (fgets(buf, sizeof buf, fp) && out.size() < 65536) {
        out += buf;
    }
    pclose(fp);
    return out;
}
#endif

bool is_safe_linux_route(const std::string& r) {
    if (!r.starts_with("default ")) return false;
    return std::all_of(r.begin(), r.end(), [](unsigned char c) {
        return std::isalnum(c) || c == ' ' || c == '.' || c == ':' || c == '/' || c == '_' || c == '-';
    });
}

bool is_safe_ip_token(const std::string& s) {
    if (s.empty()) return false;
    return std::all_of(s.begin(), s.end(), [](unsigned char c) {
        return std::isdigit(c) || c == '.' || c == ':';
    });
}
}  // namespace

std::vector<WindowsDefaultRoute> parse_windows_default_routes(const std::string& route_print_output) {
    std::vector<WindowsDefaultRoute> routes;
    for (const auto& raw_line : split(route_print_output, '\n')) {
        std::istringstream iss(raw_line);
        std::string dest, mask, next_hop, iface, metric;
        if ((iss >> dest >> mask >> next_hop) && dest == "0.0.0.0" && mask == "0.0.0.0") {
            next_hop = trim(next_hop);
            if (next_hop.empty() || next_hop == "0.0.0.0" || icontains(next_hop, "on-link") ||
                !is_safe_ip_token(next_hop)) {
                continue;
            }
            iss >> iface >> metric;
            iface = trim(iface);
            metric = trim(metric);
            if (!is_safe_ip_token(iface)) iface.clear();
            if (!std::all_of(metric.begin(), metric.end(), [](unsigned char c) { return std::isdigit(c); })) {
                metric.clear();
            }
            bool dup = false;
            for (const auto& existing : routes) {
                if (existing.gateway == next_hop && existing.interface_ip == iface) {
                    dup = true;
                    break;
                }
            }
            if (!dup) routes.push_back({next_hop, iface, metric});
        }
    }
    return routes;
}

std::vector<std::string> parse_linux_default_routes(const std::string& ip_route_output) {
    std::vector<std::string> routes;
    for (const auto& raw_line : split(ip_route_output, '\n')) {
        std::string line = trim(raw_line);
        if (is_safe_linux_route(line)) {
            if (std::find(routes.begin(), routes.end(), line) == routes.end()) {
                routes.push_back(std::move(line));
            }
        }
    }
    return routes;
}

std::string update_hosts_blocklist_content(const std::string& existing_content,
                                           const std::vector<std::string>& domains,
                                           const std::string& nl) {
    const std::string begin_marker = "# BEGIN XEPHYGUARD BLOCKLIST";
    const std::string end_marker = "# END XEPHYGUARD BLOCKLIST";
    std::ostringstream cleaned;
    bool inside = false;
    for (const auto& raw_line : split(existing_content, '\n')) {
        std::string line = raw_line;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (trim(line) == begin_marker) { inside = true; continue; }
        if (trim(line) == end_marker) { inside = false; continue; }
        if (!inside) cleaned << line << nl;
    }
    if (!domains.empty()) {
        cleaned << begin_marker << nl;
        for (const auto& d : domains) {
            if (d.empty() || d.find_first_of(" \t\r\n#") != std::string::npos) continue;
            cleaned << "0.0.0.0 " << d << nl;
            if (!d.starts_with("www.")) cleaned << "0.0.0.0 www." << d << nl;
        }
        cleaned << end_marker << nl;
    }
    return cleaned.str();
}

Agent::Agent(Config cfg, std::string config_path, ServicePlatform& plat)
    : cfg_(std::move(cfg)), config_path_(std::move(config_path)), plat_(plat) {
    queue_ = std::make_unique<EventQueue>(join(cfg_.data_dir, "queue.db"));
    sink_ = queue_.get();
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
    // Fail-safe (Contract C3 / R11): if a saved isolation state exists on startup (e.g. after a crash),
    // restore the saved routes/firewall state immediately and remove the file.
    std::error_code ec;
    if (fs::exists(isolation_state_path(), ec)) {
        log::warn("network: found leftover isolation_saved.json on startup; restoring network state");
        restore_isolation_from_disk();
    }
}

Agent::~Agent() {
    if (!active_blocked_domains_.empty()) apply_hosts_blocklist({});
}

Policy Agent::policy() const {
    std::lock_guard lk(policy_mu_);
    return policy_;
}

// ------------------------------------------------------------------------------------- policy
void Agent::apply_policy(const Policy& raw_policy) {
    Policy p = raw_policy;
    p.clamp_for_mode();
    Mode old_mode;
    {
        std::lock_guard lk(policy_mu_);
        old_mode = policy_.mode;
        policy_ = p;
    }
    rules_.set_disabled(p.disabled_rules);
    {
        std::lock_guard lk(keys_mu_);
        keys_.set_window(p.intervals.keystats_window_s);
    }
    if (old_mode != p.mode) {
        log::info("policy: mode {} -> {} (policy v{}, profile '{}')", to_string(old_mode), to_string(p.mode), p.version,
                  p.profile_name);
        std::lock_guard lk(hit_mu_);
        dedup_.reset();  // a new session should re-report what is already running
        inventory_dedup_.reset();
        reevaluate_ = true;
    }
    next_run_.clear();
    apply_hosts_blocklist(p.mode == Mode::test ? p.blocked_domains : std::vector<std::string>{});
    push_helper_config();
    if (stop_) restart_watchers();
}

bool Agent::apply_signatures(const json& env, bool require_signature) {
    if (!env.is_object() || !env.contains("bundle") || !env["bundle"].is_string()) return false;
    const std::string bundle = env["bundle"].get<std::string>();
    if (require_signature) {
        bool ok = false;
        const std::string ca = read_file(cfg_.ca_file, &ok);
        if (!ok || !crypto::verify_signature(bundle, env.value("signature", ""), ca)) {
            log::error("signatures: bundle {} has an invalid signature; keeping {}", env.value("version", "?"),
                       rules_.version());
            sink_->emit(ev::tamper, {{"kind", "signature_invalid"}, {"bundle_version", env.value("version", "")}});
            return false;
        }
    }
    auto j = json::parse(bundle, nullptr, false);
    if (j.is_discarded()) return false;
    std::string err;
    auto set = SignatureSet::from_json(j, &err);
    if (!err.empty()) {
        log::error("signatures: parse error: {}", err);
        return false;
    }
    log::info("signatures: loaded {} ({} rules)", set.version, set.rule_count());
    rules_.load(std::move(set));
    if (queue_) queue_->kv_set("signatures", env.dump());
    {
        std::lock_guard lk(hit_mu_);
        dedup_.reset();
    }
    known_extensions_.clear();
    last_ext_hash_.clear();
    rescan_extensions_ = true;
    return true;
}

// ------------------------------------------------------------------------------------ rule hits
void Agent::handle_rule_hit(const RuleHit& hit, const std::string& subject, json context, int64_t pid) {
    const Policy pol = policy();
    if (!pol.rule_enabled(hit.rule_id)) return;
    const auto actions = pol.actions_for(hit.rule_id, hit.severity);
    const bool is_inventory =
        hit.rule_id.starts_with("ext.") || hit.rule_id.starts_with("file.") || hit.category == "local_model";
    // Contract C4: Kills only when an active Test session exists AND the profile action includes kill.
    const bool wants_kill = (pol.mode == Mode::test) && !is_inventory &&
                            (std::find(actions.begin(), actions.end(), Action::kill) != actions.end());

    auto do_kill = [&]() -> json {
        if (!wants_kill || pid <= 0 || !plat_.processes) return nullptr;
        const auto exe = normalize_exe(context.value("exe", std::string{}));
        if (pol.kill_exempt.contains(exe)) return json{{"pid", pid}, {"killed", false}, {"reason", "exempt"}};
        const bool ok = plat_.processes->kill(pid);
        log::info("action: kill pid {} ({}) for {} -> {}", pid, exe, hit.rule_id, ok ? "ok" : "failed");
        return json{{"pid", pid}, {"killed", ok}};
    };

    bool report;
    {
        std::lock_guard lk(hit_mu_);
        const int64_t now = steady_ms();
        report = is_inventory ? inventory_dedup_.should_report(hit.rule_id, subject, now)
                              : dedup_.should_report(hit.rule_id, subject, now);
        if (!report) ++suppressed_hits_;
    }
    if (!report) {
        do_kill();  // enforcement still applies to repeated launches; only the report is suppressed
        return;
    }

    json taken = json::array();
    json result = json::object();
    for (auto a : actions) {
        if (a == Action::kill && !wants_kill) continue;
        if (a == Action::warn && (pol.mode == Mode::always_on || is_inventory)) continue;
        taken.push_back(to_string(a));
        switch (a) {
            case Action::warn:
                notify_student("XephyGuard notice",
                               hit.description + " was detected. This has been recorded for your instructor.",
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
    {
        std::lock_guard lk(tracker_mu_);
        rule_pids_[p.pid] = hits.front().rule_id;
    }
    sink_->emit(ev::process_start, {{"pid", p.pid}, {"ppid", p.ppid}, {"exe", exe}, {"user", p.user},
                                    {"rss_mb", p.rss_bytes / (1024 * 1024)}, {"rule_id", hits.front().rule_id}});
    for (const auto& h : hits)
        handle_rule_hit(h, exe, json{{"exe", exe}, {"pid", p.pid}, {"user", p.user}}, p.pid);
}

void Agent::scan_processes() {
    if (!plat_.processes) return;
    auto snap = plat_.processes->snapshot();
    ProcessTracker::Diff d;
    std::vector<std::pair<int64_t, std::string>> stopped_hits;
    {
        std::lock_guard lk(tracker_mu_);
        d = tracker_.update(snap);
        for (const auto& p : d.stopped)
            if (auto it = rule_pids_.find(p.pid); it != rule_pids_.end()) {
                stopped_hits.emplace_back(p.pid, it->second);
                rule_pids_.erase(it);
            }
    }
    for (const auto& p : d.started) on_process_started(p);
    for (const auto& [pid, rule] : stopped_hits) sink_->emit(ev::process_stop, {{"pid", pid}, {"rule_id", rule}});
}

void Agent::process_snapshot() {
    ProcessTracker::NameSnapshot s;
    {
        std::lock_guard lk(tracker_mu_);
        s = tracker_.name_snapshot();
    }
    // Only names (no command lines) and only the diff; the hash lets the CCC detect divergence.
    if (s.added.size() > 200) s.added.resize(200);
    if (s.removed.size() > 200) s.removed.resize(200);
    sink_->emit(ev::process_snapshot, {{"hash", s.hash}, {"count", s.count}, {"added", s.added}, {"removed", s.removed}});
}

// ----------------------------------------------------------------------------------------- ports
void Agent::scan_ports() {
    if (!plat_.network) return;
    const auto socks = plat_.network->listening_sockets();
    const auto probe = rules_.probe();
    std::set<std::string> now_keys;
    for (const auto& s : socks) {
        const std::string key = std::to_string(s.port) + "/" + std::to_string(s.pid);
        now_keys.insert(key);
        if (known_ports_.contains(key)) continue;
        std::optional<RuleHit> hit = rules_.match_port(s.port);
        const auto needs_probe = rules_.match_port(s.port, /*probe_required=*/true);
        bool probed = false;
        const bool candidate = needs_probe || (s.port > 1024 && s.rss_bytes >= probe.min_rss_mb * 1024 * 1024);
        if (!hit && probe.enabled && candidate && !probed_ports_.contains(key)) {
            probed_ports_.insert(key);
            for (const auto& path : probe.paths) {
                auto r = net::http_get_plain("127.0.0.1", s.port, path, 400);
                if ((r.status == 200 || r.status == 400 || r.status == 405 || r.status == 422) &&
                    (icontains(r.headers["content-type"], "json") || r.body.starts_with("{")) &&
                    (path.find("models") == std::string::npos || r.body.find("\"data\"") != std::string::npos ||
                     r.body.find("\"models\"") != std::string::npos)) {
                    hit = needs_probe ? needs_probe
                                      : RuleHit{"port.generic_llm_api", "local_llm",
                                                "Local server answering an LLM API path (" + path + ")", probe.severity};
                    probed = true;
                    break;
                }
            }
        }
        if (hit) {
            sink_->emit(ev::listening_port, {{"port", s.port}, {"address", s.address}, {"process", s.process},
                                             {"pid", s.pid}, {"rule_id", hit->rule_id}, {"probed", probed}});
            handle_rule_hit(*hit, std::to_string(s.port), json{{"port", s.port}, {"exe", s.process}, {"pid", s.pid}},
                            s.pid);
        }
    }
    known_ports_ = std::move(now_keys);
    if (probed_ports_.size() > 1024) probed_ports_.clear();
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

// ------------------------------------------------------------------------------------ extensions
void Agent::scan_extensions() {
    if (!plat_.extensions) return;
    auto exts = plat_.extensions->scan();
    std::set<std::string> ids;
    json ai = json::array();
    std::vector<std::pair<RuleHit, ExtensionInfo>> hits;
    for (const auto& e : exts) {
        ids.insert(e.ide + ":" + e.id + ":" + e.user);
        if (auto h = rules_.match_extension(e)) {
            ai.push_back({{"ide", e.ide}, {"id", e.id}, {"version", e.version}, {"user", e.user}, {"rule_id", h->rule_id}});
            hits.emplace_back(*h, e);
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
    for (const auto& [h, e] : hits)
        handle_rule_hit(h, e.ide + ":" + e.id + ":" + e.user,
                        json{{"ide", e.ide}, {"extension", e.id}, {"version", e.version}, {"user", e.user}});
}

// ----------------------------------------------------------------------------------- helper IPC
void Agent::on_helper_message(const IIpcServer::Peer& peer, const std::string& line) {
    if (line.size() > ipc::kMaxLine) return;
    auto j = json::parse(line, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return;
    const std::string t = j.value("t", "");
    const Policy pol = policy();
    const std::string user = peer.user;
    if (t == "hello") {
        sink_->emit(ev::helper_status, {{"connected", true}, {"user", user}, {"session", j.value("session", "")},
                                        {"display", j.value("display", "")},
                                        {"caps", j.value("caps", json::object())}, {"helper_version", j.value("v", 0)}});
        push_helper_config();
    } else if (t == "window") {
        if (!pol.collect.window) return;
        const auto app = normalize_exe(j.value("app", ""));
        sink_->emit(ev::window_focus, {{"app", limit(app, 64)},
                                       {"title", limit(pol.filter_title(app, j.value("title", "")), 256)},
                                       {"user", user}});
    } else if (t == "paste") {
        if (!pol.collect.clipboard) return;
        const bool is_copy = j.value("copy", false);
        sink_->emit(is_copy ? ev::copy : ev::paste,
                    {{"chars", j.value("chars", 0)}, {"lines", j.value("lines", 0)}, {"code", j.value("code", false)},
                     {"score", j.value("score", 0.0)}, {"src", limit(normalize_exe(j.value("src", "")), 64)},
                     {"dst", limit(normalize_exe(j.value("dst", "")), 64)}, {"user", user}});
    } else if (t == "keys") {
        if (!pol.collect.keys) return;
        json k = j;
        k.erase("t");
        k["user"] = user;
        k["source"] = "helper";
        sink_->emit(ev::key_stats, k);
    } else if (t == "paste_chord") {
        ++paste_chords_;
    } else if (t == "ping") {
        if (plat_.ipc) plat_.ipc->send(peer.conn_id, ipc::line({{"t", "pong"}}));
    }
}

void Agent::push_helper_config() {
    if (!plat_.ipc) return;
    const Policy p = policy();
    plat_.ipc->broadcast(ipc::line({{"t", "config"},
                                    {"mode", to_string(p.mode)},
                                    {"title_capture", to_string(p.title_capture)},
                                    {"collect", {{"window", p.collect.window}, {"clipboard", p.collect.clipboard},
                                                 {"keys", p.collect.keys}}}}));
    plat_.ipc->broadcast(ipc::line({{"t", "indicator"},
                                    {"mode", to_string(p.mode)},
                                    {"text", std::string("Monitored by XephyGuard · mode: ") + mode_label(p.mode) +
                                                 (status_.isolated ? " · network isolated" : "")},
                                    {"notice", p.notice_text}}));
}

void Agent::notify_student(const std::string& title, const std::string& body, bool urgent) {
    if (!plat_.ipc) return;
    plat_.ipc->broadcast(ipc::line({{"t", "notify"}, {"title", title}, {"body", body}, {"urgent", urgent}}));
}

void Agent::apply_hosts_blocklist(const std::vector<std::string>& domains) {
    if (domains == active_blocked_domains_) return;
    active_blocked_domains_ = domains;
#ifdef _WIN32
    const std::string default_hosts_path = "C:\\Windows\\System32\\drivers\\etc\\hosts";
    const std::string nl = "\r\n";
#else
    const std::string default_hosts_path = "/etc/hosts";
    const std::string nl = "\n";
#endif
    const std::string& hosts_path = hosts_path_override_.empty() ? default_hosts_path : hosts_path_override_;
    if (hosts_path_override_.empty() && !plat_.service_host) return;

    bool ok = false;
    const std::string content = read_file(hosts_path, &ok);
    if (!ok && domains.empty()) return;
    if (domains.empty() && content.find("# BEGIN XEPHYGUARD BLOCKLIST") == std::string::npos) return;

    const std::string updated = update_hosts_blocklist_content(content, domains, nl);
    if (write_file_atomic(hosts_path, updated)) {
        log::info("hosts: updated DNS sinkhole ({} blocked domains)", domains.size());
#ifndef _WIN32
        if (plat_.service_host) {
            run_cmd_silent("resolvectl flush-caches || systemd-resolve --flush-caches || true");
        }
#endif
    }
}

std::string Agent::isolation_state_path() const {
    return join(cfg_.data_dir, "isolation_saved.json");
}

void Agent::restore_isolation_from_disk() {
    const std::string state_file = isolation_state_path();
    bool ok = false;
    const std::string raw = read_file(state_file, &ok);
    json saved = json::object();
    if (ok && !raw.empty()) {
        auto parsed = json::parse(raw, nullptr, false);
        if (!parsed.is_discarded() && parsed.is_object()) {
            saved = std::move(parsed);
        }
    }
    last_restored_isolation_ = saved;

    if (plat_.service_host) {
#ifdef _WIN32
        run_cmd_silent("netsh advfirewall firewall delete rule name=\"XephyGuard_Isolate_Block\"");
        bool restored_any = false;
        if (saved.contains("windows_routes") && saved["windows_routes"].is_array()) {
            for (const auto& r : saved["windows_routes"]) {
                if (!r.is_object()) continue;
                const std::string gw = trim(r.value("gateway", std::string{}));
                const std::string metric = trim(r.value("metric", std::string{}));
                if (!is_safe_ip_token(gw)) continue;
                std::string cmd = "route.exe add 0.0.0.0 mask 0.0.0.0 " + gw;
                if (!metric.empty() && std::all_of(metric.begin(), metric.end(), [](unsigned char c) { return std::isdigit(c); })) {
                    cmd += " METRIC " + metric;
                }
                run_cmd_silent(cmd);
                restored_any = true;
            }
        }
        if (!restored_any && saved.contains("default_routes") && saved["default_routes"].is_array()) {
            for (const auto& item : saved["default_routes"]) {
                if (!item.is_string()) continue;
                const std::string gw = trim(item.get<std::string>());
                if (is_safe_ip_token(gw)) {
                    run_cmd_silent("route.exe add 0.0.0.0 mask 0.0.0.0 " + gw);
                }
            }
        }
#else
        run_cmd_silent("iptables -D OUTPUT -j XEPHYGUARD_ISOLATE 2>/dev/null || true");
        run_cmd_silent("iptables -F XEPHYGUARD_ISOLATE 2>/dev/null || true");
        run_cmd_silent("iptables -X XEPHYGUARD_ISOLATE 2>/dev/null || true");
        if (saved.contains("default_routes") && saved["default_routes"].is_array()) {
            for (const auto& item : saved["default_routes"]) {
                if (!item.is_string()) continue;
                const std::string route_line = trim(item.get<std::string>());
                if (is_safe_linux_route(route_line)) {
                    run_cmd_silent("ip -4 route replace " + route_line + " || true");
                }
            }
        }
        run_cmd_silent("nmcli networking connectivity check || true");
#endif
    }

    std::error_code ec;
    fs::remove(state_file, ec);
    isolation_enforced_ = false;
}

void Agent::apply_network_isolation(bool isolated) {
    if (isolated == isolation_enforced_) return;
    log::info("network: setting OS-level network isolation -> {}", isolated ? "ISOLATED" : "RESTORED");
    if (!isolated) {
        restore_isolation_from_disk();
        return;
    }

    const std::string state_file = isolation_state_path();
    std::error_code ec;
    if (!fs::exists(state_file, ec)) {
        json saved = {
            {"saved_at_ms", now_ms()},
            {"ccc_host", cfg_.ccc_host},
            {"firewall_active", true},
            {"default_routes", json::array()},
            {"windows_routes", json::array()},
        };
#ifdef _WIN32
        saved["platform"] = "windows";
        saved["firewall_rule"] = "XephyGuard_Isolate_Block";
        if (plat_.service_host) {
            const std::string route_out = run_cmd_capture("route.exe print 0.0.0.0");
            for (const auto& r : parse_windows_default_routes(route_out)) {
                saved["default_routes"].push_back(r.gateway);
                saved["windows_routes"].push_back(
                    {{"gateway", r.gateway}, {"interface", r.interface_ip}, {"metric", r.metric}});
            }
        }
#else
        saved["platform"] = "linux";
        saved["firewall_rule"] = "XEPHYGUARD_ISOLATE";
        if (plat_.service_host) {
            const std::string route_out = run_cmd_capture("ip -4 route show default");
            for (const auto& r : parse_linux_default_routes(route_out)) {
                saved["default_routes"].push_back(r);
            }
        }
#endif
        write_file_atomic(state_file, saved.dump(2));
    }

    isolation_enforced_ = true;
    if (!plat_.service_host) return;

#ifdef _WIN32
    in_addr addr{};
    if (!cfg_.ccc_host.empty() && inet_pton(AF_INET, cfg_.ccc_host.c_str(), &addr) == 1) {
        const uint32_t ip = ntohl(addr.s_addr);
        auto ip_str = [](uint32_t v) {
            return std::to_string((v >> 24) & 0xFF) + "." + std::to_string((v >> 16) & 0xFF) + "." +
                   std::to_string((v >> 8) & 0xFF) + "." + std::to_string(v & 0xFF);
        };
        std::string remoteip;
        if (ip > 0x80000000u) {
            remoteip = "1.0.0.0-126.255.255.255,128.0.0.0-" + ip_str(ip - 1) + "," + ip_str(ip + 1) + "-255.255.255.254";
        } else if (ip < 0x7F000000u && ip > 0x01000000u) {
            remoteip = "1.0.0.0-" + ip_str(ip - 1) + "," + ip_str(ip + 1) + "-126.255.255.255,128.0.0.0-255.255.255.254";
        }
        if (!remoteip.empty()) {
            run_cmd_silent("netsh advfirewall firewall delete rule name=\"XephyGuard_Isolate_Block\"");
            run_cmd_silent("netsh advfirewall firewall add rule name=\"XephyGuard_Isolate_Block\" dir=out action=block remoteip=" +
                           remoteip + " enable=yes");
        }
    }
    run_cmd_silent("route.exe delete 0.0.0.0 mask 0.0.0.0");
#else
    if (!cfg_.ccc_host.empty() && is_safe_ip_token(cfg_.ccc_host)) {
        run_cmd_silent("iptables -N XEPHYGUARD_ISOLATE 2>/dev/null || iptables -F XEPHYGUARD_ISOLATE");
        run_cmd_silent("iptables -C OUTPUT -j XEPHYGUARD_ISOLATE 2>/dev/null || iptables -I OUTPUT 1 -j XEPHYGUARD_ISOLATE");
        run_cmd_silent("iptables -A XEPHYGUARD_ISOLATE -o lo -j ACCEPT");
        run_cmd_silent("iptables -A XEPHYGUARD_ISOLATE -d " + cfg_.ccc_host + " -j ACCEPT");
        run_cmd_silent("iptables -A XEPHYGUARD_ISOLATE -j REJECT");
    }
    run_cmd_silent("ip -4 route del default || true");
    run_cmd_silent("nmcli networking connectivity check || true");
#endif
}

void Agent::renew_isolation(int64_t now_steady, int lease_s, const std::string& reason) {
    isolation_lease_s_ = lease_s > 0 ? std::clamp(lease_s, 1, 86400) : 600;
    isolation_renewed_ms_ = now_steady;
    if (!reason.empty()) isolation_reason_ = reason;
    const bool was_isolated = status_.isolated.exchange(true);
    if (!isolation_enforced_) {
        apply_network_isolation(true);
    }
    if (!was_isolated) {
        push_helper_config();
    }
}

void Agent::check_isolation_lease(int64_t now_steady) {
    if (!isolation_enforced_ && !status_.isolated.load()) return;
    if (isolation_lease_s_ <= 0) return;
    if (now_steady - isolation_renewed_ms_ > int64_t(isolation_lease_s_) * 1000) {
        const int expired_lease_s = isolation_lease_s_;
        status_.isolated = false;
        isolation_reason_.clear();
        apply_network_isolation(false);
        push_helper_config();
        log::warn("network: isolation_lease_expired after {}s without renewal; network restored", expired_lease_s);
        sink_->emit(ev::isolation_lease_expired, {{"lease_s", expired_lease_s}});
        notify_student("XephyGuard",
                       "Network access for this PC was automatically restored (isolation lease expired).",
                       false);
        wake_uplink();
    }
}

void Agent::scan_copilot_activity(int64_t /*now*/) {
    bool file_changed = false;
    std::string detail_file;
    std::error_code ec;

    auto check_file = [&](const fs::path& p) {
        if (!fs::is_regular_file(p, ec)) return;
        const auto mtime = uint64_t(fs::last_write_time(p, ec).time_since_epoch().count());
        const auto fsize = uint64_t(fs::file_size(p, ec));
        if (ec) return;
        const uint64_t state = mtime ^ (fsize * 0x9e3779b97f4a7c15ULL);
        const std::string key = p.string();
        auto it = copilot_file_state_.find(key);
        if (!copilot_scan_initialized_) {
            copilot_file_state_[key] = state;
        } else if (it == copilot_file_state_.end() || it->second != state) {
            copilot_file_state_[key] = state;
            file_changed = true;
            detail_file = p.filename().string();
        }
    };

    std::vector<fs::path> code_user_dirs;
    auto add_dir = [&](const fs::path& d) {
        std::error_code dir_ec;
        if (!fs::is_directory(d, dir_ec)) return;
        for (const auto& existing : code_user_dirs) {
            if (existing == d) return;
        }
        code_user_dirs.push_back(d);
    };

    for (const auto& h : user_homes()) {
        if (h.empty()) continue;
        const fs::path hp(h);
        add_dir(hp / "AppData" / "Roaming" / "Code" / "User");
        add_dir(hp / ".config" / "Code" / "User");
    }
#ifdef _WIN32
    const fs::path users("C:\\Users");
    if (fs::is_directory(users, ec)) {
        for (const auto& u : fs::directory_iterator(users, fs::directory_options::skip_permission_denied, ec)) {
            const std::string uname = u.path().filename().string();
            if (uname == "Public" || uname == "Default" || uname == "Default User" || uname == "All Users") continue;
            add_dir(u.path() / "AppData" / "Roaming" / "Code" / "User");
        }
    }
#endif

    for (const auto& code_user : code_user_dirs) {
        const fs::path gs = code_user / "globalStorage" / "github.copilot-chat";
        if (fs::is_directory(gs, ec)) {
            check_file(gs / "session-store.db");
            check_file(gs / "session-store.db-wal");
            check_file(gs / "commandEmbeddings.json");
        }
        const fs::path ws_root = code_user / "workspaceStorage";
        if (fs::is_directory(ws_root, ec)) {
            for (const auto& ws : fs::directory_iterator(ws_root, fs::directory_options::skip_permission_denied, ec)) {
                const fs::path cs = ws.path() / "chatSessions";
                if (fs::is_directory(cs, ec)) {
                    for (const auto& f : fs::directory_iterator(cs, fs::directory_options::skip_permission_denied, ec))
                        check_file(f.path());
                }
                for (const char* gc_name : {"GitHub.copilot-chat", "github.copilot-chat"}) {
                    const fs::path gc = ws.path() / gc_name;
                    if (!fs::is_directory(gc, ec)) continue;
                    const fs::path tr = gc / "transcripts";
                    if (fs::is_directory(tr, ec)) {
                        for (const auto& f : fs::directory_iterator(tr, fs::directory_options::skip_permission_denied, ec))
                            check_file(f.path());
                    }
                    const fs::path dl = gc / "debug-logs";
                    if (fs::is_directory(dl, ec)) {
                        for (const auto& sub : fs::directory_iterator(dl, fs::directory_options::skip_permission_denied, ec)) {
                            if (fs::is_directory(sub.path(), ec)) check_file(sub.path() / "main.jsonl");
                        }
                    }
                }
            }
        }
    }
    copilot_scan_initialized_ = true;

    if (file_changed) {
        sink_->emit(ev::ide_ai_activity, {
            {"tool", "github-copilot"},
            {"ide", "vscode"},
            {"evidence", "file_activity"},
            {"detail", detail_file},
            {"confidence", "medium"},
            {"meaning", "IDE AI extension storage was written to"},
            {"not_meaning", "Does not prove chat use; inline completions, sign-in or telemetry produce the same signal"},
            {"user", active_user()}
        });
        wake_uplink();
    }
}

void Agent::check_helpers() {
    if (!plat_.system) return;
    const auto sessions = plat_.system->sessions();
    std::vector<uint32_t> connected;
    {
        std::lock_guard lk(helpers_mu_);
        for (const auto& [id, p] : helpers_) connected.push_back(p.uid);
    }
    if (plat_.helper_launcher) plat_.helper_launcher->ensure_helpers(sessions, connected);
    const bool graphical = std::any_of(sessions.begin(), sessions.end(), [](const UserSession& s) {
        return s.session_type == "x11" || s.session_type == "wayland" || s.session_type == "console" ||
               s.session_type == "rdp";
    });
    const int64_t now = steady_ms();
    if (graphical && connected.empty()) {
        if (helper_missing_since_ == 0) helper_missing_since_ = now;
        if (!helper_missing_reported_ && now - helper_missing_since_ > 90000) {
            helper_missing_reported_ = true;
            sink_->emit(ev::tamper, {{"kind", "helper_missing"}, {"user", active_user()},
                                     {"detail", "a user is logged in but the session helper is not running"}});
        }
    } else {
        helper_missing_since_ = 0;
        helper_missing_reported_ = false;
    }
}

// ------------------------------------------------------------------------------- watchers, misc
void Agent::restart_watchers() {
    const Policy p = policy();
    if (plat_.files) {
        if (files_running_) { plat_.files->stop(); files_running_ = false; }
        if (p.collect.files && !p.work_dirs.empty()) {
            std::vector<std::string> dirs;
            for (const auto& h : user_homes())
                for (const auto& d : p.work_dirs) dirs.push_back(expand_path(d, h));
            files_running_ = plat_.files->watch(dirs, [this](const FileActivity& a) {
                std::lock_guard lk(files_mu_);
                auto& agg = file_agg_[a.dir];
                if (agg.is_null()) agg = {{"dir", a.dir}, {"saves", 0}, {"created", 0}, {"deleted", 0}, {"bytes_delta", 0},
                                          {"exts", json::array()}, {"first_ts", now_ms()}};
                if (a.action == "created") agg["created"] = agg["created"].get<int>() + 1;
                else if (a.action == "deleted") agg["deleted"] = agg["deleted"].get<int>() + 1;
                else agg["saves"] = agg["saves"].get<int>() + 1;
                agg["bytes_delta"] = agg["bytes_delta"].get<int64_t>() + a.size_delta;
                agg["total_bytes"] = a.total_bytes;
                auto& exts = agg["exts"];
                if (!a.file_ext.empty() && exts.size() < 10 &&
                    std::find(exts.begin(), exts.end(), a.file_ext) == exts.end())
                    exts.push_back(a.file_ext);
            });
        }
    }
    if (plat_.input) {
        if (p.collect.keys && !input_running_) {
            input_running_ = plat_.input->start([this](int64_t t, bool bs, bool paste_chord) {
                if (paste_chord) {
                    // Ask the user's helper to measure the clipboard being pasted (size/lines/code-likeness only).
                    ++paste_chords_;
                    if (plat_.ipc && policy().collect.clipboard) plat_.ipc->broadcast(ipc::line({{"t", "paste_probe"}}));
                    return;
                }
                std::lock_guard lk(keys_mu_);
                keys_.on_key(t, bs);
            });
            if (input_running_) log::info("input: key-timing monitor started (timing only, no key identities)");
        } else if (!p.collect.keys && input_running_) {
            plat_.input->stop();
            input_running_ = false;
            std::lock_guard lk(keys_mu_);
            keys_.flush(now_ms(), true);  // discard
        }
    }
}

void Agent::flush_keystats(int64_t now, bool force) {
    std::optional<KeyStatsWindow> w;
    {
        std::lock_guard lk(keys_mu_);
        w = keys_.flush(now, force);
    }
    const uint32_t chords = paste_chords_.exchange(0);
    if (!w && chords == 0) return;
    if (!policy().collect.keys) return;
    json j = w ? w->to_json() : json{{"window_start", now}, {"keys", 0}};
    j["paste_chords"] = chords;
    j["user"] = active_user();
    j["source"] = "service";
    sink_->emit(ev::key_stats, j);
}

void Agent::flush_file_activity() {
    std::map<std::string, json> agg;
    {
        std::lock_guard lk(files_mu_);
        agg.swap(file_agg_);
    }
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
        }
    }
    // Clock: wall time moved relative to the monotonic clock.
    const int64_t wms = now_ms() - steady_ms();
    if (!initial && std::abs(wms - wall_minus_steady_) > 5000)
        report("clock_change", {{"delta_ms", wms - wall_minus_steady_}, {"at", now_ms()}});
    wall_minus_steady_ = wms;
    if (!manifest.empty() || !mok) {}  // manifest is optional in development builds
}

// ------------------------------------------------------------------------------------ heartbeat
json Agent::build_heartbeat() {
    const Policy p = policy();
    json caps = json::object();
    caps["processes"] = plat_.processes != nullptr;
    caps["network"] = plat_.network != nullptr;
    caps["devices"] = plat_.devices != nullptr;
    caps["extensions"] = plat_.extensions != nullptr;
    caps["files"] = plat_.files != nullptr;
    caps["keys"] = plat_.input != nullptr && plat_.input->available();
    caps["gpu"] = plat_.gpu != nullptr;
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
    std::vector<std::string> users;
    if (plat_.system)
        for (const auto& s : plat_.system->sessions()) users.push_back(s.user + "@" + s.session_type);
    int helpers;
    {
        std::lock_guard lk(helpers_mu_);
        helpers = int(helpers_.size());
    }
    return json{{"agent_version", XG_VERSION},
                {"uptime_s", plat_.system ? plat_.system->uptime_s() : 0},
                {"agent_uptime_s", (steady_ms() - started_steady_) / 1000},
                {"mode", to_string(p.mode)},
                {"session_id", p.session_id},
                {"policy_version", p.version},
                {"signatures_version", rules_.version()},
                {"user", active_user()},
                {"sessions", users},
                {"clock_offset_ms", status_.clock_offset_ms.load()},
                {"local_time_ms", now_ms()},
                {"queue_depth", queue_ ? queue_->size() : 0},
                {"helpers", helpers},
                {"realtime", status_.realtime_connected.load()},
                {"suppressed_hits", suppressed_hits_},
                {"capabilities", caps},
                {"usage", usage}};
}

// --------------------------------------------------------------------------------------- uplink
net::TlsOptions Agent::tls_options(bool with_cert) const {
    net::TlsOptions o;
    o.ca_file = cfg_.ca_file;
    if (with_cert) {
        o.cert_file = cfg_.cert_file;
        o.key_file = cfg_.key_file;
    }
    o.expected_host = cfg_.ccc_host;
    o.timeout_ms = 10000;
    return o;
}

std::unique_ptr<net::HttpsClient> Agent::make_api_client(bool with_cert) {
    auto c = std::make_unique<net::HttpsClient>(cfg_.ccc_host, cfg_.api_port, tls_options(with_cert));
    if (with_cert) c->set_bearer(cfg_.token);
    return c;
}

bool Agent::poll_enrollment() {
    if (cfg_.enrollment_id.empty()) {
        if (!warned_not_enrolled_) {
            log::warn("agent is not enrolled: run `xephyguard-agent enroll --key <lab key>`");
            warned_not_enrolled_ = true;
        }
        return false;
    }
    auto api = make_api_client(false);
    auto r = api->request("GET", std::string(protocol::kEnrollPath) + "/" + cfg_.enrollment_id +
                                     "?poll_token=" + cfg_.poll_token);
    if (!r.ok()) return false;
    auto j = json::parse(r.body, nullptr, false);
    if (j.is_discarded()) return false;
    const auto st = j.value("status", "");
    if (st == "approved" && j.contains("cert_pem") && j.contains("token")) {
        write_file_atomic(cfg_.cert_file, j["cert_pem"].get<std::string>());
        if (j.contains("ca_pem")) write_file_atomic(cfg_.ca_file, j["ca_pem"].get<std::string>());
        cfg_.pc_id = std::to_string(j.value("pc_id", 0));
        cfg_.token = j["token"].get<std::string>();
        cfg_.poll_token.clear();
        cfg_.save(config_path_);
        if (!realtime_cfg_.is_null() && realtime_cfg_.is_object()) {
            realtime_cfg_["channel"] = "private-pc." + cfg_.pc_id;
            if (queue_) queue_->kv_set("realtime", realtime_cfg_.dump());
        }
        if (auto h = crypto::sha256_file_hex(config_path_)) file_hashes_[config_path_] = *h;
        log::info("enrollment approved: this PC is #{}", cfg_.pc_id);
        policy_dirty_ = true;
        return true;
    }
    if (st == "rejected") log::error("enrollment was rejected by the CCC administrator");
    return false;
}

bool Agent::fetch_policy() {
    std::lock_guard lk(api_mu_);
    if (!api_) api_ = make_api_client(true);
    auto r = api_->request("GET", protocol::kPolicyPath);
    if (!r.ok()) {
        log::warn("policy: fetch failed ({} {})", r.status, r.error);
        return false;
    }
    auto j = json::parse(r.body, nullptr, false);
    if (j.is_discarded() || !j.contains("policy")) return false;
    if (j.contains("signatures") && j["signatures"].is_object() &&
        j["signatures"].value("version", "") != rules_.version())
        apply_signatures(j["signatures"], true);
    std::string err;
    Policy p = Policy::from_json(j["policy"], &err);
    if (!err.empty()) {
        log::error("policy: invalid policy from CCC: {}", err);
        return false;
    }
    apply_policy(p);
    queue_->kv_set("policy", j["policy"].dump());
    if (j.contains("realtime") && j["realtime"].is_object()) {
        realtime_cfg_ = j["realtime"];
        queue_->kv_set("realtime", realtime_cfg_.dump());
    }
    policy_dirty_ = false;
    return true;
}

bool Agent::flush_once() {
    std::lock_guard lk(api_mu_);
    if (!api_) api_ = make_api_client(true);
    const auto events = queue_ ? queue_->peek(protocol::kMaxBatchEvents) : std::vector<Event>{};
    size_t included = 0;
    const json body = protocol::build_batch(cfg_.pc_id, events, build_heartbeat(), &included);
    const std::string s = body.dump();
    const int64_t t0 = now_ms();
    auto r = api_->request("POST", protocol::kEventsPath, s);
    const int64_t t1 = now_ms();
    if (!r.ok()) {
        if (r.status == 401 || r.status == 403) log::error("uplink: CCC rejected our credentials ({})", r.status);
        else log::debug("uplink: send failed ({} {})", r.status, r.error);
        return false;
    }
    status_.bytes_up += s.size();
    std::string err;
    auto ack = protocol::parse_batch_ack(r.body, &err);
    if (!ack) {
        log::warn("uplink: bad ack: {}", err);
        return false;
    }
    if (queue_ && ack->ack_seq > 0) queue_->ack(ack->ack_seq);
    status_.last_ack_seq = ack->ack_seq;
    if (ack->server_time_ms > 0) status_.clock_offset_ms = protocol::clock_offset(t0, t1, ack->server_time_ms);
    const Policy p = policy();
    if (ack->policy_version != 0 && ack->policy_version != p.version) policy_dirty_ = true;
    if (!ack->signatures_version.empty() && ack->signatures_version != rules_.version()) policy_dirty_ = true;
    if (ack->isolated) {
        renew_isolation(steady_ms(), ack->isolation_lease_s);
    } else if (status_.isolated.load() || isolation_enforced_) {
        status_.isolated = false;
        isolation_reason_.clear();
        apply_network_isolation(false);
        push_helper_config();
    }
    return included == events.size() || included > 0;
}

void Agent::uplink_loop(std::atomic<bool>& stop) {
    int fail_backoff_s = 1;
    while (!stop) {
        int wait_ms;
        if (!cfg_.enrolled()) {
            poll_enrollment();
            wait_ms = 5000;
        } else {
            if (policy_dirty_) {
                // fetch_policy takes api_mu_ itself
                fetch_policy();
            }
            const bool ok = flush_once();
            if (ok != status_.online.load()) log::info("uplink: CCC {}", ok ? "reachable" : "unreachable (buffering locally)");
            status_.online = ok;
            if (ok) {
                fail_backoff_s = 1;
                // A new policy announced in the ack is fetched right away (realtime may be unavailable).
                wait_ms = policy_dirty_ ? 0
                          : (queue_ && queue_->size() > 0) ? 200 : policy().intervals.heartbeat_s * 1000;
            } else {
                wait_ms = fail_backoff_s * 1000;
                fail_backoff_s = std::min(fail_backoff_s * 2, 30);
            }
        }
        std::unique_lock lk(flush_mu_);
        flush_cv_.wait_for(lk, std::chrono::milliseconds(wait_ms),
                           [&] { return stop.load() || wake_uplink_.exchange(false); });
    }
}

void Agent::realtime_loop(std::atomic<bool>& stop) {
    while (!stop) {
        if (!cfg_.enrolled() || realtime_cfg_.is_null() || !realtime_cfg_.contains("app_key")) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            continue;
        }
        net::PusherConfig pc;
        pc.host = cfg_.ccc_host;
        // Reverb is served by the same nginx endpoint as the API, so reuse the port the agent already
        // reaches the CCC on (the server cannot know about NAT / port forwarding in between).
        pc.port = cfg_.api_port;
        pc.app_key = realtime_cfg_.value("app_key", "");
        pc.channel = realtime_cfg_.value("channel", "private-pc." + cfg_.pc_id);
        pc.auth_path = realtime_cfg_.value("auth_path", protocol::kBroadcastAuthPath);
        auto api = make_api_client(true);
        net::PusherClient client(pc, tls_options(true), *api, [this](const std::string& event, const json& data) {
            if (event != "command") return;
            std::string err;
            if (auto cmd = protocol::parse_command(data, &err)) handle_command(*cmd);
            else log::warn("realtime: rejected command: {}", err);
        });
        std::atomic<bool> session_stop{false};
        std::thread watcher([&] {
            while (!stop && !session_stop) {
                status_.realtime_connected = client.connected();
                const std::string desired_channel =
                    realtime_cfg_.is_object() ? realtime_cfg_.value("channel", "private-pc." + cfg_.pc_id)
                                              : ("private-pc." + cfg_.pc_id);
                const std::string desired_key =
                    realtime_cfg_.is_object() ? realtime_cfg_.value("app_key", "") : "";
                if (desired_channel != pc.channel || desired_key != pc.app_key) {
                    session_stop = true;
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
            if (stop) session_stop = true;
        });
        client.run(session_stop);
        session_stop = true;
        watcher.join();
        status_.realtime_connected = false;
    }
}

void Agent::command_result(const std::string& id, const std::string& status, const json& detail) {
    sink_->emit(ev::command_result, {{"command_id", id}, {"status", status}, {"detail", detail}});
    wake_uplink();
}

void Agent::handle_command(const protocol::Command& cmd) {
    log::info("command {} ({})", cmd.type, cmd.id);
    if (cmd.type == "policy_updated") {
        policy_dirty_ = true;
        wake_uplink();
        command_result(cmd.id, "ok", json::object());
    } else if (cmd.type == "message") {
        notify_student(cmd.args.value("title", "Message from your instructor"), cmd.args.value("text", ""),
                       cmd.args.value("urgent", false));
        command_result(cmd.id, "ok", {{"helpers", status_.helpers_connected.load()}});
    } else if (cmd.type == "kill") {
        const Policy pol = policy();
        if (pol.mode != Mode::test) {
            log::warn("command: ignoring kill outside Test mode (current mode: {})", to_string(pol.mode));
            command_result(cmd.id, "ignored", {{"reason", "not_in_test_mode"}});
            return;
        }
        json killed = json::array();
        std::vector<int64_t> pids;
        if (cmd.args.contains("pid") && cmd.args["pid"].is_number_integer()) pids.push_back(cmd.args["pid"].get<int64_t>());
        if (cmd.args.contains("exe") && cmd.args["exe"].is_string() && plat_.processes) {
            const auto exe = normalize_exe(cmd.args["exe"].get<std::string>());
            for (const auto& p : plat_.processes->snapshot())
                if (normalize_exe(p.exe_path.empty() ? p.name : p.exe_path) == exe) pids.push_back(p.pid);
        }
        for (auto pid : pids) {
            std::string exe;
            {
                std::lock_guard lk(tracker_mu_);
                if (auto* p = tracker_.get(pid)) exe = normalize_exe(p->exe_path.empty() ? p->name : p->exe_path);
            }
            const bool ok = !pol.kill_exempt.contains(exe) && plat_.processes && plat_.processes->kill(pid);
            killed.push_back({{"pid", pid}, {"exe", exe}, {"killed", ok}});
        }
        notify_student("XephyGuard", "A program was closed by your instructor.", false);
        command_result(cmd.id, "ok", {{"processes", killed}});
    } else if (cmd.type == "isolate_notice") {
        const bool iso = cmd.args.value("isolated", false);
        const int lease_s = cmd.args.value("lease_s", 600);
        const std::string reason = cmd.args.value("reason", std::string{});
        if (iso) {
            renew_isolation(steady_ms(), lease_s, reason);
            push_helper_config();
            std::string msg = "Network access for this PC was paused by the instructor.";
            if (!reason.empty()) msg += " Reason: " + reason;
            notify_student("XephyGuard", msg, true);
        } else {
            status_.isolated = false;
            isolation_reason_.clear();
            apply_network_isolation(false);
            push_helper_config();
            notify_student("XephyGuard", "Network access for this PC was restored.", true);
        }
        command_result(cmd.id, "ok", json::object());
    } else if (cmd.type == "rescan") {
        const auto what = cmd.args.value("what", "all");
        if (what == "extensions" || what == "all") rescan_extensions_ = true;
        if (what == "artifacts" || what == "all") rescan_artifacts_ = true;
        if (what == "processes" || what == "all") next_run_.erase("proc");
        command_result(cmd.id, "ok", json::object());
    } else if (cmd.type == "flush" || cmd.type == "ping") {
        wake_uplink();
        command_result(cmd.id, "ok", {{"pong", now_ms()}});
    } else {
        command_result(cmd.id, "ignored", json::object());
    }
}

// --------------------------------------------------------------------------------- artifacts
void Agent::artifact_loop(std::atomic<bool>& stop) {
    // Delay the first scan so boot is not slowed down.
    int64_t next = steady_ms() + 60000;
    while (!stop) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        const Policy p = policy();
        if (!p.collect.artifacts) continue;
        if (steady_ms() < next && !rescan_artifacts_.exchange(false)) continue;
        next = steady_ms() + int64_t(p.intervals.artifact_scan_s) * 1000;
        const auto homes = user_homes();
        auto res = scan_artifacts(rules_, homes, stop);
        json found = json::array();
        for (const auto& a : res.found)
            found.push_back({{"path", a.path}, {"size_mb", a.size_bytes / (1024 * 1024)}, {"rule_id", a.rule_id}});
        sink_->emit(ev::artifact_scan, {{"count", res.found.size()}, {"files_visited", res.files_visited},
                                        {"truncated", res.truncated}, {"found", found}});
        for (const auto& a : res.found) {
            RuleHit h{a.rule_id, "local_model", "Local AI model files found", Severity::high};
            for (const auto& r : rules_.artifact_rules())
                if (r.id == a.rule_id) { h.description = r.description; h.severity = r.severity; }
            handle_rule_hit(h, a.path, json{{"path", a.path}, {"size_mb", a.size_bytes / (1024 * 1024)}});
        }
    }
}

// ------------------------------------------------------------------------------------ main loop
void Agent::reevaluate_all() {
    // Processes, ports, adapters and extensions that were already present when the mode changed
    // are evaluated again under the new policy (e.g. an AI tool left open when a Test starts).
    std::vector<ProcessInfo> running;
    if (plat_.processes) running = plat_.processes->snapshot();
    {
        std::lock_guard lk(tracker_mu_);
        tracker_.update(running);
    }
    for (const auto& p : running) on_process_started(p);
    known_ports_.clear();
    adapters_.clear();
    default_routes_ = -1;
    last_ext_hash_.clear();
    known_extensions_.clear();
    network_dirty_ = true;
    rescan_extensions_ = true;
    next_run_.erase("ports");
}

void Agent::run_periodic(int64_t now) {
    check_isolation_lease(now);
    if (reevaluate_.exchange(false)) reevaluate_all();
    const Policy p = policy();
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
    if (p.collect.network && (network_dirty_.exchange(false) || due("net", 30))) scan_network();
    if (p.collect.extensions && (rescan_extensions_.exchange(false) || due("ext", p.intervals.extension_scan_s)))
        scan_extensions();
    if (due("copilot", 3)) scan_copilot_activity(now);
    flush_keystats(now_ms(), false);
    if (due("files", 10)) flush_file_activity();
    if (p.collect.gpu && due("gpu", p.intervals.gpu_sample_s)) sample_gpu();
    if (due("integrity", p.intervals.integrity_check_s)) check_integrity(false);
    if (due("helpers", 15)) check_helpers();
}

int Agent::run(std::atomic<bool>& stop) {
    stop_ = &stop;
    started_steady_ = steady_ms();
    wall_minus_steady_ = now_ms() - steady_ms();
    const bool clean = queue_->kv_get("clean_shutdown").value_or("1") == "1";
    queue_->kv_set("clean_shutdown", "0");
    sink_->emit(ev::agent_started, {{"version", XG_VERSION},
                                    {"previous_exit", clean ? "clean" : "unclean"},
                                    {"os", plat_.system ? plat_.system->os_name() + " " + plat_.system->os_version() : ""},
                                    {"hostname", plat_.system ? plat_.system->hostname() : ""}});
    check_integrity(true);

    if (plat_.ipc) {
        const bool ok = plat_.ipc->start(
            cfg_.ipc_path, [this](const IIpcServer::Peer& peer, const std::string& line) { on_helper_message(peer, line); },
            [this](const IIpcServer::Peer& peer, bool connected) {
                {
                    std::lock_guard lk(helpers_mu_);
                    if (connected) helpers_[peer.conn_id] = peer;
                    else helpers_.erase(peer.conn_id);
                    status_.helpers_connected = int(helpers_.size());
                }
                if (!connected)
                    sink_->emit(ev::helper_status, {{"connected", false}, {"user", peer.user}, {"pid", peer.pid}});
                else
                    push_helper_config();
            });
        if (!ok) log::error("ipc: cannot listen on {}", cfg_.ipc_path);
    }
    if (plat_.processes)
        plat_.processes->watch([this](const ProcessInfo& p) {
            bool fresh;
            {
                std::lock_guard lk(tracker_mu_);
                fresh = tracker_.note_started(p);
            }
            if (fresh && policy().collect.processes) on_process_started(p);
        });
    if (plat_.network) plat_.network->watch([this] { network_dirty_ = true; });
    if (plat_.devices)
        plat_.devices->start([this](const DeviceInfo& d) {
            if (!policy().collect.devices) return;
            auto hit = rules_.match_device(d);
            sink_->emit(ev::device, {{"action", d.action}, {"subsystem", d.subsystem}, {"class", d.devclass},
                                     {"vendor", d.vendor_id}, {"product", d.product_id}, {"model", limit(d.model, 128)},
                                     {"serial_hash", d.serial_hash}, {"rule_id", hit ? hit->rule_id : ""}});
            if (hit && d.action == "add")
                handle_rule_hit(*hit, d.vendor_id + ":" + d.product_id + ":" + d.serial_hash,
                                json{{"class", d.devclass}, {"vendor", d.vendor_id}, {"model", d.model}});
        });
    restart_watchers();
    push_helper_config();

    threads_.emplace_back([this, &stop] { uplink_loop(stop); });
    threads_.emplace_back([this, &stop] { realtime_loop(stop); });
    threads_.emplace_back([this, &stop] { artifact_loop(stop); });
    if (plat_.service_host) plat_.service_host->notify_ready();
    log::info("agent {} running (pc {}, mode {})", XG_VERSION, cfg_.pc_id.empty() ? "unenrolled" : cfg_.pc_id,
              to_string(policy().mode));

    while (!stop) {
        try {
            run_periodic(steady_ms());
        } catch (const std::exception& e) {
            log::error("periodic task failed: {}", e.what());
        }
        if (plat_.service_host) plat_.service_host->watchdog_ping();
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }

    // Clean shutdown: announce it so the CCC can distinguish it from a kill or a network cut.
    log::info("agent stopping");
    if (plat_.service_host) plat_.service_host->notify_stopping();
    sink_->emit(ev::agent_stopping, {{"reason", "service_stop"}, {"uptime_s", (steady_ms() - started_steady_) / 1000}});
    flush_keystats(now_ms(), true);
    flush_file_activity();
    wake_uplink();
    for (auto& t : threads_) t.join();
    threads_.clear();
    if (cfg_.enrolled()) flush_once();
    if (isolation_enforced_) apply_network_isolation(false);
    if (!active_blocked_domains_.empty()) apply_hosts_blocklist({});
    queue_->kv_set("clean_shutdown", "1");
    if (plat_.processes) plat_.processes->stop();
    if (plat_.network) plat_.network->stop();
    if (plat_.devices) plat_.devices->stop();
    if (plat_.files) plat_.files->stop();
    if (plat_.input) plat_.input->stop();
    if (plat_.ipc) plat_.ipc->stop();
    return 0;
}

}  // namespace xg
