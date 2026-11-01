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

#ifdef _WIN32
#include <windows.h>
#endif

using namespace xg;

namespace {

struct HelperState {
    std::mutex mu;
    bool want_window = false, want_clipboard = false, want_keys = false;
    bool window_on = false, clipboard_on = false, keys_on = false;
    std::string mode = "always_on";
    std::string last_app;
};

int helper_main() {
    log::init("", log::Level::info, true);
    auto plat = make_helper_platform();
    HelperState st;
    KeyStatsAggregator keys(5);
    std::mutex keys_mu;
    const std::string endpoint = default_ipc_endpoint();
    auto send = [&](const json& j) { plat->ipc->send(ipc::line(j)); };

    auto report_paste = [&](PasteInfo pi, bool is_copy) {
        send({{"t", "paste"}, {"copy", is_copy}, {"chars", pi.chars}, {"lines", pi.lines}, {"code", pi.looks_like_code},
              {"score", pi.code_score}, {"src", pi.source_app}, {"dst", is_copy ? "" : st.last_app}});
    };

    auto apply_config = [&] {
        std::lock_guard lk(st.mu);
        if (plat->window && st.want_window != st.window_on) {
            if (st.want_window)
                st.window_on = plat->window->start([&](const WindowInfo& w) {
                    {
                        std::lock_guard lk2(st.mu);
                        st.last_app = w.app;
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
                        send({{"t", "paste_chord"}});
                        PasteInfo pi;
                        if (plat->probe_clipboard && plat->probe_clipboard(pi)) report_paste(pi, false);
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
                {
                    std::lock_guard lk(st.mu);
                    const auto c = j.value("collect", json::object());
                    st.want_window = c.value("window", false);
                    st.want_clipboard = c.value("clipboard", false);
                    st.want_keys = c.value("keys", false);
                    st.mode = j.value("mode", "always_on");
                }
                apply_config();
            } else if (t == "indicator") {
                plat->ui->set_indicator(j.value("text", "Monitored by XephyGuard"), j.value("mode", "always_on"));
            } else if (t == "notify") {
                plat->ui->notify(j.value("title", "XephyGuard"), j.value("body", ""), j.value("urgent", false));
            } else if (t == "paste_probe") {
                PasteInfo pi;
                if (plat->probe_clipboard && plat->probe_clipboard(pi)) report_paste(pi, false);
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
        }
    }
}

}  // namespace

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) { return helper_main(); }
#else
int main() { return helper_main(); }
#endif
