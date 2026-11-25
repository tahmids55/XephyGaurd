// Linux network monitor: adapters (/sys/class/net + getifaddrs), default routes (/proc/net/route,
// /proc/net/ipv6_route), listening sockets (/proc/net/tcp*), NETLINK_SOCK_DIAG TCP byte-delta
// attribution + TLS ClientHello SNI tuple tracking, and rtnetlink change notifications.
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <linux/filter.h>
#include <linux/if_packet.h>
#include <linux/inet_diag.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/sock_diag.h>
#include <linux/tcp.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <unordered_map>

#include "linux_common.hpp"
#include "xg/log.hpp"
#include "xg/util.hpp"

namespace xg::lnx {
namespace {

constexpr uint32_t kTcpEstablishedMask = 1U << 1;

std::string clean_ip_str(std::string ip) {
    if (ip.rfind("::ffff:", 0) == 0) return ip.substr(7);
    return ip;
}

bool is_local_or_link_ip(const std::string& ip) {
    return ip.empty() || ip == "0.0.0.0" || ip == "::" || ip == "::1" || ip.rfind("127.", 0) == 0 ||
           ip.rfind("fe80", 0) == 0;
}

std::string diag_dst_ip(uint8_t family, const uint32_t dst[4]) {
    char buf[INET6_ADDRSTRLEN] = {0};
    if (family == AF_INET) {
        in_addr a{};
        a.s_addr = dst[0];
        if (::inet_ntop(AF_INET, &a, buf, sizeof buf)) return buf;
    } else if (family == AF_INET6) {
        in6_addr a6{};
        std::memcpy(&a6, dst, 16);
        if (::inet_ntop(AF_INET6, &a6, buf, sizeof buf)) return clean_ip_str(buf);
    }
    return {};
}

bool matches_ai_domain_list(const std::string& sni, const std::vector<std::string>& domains,
                            const std::vector<std::string>& telemetry) {
    if (sni.empty() || is_ai_telemetry_domain(sni, telemetry)) return false;
    for (const auto& pat : domains) {
        if (pat.empty()) continue;
        if (sni == pat || iends_with(sni, "." + pat)) return true;
    }
    return false;
}

std::string fallback_ai_domain_for_proc(const std::string& proc, const WebHints& hints) {
    auto it = hints.process_domains.find(normalize_exe(proc));
    return it == hints.process_domains.end() ? std::string{} : it->second;
}

std::mutex g_owners_mu;
std::unordered_map<uint64_t, std::pair<int64_t, std::string>> g_inode_owners;

void resolve_socket_inodes(const std::set<uint64_t>& needed_inodes,
                          std::unordered_map<uint64_t, std::pair<int64_t, std::string>>& resolved) {
    if (needed_inodes.empty()) return;
    bool missing = false;
    {
        std::lock_guard lk(g_owners_mu);
        for (uint64_t ino : needed_inodes) {
            if (auto it = g_inode_owners.find(ino); it != g_inode_owners.end()) {
                resolved[ino] = it->second;
            } else {
                missing = true;
            }
        }
    }
    if (!missing) return;

    std::unordered_map<uint64_t, std::pair<int64_t, std::string>> discovered;
    for (const auto& d : list_dir("/proc")) {
        if (d.empty() || !std::isdigit(static_cast<unsigned char>(d[0]))) continue;
        const int64_t pid = std::strtoll(d.c_str(), nullptr, 10);
        if (pid <= 0) continue;
        const std::string fdd = "/proc/" + d + "/fd";
        std::string exe_cached;
        bool exe_loaded = false;
        for (const auto& fd : list_dir(fdd)) {
            const auto link = read_link(fdd + "/" + fd);
            if (!link.starts_with("socket:[") || link.back() != ']') continue;
            const uint64_t ino = std::strtoull(link.substr(8, link.size() - 9).c_str(), nullptr, 10);
            if (!needed_inodes.contains(ino)) continue;
            if (!exe_loaded) {
                exe_cached = normalize_exe(read_link("/proc/" + d + "/exe"));
                if (exe_cached.empty()) exe_cached = to_lower(trim(read_small("/proc/" + d + "/comm")));
                exe_loaded = true;
            }
            discovered[ino] = {pid, exe_cached};
        }
    }

    std::lock_guard lk(g_owners_mu);
    if (g_inode_owners.size() > 8192) g_inode_owners.clear();
    for (const auto& [ino, owner] : discovered) {
        g_inode_owners[ino] = owner;
        resolved[ino] = owner;
    }
}

void dump_family_tcp(int nl_fd, uint8_t family, uint32_t seq, std::vector<TcpSocketStat>& out) {
    struct {
        nlmsghdr nlh;
        inet_diag_req_v2 req;
    } req{};
    req.nlh.nlmsg_len = sizeof(req);
    req.nlh.nlmsg_type = SOCK_DIAG_BY_FAMILY;
    req.nlh.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
    req.nlh.nlmsg_seq = seq;
    req.req.sdiag_family = family;
    req.req.sdiag_protocol = IPPROTO_TCP;
    req.req.idiag_ext = (1U << (INET_DIAG_INFO - 1));
    req.req.idiag_states = kTcpEstablishedMask;

    sockaddr_nl dst{};
    dst.nl_family = AF_NETLINK;
    iovec iov{&req, sizeof(req)};
    msghdr msg{};
    msg.msg_name = &dst;
    msg.msg_namelen = sizeof(dst);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    if (::sendmsg(nl_fd, &msg, 0) < 0) return;

    alignas(nlmsghdr) char buf[32768];
    for (;;) {
        const ssize_t n = ::recv(nl_fd, buf, sizeof(buf), 0);
        if (n <= 0) break;
        bool done = false;
        size_t rem = size_t(n);
        for (auto* h = reinterpret_cast<nlmsghdr*>(buf); NLMSG_OK(h, rem); h = NLMSG_NEXT(h, rem)) {
            if (h->nlmsg_type == NLMSG_DONE || h->nlmsg_type == NLMSG_ERROR) {
                done = true;
                break;
            }
            if (h->nlmsg_type != SOCK_DIAG_BY_FAMILY || h->nlmsg_len < NLMSG_LENGTH(sizeof(inet_diag_msg))) {
                continue;
            }
            const auto* diag = reinterpret_cast<const inet_diag_msg*>(NLMSG_DATA(h));
            const std::string rip = diag_dst_ip(diag->idiag_family, diag->id.idiag_dst);
            if (is_local_or_link_ip(rip)) continue;

            TcpSocketStat s;
            s.inode = diag->idiag_inode;
            s.local_port = ntohs(diag->id.idiag_sport);
            s.remote_port = ntohs(diag->id.idiag_dport);
            s.remote_ip = rip;

            int attr_len = int(h->nlmsg_len) - int(NLMSG_LENGTH(sizeof(inet_diag_msg)));
            const auto* rta = reinterpret_cast<const rtattr*>(reinterpret_cast<const char*>(diag) + sizeof(*diag));
            for (; RTA_OK(rta, attr_len); rta = RTA_NEXT(rta, attr_len)) {
                if (rta->rta_type == INET_DIAG_INFO &&
                    RTA_PAYLOAD(rta) >= offsetof(struct tcp_info, tcpi_bytes_received) + sizeof(uint64_t)) {
                    struct tcp_info ti{};
                    std::memcpy(&ti, RTA_DATA(rta), std::min(sizeof(ti), size_t(RTA_PAYLOAD(rta))));
                    s.bytes_sent = ti.tcpi_bytes_sent;
                    s.bytes_recv = ti.tcpi_bytes_received;
                }
            }
            if (s.inode != 0) out.push_back(std::move(s));
        }
        if (done) break;
    }
}

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

int open_tls_sni_packet_socket() {
    const int fd = ::socket(AF_PACKET, SOCK_DGRAM | SOCK_CLOEXEC, htons(ETH_P_ALL));
    if (fd < 0) return -1;

    // Attach kernel BPF passing only IPv4/IPv6 TCP frames destined to port 443.
    static const sock_filter kBpf[] = {
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, uint32_t(SKF_AD_OFF + SKF_AD_PROTOCOL)),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, ETH_P_IP, 0, 5),
        BPF_STMT(BPF_LD | BPF_B | BPF_ABS, 9),                               // ip.proto
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, IPPROTO_TCP, 0, 10),
        BPF_STMT(BPF_LDX | BPF_B | BPF_MSH, 0),                              // X = ip header len
        BPF_STMT(BPF_LD | BPF_H | BPF_IND, 2),                               // tcp.dport
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 443, 6, 7),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, ETH_P_IPV6, 0, 6),
        BPF_STMT(BPF_LD | BPF_B | BPF_ABS, 6),                               // ip6.nxt
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, IPPROTO_TCP, 0, 4),
        BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 42),                              // tcp.dport at offset 40+2
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 443, 1, 2),
        BPF_STMT(BPF_RET | BPF_K, 0),                                        // fallthrough reject guard
        BPF_STMT(BPF_RET | BPF_K, 2048),                                     // accept <=2KB ClientHello
        BPF_STMT(BPF_RET | BPF_K, 0),                                        // drop
    };
    const sock_fprog prog{uint16_t(sizeof(kBpf) / sizeof(kBpf[0])), const_cast<sock_filter*>(kBpf)};
    ::setsockopt(fd, SOL_SOCKET, SO_ATTACH_FILTER, &prog, sizeof(prog));
    return fd;
}

