// Linux process monitor: /proc scan (cached per pid) + netlink proc connector for exec events.
#include <linux/cn_proc.h>
#include <linux/connector.h>
#include <linux/netlink.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <thread>
#include <unordered_map>

#include "linux_common.hpp"
#include "xg/log.hpp"
#include "xg/util.hpp"

namespace xg::lnx {
namespace {

uint64_t read_total_cpu_ticks() {
    const std::string s = read_small("/proc/stat");
    auto nl = s.find('\n');
    const std::string line = nl == std::string::npos ? s : s.substr(0, nl);
    uint64_t total = 0;
    for (const auto& f : split(line, ' ')) {
        if (f.empty() || f == "cpu") continue;
        total += std::strtoull(f.c_str(), nullptr, 10);
    }
    return total;
}

bool read_proc(int64_t pid, ProcessInfo& p, bool full, uint64_t* out_cpu_ticks = nullptr) {
    const std::string base = "/proc/" + std::to_string(pid);
    const std::string stat = read_small(base + "/stat");
    if (stat.empty()) return false;
    // pid (comm) state ppid ... field 22 = starttime ; comm may contain spaces/parens.
    auto l = stat.find('('), r = stat.rfind(')');
    if (l == std::string::npos || r == std::string::npos) return false;
    p.pid = pid;
    p.name = stat.substr(l + 1, r - l - 1);
    auto fields = split(stat.substr(r + 2), ' ');
    if (fields.size() > 21) {
        p.ppid = std::strtoll(fields[1].c_str(), nullptr, 10);
        if (out_cpu_ticks) {
            *out_cpu_ticks = std::strtoull(fields[11].c_str(), nullptr, 10) +
                             std::strtoull(fields[12].c_str(), nullptr, 10);
        }
        p.start_time = std::strtoll(fields[19].c_str(), nullptr, 10);
        const long long rss_pages = std::strtoll(fields[21].c_str(), nullptr, 10);
        if (rss_pages > 0) p.rss_bytes = uint64_t(rss_pages) * uint64_t(sysconf(_SC_PAGESIZE));
    }
    if (!full) return true;
    p.exe_path = read_link(base + "/exe");
    if (p.exe_path.ends_with(" (deleted)")) p.exe_path.resize(p.exe_path.size() - 10);
    std::string cmd = read_small(base + "/cmdline");
    if (cmd.size() > 4096) cmd.resize(4096);
    for (auto& c : cmd) if (c == '\0') c = ' ';
    p.cmdline = trim(cmd);
    p.user = user_name(file_uid(base));
    return true;
}

class ProcessMonitor final : public IProcessMonitor {
public:
    ~ProcessMonitor() override { stop(); }

    std::vector<ProcessInfo> snapshot() override {
        std::vector<ProcessInfo> out;
        std::unordered_map<int64_t, ProcessInfo> next;
        std::unordered_map<int64_t, uint64_t> next_cpu;
        const uint64_t sys_ticks = read_total_cpu_ticks();
        const uint64_t delta_sys = (last_sys_ticks_ > 0 && sys_ticks > last_sys_ticks_) ? (sys_ticks - last_sys_ticks_) : 0;
        last_sys_ticks_ = sys_ticks;

        for (const auto& d : list_dir("/proc")) {
            if (d.empty() || !std::isdigit(static_cast<unsigned char>(d[0]))) continue;
            const int64_t pid = std::strtoll(d.c_str(), nullptr, 10);
            ProcessInfo light;
            uint64_t cpu_ticks = 0;
            if (!read_proc(pid, light, false, &cpu_ticks)) continue;
            double cpu_pct = 0.0;
            if (auto cit = cpu_ticks_.find(pid); cit != cpu_ticks_.end() && delta_sys > 0 && cpu_ticks >= cit->second) {
                cpu_pct = std::clamp(std::round((double(cpu_ticks - cit->second) / double(delta_sys)) * 1000.0) / 10.0, 0.0, 100.0);
            }
            next_cpu[pid] = cpu_ticks;

            auto it = cache_.find(pid);
            if (it != cache_.end() && it->second.start_time == light.start_time) {
                it->second.rss_bytes = light.rss_bytes;
                it->second.cpu_pct = cpu_pct;
                next.emplace(pid, it->second);
            } else {
                ProcessInfo full;
                if (!read_proc(pid, full, true)) continue;
                full.cpu_pct = cpu_pct;
                next.emplace(pid, std::move(full));
            }
        }
        cache_.swap(next);
        cpu_ticks_.swap(next_cpu);
        out.reserve(cache_.size());
        for (const auto& [pid, p] : cache_) out.push_back(p);
        return out;
    }

