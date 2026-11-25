// Helper-side OS settings for the logged-in user: GNOME proxy PAC via gsettings (fork/exec, no shell).
// Moved from apps/helper_main.cpp unchanged (plan step 22, I-AGENT-22).
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdlib>
#include <mutex>
#include <string>

#include "linux_common.hpp"

namespace xg::lnx {

namespace {
void run_gsettings(const char* key, const char* val) {
    pid_t pid = ::fork();
    if (pid == 0) {
        int devnull = ::open("/dev/null", O_RDWR);
        if (devnull >= 0) {
            ::dup2(devnull, 0);
            ::dup2(devnull, 1);
            ::dup2(devnull, 2);
            ::close(devnull);
        }
        ::execlp("gsettings", "gsettings", "set", "org.gnome.system.proxy", key, val, static_cast<char*>(nullptr));
        _exit(127);
    }
    if (pid > 0) {
        int status = 0;
        ::waitpid(pid, &status, 0);
    }
}

void set_user_proxy_pac(const std::string& pac_url) {
    static std::mutex mu;
    static std::string applied_pac;
    std::lock_guard lk(mu);
    if (pac_url == applied_pac) return;
    if (!std::getenv("DBUS_SESSION_BUS_ADDRESS")) {
        applied_pac = pac_url;
        return;
    }
    if (!pac_url.empty()) {
        run_gsettings("autoconfig-url", pac_url.c_str());
        run_gsettings("mode", "auto");
        applied_pac = pac_url;
    } else if (!applied_pac.empty()) {
        run_gsettings("mode", "none");
        run_gsettings("autoconfig-url", "");
        applied_pac.clear();
    }
}

class HelperSystem final : public IHelperSystem {
public:
    void set_user_proxy(const std::string& pac_url) override { set_user_proxy_pac(pac_url); }
};
}  // namespace

std::unique_ptr<IHelperSystem> make_helper_system() { return std::make_unique<HelperSystem>(); }

}  // namespace xg::lnx
