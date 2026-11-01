#include <sys/utsname.h>
#include <unistd.h>

#include <algorithm>

#include "linux_common.hpp"
#include "xg/util.hpp"

namespace xg::lnx {
namespace {

class SystemInfo final : public ISystemInfo {
public:
    std::string hostname() override {
        char buf[256] = {0};
        ::gethostname(buf, sizeof buf - 1);
        return buf;
    }
    std::string os_name() override {
        auto v = os_release("NAME");
        return v.empty() ? "Linux" : v;
    }
    std::string os_version() override {
        utsname u{};
        ::uname(&u);
        return os_release("VERSION_ID") + " (kernel " + u.release + ")";
    }
    std::vector<std::string> mac_addresses() override {
        std::vector<std::string> out;
        for (const auto& n : list_dir("/sys/class/net")) {
            if (n == "lo") continue;
            if (read_link("/sys/class/net/" + n).find("/virtual/") != std::string::npos) continue;
            auto mac = trim(read_small("/sys/class/net/" + n + "/address"));
            if (!mac.empty() && mac != "00:00:00:00:00:00") out.push_back(mac);
        }
        std::sort(out.begin(), out.end());
        return out;
    }
    std::vector<UserSession> sessions() override {
        // systemd-logind state files: /run/systemd/sessions/<id>  (KEY=VALUE lines)
        std::vector<UserSession> out;
        for (const auto& id : list_dir("/run/systemd/sessions")) {
            if (id.find('.') != std::string::npos) continue;  // *.ref files
            std::string user, type, display, active, cls, state, uid;
            for (const auto& line : split(read_small("/run/systemd/sessions/" + id), '\n')) {
                auto eq = line.find('=');
                if (eq == std::string::npos) continue;
                auto k = line.substr(0, eq), v = line.substr(eq + 1);
                if (k == "USER") user = v;
                else if (k == "UID") uid = v;
                else if (k == "TYPE") type = v;
                else if (k == "DISPLAY") display = v;
                else if (k == "ACTIVE") active = v;
                else if (k == "CLASS") cls = v;
                else if (k == "STATE") state = v;
            }
            if (cls != "user" || user.empty() || state == "closing") continue;
            UserSession s;
            s.user = user;
            s.uid = uint32_t(std::stoul(uid.empty() ? "0" : uid));
            s.home = user_home(user);
            s.session_type = type.empty() ? "tty" : type;
            s.display = display;
            s.active = active == "1";
            out.push_back(std::move(s));
        }
        return out;
    }
    int64_t uptime_s() override {
        auto s = read_small("/proc/uptime");
        return s.empty() ? 0 : int64_t(std::stod(s));
    }
    std::string self_exe_path() override { return read_link("/proc/self/exe"); }
    SelfUsage self_usage() override {
        SelfUsage u;
        auto statm = split(read_small("/proc/self/statm"), ' ');
        if (statm.size() > 1) u.rss_bytes = std::stoull(statm[1]) * uint64_t(sysconf(_SC_PAGESIZE));
        auto stat = read_small("/proc/self/stat");
        auto r = stat.rfind(')');
        if (r != std::string::npos) {
            auto f = split(stat.substr(r + 2), ' ');
            if (f.size() > 12) {
                const long tck = sysconf(_SC_CLK_TCK);
                u.cpu_time_ms = (std::stoull(f[11]) + std::stoull(f[12])) * 1000ULL / uint64_t(tck);
            }
        }
        return u;
    }

private:
    static std::string os_release(const std::string& key) {
        for (const auto& line : split(read_small("/etc/os-release"), '\n'))
            if (line.rfind(key + "=", 0) == 0) {
                auto v = line.substr(key.size() + 1);
                if (v.size() >= 2 && v.front() == '"') v = v.substr(1, v.size() - 2);
                return v;
            }
        return {};
    }
};

}  // namespace

std::unique_ptr<ISystemInfo> make_system_info() { return std::make_unique<SystemInfo>(); }

}  // namespace xg::lnx
