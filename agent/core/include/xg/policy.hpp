#pragma once
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "xg/event.hpp"
#include "xg/types.hpp"

namespace xg {

enum class Mode { always_on, practice, test };
enum class TitleCapture { off, app_only, full };
enum class Action { log, warn, kill, alert, isolate };

Mode parse_mode(const std::string& s);
const char* to_string(Mode m);
TitleCapture parse_title_capture(const std::string& s);
const char* to_string(TitleCapture t);
NetworkLock parse_network_lock(const std::string& s);
const char* to_string(NetworkLock l);
Action parse_action(const std::string& s);
const char* to_string(Action a);

/// The policy pushed by the CCC for this PC (mode + profile + per-PC overrides already merged).
struct Policy {
    int64_t version = 0;
    Mode mode = Mode::always_on;
    int64_t session_id = 0;
    std::string profile_name;
    TitleCapture title_capture = TitleCapture::app_only;

    struct Collect {
        bool processes = true, ports = true, extensions = true, network = true, devices = true;
        bool artifacts = true, window = false, clipboard = false, keys = false, files = false, gpu = false;
    } collect;

    struct Intervals {
        int heartbeat_s = 5, process_scan_s = 2, snapshot_s = 15, port_scan_s = 15;
        int extension_scan_s = 15, artifact_scan_s = 1800, gpu_sample_s = 10, keystats_window_s = 5;
        int integrity_check_s = 60;
    } intervals;

    std::map<Severity, std::vector<Action>> default_actions;
    std::map<std::string, std::vector<Action>> rule_actions;
    std::set<std::string> disabled_rules;
    std::vector<std::string> work_dirs;       // e.g. "~/lab", "%USERPROFILE%\\Documents\\Lab"
    std::set<std::string> kill_exempt;        // never kill these normalized exe names
    std::string signatures_version;
    std::string notice_text;                  // indicator / banner text
    bool suppress_ai_overview = false;        // suppress Google Search AI overview locally via browser policies
    std::string google_search_url;            // optional parameterized udm=14 URL template from server policy
    std::string proxy_mode;                   // gateway | proxy_only
    std::string proxy_pac_url;                // https://<ccc>/proxy.pac when proxy_mode == proxy_only
    NetworkLock network_lock = NetworkLock::off;  // per session; applied only in proxy_only and not Always-on

    /// Windows whose content the live screen view must never show (F43, screen Step 11): a window matches when
    /// every field that is set matches (process = executable name, case-insensitive; title_regex = case-insensitive
    /// search). Delivered as `screen.privacy_rules`; the lists live in the CCC data, never in the agent.
    struct MaskRule {
        std::string process, title_regex;
    };
    std::vector<MaskRule> screen_mask_rules;
    /// Screen tile encoding (`screen.codec` "jpeg"|"webp", `screen.quality` 10..100, `screen.webp_method` 0..6).
    /// Optional; a missing or invalid value keeps the default (no effect on what is shown, only on cost).
    std::string screen_codec = "jpeg";
    int screen_quality = 85;
    int screen_webp_method = 1;
    /// `screen.cpu_budget_pct` (number 1..100, default 8): CPU budget of the screen capture/encode thread.
    double screen_cpu_budget_pct = 8.0;

    static Policy default_for(Mode m);
    static Policy from_json(const json& j, std::string* err = nullptr);
    json to_json() const;

    std::vector<Action> actions_for(const std::string& rule_id, Severity sev) const;
    bool rule_enabled(const std::string& rule_id) const { return !disabled_rules.contains(rule_id); }
    /// Applies title_capture to a window title before it leaves the PC.
    std::string filter_title(const std::string& app, const std::string& title) const;
};

}  // namespace xg