class NetworkMonitor final : public INetworkMonitor {
public:
    NetworkMonitor() { start_sni_sniffer(); }
    ~NetworkMonitor() override { stop(); }

    void set_ai_domains(std::vector<std::string> domains) override {
        std::lock_guard lk(domains_mu_);
        ai_domains_.clear();
        for (auto& d : domains) {
            d = to_lower(trim(d));
            if (!d.empty()) ai_domains_.push_back(std::move(d));
        }
    }

    void set_web_hints(const WebHints& hints) override {
        std::lock_guard lk(tracker_mu_);
        tracker_.hints = hints;
    }

    std::vector<OutboundAiFlow> active_ai_flows() override {
        auto sockets = query_sock_diag_tcp();
        if (sockets.empty()) return {};

        std::vector<std::string> domains;
        {
            std::lock_guard lk(domains_mu_);
            domains = ai_domains_;
        }

        std::lock_guard lk(tracker_mu_);
        return tracker_.evaluate(sockets, domains, [](std::vector<TcpSocketStat*>& unresolved) {
            std::set<uint64_t> need;
            for (const auto* s : unresolved) need.insert(s->inode);
            std::unordered_map<uint64_t, std::pair<int64_t, std::string>> owners;
            resolve_socket_inodes(need, owners);
            for (auto* s : unresolved) {
                if (auto it = owners.find(s->inode); it != owners.end()) {
                    s->pid = it->second.first;
                    s->process = it->second.second;
                }
            }
        });
    }

