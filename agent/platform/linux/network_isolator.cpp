// Linux network isolation via nftables: a dedicated table that drops outbound traffic to public addresses.
// LAN / link-local / multicast stay reachable and the CCC is carved out of the blocked ranges.
// NetworkManager then shows its "no connectivity" (?) icon because its connectivity probe is dropped.
#include <netdb.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <arpa/inet.h>

#include <cstdio>
#include <map>
#include <mutex>
#include <set>

#include "linux_common.hpp"
#include "xg/isolation.hpp"
#include "xg/log.hpp"

namespace xg::lnx {
namespace {

constexpr const char* kTable = "xephyguard_isolate";

int run(const std::string& cmd, const std::string& stdin_text = {}) {
    const std::string full = cmd + " >/dev/null 2>&1";
    FILE* p = popen(full.c_str(), stdin_text.empty() ? "r" : "w");
    if (!p) return -1;
    if (!stdin_text.empty()) fwrite(stdin_text.data(), 1, stdin_text.size(), p);
    const int st = pclose(p);
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

class NetworkIsolator final : public INetworkIsolator {
public:
    bool isolate(const std::vector<std::string>& exempt_hosts) override {
        std::lock_guard lk(mu_);
        for (const auto& h : exempt_hosts) {
            auto ips = resolve(h);
            if (!ips.empty()) known_ips_[h] = std::move(ips);
        }
        std::set<std::string> exempt;
        for (const auto& h : exempt_hosts)
            if (auto it = known_ips_.find(h); it != known_ips_.end()) exempt.insert(it->second.begin(), it->second.end());

        std::string v4;
        for (const auto& r : isolation::blocked_ipv4_ranges({exempt.begin(), exempt.end()})) {
            if (!v4.empty()) v4 += ", ";
            v4 += r;
        }
        std::string rules = std::string("table inet ") + kTable + " {\n  chain out {\n    type filter hook output priority 0; policy accept;\n" +
                            "    ip daddr { " + v4 + " } drop\n    ip6 daddr " + isolation::blocked_ipv6_prefix() + " drop\n  }\n}\n";
        run(std::string("nft delete table inet ") + kTable);
        const int rc = run("nft -f -", rules);
        if (rc != 0) log::error("isolate: nft failed (exit {})", rc);
        else log::info("isolate: outbound Internet blocked (LAN + {} exempt address(es) still reachable)", exempt.size());
        return rc == 0;
    }

    bool restore() override {
        std::lock_guard lk(mu_);
        run(std::string("nft delete table inet ") + kTable);
        log::info("isolate: outbound Internet restored");
        return true;
    }

    bool active() override {
        std::lock_guard lk(mu_);
        return run(std::string("nft list table inet ") + kTable) == 0;
    }

private:
    static std::vector<std::string> resolve(const std::string& host) {
        std::vector<std::string> out;
        if (host.empty()) return out;
        addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo* res = nullptr;
        if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || !res) return out;
        for (auto* p = res; p; p = p->ai_next) {
            char buf[INET_ADDRSTRLEN] = {0};
            inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in*>(p->ai_addr)->sin_addr, buf, sizeof buf);
            out.emplace_back(buf);
        }
        freeaddrinfo(res);
        return out;
    }

    std::mutex mu_;
    std::map<std::string, std::vector<std::string>> known_ips_;
};

}  // namespace

std::unique_ptr<INetworkIsolator> make_network_isolator() { return std::make_unique<NetworkIsolator>(); }

}  // namespace xg::lnx
