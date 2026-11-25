#ifndef _WIN32

#include <catch2/catch_test_macros.hpp>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#include "linux_common.hpp"
#include "xg/interfaces.hpp"
#include "xg/util.hpp"
#include <nlohmann/json.hpp>

#ifdef XG_HAVE_X11
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#endif

namespace {

struct EnvGuard {
    std::string name;
    bool had = false;
    std::string old;
    explicit EnvGuard(std::string n) : name(std::move(n)) {
        if (const char* v = std::getenv(name.c_str())) {
            had = true;
            old = v;
        }
    }
    void set(const char* val) const {
        if (val) ::setenv(name.c_str(), val, 1);
        else ::unsetenv(name.c_str());
    }
    ~EnvGuard() {
        if (had) ::setenv(name.c_str(), old.c_str(), 1);
        else ::unsetenv(name.c_str());
    }
};

}  // namespace

#ifdef XG_HAVE_X11
struct XvfbGuard {
    std::string disp;
    pid_t pid = -1;
    Display* d = nullptr;
    EnvGuard g_disp{"DISPLAY"};
    EnvGuard g_sess{"XDG_SESSION_TYPE"};

    bool start() {
        for (int port = 91; port < 120; ++port) {
            std::string lock = "/tmp/.X" + std::to_string(port) + "-lock";
            std::error_code ec;
            if (std::filesystem::exists(lock, ec)) continue;
            disp = ":" + std::to_string(port);
            pid = ::fork();
            if (pid == 0) {
                int devnull = ::open("/dev/null", O_RDWR);
                if (devnull >= 0) { ::dup2(devnull, 1); ::dup2(devnull, 2); ::close(devnull); }
                ::execl("/usr/bin/Xvfb", "Xvfb", disp.c_str(), "-screen", "0", "800x600x24", "-nolisten", "tcp",
                        static_cast<char*>(nullptr));
                _exit(127);
            }
            if (pid > 0) break;
        }
        if (pid <= 0) return false;
        g_disp.set(disp.c_str());
        g_sess.set("x11");
        for (int i = 0; i < 40; ++i) {
            d = XOpenDisplay(disp.c_str());
            if (d) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
        stop();
        return false;
    }

    void stop() {
        if (d) { XCloseDisplay(d); d = nullptr; }
        if (pid > 0) {
            ::kill(pid, SIGTERM);
            ::waitpid(pid, nullptr, 0);
            pid = -1;
        }
    }

    ~XvfbGuard() { stop(); }
};

TEST_CASE("X11 window monitor survives 1000 rapid window open/close cycles", "[linux][x11]") {
    if (::access("/usr/bin/Xvfb", X_OK) != 0) SKIP("Xvfb not installed");

    XvfbGuard xvfb;
    REQUIRE(xvfb.start());
    Display* d = xvfb.d;

    auto mon = xg::lnx::make_window_monitor();
    REQUIRE(mon->available());
    std::atomic<int> events{0};
    REQUIRE(mon->start([&](const xg::WindowInfo&) { events.fetch_add(1, std::memory_order_relaxed); }));

    const Atom active_atom = XInternAtom(d, "_NET_ACTIVE_WINDOW", False);
    const Atom name_atom = XInternAtom(d, "_NET_WM_NAME", False);
    const Atom utf8_atom = XInternAtom(d, "UTF8_STRING", False);
    const Window root = DefaultRootWindow(d);

    for (int i = 0; i < 1000; ++i) {
        Window w = XCreateSimpleWindow(d, root, 10, 10, 200, 100, 0, 0, 0);
        std::string title = "win-" + std::to_string(i);
        XChangeProperty(d, w, name_atom, utf8_atom, 8, PropModeReplace,
                        reinterpret_cast<const unsigned char*>(title.data()), int(title.size()));
        XChangeProperty(d, root, active_atom, XA_WINDOW, 32, PropModeReplace,
                        reinterpret_cast<const unsigned char*>(&w), 1);
        XFlush(d);
        // Destroy immediately to provoke BadWindow races in X11WindowMonitor
        XDestroyWindow(d, w);
        XFlush(d);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    mon->stop();
    SUCCEED("Survived 1000 rapid X11 create/destroy cycles cleanly");
}
#endif

TEST_CASE("Linux browser policy writer merges Firefox Certificates and sets Chromium/Edge PAC", "[linux][policy]") {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / ("xg-pol-" + std::to_string(::getpid()));
    fs::remove_all(root);
    fs::create_directories(root / "etc/firefox/policies");
    REQUIRE(xg::write_file_atomic((root / "etc/firefox/policies/policies.json").string(),
                                  R"({"policies":{"Certificates":{"ImportEnterpriseRoots":true,"Install":["/tmp/ca.crt"]}}})"));

    REQUIRE(xg::lnx::apply_linux_browser_policies(root.string(), true, "", "http://192.168.0.51/proxy.pac"));

    for (const char* p : {"etc/opt/chrome/policies/managed/xephyguard.json",
                          "etc/chromium/policies/managed/xephyguard.json",
                          "etc/opt/edge/policies/managed/xephyguard.json"}) {
        auto j = nlohmann::json::parse(xg::read_file((root / p).string()));
        CHECK(j["ProxyMode"] == "pac_script");
        CHECK(j["ProxyPacUrl"] == "http://192.168.0.51/proxy.pac");
        CHECK(j["QuicAllowed"] == false);
        CHECK(j["DnsOverHttpsMode"] == "off");
        CHECK(j["DefaultSearchProviderEnabled"] == true);
        CHECK(j["DefaultSearchProviderSearchURL"].get<std::string>().find("udm=14") != std::string::npos);
    }

    auto ff = nlohmann::json::parse(xg::read_file((root / "etc/firefox/policies/policies.json").string()));
    CHECK(ff["policies"]["Certificates"]["ImportEnterpriseRoots"] == true);
    CHECK(ff["policies"]["Certificates"]["Install"][0] == "/tmp/ca.crt");
    CHECK(ff["policies"]["Proxy"]["Mode"] == "autoConfig");
    CHECK(ff["policies"]["Proxy"]["AutoConfigURL"] == "http://192.168.0.51/proxy.pac");
    CHECK(ff["policies"]["Proxy"]["Locked"] == true);
    CHECK(ff["policies"]["DNSOverHTTPS"]["Enabled"] == false);
    CHECK(ff["policies"]["SearchEngines"]["Default"] == "Google");

    REQUIRE(xg::lnx::apply_linux_browser_policies(root.string(), false, "", ""));
    auto chrome_cleared = nlohmann::json::parse(xg::read_file((root / "etc/opt/chrome/policies/managed/xephyguard.json").string()));
    CHECK_FALSE(chrome_cleared.contains("ProxyMode"));
    CHECK_FALSE(chrome_cleared.contains("ProxyPacUrl"));
    CHECK_FALSE(chrome_cleared.contains("DefaultSearchProviderSearchURL"));

    auto ff_cleared = nlohmann::json::parse(xg::read_file((root / "etc/firefox/policies/policies.json").string()));
    CHECK_FALSE(ff_cleared["policies"].contains("Proxy"));
    CHECK_FALSE(ff_cleared["policies"].contains("SearchEngines"));
    CHECK(ff_cleared["policies"]["Certificates"]["ImportEnterpriseRoots"] == true);

    fs::remove_all(root);
}

TEST_CASE("Linux systemd-resolved drop-in writes routable CCC IP and ignores loopback", "[linux][dns]") {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / ("xg-dns-" + std::to_string(::getpid()));
    fs::remove_all(root);
    const fs::path dropin = root / "etc/systemd/resolved.conf.d/xephyguard.conf";

    CHECK_FALSE(xg::lnx::apply_linux_resolved_dns(root.string(), "127.0.0.1", false));
    CHECK_FALSE(fs::exists(dropin));

    REQUIRE(xg::lnx::apply_linux_resolved_dns(root.string(), "192.168.0.51", false));
    const std::string body = xg::read_file(dropin.string());
    CHECK(body.find("DNS=192.168.0.51") != std::string::npos);
    CHECK(body.find("DNSOverTLS=no") != std::string::npos);

    REQUIRE(xg::lnx::apply_linux_resolved_dns(root.string(), "", false));
    CHECK_FALSE(fs::exists(dropin));
    fs::remove_all(root);
}

TEST_CASE("Linux SOCK_DIAG delta tracker suppresses idle keep-alives and shared CDN false positives", "[linux][net]") {
    xg::lnx::SocketDeltaTracker tracker;
    tracker.hints.process_domains["copilot-runtime"] = "api.githubcopilot.com";  // from the bundle's web section
    tracker.hints.telemetry_domains = {"telemetry"};
    const std::vector<std::string> domains = {"chatgpt.com", "githubcopilot.com"};

    // Two connections to the SAME Cloudflare CDN IP: socket 101 is ChatGPT, socket 102 is non-AI Cloudflare docs
    tracker.tuple_sni[xg::lnx::tuple_key(50101, "104.18.32.47", 443)] = "chatgpt.com";
    tracker.tuple_sni[xg::lnx::tuple_key(50102, "104.18.32.47", 443)] = "docs.cloudflare.com";
    // Third socket: copilot-runtime fallback without SNI record
    std::vector<xg::lnx::TcpSocketStat> t0 = {
        {101, 50101, "104.18.32.47", 443, 0, "", 1000, 2000},
        {102, 50102, "104.18.32.47", 443, 0, "", 1000, 2000},
        {103, 50103, "20.1.2.3", 443, 0, "", 500, 500},
    };
    auto resolver = [](std::vector<xg::lnx::TcpSocketStat*>& socks) {
        for (auto* s : socks) {
            if (s->inode == 101 || s->inode == 102) { s->pid = 222; s->process = "chrome"; }
            if (s->inode == 103) { s->pid = 333; s->process = "copilot-runtime"; }
        }
    };

    // Initial baseline sample -> zero emissions
    CHECK(tracker.evaluate(t0, domains, resolver).empty());

    // Keep-alive tick (< 350 sent, < 600 recv) -> zero emissions
    std::vector<xg::lnx::TcpSocketStat> t1 = t0;
    t1[0].bytes_sent += 80;
    t1[0].bytes_recv += 120;
    t1[1].bytes_sent += 4000;  // busy non-AI CDN download must never trigger
    t1[1].bytes_recv += 12000;
    t1[2].bytes_sent += 40;
    t1[2].bytes_recv += 90;
    CHECK(tracker.evaluate(t1, domains, resolver).empty());

    // Real AI prompt burst on socket 101 + copilot-runtime socket 103 + heavy CDN download on 102
    std::vector<xg::lnx::TcpSocketStat> t2 = t1;
    t2[0].bytes_sent += 650;
    t2[0].bytes_recv += 1800;
    t2[1].bytes_sent += 2000;
    t2[1].bytes_recv += 50000;
    t2[2].bytes_sent += 500;
    t2[2].bytes_recv += 1500;

    const auto flows = tracker.evaluate(t2, domains, resolver);
    REQUIRE(flows.size() == 2);
    CHECK(flows[0].pid == 222);
    CHECK(flows[0].domain == "chatgpt.com");
    CHECK(flows[0].bytes_sent == 650);
    CHECK(flows[0].bytes_recv == 1800);
    CHECK(flows[1].pid == 333);
    CHECK(flows[1].domain == "api.githubcopilot.com");
}

TEST_CASE("Linux GpuMonitor reads Intel i915/Xe RC6 residency and AMD sysfs accurately") {
    auto write_file = [](const std::filesystem::path& p, const std::string& content) {
        std::filesystem::create_directories(p.parent_path());
        std::ofstream out(p);
        out << content;
    };

    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / ("xg-drm-" + xg::random_hex(4));
    std::filesystem::create_directories(root);

    {
        auto mon = xg::lnx::make_gpu_monitor(root.string());
        CHECK_FALSE(mon->available());
        CHECK_FALSE(mon->sample().has_value());
    }

    // Intel i915: initial frequency fallback + RC6 residency delta on second poll.
    const auto i915_root = root / "i915";
    std::filesystem::create_directories(i915_root);
    write_file(i915_root / "card1/device/vendor", "0x8086\n");
    write_file(i915_root / "card1/device/device", "0x46b3\n");
    write_file(i915_root / "card1/device/uevent", "DRIVER=i915\nPCI_ID=8086:46B3\n");
    write_file(i915_root / "card1/gt/gt0/rc6_residency_ms", "1000\n");
    write_file(i915_root / "card1/gt/gt0/rps_act_freq_mhz", "550\n");
    write_file(i915_root / "card1/gt/gt0/rps_RP0_freq_mhz", "1100\n");

    {
        auto mon = xg::lnx::make_gpu_monitor(i915_root.string());
        REQUIRE(mon->available());
        auto s1 = mon->sample();
        REQUIRE(s1.has_value());
        CHECK(s1->name == "intel-i915 (0x46b3)");
        CHECK(s1->util_pct == 50.0);

        std::this_thread::sleep_for(std::chrono::milliseconds(80));
        write_file(i915_root / "card1/gt/gt0/rc6_residency_ms", "1020\n");
        auto s2 = mon->sample();
        REQUIRE(s2.has_value());
        CHECK(s2->util_pct >= 40.0);
        CHECK(s2->util_pct <= 95.0);
    }

    // Intel Xe driver layout.
    const auto xe_root = root / "xe";
    std::filesystem::create_directories(xe_root);
    write_file(xe_root / "card0/device/vendor", "0x8086\n");
    write_file(xe_root / "card0/device/device", "0x7d55\n");
    write_file(xe_root / "card0/device/uevent", "DRIVER=xe\n");
    write_file(xe_root / "card0/device/tile0/gt0/gtidle/idle_residency_ms", "500\n");
    write_file(xe_root / "card0/device/tile0/gt0/freq0/act_freq", "0\n");
    write_file(xe_root / "card0/device/tile0/gt0/freq0/rp0_freq", "2250\n");

    {
        auto mon = xg::lnx::make_gpu_monitor(xe_root.string());
        REQUIRE(mon->available());
        auto s = mon->sample();
        REQUIRE(s.has_value());
        CHECK(s->name == "intel-xe (0x7d55)");
        CHECK(s->util_pct == 0.0);
    }

    // AMD amdgpu sysfs layout.
    const auto amd_root = root / "amd";
    std::filesystem::create_directories(amd_root);
    write_file(amd_root / "card0/device/gpu_busy_percent", "37\n");
    write_file(amd_root / "card0/device/mem_info_vram_used", "536870912\n");
    write_file(amd_root / "card0/device/mem_info_vram_total", "4294967296\n");
    write_file(amd_root / "card0/device/uevent", "DRIVER=amdgpu\n");

    {
        auto mon = xg::lnx::make_gpu_monitor(amd_root.string());
        REQUIRE(mon->available());
        auto s = mon->sample();
        REQUIRE(s.has_value());
        CHECK(s->name == "amdgpu:card0");
        CHECK(s->util_pct == 37.0);
        CHECK(s->mem_used_mb == 512.0);
        CHECK(s->mem_total_mb == 4096.0);
    }

    std::filesystem::remove_all(root);
}

TEST_CASE("Linux indicator geometry avoids top bar and sanitizes UTF-8 punctuation") {
    CHECK(xg::lnx::sanitize_indicator_label("Monitored by XephyGuard · mode: Always-on") ==
          "Monitored by XephyGuard - mode: Always-on");

    // Standard Ubuntu GNOME desktop workarea: 31px top navbar, 1920x1049 usable space.
    const xg::lnx::IndicatorWorkArea gnome_wa{0, 31, 1920, 1049};
    const auto r = xg::lnx::compute_indicator_rect(gnome_wa, 240);
    CHECK(r.width >= 274);
    CHECK(r.height == 24);
    CHECK(r.y >= gnome_wa.y);
    CHECK(r.y + r.height <= gnome_wa.y + gnome_wa.height);
    CHECK(r.x + r.width <= gnome_wa.x + gnome_wa.width);
    CHECK(r.y > 900);  // sits cleanly in bottom-right margin, far below navbar / window controls

    // Left + bottom dock workarea: still stays strictly inside available region.
    const xg::lnx::IndicatorWorkArea docked_wa{68, 31, 1852, 990};
    const auto rd = xg::lnx::compute_indicator_rect(docked_wa, 240);
    CHECK(rd.x >= docked_wa.x);
    CHECK(rd.x + rd.width <= docked_wa.x + docked_wa.width);
    CHECK(rd.y >= docked_wa.y);
    CHECK(rd.y + rd.height <= docked_wa.y + docked_wa.height);
}

#ifdef XG_HAVE_X11
TEST_CASE("X11StudentUi maps indicator inside _NET_WORKAREA clear of top navbar", "[linux][x11]") {
    if (::access("/usr/bin/Xvfb", X_OK) != 0) SKIP("Xvfb not installed");

    XvfbGuard xvfb;
    REQUIRE(xvfb.start());
    Display* d = xvfb.d;

    const Window root = DefaultRootWindow(d);
    const Atom workarea_atom = XInternAtom(d, "_NET_WORKAREA", False);
    const Atom desktop_atom = XInternAtom(d, "_NET_CURRENT_DESKTOP", False);
    const unsigned long desktop_idx = 0;
    const unsigned long wa[4] = {0, 31, 800, 569};
    XChangeProperty(d, root, desktop_atom, XA_CARDINAL, 32, PropModeReplace,
                    reinterpret_cast<const unsigned char*>(&desktop_idx), 1);
    XChangeProperty(d, root, workarea_atom, XA_CARDINAL, 32, PropModeReplace,
                    reinterpret_cast<const unsigned char*>(wa), 4);
    XSync(d, False);

    auto ui = xg::lnx::make_student_ui();
    REQUIRE(ui);
    ui->set_indicator("Monitored by XephyGuard · mode: Test", "test");
    ui->pump(30);

    Window r_ret = None, p_ret = None;
    Window* children = nullptr;
    unsigned int nchildren = 0;
    REQUIRE(XQueryTree(d, root, &r_ret, &p_ret, &children, &nchildren) != 0);

    bool found = false;
    XWindowAttributes found_attrs{};
    for (unsigned int i = 0; i < nchildren; ++i) {
        char* name = nullptr;
        if (XFetchName(d, children[i], &name) && name) {
            if (std::string(name) == "XephyGuard") {
                found = XGetWindowAttributes(d, children[i], &found_attrs) != 0;
            }
            XFree(name);
            if (found) break;
        }
    }
    if (children) XFree(children);

    REQUIRE(found);
    CHECK(found_attrs.map_state == IsViewable);
    CHECK(found_attrs.y >= 31);
    CHECK(found_attrs.x + found_attrs.width <= 800);
    CHECK(found_attrs.y + found_attrs.height <= 600);

    ui.reset();
}
#endif

TEST_CASE("Linux system info reports screen caps by session type until a helper reports its own", "[linux]") {
    auto sys = xg::lnx::make_system_info();
    using S = xg::UserSession;
    auto caps = sys->fallback_screen_caps({S{"a", "/home/a", 1000, "wayland-0", "wayland", true}}, true);
    CHECK(caps["session_type"] == "wayland");
    CHECK(caps["live"] == false);
    caps = sys->fallback_screen_caps({S{"a", "/home/a", 1000, ":0", "x11", true}}, true);
    CHECK(caps["session_type"] == "x11");
    CHECK(caps["live"] == true);
    CHECK(sys->fallback_screen_caps({S{"a", "/home/a", 1000, ":0", "x11", true}}, false)["live"] == false);
}

TEST_CASE("ChildReaper reaps only its own children (I-AGENT-15)", "[linux]") {
    xg::lnx::ChildReaper r;
    const pid_t mine = ::fork();
    if (mine == 0) _exit(0);
    const pid_t other = ::fork();
    if (other == 0) _exit(7);
    r.add(mine);
    for (int i = 0; i < 200 && r.reap() > 0; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    CHECK(r.reap() == 0);
    // The child we did not register still has its exit status for its real owner.
    int status = 0;
    REQUIRE(::waitpid(other, &status, 0) == other);
    CHECK(WEXITSTATUS(status) == 7);
}

TEST_CASE("run_program executes by absolute path without a shell and feeds stdin (I-AGENT-25)", "[linux]") {
    using xg::lnx::run_program;
    CHECK(run_program({"/bin/true"}) == 0);
    CHECK(run_program({"/bin/false"}) == 1);
    CHECK(run_program({"/nonexistent/prog"}) == 127);
    CHECK(run_program({"true"}) == -1);  // no PATH lookup
    // stdin reaches the child; shell metacharacters are plain argument bytes.
    CHECK(run_program({"/usr/bin/grep", "-q", "table inet"}, "table inet x {}\n") == 0);
    CHECK(run_program({"/usr/bin/grep", "-q", "; touch /tmp/xg-should-not-exist"}, "nothing\n") == 1);
    CHECK_FALSE(std::filesystem::exists("/tmp/xg-should-not-exist"));
    // A child that ignores a large stdin must not kill us with SIGPIPE.
    CHECK(run_program({"/bin/true"}, std::string(1 << 20, 'x')) == 0);
}

TEST_CASE("isolation ruleset keeps IPv4 and IPv6 CCC addresses reachable (I-AGENT-26)", "[linux]") {
    const auto r = xg::lnx::nft_isolation_rules({"203.0.113.5", "2001:db8::5", "bad;drop"});
    CHECK(r.find("203.0.113.4") != std::string::npos);  // range split around the CCC
    CHECK(r.find("ip6 daddr { 2001:db8::5 } accept") != std::string::npos);
    CHECK(r.find("ip6 daddr { 2001:db8::5 } accept") < r.find("ip6 daddr 2000::/3 drop"));
    CHECK(r.find("bad;drop") == std::string::npos);
    CHECK(xg::lnx::nft_isolation_rules({"203.0.113.5"}).find("accept\n") == std::string::npos);
}

TEST_CASE("network lock rulesets: observe drops only QUIC, strict drops every public destination", "[linux]") {
    using xg::NetworkLock;
    CHECK(xg::lnx::nft_lock_rules(NetworkLock::off, {"203.0.113.5"}).empty());
    const auto obs = xg::lnx::nft_lock_rules(NetworkLock::observe, {"203.0.113.5", "2001:db8::5"});
    CHECK(obs.find("table inet xephyguard_lock") != std::string::npos);
    CHECK(obs.find("xephyguard_isolate") == std::string::npos);  // never touches the isolation table
    CHECK(obs.find("} udp dport 443 drop") != std::string::npos);
    CHECK(obs.find("2000::/3 udp dport 443 drop") != std::string::npos);
    CHECK(obs.find("203.0.113.4") != std::string::npos);  // the CCC stays reachable
    CHECK(obs.find("ip6 daddr { 2001:db8::5 } accept") != std::string::npos);
    const auto strict = xg::lnx::nft_lock_rules(NetworkLock::strict, {"203.0.113.5"});
    CHECK(strict.find("udp dport") == std::string::npos);
    CHECK(strict.find("} drop") != std::string::npos);
    CHECK(strict.find("2000::/3 drop") != std::string::npos);
    CHECK(strict.find("203.0.113.4") != std::string::npos);
}

TEST_CASE("Linux kill refuses a pid whose start time changed (F35)", "[linux]") {
    auto pm = xg::lnx::make_process_monitor();
    const pid_t child = ::fork();
    if (child == 0) {
        ::signal(SIGTERM, SIG_DFL);  // not Catch2's inherited fatal-signal reporter
        ::pause();
        _exit(0);
    }
    int64_t start = 0;
    for (const auto& p : pm->snapshot())
        if (p.pid == child) start = p.start_time;
    REQUIRE(start != 0);
    CHECK_FALSE(pm->kill(child, start + 1));  // wrong start: untouched
    CHECK(::kill(child, 0) == 0);
    CHECK(pm->kill(child, start));
    int status = 0;
    REQUIRE(::waitpid(child, &status, 0) == child);
    CHECK(WIFSIGNALED(status));
}

TEST_CASE("Linux browser policy blocks AI extensions in Test mode and keeps admin entries (F80)", "[linux][policy]") {
    const auto root = std::filesystem::temp_directory_path() / ("xg-ext-" + xg::random_hex(4));
    const auto chrome = root / "etc/opt/chrome/policies/managed/xephyguard.json";
    std::filesystem::create_directories(chrome.parent_path());
    REQUIRE(xg::write_file_atomic(chrome.string(), R"({"ExtensionInstallBlocklist":["adminblockedextensionidaaaaaaaaa"]})"));
    const std::vector<std::string> ai = {"jgjaeacdkonaoafenlfkkkmbaopkbilf"};
    REQUIRE(xg::lnx::apply_linux_browser_policies(root.string(), false, "", "", ai, ai));
    auto doc = xg::json::parse(xg::read_file(chrome.string()));
    CHECK(doc["ExtensionInstallBlocklist"] ==
          xg::json({"adminblockedextensionidaaaaaaaaa", "jgjaeacdkonaoafenlfkkkmbaopkbilf"}));
    REQUIRE(xg::lnx::apply_linux_browser_policies(root.string(), false, "", "", {}, ai));  // Test over
    doc = xg::json::parse(xg::read_file(chrome.string()));
    CHECK(doc["ExtensionInstallBlocklist"] == xg::json({"adminblockedextensionidaaaaaaaaa"}));
    std::filesystem::remove_all(root);
}

#endif  // _WIN32