    bool watch(std::function<void(const ProcessInfo&)> on_exec) override {
        int s = ::socket(PF_NETLINK, SOCK_DGRAM | SOCK_CLOEXEC, NETLINK_CONNECTOR);
        if (s < 0) return false;
        sockaddr_nl sa{};
        sa.nl_family = AF_NETLINK;
        sa.nl_groups = CN_IDX_PROC;
        sa.nl_pid = 0;
        if (::bind(s, reinterpret_cast<sockaddr*>(&sa), sizeof sa) < 0) { ::close(s); return false; }
        // nlmsghdr | cn_msg | proc_cn_mcast_op, laid out by hand (cn_msg ends in a flexible array).
        alignas(nlmsghdr) char req[NLMSG_SPACE(sizeof(cn_msg) + sizeof(proc_cn_mcast_op))] = {};
        auto* hdr = reinterpret_cast<nlmsghdr*>(req);
        hdr->nlmsg_len = NLMSG_LENGTH(sizeof(cn_msg) + sizeof(proc_cn_mcast_op));
        hdr->nlmsg_pid = uint32_t(getpid());
        hdr->nlmsg_type = NLMSG_DONE;
        auto* msg = reinterpret_cast<cn_msg*>(NLMSG_DATA(hdr));
        msg->id.idx = CN_IDX_PROC;
        msg->id.val = CN_VAL_PROC;
        msg->len = sizeof(proc_cn_mcast_op);
        const proc_cn_mcast_op op = PROC_CN_MCAST_LISTEN;
        std::memcpy(msg->data, &op, sizeof op);
        if (::send(s, req, hdr->nlmsg_len, 0) < 0) { ::close(s); return false; }
        fd_ = s;
        running_ = true;
        thread_ = std::thread([this, on_exec] {
            alignas(nlmsghdr) char buf[4096];
            while (running_) {
                pollfd pfd{fd_, POLLIN, 0};
                if (::poll(&pfd, 1, 500) <= 0) continue;
                ssize_t n = ::recv(fd_, buf, sizeof buf, 0);
                if (n <= 0) continue;
                for (auto* h = reinterpret_cast<nlmsghdr*>(buf); NLMSG_OK(h, size_t(n)); h = NLMSG_NEXT(h, n)) {
                    auto* cn = reinterpret_cast<cn_msg*>(NLMSG_DATA(h));
                    auto* ev = reinterpret_cast<proc_event*>(cn->data);
                    if (ev->what != PROC_EVENT_EXEC) continue;
                    ProcessInfo p;
                    if (read_proc(ev->event_data.exec.process_tgid, p, true)) on_exec(p);
                }
            }
        });
        log::info("process monitor: netlink proc connector active");
        return true;
    }

    bool kill(int64_t pid) override {
        if (pid <= 1) return false;
        ProcessInfo target;
        const bool had_info = read_proc(pid, target, false);
        if (::kill(pid_t(pid), SIGTERM) != 0) return false;
        // Escalate if the same process (matching start_time) ignores SIGTERM.
        const int64_t expected_start = target.start_time;
        std::thread([pid, had_info, expected_start] {
            std::this_thread::sleep_for(std::chrono::milliseconds(1500));
            ProcessInfo check;
            if (!read_proc(pid, check, false)) return;
            if (had_info && expected_start != 0 && check.start_time != expected_start) return;
            ::kill(pid_t(pid), SIGKILL);
        }).detach();
        return true;
    }

    void stop() override {
        running_ = false;
        if (thread_.joinable()) thread_.join();
        if (fd_ >= 0) ::close(fd_);
        fd_ = -1;
    }

private:
    std::unordered_map<int64_t, ProcessInfo> cache_;
    std::unordered_map<int64_t, uint64_t> cpu_ticks_;
    uint64_t last_sys_ticks_ = 0;
    std::atomic<bool> running_{false};
    std::thread thread_;
    int fd_ = -1;
};

}  // namespace

std::unique_ptr<IProcessMonitor> make_process_monitor() { return std::make_unique<ProcessMonitor>(); }

}  // namespace xg::lnx
