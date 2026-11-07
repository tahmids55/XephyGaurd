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
        int heartbeat_s = 5, process_scan_s = 2, snapshot_s = 60, port_scan_s = 15;
        int extension_scan_s = 300, artifact_scan_s = 1800, gpu_sample_s = 10, keystats_window_s = 5;
        int integrity_check_s = 60;
    } intervals;

    std::map<Severity, std::vector<Action>> default_actions;
    std::map<std::string, std::vector<Action>> rule_actions;
    std::set<std::string> disabled_rules;
    std::vector<std::string> work_dirs;       // e.g. "~/lab", "%USERPROFILE%\\Documents\\Lab"
    std::set<std::string> kill_exempt;        // never kill these normalized exe names
    std::vector<std::string> blocked_domains; // host-level DNS sinkhole domains
    std::string signatures_version;
    std::string notice_text;                  // indicator / banner text
    std::string ai_policy = "forbidden";      // "forbidden" | "permitted_with_disclosure" | "open"
    std::string target_agent_version;         // pinned agent version for staged rollout / canary

    static Policy default_for(Mode m);
    static Policy from_json(const json& j, std::string* err = nullptr);
    json to_json() const;

    /// Clamps collectors and title_capture to the ETHICS.md tiered collection table (Contract C1).
    void clamp_for_mode();

    std::vector<Action> actions_for(const std::string& rule_id, Severity sev) const;
    bool rule_enabled(const std::string& rule_id) const { return !disabled_rules.contains(rule_id); }
    /// Applies title_capture to a window title before it leaves the PC.
    std::string filter_title(const std::string& app, const std::string& title) const;
};

}  // namespace xg
