// systemd integration without libsystemd: sd_notify over $NOTIFY_SOCKET, signal handling, watchdog.
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <thread>
#include <algorithm>
#include <map>
#include <sys/wait.h>

#include "linux_common.hpp"
#include "xg/log.hpp"
#include "xg/util.hpp"

namespace xg::lnx {
namespace {

std::atomic<bool>* g_stop = nullptr;
extern "C" void on_signal(int) {
    if (g_stop) g_stop->store(true);
}

class ServiceHost final : public IServiceHost {
public:
    int run(const std::function<int(std::atomic<bool>&)>& body) override {
        std::atomic<bool> stop{false};
        g_stop = &stop;
        struct sigaction sa{};
        sa.sa_handler = on_signal;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGTERM, &sa, nullptr);
        sigaction(SIGINT, &sa, nullptr);
        signal(SIGPIPE, SIG_IGN);
        if (const char* w = std::getenv("WATCHDOG_USEC")) watchdog_ms_ = std::atoll(w) / 1000 / 2;
        int rc = body(stop);
        g_stop = nullptr;
        return rc;
    }
    void notify_ready() override { notify("READY=1\nSTATUS=XephyGuard agent running"); }
    void notify_stopping() override { notify("STOPPING=1"); }
    void watchdog_ping() override {
        if (watchdog_ms_ <= 0) return;
        const int64_t now = steady_ms();
        if (now - last_ping_ < watchdog_ms_) return;
        last_ping_ = now;
        notify("WATCHDOG=1");
    }

private:
    static void notify(const char* msg) {
        const char* path = std::getenv("NOTIFY_SOCKET");
        if (!path || !*path) return;
        int fd = ::socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
        if (fd < 0) return;
        sockaddr_un sa{};
        sa.sun_family = AF_UNIX;
        std::strncpy(sa.sun_path, path, sizeof sa.sun_path - 1);
        if (path[0] == '@') sa.sun_path[0] = '\0';  // abstract namespace
        socklen_t len = socklen_t(offsetof(sockaddr_un, sun_path) + std::strlen(path));
        ::sendto(fd, msg, std::strlen(msg), MSG_NOSIGNAL, reinterpret_cast<sockaddr*>(&sa), len);
        ::close(fd);
    }
    int64_t watchdog_ms_ = 0;
    int64_t last_ping_ = 0;
};

class HelperLauncher final : public IHelperLauncher {
public:
    // The helper is a globally-enabled systemd *user* unit (xephyguard-helper.service, Restart=always).
    // If a graphical session has no helper, ask that user's systemd instance to start it again.
    int ensure_helpers(const std::vector<UserSession>& sessions, const std::vector<uint32_t>& connected) override {
        int launched = 0;
        const int64_t now = steady_ms();
        for (const auto& s : sessions) {
            if (s.session_type != "x11" && s.session_type != "wayland") continue;
            if (std::find(connected.begin(), connected.end(), s.uid) != connected.end()) continue;
            auto& last = last_attempt_[s.uid];
            if (last != 0 && now - last < 120000) continue;
            last = now;
            const std::string machine = s.user + "@.host";
            pid_t pid = fork();
            if (pid == 0) {
                execlp("systemctl", "systemctl", "--user", "-M", machine.c_str(), "start", "xephyguard-helper.service",
                       static_cast<char*>(nullptr));
                _exit(127);
            }
            if (pid > 0) {
                ++launched;
                log::info("helper: (re)starting helper for {} ({})", s.user, s.session_type);
            }
        }
        // Reap finished children without blocking.
        while (waitpid(-1, nullptr, WNOHANG) > 0) {}
        return launched;
    }

private:
    std::map<uint32_t, int64_t> last_attempt_;
};

}  // namespace

std::unique_ptr<IServiceHost> make_service_host() { return std::make_unique<ServiceHost>(); }
std::unique_ptr<IHelperLauncher> make_helper_launcher() { return std::make_unique<HelperLauncher>(); }

}  // namespace xg::lnx
