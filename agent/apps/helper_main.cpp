// xephyguard-helper: runs inside each logged-in user session. Collects what only the session can see
// (focused window, clipboard sizes, key timing on Windows), shows the transparency indicator and
// student notices, and forwards everything to the service over authenticated local IPC.
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "xg/interfaces.hpp"
#include "xg/crypto.hpp"
#include "xg/ipc_framing.hpp"
#include "xg/ipc_messages.hpp"
#include "xg/keystats.hpp"
#include "xg/log.hpp"
#include "xg/util.hpp"
#include "xg/config.hpp"
#include "screen_streamer.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

using namespace xg;

namespace {

static std::string extract_active_file(std::string_view /*app*/, std::string_view title) {
    if (title.empty()) return "";
    std::string t(title);

    // Skip leading dirty/unsaved markers (VS Code '● ', Notepad '* ', etc.)
    size_t i = 0;
    while (i < t.size()) {
        if ((uint8_t)t[i] == 0xE2 && i + 2 < t.size() && (uint8_t)t[i + 1] == 0x97 && (uint8_t)t[i + 2] == 0x8F) {
            i += 3;  // UTF-8 '●' (U+25CF)
        } else if (t[i] == '*' || t[i] == ' ' || t[i] == '\t') {
            i++;
        } else {
            break;
        }
    }
    t = t.substr(i);

    // Split on standard delimiter " - " or en-dash " – "
    auto pos = t.find(" - ");
    if (pos == std::string::npos) pos = t.find(" \xe2\x80\x93 ");
    std::string candidate = (pos != std::string::npos) ? t.substr(0, pos) : t;

    // Trim whitespace
    while (!candidate.empty() && (candidate.front() == ' ' || candidate.front() == '\t')) candidate.erase(candidate.begin());
    while (!candidate.empty() && (candidate.back() == ' ' || candidate.back() == '\t')) candidate.pop_back();

    // If candidate contains path separators (e.g. "C:\dir\a.cpp" or "dir/a.cpp"), extract basename
    auto slash = candidate.find_last_of("/\\");
    if (slash != std::string::npos && slash + 1 < candidate.size()) {
        candidate = candidate.substr(slash + 1);
    }

    // Check if candidate looks like a file name (contains dot followed by an extension)
    auto dot = candidate.rfind('.');
    if (dot != std::string::npos && dot > 0 && dot + 1 < candidate.size()) {
        bool valid_ext = true;
        for (size_t k = dot + 1; k < candidate.size(); ++k) {
            char c = candidate[k];
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) {
                valid_ext = false;
                break;
            }
        }
        if (valid_ext && candidate.size() - (dot + 1) <= 10 && candidate.size() <= 40) {
            return candidate;
        }
    }

    return "";
}

static std::string format_app_with_file(const std::string& app, const std::string& file) {
    if (app.empty()) return file;
    if (file.empty()) return app;
    return app + " (" + file + ")";
}

struct LastCopyInfo {
    std::string source_app;
    uint64_t chars = 0;
    uint64_t lines = 0;
    bool looks_like_code = false;
    double code_score = 0.0;
    int64_t timestamp_ms = 0;
};

struct HelperState {
    std::recursive_mutex mu;
    bool want_window = false, want_clipboard = false, want_keys = false;
    bool screen_on = false;  // screen streaming armed: the window monitor must run to feed the privacy mask
    bool window_on = false, clipboard_on = false, keys_on = false;
    std::string mode = "always_on";
    bool full_titles = false;  // title_capture=full: file names from titles may label copy/paste (I-AGENT-9)
    std::string indicator_base = "Monitored by XephyGuard";
    std::string indicator_mode = "always_on";
    std::string last_app;
    std::string last_title;
    std::string last_file;
    LastCopyInfo last_copy;
    // Foreground window for the screen privacy mask only: kept in memory, never sent (ETHICS.md). Separate from
    // last_app/last_title, which exist only while window collection is on.
    std::mutex mask_mu;
    bool mask_known = false;
    std::string mask_app, mask_title;
};