    std::vector<OutboundTunnel> active_tunnels() override {
        WebHints hints;
        {
            std::lock_guard lk(tracker_mu_);
            hints = tracker_.hints;
        }
        if (hints.tunnel.id.empty()) return {};
        std::vector<OutboundTunnel> out;
        std::vector<uint64_t> inodes;
        std::set<uint64_t> need;
        for (const auto& s : query_sock_diag_tcp()) {  // established sockets only
            if (!hints.is_tunnel(s.remote_ip, s.remote_port)) continue;
            out.push_back({0, {}, s.remote_ip, s.remote_port});
            need.insert(s.inode);
            inodes.push_back(s.inode);
            if (out.size() >= 64) break;  // bounded: one report per process/destination is enough
        }
        if (out.empty()) return out;
        std::unordered_map<uint64_t, std::pair<int64_t, std::string>> owners;
        resolve_socket_inodes(need, owners);
        for (size_t i = 0; i < out.size(); ++i)
            if (auto it = owners.find(inodes[i]); it != owners.end()) {
                out[i].pid = it->second.first;
                out[i].process = it->second.second;
            }
        return out;
    }

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
        std::set<uint64_t> need;
        std::vector<std::pair<uint64_t, size_t>> by_inode;
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
                const uint64_t ino = std::strtoull(f[9].c_str(), nullptr, 10);
                if (ino > 0) {
                    need.insert(ino);
                    by_inode.emplace_back(ino, out.size());
                }
                out.push_back(s);
            }
        };
        parse("/proc/net/tcp", false);
        parse("/proc/net/tcp6", true);

        std::unordered_map<uint64_t, std::pair<int64_t, std::string>> owners;
        resolve_socket_inodes(need, owners);
        for (const auto& [ino, idx] : by_inode) {
            if (auto it = owners.find(ino); it != owners.end()) {
                out[idx].pid = it->second.first;
                out[idx].process = it->second.second;
            }
        }
        for (auto& s : out)
            if (s.pid > 0) {
                auto st = split(read_small("/proc/" + std::to_string(s.pid) + "/statm"), ' ');
                if (st.size() > 1) s.rss_bytes = std::stoull(st[1]) * uint64_t(sysconf(_SC_PAGESIZE));
            }
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
        sni_running_ = false;
        if (thread_.joinable()) thread_.join();
        if (sni_thread_.joinable()) sni_thread_.join();
        if (fd_ >= 0) ::close(fd_);
        if (sni_fd_ >= 0) ::close(sni_fd_);
        fd_ = -1;
        sni_fd_ = -1;
    }

