// Linux network monitor: adapters (/sys/class/net + getifaddrs), default routes (/proc/net/route,
// /proc/net/ipv6_route), listening sockets (/proc/net/tcp*), and rtnetlink change notifications.
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <map>
#include <set>
#include <thread>

#include "linux_common.hpp"
#include "xg/util.hpp"

namespace xg::lnx {
namespace {

std::set<std::string> default_route_ifaces() {
    std::set<std::string> out;
    for (const auto& line : split(read_small("/proc/net/route"), '\n')) {
        auto f = split(line, '\t');
        if (f.size() > 2 && f[1] == "00000000" && f[0] != "Iface") out.insert(trim(f[0]));
    }
    for (const auto& line : split(read_small("/proc/net/ipv6_route"), '\n')) {
        auto f = split(line, ' ');
        if (f.size() >= 10 && f[0] == std::string(32, '0') && f[1] == "00" && f.back() != "lo") out.insert(trim(f.back()));
    }
    return out;
}

class NetworkMonitor final : public INetworkMonitor {
public:
    ~NetworkMonitor() override { stop(); }

    std::vector<AdapterInfo> adapters() override {
        std::map<std::string, AdapterInfo> m;
        const auto routes = default_route_ifaces();
        for (const auto& name : list_dir("/sys/class/net")) {
            if (name == "lo") continue;
            const std::string base = "/sys/class/net/" + name;
            AdapterInfo a;
            a.name = name;
            a.mac = trim(read_small(base + "/address"));
            a.driver = path_basename(read_link(base + "/device/driver"));
            const auto oper = trim(read_small(base + "/operstate"));
            unsigned long flags = 0;
            try { flags = std::stoul(trim(read_small(base + "/flags")), nullptr, 16); } catch (...) {}
            a.up = oper == "up" || ((flags & IFF_UP) && oper == "unknown");
            a.is_wireless = !list_dir(base + "/wireless").empty() || !read_link(base + "/phy80211").empty();
            a.is_virtual = read_link(base).find("/virtual/") != std::string::npos;
            const auto type = trim(read_small(base + "/type"));      // 65534 = tun (none), 1 = ether, 512 = ppp
            if (a.driver.empty() && (type == "65534" || type == "512")) a.driver = "tun";
            const auto uevent = read_small(base + "/uevent");
            if (uevent.find("DEVTYPE=wireguard") != std::string::npos) a.driver = "wireguard";
            a.description = trim(read_small(base + "/device/../product"));
            a.is_default_route = routes.contains(name);
            m[name] = a;
        }
        ifaddrs* ifa = nullptr;
        if (getifaddrs(&ifa) == 0) {
            for (auto* i = ifa; i; i = i->ifa_next) {
                if (!i->ifa_addr || !m.contains(i->ifa_name)) continue;
                char buf[INET6_ADDRSTRLEN] = {0};
                if (i->ifa_addr->sa_family == AF_INET)
                    inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in*>(i->ifa_addr)->sin_addr, buf, sizeof buf);
                else if (i->ifa_addr->sa_family == AF_INET6) {
                    auto* a6 = reinterpret_cast<sockaddr_in6*>(i->ifa_addr);
                    if (IN6_IS_ADDR_LINKLOCAL(&a6->sin6_addr)) continue;
                    inet_ntop(AF_INET6, &a6->sin6_addr, buf, sizeof buf);
                } else continue;
                m[i->ifa_name].addresses.emplace_back(buf);
            }
            freeifaddrs(ifa);
        }
        std::vector<AdapterInfo> out;
        for (auto& [n, a] : m) {
            std::sort(a.addresses.begin(), a.addresses.end());
            out.push_back(std::move(a));
        }
        return out;
    }

