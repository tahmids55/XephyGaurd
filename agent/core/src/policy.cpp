#include "xg/policy.hpp"

#include <algorithm>

#include "xg/util.hpp"

namespace xg {

Mode parse_mode(const std::string& s) {
    auto l = to_lower(s);
    if (l == "practice") return Mode::practice;
    if (l == "test" || l == "exam") return Mode::test;
    return Mode::always_on;
}
const char* to_string(Mode m) {
    switch (m) {
        case Mode::always_on: return "always_on";
        case Mode::practice: return "practice";
        case Mode::test: return "test";
    }
    return "always_on";
}
TitleCapture parse_title_capture(const std::string& s) {
    auto l = to_lower(s);
    if (l == "off") return TitleCapture::off;
    if (l == "full") return TitleCapture::full;
    return TitleCapture::app_only;
}
const char* to_string(TitleCapture t) {
    switch (t) {
        case TitleCapture::off: return "off";
        case TitleCapture::app_only: return "app_only";
        case TitleCapture::full: return "full";
    }
    return "app_only";
}
Action parse_action(const std::string& s) {
    auto l = to_lower(s);
    if (l == "warn") return Action::warn;
    if (l == "kill") return Action::kill;
    if (l == "alert") return Action::alert;
    if (l == "isolate") return Action::isolate;
    return Action::log;
}
const char* to_string(Action a) {
    switch (a) {
        case Action::log: return "log";
        case Action::warn: return "warn";
        case Action::kill: return "kill";
        case Action::alert: return "alert";
        case Action::isolate: return "isolate";
    }
    return "log";
}

Policy Policy::default_for(Mode m) {
    Policy p;
    p.mode = m;
    using A = Action;
    switch (m) {
        case Mode::always_on:
            // Minimal collection: heartbeat, process/extension scan, adapter changes. Log only.
            p.title_capture = TitleCapture::off;
            p.collect = {};
            p.collect.artifacts = false;
            p.intervals.process_scan_s = 5;
            p.intervals.port_scan_s = 60;
            p.intervals.heartbeat_s = 15;
            for (auto s : {Severity::info, Severity::low, Severity::medium})
                p.default_actions[s] = {A::log};
            // Detect + alert only (never close anything) outside tests.
            p.default_actions[Severity::high] = {A::log, A::alert};
            p.default_actions[Severity::critical] = {A::log, A::alert};
            break;
        case Mode::practice:
            p.title_capture = TitleCapture::app_only;
            p.collect.window = true;
            p.collect.clipboard = true;
            p.collect.keys = true;
            p.default_actions[Severity::info] = {A::log};
            p.default_actions[Severity::low] = {A::log};
            p.default_actions[Severity::medium] = {A::log};
            p.default_actions[Severity::high] = {A::log, A::alert};
            p.default_actions[Severity::critical] = {A::log, A::alert};
            break;
        case Mode::test:
            p.title_capture = TitleCapture::app_only;
            p.collect.window = p.collect.clipboard = p.collect.keys = p.collect.files = p.collect.gpu = true;
            p.suppress_ai_overview = true;
            p.intervals.extension_scan_s = 15;
            p.intervals.artifact_scan_s = 900;
            p.default_actions[Severity::info] = {A::log};
            p.default_actions[Severity::low] = {A::log};
            p.default_actions[Severity::medium] = {A::log, A::alert};
            p.default_actions[Severity::high] = {A::log, A::warn, A::alert, A::kill};      // AI apps are closed in tests
            p.default_actions[Severity::critical] = {A::log, A::warn, A::alert, A::kill};
            break;
    }
    // System-critical processes are never killed regardless of rules.
    p.kill_exempt = {"systemd", "init", "explorer", "winlogon", "csrss", "lsass", "services", "smss", "wininit",
                     "xephyguard-agent", "xephyguard-helper", "sshd", "xorg", "gnome-shell", "plasmashell", "dwm"};
    p.notice_text = "This computer is monitored by XephyGuard. No keystrokes, clipboard text or screen content is recorded.";
    return p;
}

Policy Policy::from_json(const json& j, std::string* err) {
    Mode m = parse_mode(j.value("mode", std::string("always_on")));
    Policy p = default_for(m);
    try {
        p.version = j.value("version", int64_t(0));
        p.session_id = j.value("session_id", int64_t(0));
        p.profile_name = j.value("profile", std::string{});
        if (j.contains("title_capture")) p.title_capture = parse_title_capture(j["title_capture"].get<std::string>());
        if (j.contains("collect") && j["collect"].is_object()) {
            const auto& c = j["collect"];
            auto b = [&](const char* k, bool& dst) { if (c.contains(k) && c[k].is_boolean()) dst = c[k].get<bool>(); };
            b("processes", p.collect.processes); b("ports", p.collect.ports); b("extensions", p.collect.extensions);
            b("network", p.collect.network); b("devices", p.collect.devices); b("artifacts", p.collect.artifacts);
            b("window", p.collect.window); b("clipboard", p.collect.clipboard); b("keys", p.collect.keys);
            b("files", p.collect.files); b("gpu", p.collect.gpu);
        }
        if (j.contains("intervals") && j["intervals"].is_object()) {
            const auto& c = j["intervals"];
            auto i = [&](const char* k, int& dst, int lo, int hi) {
                if (c.contains(k) && c[k].is_number_integer()) dst = std::clamp(c[k].get<int>(), lo, hi);
            };
            i("heartbeat_s", p.intervals.heartbeat_s, 2, 300);
            i("process_scan_s", p.intervals.process_scan_s, 1, 300);
            i("snapshot_s", p.intervals.snapshot_s, 10, 3600);
            i("port_scan_s", p.intervals.port_scan_s, 2, 3600);
            i("extension_scan_s", p.intervals.extension_scan_s, 10, 86400);
            i("artifact_scan_s", p.intervals.artifact_scan_s, 60, 86400);
            i("gpu_sample_s", p.intervals.gpu_sample_s, 2, 3600);
            i("keystats_window_s", p.intervals.keystats_window_s, 1, 60);
            i("integrity_check_s", p.intervals.integrity_check_s, 10, 3600);
        }
        if (j.contains("actions") && j["actions"].is_object()) {
            const auto& a = j["actions"];
            if (a.contains("default") && a["default"].is_object())
                for (auto& [sev, list] : a["default"].items()) {
                    std::vector<Action> acts;
                    for (const auto& x : list) acts.push_back(parse_action(x.get<std::string>()));
                    p.default_actions[parse_severity(sev)] = acts;
                }
            if (a.contains("rules") && a["rules"].is_object())
                for (auto& [rid, list] : a["rules"].items()) {
                    std::vector<Action> acts;
                    for (const auto& x : list) acts.push_back(parse_action(x.get<std::string>()));
                    p.rule_actions[rid] = acts;
                }
        }
        for (const auto& r : j.value("disabled_rules", json::array())) p.disabled_rules.insert(r.get<std::string>());
        for (const auto& d : j.value("work_dirs", json::array())) p.work_dirs.push_back(d.get<std::string>());
        for (const auto& k : j.value("kill_exempt", json::array())) p.kill_exempt.insert(normalize_exe(k.get<std::string>()));
        p.signatures_version = j.value("signatures_version", std::string{});
        if (j.contains("notice_text") && j["notice_text"].is_string()) p.notice_text = j["notice_text"].get<std::string>();
        if (j.contains("suppress_ai_overview") && j["suppress_ai_overview"].is_boolean()) {
            p.suppress_ai_overview = j["suppress_ai_overview"].get<bool>();
        }
        if (j.contains("google_search_url") && j["google_search_url"].is_string()) {
            p.google_search_url = j["google_search_url"].get<std::string>();
        }
        if (j.contains("proxy_mode") && j["proxy_mode"].is_string()) {
            p.proxy_mode = j["proxy_mode"].get<std::string>();
        }
        if (j.contains("proxy_pac_url") && j["proxy_pac_url"].is_string()) {
            p.proxy_pac_url = j["proxy_pac_url"].get<std::string>();
        }
    } catch (const std::exception& e) {
        if (err) *err = e.what();
    }
    return p;
}

json Policy::to_json() const {
    json acts_default = json::object();
    for (const auto& [s, list] : default_actions) {
        json a = json::array();
        for (auto x : list) a.push_back(to_string(x));
        acts_default[to_string(s)] = a;
    }
    json acts_rules = json::object();
    for (const auto& [r, list] : rule_actions) {
        json a = json::array();
        for (auto x : list) a.push_back(to_string(x));
        acts_rules[r] = a;
    }
    return json{
        {"version", version},
        {"mode", to_string(mode)},
        {"session_id", session_id},
        {"profile", profile_name},
        {"title_capture", to_string(title_capture)},
        {"collect", {{"processes", collect.processes}, {"ports", collect.ports}, {"extensions", collect.extensions},
                     {"network", collect.network}, {"devices", collect.devices}, {"artifacts", collect.artifacts},
                     {"window", collect.window}, {"clipboard", collect.clipboard}, {"keys", collect.keys},
                     {"files", collect.files}, {"gpu", collect.gpu}}},
        {"intervals", {{"heartbeat_s", intervals.heartbeat_s}, {"process_scan_s", intervals.process_scan_s},
                       {"snapshot_s", intervals.snapshot_s}, {"port_scan_s", intervals.port_scan_s},
                       {"extension_scan_s", intervals.extension_scan_s}, {"artifact_scan_s", intervals.artifact_scan_s},
                       {"gpu_sample_s", intervals.gpu_sample_s}, {"keystats_window_s", intervals.keystats_window_s},
                       {"integrity_check_s", intervals.integrity_check_s}}},
        {"actions", {{"default", acts_default}, {"rules", acts_rules}}},
        {"disabled_rules", disabled_rules},
        {"work_dirs", work_dirs},
        {"kill_exempt", kill_exempt},
        {"signatures_version", signatures_version},
        {"notice_text", notice_text},
        {"suppress_ai_overview", suppress_ai_overview},
        {"google_search_url", google_search_url},
        {"proxy_mode", proxy_mode},
        {"proxy_pac_url", proxy_pac_url},
    };
}

std::vector<Action> Policy::actions_for(const std::string& rule_id, Severity sev) const {
    if (!rule_enabled(rule_id)) return {};
    if (auto it = rule_actions.find(rule_id); it != rule_actions.end()) return it->second;
    if (auto it = default_actions.find(sev); it != default_actions.end()) return it->second;
    return {Action::log};
}

std::string Policy::filter_title(const std::string& app, const std::string& title) const {
    switch (title_capture) {
        case TitleCapture::off: return {};
        case TitleCapture::app_only: return app;
        case TitleCapture::full: return title.size() > 256 ? title.substr(0, 256) : title;
    }
    return {};
}

}  // namespace xg