private:
    void start_sni_sniffer() {
        sni_fd_ = open_tls_sni_packet_socket();
        if (sni_fd_ < 0) {
            log::debug("network monitor: AF_PACKET SNI listener unavailable (non-root or restricted); relying on gateway + exe rules");
            return;
        }
        sni_running_ = true;
        sni_thread_ = std::thread([this] {
            uint8_t buf[2048];
            while (sni_running_) {
                pollfd pfd{sni_fd_, POLLIN, 0};
                if (::poll(&pfd, 1, 500) <= 0) continue;
                sockaddr_ll from{};
                socklen_t flen = sizeof(from);
                const ssize_t n = ::recvfrom(sni_fd_, buf, sizeof(buf), 0, reinterpret_cast<sockaddr*>(&from), &flen);
                if (n <= 40 || from.sll_pkttype != PACKET_OUTGOING) continue;

                const uint16_t eth_proto = ntohs(from.sll_protocol);
                std::string rip;
                uint16_t src_port = 0, dst_port = 0;
                size_t payload_off = 0;
                if (eth_proto == ETH_P_IP && n >= 40) {
                    const size_t ihl = size_t(buf[0] & 0x0f) * 4;
                    if ((buf[0] >> 4) != 4 || buf[9] != IPPROTO_TCP || ihl < 20 || ihl + 20 > size_t(n)) continue;
                    src_port = (uint16_t(buf[ihl]) << 8) | buf[ihl + 1];
                    dst_port = (uint16_t(buf[ihl + 2]) << 8) | buf[ihl + 3];
                    const size_t tcp_len = size_t((buf[ihl + 12] >> 4) & 0x0f) * 4;
                    if (tcp_len < 20 || ihl + tcp_len >= size_t(n)) continue;
                    payload_off = ihl + tcp_len;
                    char ipbuf[INET_ADDRSTRLEN] = {0};
                    if (::inet_ntop(AF_INET, buf + 16, ipbuf, sizeof(ipbuf))) rip = ipbuf;
                } else if (eth_proto == ETH_P_IPV6 && n >= 60) {
                    if ((buf[0] >> 4) != 6 || buf[6] != IPPROTO_TCP) continue;
                    src_port = (uint16_t(buf[40]) << 8) | buf[41];
                    dst_port = (uint16_t(buf[42]) << 8) | buf[43];
                    const size_t tcp_len = size_t((buf[40 + 12] >> 4) & 0x0f) * 4;
                    if (tcp_len < 20 || 40 + tcp_len >= size_t(n)) continue;
                    payload_off = 40 + tcp_len;
                    char ipbuf[INET6_ADDRSTRLEN] = {0};
                    if (::inet_ntop(AF_INET6, buf + 24, ipbuf, sizeof(ipbuf))) rip = clean_ip_str(ipbuf);
                } else {
                    continue;
                }
                if (dst_port != 443 || rip.empty() || size_t(n) - payload_off < 43) continue;
                const std::string sni = parse_tls_sni(buf + payload_off, size_t(n) - payload_off);
                if (!sni.empty()) {
                    std::lock_guard lk(tracker_mu_);
                    if (tracker_.tuple_sni.size() > 4096) tracker_.tuple_sni.clear();
                    tracker_.tuple_sni[tuple_key(src_port, rip, dst_port)] = sni;
                }
            }
        });
    }

    std::mutex domains_mu_;
    std::vector<std::string> ai_domains_;
    std::mutex tracker_mu_;
    SocketDeltaTracker tracker_;
    std::atomic<bool> running_{false};
    std::atomic<bool> sni_running_{false};
    std::thread thread_;
    std::thread sni_thread_;
    int fd_ = -1;
    int sni_fd_ = -1;
};

}  // namespace

