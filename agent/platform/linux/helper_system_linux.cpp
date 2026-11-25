// Helper-side OS settings for the logged-in user: GNOME proxy PAC via gsettings (fork/exec, no shell).
// Moved from apps/helper_main.cpp unchanged (plan step 22, I-AGENT-22).
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdlib>
#include <mutex>
#include <set>
#include <string>

#include "linux_common.hpp"
#include "xg/util.hpp"

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
    bool session_env_stale() override {
        return lnx::session_env_stale(std::getenv("XDG_SESSION_TYPE"), logind_graphical_type(::getuid()));
    }
};
}  // namespace

std::string logind_graphical_type(uint32_t uid, const std::string& dir) {
    std::set<std::string> active, live;
    const std::string want_uid = std::to_string(uid);
    for (const auto& id : list_dir(dir)) {
        if (id.find('.') != std::string::npos) continue;  // *.ref pipes
        std::string s_uid, type, cls, state, is_active;
        for (const auto& line : split(read_small(dir + "/" + id), '\n')) {
            const auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            const auto k = line.substr(0, eq), v = line.substr(eq + 1);
            if (k == "UID") s_uid = v;
            else if (k == "TYPE") type = v;
            else if (k == "CLASS") cls = v;
            else if (k == "STATE") state = v;
            else if (k == "ACTIVE") is_active = v;
        }
        if (s_uid != want_uid || cls != "user" || state == "closing" || (type != "x11" && type != "wayland")) continue;
        live.insert(type);
        if (is_active == "1") active.insert(type);
    }
    const auto& pick = active.empty() ? live : active;
    return pick.size() == 1 ? *pick.begin() : std::string{};
}

bool session_env_stale(const char* env_type, const std::string& logind_type) {
    if (logind_type.empty()) return false;
    return !env_type || logind_type != env_type;
}

std::unique_ptr<IHelperSystem> make_helper_system() { return std::make_unique<HelperSystem>(); }

}  // namespace xg::lnx
