// xephyguard-helper: runs inside each logged-in user session. Collects what only the session can see
// (focused window, clipboard sizes, key timing on Windows), shows the transparency indicator and
// student notices, and forwards everything to the service over authenticated local IPC.
#include <atomic>
#include <mutex>
#include <string>
#include <thread>

#include "xg/interfaces.hpp"
#include "xg/ipc_messages.hpp"
#include "xg/keystats.hpp"
#include "xg/log.hpp"
#include "xg/util.hpp"
#include "xg/config.hpp"
#include "screen_streamer.hpp"

#ifdef _WIN32
#include <windows.h>
namespace {
void set_user_taskmgr_disabled(bool disabled) {
    HKEY hk = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\System",
                        0, nullptr, 0, KEY_SET_VALUE, nullptr, &hk, nullptr) == ERROR_SUCCESS) {
        if (disabled) {
            DWORD one = 1;
            RegSetValueExW(hk, L"DisableTaskMgr", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&one), sizeof(one));
        } else {
            RegDeleteValueW(hk, L"DisableTaskMgr");
        }
        RegCloseKey(hk);
    }
}
struct TaskMgrGuard {
    ~TaskMgrGuard() { set_user_taskmgr_disabled(false); }
};
}  // namespace
#endif

using namespace xg;

namespace {

static std::string extract_active_file(std::string_view app, std::string_view title) {
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
    bool window_on = false, clipboard_on = false, keys_on = false;
    std::string mode = "always_on";
    std::string last_app;
    std::string last_title;
    std::string last_file;
    LastCopyInfo last_copy;
};

int helper_main() {
    log::init("", log::Level::info, true);
#ifdef _WIN32
    TaskMgrGuard taskmgr_guard;
#endif
    auto plat = make_helper_platform();
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
                    src = !app_label.empty() ? app_label : "unknown";
                } else if (!st.last_file.empty() && src.find('(') == std::string::npos) {
                    src = format_app_with_file(src, st.last_file);
                }
                st.last_copy.source_app = src;
                st.last_copy.chars = chars;
                st.last_copy.lines = lines;
                st.last_copy.looks_like_code = code;
                st.last_copy.code_score = score;
                st.last_copy.timestamp_ms = now_ms();
            } else {
                std::string app_label = format_app_with_file(st.last_app, st.last_file);
                dst = !app_label.empty() ? app_label : (!pi.target_app.empty() ? pi.target_app : "unknown");
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
    auto handle_paste_chord = [&] {
        const int64_t now = now_ms();
        const int64_t last = last_paste_trigger_ms.load();
        if (now - last < 250) {
            return;  // debounce auto-repeats from holding Ctrl+V
        }
        last_paste_trigger_ms.store(now);

        send({{"t", "paste_chord"}});

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
            if (probed) {
                report_paste(pi, false);
            } else {
                // Fallback: clipboard was temporarily held by target app or probe timed out.
                // Use last known copy attributes!
                PasteInfo fallback_pi;
                {
                    std::lock_guard lk(st.mu);
                    fallback_pi.source_app = st.last_copy.source_app;
                    fallback_pi.chars = st.last_copy.chars;
                    fallback_pi.lines = st.last_copy.lines;
                    fallback_pi.looks_like_code = st.last_copy.looks_like_code;
                    fallback_pi.code_score = st.last_copy.code_score;
                }
                report_paste(fallback_pi, false);
            }
        }).detach();
    };

    auto apply_config = [&] {
        std::lock_guard lk(st.mu);
        if (plat->window && st.want_window != st.window_on) {
            if (st.want_window)
                st.window_on = plat->window->start([&](const WindowInfo& w) {
                    {
                        std::lock_guard lk2(st.mu);
                        st.last_app = w.app;
                        st.last_title = w.title;
                        st.last_file = extract_active_file(w.app, w.title);
                    }
                    send({{"t", "window"}, {"app", w.app}, {"title", w.title}, {"pid", w.pid}});
                });
            else {
                plat->window->stop();
                st.window_on = false;
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
                        handle_paste_chord();
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

    ScreenStreamer screen_streamer;
    auto check_screen_stream = [&]() {
        if (screen_streamer.is_running()) return;
        bool ok = false;
        std::string err;
        Config cfg = Config::load(default_config_path(), &ok, &err);
        if (ok && !cfg.ccc_host.empty() && !cfg.pc_id.empty()) {
            uint32_t pc_id = 0;
            try { pc_id = static_cast<uint32_t>(std::stoul(cfg.pc_id)); } catch (...) {}
            if (pc_id > 0) {
                screen_streamer.start(cfg.ccc_host, pc_id, 8082);
            }
        }
    };
    check_screen_stream();

    while (true) {
        if (!plat->ipc->connected()) {
            if (was_connected) plat->ui->set_indicator("XephyGuard service not reachable", "always_on");
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
            } else {
                plat->ui->pump(2000);
                continue;
            }
        }
        plat->ui->pump(100);
        while (auto line = plat->ipc->receive(0)) {
            auto j = json::parse(*line, nullptr, false);
            if (j.is_discarded()) continue;
            const std::string t = j.value("t", "");
            if (t == "config") {
                const std::string host = j.value("ccc_host", "");
                const std::string pc_id_str = j.value("pc_id", "");
                if (!host.empty() && !pc_id_str.empty()) {
                    uint32_t pc_id = 0;
                    try { pc_id = static_cast<uint32_t>(std::stoul(pc_id_str)); } catch (...) {}
                    if (pc_id > 0) {
                        screen_streamer.start(host, pc_id, 8082);
                    }
                }
                {
                    std::lock_guard lk(st.mu);
                    const auto c = j.value("collect", json::object());
                    st.want_window = c.value("window", false);
                    st.want_clipboard = c.value("clipboard", false);
                    st.want_keys = c.value("keys", false);
                    st.mode = j.value("mode", "always_on");
#ifdef _WIN32
                    set_user_taskmgr_disabled(st.mode == "test");
#endif
                }
                apply_config();
            } else if (t == "indicator") {
                plat->ui->set_indicator(j.value("text", "Monitored by XephyGuard"), j.value("mode", "always_on"));
            } else if (t == "notify") {
                plat->ui->notify(j.value("title", "XephyGuard"), j.value("body", ""), j.value("urgent", false));
            } else if (t == "paste_probe") {
                handle_paste_chord();
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