std::string tuple_key(uint16_t local_port, const std::string& remote_ip, uint16_t remote_port) {
    return std::to_string(local_port) + "->" + remote_ip + ":" + std::to_string(remote_port);
}

std::vector<TcpSocketStat> query_sock_diag_tcp() {
    const int nl = ::socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_SOCK_DIAG);
    if (nl < 0) return {};
    timeval tv{0, 250000};
    ::setsockopt(nl, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    std::vector<TcpSocketStat> sockets;
    dump_family_tcp(nl, AF_INET, 1, sockets);
    dump_family_tcp(nl, AF_INET6, 2, sockets);
    ::close(nl);
    return sockets;
}

std::vector<OutboundAiFlow> SocketDeltaTracker::evaluate(
    std::vector<TcpSocketStat>& sockets,
    const std::vector<std::string>& ai_domains,
    const std::function<void(std::vector<TcpSocketStat*>&)>& resolve_owners) {
    std::set<uint64_t> live_inodes;
    struct ActiveEntry {
        TcpSocketStat* sock;
        uint64_t d_out;
        uint64_t d_in;
    };
    std::vector<ActiveEntry> candidates;
    std::vector<TcpSocketStat*> unresolved;

    for (auto& s : sockets) {
        if (s.inode == 0) continue;
        live_inodes.insert(s.inode);
        auto& prev = conn_bytes[s.inode];
        if (!prev.initialized) {
            prev.initialized = true;
            prev.out = s.bytes_sent;
            prev.in = s.bytes_recv;
            continue;
        }
        const uint64_t d_out = (s.bytes_sent >= prev.out) ? (s.bytes_sent - prev.out) : 0;
        const uint64_t d_in = (s.bytes_recv >= prev.in) ? (s.bytes_recv - prev.in) : 0;
        prev.out = s.bytes_sent;
        prev.in = s.bytes_recv;
        if (d_out >= 500ULL * 1024 * 1024 || d_in >= 500ULL * 1024 * 1024) continue;
        if (d_out < 350 && d_in < 600) continue;  // suppress idle HTTP/2 & TLS keep-alive pings

        candidates.push_back({&s, d_out, d_in});
        if (s.pid == 0 && s.process.empty()) unresolved.push_back(&s);
    }

    if (conn_bytes.size() > 2048) {
        for (auto it = conn_bytes.begin(); it != conn_bytes.end();) {
            if (!live_inodes.contains(it->first)) it = conn_bytes.erase(it);
            else ++it;
        }
    }

    if (!unresolved.empty() && resolve_owners) resolve_owners(unresolved);

    std::map<std::pair<int64_t, std::string>, OutboundAiFlow> grouped;
    for (const auto& c : candidates) {
        const TcpSocketStat& s = *c.sock;
        const std::string key = tuple_key(s.local_port, s.remote_ip, s.remote_port);
        std::string matched_domain;

        // Exact 4-tuple SNI match first: if SNI is known non-AI (e.g. Cloudflare CDN blog), do NOT guess by IP.
        if (auto sit = tuple_sni.find(key); sit != tuple_sni.end()) {
            if (matches_ai_domain_list(sit->second, ai_domains, hints.telemetry_domains)) {
                matched_domain = sit->second;
            } else {
                continue;
            }
        } else {
            matched_domain = fallback_ai_domain_for_proc(s.process, hints);
        }

        if (matched_domain.empty()) continue;

        auto& g = grouped[{s.pid, matched_domain}];
        g.pid = s.pid;
        g.process = s.process;
        g.remote_ip = s.remote_ip;
        g.remote_port = s.remote_port;
        g.domain = matched_domain;
        g.bytes_sent += c.d_out;
        g.bytes_recv += c.d_in;
    }

    std::vector<OutboundAiFlow> out;
    for (auto& [k, v] : grouped) {
        if (v.bytes_sent >= 350 || v.bytes_recv >= 600) out.push_back(std::move(v));
    }
    return out;
}