    std::vector<ListeningSocket> listening_sockets() override {
        std::vector<ListeningSocket> out;
        std::map<std::string, size_t> by_inode;
        auto parse = [&](const std::string& file, bool v6) {
            for (const auto& line : split(read_small(file), '\n')) {
                auto f = split(line, ' ');
                if (f.size() < 10 || f[3] != "0A") continue;  // 0A = LISTEN
                auto c = f[1].find(':');
                if (c == std::string::npos) continue;
                ListeningSocket s;
                s.port = uint16_t(std::stoul(f[1].substr(c + 1), nullptr, 16));
                const auto hex = f[1].substr(0, c);
                if (!v6) {
                    uint32_t a = uint32_t(std::stoul(hex, nullptr, 16));
                    char buf[INET_ADDRSTRLEN];
                    in_addr ia{a};
                    inet_ntop(AF_INET, &ia, buf, sizeof buf);
                    s.address = buf;
                } else {
                    s.address = hex == std::string(32, '0') ? "::" : (hex == "00000000000000000000000001000000" ? "::1" : "v6");
                }
                by_inode[f[9]] = out.size();
                out.push_back(s);
            }
        };
        parse("/proc/net/tcp", false);
        parse("/proc/net/tcp6", true);
        // Resolve owners only for new inodes (fd scan is the expensive part).
        bool need = false;
        for (const auto& [ino, idx] : by_inode)
            if (auto it = owners_.find(ino); it != owners_.end()) {
                out[idx].pid = it->second.first;
                out[idx].process = it->second.second;
            } else need = true;
        if (need) {
            for (const auto& d : list_dir("/proc")) {
                if (d.empty() || !std::isdigit(static_cast<unsigned char>(d[0]))) continue;
                const std::string fdd = "/proc/" + d + "/fd";
                for (const auto& fd : list_dir(fdd)) {
                    auto l = read_link(fdd + "/" + fd);
                    if (!l.starts_with("socket:[")) continue;
                    auto ino = l.substr(8, l.size() - 9);
                    auto it = by_inode.find(ino);
                    if (it == by_inode.end()) continue;
                    const auto exe = path_basename(read_link("/proc/" + d + "/exe"));
                    owners_[ino] = {std::stoll(d), exe};
                    out[it->second].pid = std::stoll(d);
                    out[it->second].process = exe;
                }
            }
        }
        for (auto& s : out)
            if (s.pid > 0) {
                auto st = split(read_small("/proc/" + std::to_string(s.pid) + "/statm"), ' ');
                if (st.size() > 1) s.rss_bytes = std::stoull(st[1]) * uint64_t(sysconf(_SC_PAGESIZE));
            }
        if (owners_.size() > 4096) owners_.clear();
        return out;
    }

    bool watch(std::function<void()> on_change) override {
        int s = ::socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);
        if (s < 0) return false;
        sockaddr_nl sa{};
        sa.nl_family = AF_NETLINK;
        sa.nl_groups = RTMGRP_LINK | RTMGRP_IPV4_IFADDR | RTMGRP_IPV6_IFADDR | RTMGRP_IPV4_ROUTE | RTMGRP_IPV6_ROUTE;
        if (::bind(s, reinterpret_cast<sockaddr*>(&sa), sizeof sa) < 0) { ::close(s); return false; }
        fd_ = s;
        running_ = true;
        thread_ = std::thread([this, on_change] {
            char buf[8192];
            while (running_) {
                pollfd pfd{fd_, POLLIN, 0};
                if (::poll(&pfd, 1, 500) <= 0) continue;
                if (::recv(fd_, buf, sizeof buf, 0) > 0) on_change();
            }
        });
        return true;
    }

    void stop() override {
        running_ = false;
        if (thread_.joinable()) thread_.join();
        if (fd_ >= 0) ::close(fd_);
        fd_ = -1;
    }

private:
    std::map<std::string, std::pair<int64_t, std::string>> owners_;
    std::atomic<bool> running_{false};
    std::thread thread_;
    int fd_ = -1;
};

}  // namespace

std::unique_ptr<INetworkMonitor> make_network_monitor() { return std::make_unique<NetworkMonitor>(); }

}  // namespace xg::lnx