int count_monitors() {
#ifdef _WIN32
    int m = GetSystemMetrics(SM_CMONITORS);
    return m > 0 ? m : 1;
#else
    int connected = 0;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator("/sys/class/drm", ec)) {
        const std::string name = entry.path().filename().string();
        if (name.rfind("card", 0) != 0 || name.find('-') == std::string::npos) continue;
        std::ifstream f(entry.path() / "status");
        std::string status;
        if (std::getline(f, status) && status == "connected") ++connected;
    }
    return connected > 0 ? connected : 1;
#endif
}

bool linux_screen_ready() {
#ifdef _WIN32
    return true;
#else
    const char* stype = std::getenv("XDG_SESSION_TYPE");
    if (stype && std::string(stype) == "wayland") return false;
    const char* disp = std::getenv("DISPLAY");
    return disp && *disp;
#endif
}

int helper_main() {
    log::init("", log::Level::info, true);
    auto plat = make_helper_platform();
    // Undo per-user settings (Task Manager lockdown, PAC) when the helper exits.
    struct SettingsGuard {
        IHelperSystem* sys;
        ~SettingsGuard() {
            if (!sys) return;
            sys->set_taskmgr_disabled(false);
            sys->set_user_proxy({});
        }
    } settings_guard{plat->system.get()};
    HelperState st;
    KeyStatsAggregator keys(5);
    std::mutex keys_mu;
    const std::string endpoint = default_ipc_endpoint();
    auto send = [&](const json& j) { plat->ipc->send(ipc::line(j)); };

    auto report_paste = [&](PasteInfo pi, bool is_copy) {
        std::string src = pi.source_app;
        std::string dst;
        uint64_t chars = pi.chars;
        uint64_t lines = pi.lines;
        bool code = pi.looks_like_code;
        double score = pi.code_score;

        {
            std::lock_guard lk(st.mu);
            if (is_copy) {
                std::string app_label = format_app_with_file(st.last_app, st.last_file);
                if (src.empty()) {
                    src = !app_label.empty() ? app_label : pi.target_app;
                } else if (!st.last_file.empty() && src.find('(') == std::string::npos) {
                    src = format_app_with_file(src, st.last_file);
                }
                if (chars > 0 || lines > 0) {
                    st.last_copy.source_app = src;
                    st.last_copy.chars = chars;
                    st.last_copy.lines = lines;
                    st.last_copy.looks_like_code = code;
                    st.last_copy.code_score = score;
                    st.last_copy.timestamp_ms = now_ms();
                }
            } else {
                std::string app_label = format_app_with_file(st.last_app, st.last_file);
                dst = !app_label.empty() ? app_label : pi.target_app;
                if (src.empty() || (src == dst && !st.last_copy.source_app.empty() && st.last_copy.source_app != dst)) {
                    src = st.last_copy.source_app;
                } else if (src == st.last_app && !st.last_copy.source_app.empty()) {
                    src = st.last_copy.source_app;
                }
                if (chars == 0 && st.last_copy.chars > 0) {
                    chars = st.last_copy.chars;
                    lines = st.last_copy.lines;
                    code = st.last_copy.looks_like_code;
                    score = st.last_copy.code_score;
                }
            }
        }

        if (chars == 0 && lines == 0) {
            return;
        }
        send({{"t", "paste"},
              {"copy", is_copy},
              {"chars", chars},
              {"lines", lines},
              {"code", code},
              {"score", score},
              {"src", src},
              {"dst", is_copy ? "" : dst}});
    };

    std::atomic<int64_t> last_paste_trigger_ms{0};
    auto handle_paste_chord = [&](bool from_local_input) {
        const int64_t now = now_ms();
        const int64_t last = last_paste_trigger_ms.load();
        if (now - last < 250) {
            return;  // debounce auto-repeats from holding Ctrl+V
        }
        last_paste_trigger_ms.store(now);

        if (from_local_input) {
            send({{"t", "paste_chord"}});
        }

        // Run clipboard probe in a detached thread with a short delay so the target app
        // (e.g. VS Code) receives the paste message and finishes its clipboard read first.
        HelperPlatform* pplat = plat.get();
        std::thread([&st, pplat, report_paste]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            PasteInfo pi;
            bool probed = false;
            if (pplat->probe_clipboard) {
                probed = pplat->probe_clipboard(pi);
            }
            if (probed && (pi.chars > 0 || pi.lines > 0)) {
                report_paste(pi, false);
            } else {
                // Fallback: clipboard was temporarily held by target app or probe timed out.
                // Use last known copy attributes only if non-empty.
                PasteInfo fallback_pi;
                {
                    std::lock_guard lk(st.mu);
                    fallback_pi.source_app = st.last_copy.source_app;
                    fallback_pi.chars = st.last_copy.chars;
                    fallback_pi.lines = st.last_copy.lines;
                    fallback_pi.looks_like_code = st.last_copy.looks_like_code;
                    fallback_pi.code_score = st.last_copy.code_score;
                    fallback_pi.target_app = pi.target_app;
                }
                if (fallback_pi.chars > 0 || fallback_pi.lines > 0) {
                    report_paste(fallback_pi, false);
                }
            }
        }).detach();
    };

    auto apply_config = [&] {
        std::lock_guard lk(st.mu);
        const bool need_window = st.want_window || st.screen_on;
        if (plat->window && need_window != st.window_on) {
            if (need_window)
                st.window_on = plat->window->start([&](const WindowInfo& w) {
                    {
                        std::lock_guard lk3(st.mask_mu);
                        st.mask_known = true;
                        st.mask_app = w.app;
                        st.mask_title = w.title;
                    }
                    {
                        std::lock_guard lk2(st.mu);
                        if (!st.want_window) return;  // running only for the screen mask
                        st.last_app = w.app;
                        st.last_title = w.title;
                        st.last_file = st.full_titles ? extract_active_file(w.app, w.title) : std::string{};
                    }
                    send({{"t", "window"}, {"app", w.app}, {"title", w.title}, {"pid", w.pid}});
                });
            else {
                plat->window->stop();
                st.window_on = false;
                std::lock_guard lk3(st.mask_mu);
                st.mask_known = false;
            }
        }
        if (plat->clipboard && st.want_clipboard != st.clipboard_on) {
            if (st.want_clipboard) st.clipboard_on = plat->clipboard->start([&](const PasteInfo& p) { report_paste(p, true); });
            else {
                plat->clipboard->stop();
                st.clipboard_on = false;
            }
        }
        if (plat->input && st.want_keys != st.keys_on) {
            if (st.want_keys)
                st.keys_on = plat->input->start([&](int64_t t, bool bs, bool paste_chord) {
                    if (paste_chord) {
                        handle_paste_chord(true);
                        return;
                    }
                    std::lock_guard lk3(keys_mu);
                    keys.on_key(t, bs);
                });
            else {
                plat->input->stop();
                st.keys_on = false;
            }
        }
    };

    int64_t last_ping = 0;
    bool was_connected = false;

    constexpr bool kDefaultScreenEnabled = true;
    std::atomic<bool> viewing_active{false};
    std::atomic<bool> indicator_dirty{false};
    std::atomic<bool> cfg_screen_enabled{kDefaultScreenEnabled};
    auto on_viewing_changed = [&](bool active) {
        viewing_active.store(active);
        indicator_dirty.store(true);
    };
    auto refresh_indicator = [&]() {
        if (!indicator_dirty.exchange(false)) return;
        std::string text, mode;
        {
            std::lock_guard lk(st.mu);
            mode = st.indicator_mode;
            text = viewing_active.load()
                       ? "Monitored by XephyGuard · instructor is viewing your screen"
                       : st.indicator_base;
        }
        plat->ui->set_indicator(text, mode);
    };
    auto send_screen_caps = [&](bool enabled) {
#ifdef _WIN32
        const std::string stype = "win";
        const bool can_live = enabled;
        const bool secure_desk = true;
#else
        const char* env_s = std::getenv("XDG_SESSION_TYPE");
        const bool is_wayland = env_s && std::string(env_s) == "wayland";
        const bool has_x11 = linux_screen_ready();
        const std::string stype = is_wayland ? "wayland" : (has_x11 ? "x11" : "headless");
        const bool can_live = enabled && has_x11;
        const bool secure_desk = false;
#endif
        send({{"t", "screen_caps"},
              {"session_type", stype},
              {"snapshot", can_live},
              {"live", can_live},
              {"delta_tiles", can_live},
              {"monitors", count_monitors()},
              {"secure_desktop", secure_desk},
              {"ipc_stream", can_live}});  // frames go to the service, which relays them (DECISIONS #24)
    };

    ScreenStreamer screen_streamer;
    // Fail-safe: until the window monitor has reported a window the streamer masks the picture (screen Step 11).
    screen_streamer.set_window_source([&st]() -> std::optional<ScreenWindow> {
        std::lock_guard lk(st.mask_mu);
        if (!st.mask_known) return std::nullopt;
        return ScreenWindow{st.mask_app, st.mask_title};
    });
    // The helper only captures and encodes: frames go to the service over IPC and the service alone talks to
    // the gateway with its mTLS cert (DECISIONS #24). The helper never sees a gateway credential and never
    // reads agent.toml (I-AGENT-2). Settings come only from the service.
    uint32_t screen_stream_id = 0;
    auto send_frame = [&](std::string_view f) { return ipc::send_screen_frame(*plat->ipc, f); };
    auto check_screen_stream = [&]() {
        if (screen_stream_id == 0 || !cfg_screen_enabled.load() || !linux_screen_ready()) return;
        if (!screen_streamer.is_running()) screen_streamer.start(screen_stream_id, send_frame, on_viewing_changed);
    };

    int64_t next_cost_ms = 0;
    while (true) {
        screen_streamer.tick();  // lease lapsed -> stop capturing
        // Streaming cost for the heartbeat (screen Step 57): every 10 s while viewed, numbers only.
        if (viewing_active.load() && plat->ipc->connected() && steady_ms() >= next_cost_ms) {
            const ScreenCost c = screen_streamer.cost();
            send({{"t", "screen_cost"}, {"cpu_pct", c.cpu_pct}, {"level", c.level}});
            next_cost_ms = steady_ms() + 10000;
        }
        if (!plat->ipc->connected()) {
            if (was_connected) plat->ui->set_indicator("XephyGuard service not reachable", "always_on");
            screen_streamer.on_link_lost();
            was_connected = false;
            if (plat->ipc->connect(endpoint)) {
                was_connected = true;
                send({{"t", "hello"},
                      {"v", ipc::kVersion},
                      {"session", std::getenv("XDG_SESSION_TYPE") ? std::getenv("XDG_SESSION_TYPE") : "unknown"},
                      {"display", std::getenv("DISPLAY") ? std::getenv("DISPLAY") : ""},
                      {"caps", {{"window", plat->window && plat->window->available()},
                                {"clipboard", plat->clipboard && plat->clipboard->available()},
                                {"keys", plat->input && plat->input->available()}}}});
                send_screen_caps(cfg_screen_enabled.load());
            } else {
                plat->ui->pump(2000);
                continue;
            }
        }
        plat->ui->pump(100);
        refresh_indicator();
        while (auto line = plat->ipc->receive(0)) {
            auto j = json::parse(*line, nullptr, false);
            if (j.is_discarded()) continue;
            const std::string t = j.value("t", "");
            if (t == "config") {
                const std::string pc_id_str = j.value("pc_id", "");
                bool s_enabled = kDefaultScreenEnabled;
                if (j.contains("screen") && j["screen"].is_object()) {
                    s_enabled = j["screen"].value("enabled", s_enabled);
                }
                cfg_screen_enabled.store(s_enabled);
                if (j.contains("screen") && j["screen"].is_object()) {
                    // The service sends the whole list on every config; a malformed one masks everything.
                    std::vector<MaskRule> rules;
                    bool bad = false;
                    const auto& pr = j["screen"].value("privacy_rules", json::array());
                    if (!pr.is_array() || pr.size() > 128) bad = true;
                    else for (const auto& r : pr) {
                        auto ok_str = [&](const char* k) { return !r.contains(k) || r[k].is_string(); };
                        if (!r.is_object() || !ok_str("process") || !ok_str("title_regex")) { bad = true; break; }
                        MaskRule m{r.value("process", std::string{}), r.value("title_regex", std::string{})};
                        if (m.app.empty() && m.title_regex.empty()) { bad = true; break; }
                        rules.push_back(std::move(m));
                    }
                    if (bad) rules = {MaskRule{{}, ".*"}};  // match every title
                    screen_streamer.set_mask_rules(std::move(rules));
                    // Encoder settings: the service already validated them; anything odd keeps the default.
                    const auto& sc = j["screen"];
                    ScreenEncoding enc;
                    if (sc.contains("codec") && sc["codec"].is_string()) enc.webp = sc["codec"].get<std::string>() == "webp";
                    if (sc.contains("quality") && sc["quality"].is_number_integer())
                        enc.quality = static_cast<int>(std::clamp<int64_t>(sc["quality"].get<int64_t>(), 10, 100));
                    if (sc.contains("webp_method") && sc["webp_method"].is_number_integer())
                        enc.webp_method = static_cast<int>(std::clamp<int64_t>(sc["webp_method"].get<int64_t>(), 0, 6));
                    screen_streamer.set_encoding(enc);
                    if (sc.contains("cpu_budget_pct") && sc["cpu_budget_pct"].is_number())
                        screen_streamer.set_cpu_budget_pct(sc["cpu_budget_pct"].get<double>());  // clamped 1..100
                }
                uint32_t pc_id = 0;
                try { pc_id = static_cast<uint32_t>(std::stoul(pc_id_str)); } catch (...) {}
                if (pc_id > 0 && s_enabled && linux_screen_ready()) {
                    if (pc_id != screen_stream_id) screen_streamer.stop();
                    screen_stream_id = pc_id;
                    screen_streamer.start(pc_id, send_frame, on_viewing_changed);
                } else if (screen_streamer.is_running()) {
                    screen_streamer.stop();
                }
                send_screen_caps(s_enabled);
                {
                    std::lock_guard lk(st.mu);
                    const auto c = j.value("collect", json::object());
                    st.want_window = c.value("window", false);
                    st.screen_on = pc_id > 0 && s_enabled && linux_screen_ready();
                    st.want_clipboard = c.value("clipboard", false);
                    st.want_keys = c.value("keys", false);
                    st.mode = j.value("mode", "always_on");
                    st.full_titles = j.contains("title_capture") && j["title_capture"].is_string() &&
                                     j["title_capture"].get<std::string>() == "full";
                    if (!st.full_titles) st.last_file.clear();
                    if (plat->system) plat->system->set_taskmgr_disabled(st.mode == "test");  // no-op on Linux
                    if (j.contains("proxy_pac_url") && j["proxy_pac_url"].is_string() && plat->system) {
                        plat->system->set_user_proxy(j["proxy_pac_url"].get<std::string>());
                    }
                }
                apply_config();
            } else if (t == "clear_proxy_pac") {
                if (plat->system) plat->system->set_user_proxy({});
            } else if (t == "indicator") {
                {
                    std::lock_guard lk(st.mu);
                    st.indicator_base = j.value("text", "Monitored by XephyGuard");
                    st.indicator_mode = j.value("mode", "always_on");
                }
                indicator_dirty.store(true);
                refresh_indicator();
            } else if (t == "notify") {
                plat->ui->notify(j.value("title", "XephyGuard"), j.value("body", ""), j.value("urgent", false));
            } else if (t == "screen_lease") {
                // The CCC holds a live-view lease for ttl_s seconds; capture runs only inside it (screen Step 15).
                const int64_t ttl = j.value("ttl_s", int64_t{0});
                if (ttl > 0) screen_streamer.grant_lease(static_cast<int>(std::min<int64_t>(ttl, 120)));
            } else if (t == "screen_rx") {
                // One gateway control message relayed by the service (Ack / KeyframeReq / Status).
                if (j.contains("data") && j["data"].is_string()) {
                    if (auto b = crypto::base64_decode(j["data"].get<std::string>()); b && b->size() <= 1024)
                        screen_streamer.on_gateway_message(*b);
                }
            } else if (t == "paste_probe") {
                handle_paste_chord(false);
            }
        }
        {
            std::lock_guard lk(keys_mu);
            if (auto w = keys.flush(now_ms())) {
                json k = w->to_json();
                k["t"] = "keys";
                send(k);
            }
        }
        if (steady_ms() - last_ping > 10000) {
            send({{"t", "ping"}});
            last_ping = steady_ms();
            check_screen_stream();
        }
    }
}

}  // namespace

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) { return helper_main(); }
#else
int main() { return helper_main(); }
#endif