std::unordered_map<int64_t, PidNetStats> sample_pid_net_stats() {
    static std::mutex mu;
    static std::unordered_map<uint64_t, uint64_t> prev_bytes;
    static int64_t last_ms = 0;

    auto sockets = query_sock_diag_tcp();
    std::set<uint64_t> needed;
    for (const auto& s : sockets) {
        if (s.inode > 0) needed.insert(s.inode);
    }
    std::unordered_map<uint64_t, std::pair<int64_t, std::string>> owners;
    resolve_socket_inodes(needed, owners);

    std::lock_guard lk(mu);
    const int64_t now = steady_ms();
    const int64_t dt_ms = (last_ms > 0 && now > last_ms) ? (now - last_ms) : 0;
    last_ms = now;

    std::unordered_map<uint64_t, uint64_t> next_bytes;
    std::unordered_map<int64_t, std::pair<uint64_t, uint32_t>> agg;  // pid -> {delta_bytes, conns}
    for (const auto& s : sockets) {
        const uint64_t total = s.bytes_sent + s.bytes_recv;
        next_bytes[s.inode] = total;
        auto oit = owners.find(s.inode);
        if (oit == owners.end() || oit->second.first <= 0) continue;
        const int64_t pid = oit->second.first;
        auto& a = agg[pid];
        a.second += 1;
        if (dt_ms > 0) {
            if (auto pit = prev_bytes.find(s.inode); pit != prev_bytes.end() && total >= pit->second) {
                const uint64_t d = total - pit->second;
                if (d < 500ULL * 1024 * 1024) a.first += d;
            }
        }
    }
    prev_bytes.swap(next_bytes);

    std::unordered_map<int64_t, PidNetStats> out;
    for (const auto& [pid, a] : agg) {
        PidNetStats st;
        st.net_conns = a.second;
        if (dt_ms > 0 && a.first > 0) {
            const double kbps = (double(a.first) / 1024.0) / (double(dt_ms) / 1000.0);
            st.net_kbps = std::round(kbps * 10.0) / 10.0;
        }
        out.emplace(pid, st);
    }
    return out;
}

std::unique_ptr<INetworkMonitor> make_network_monitor() { return std::make_unique<NetworkMonitor>(); }

}  // namespace xg::lnx
