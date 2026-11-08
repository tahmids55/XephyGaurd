// Windows network isolation via Windows Defender Firewall (netsh advfirewall).
//
// One outbound BLOCK rule covering every public IPv4 range (+ IPv6 global unicast). Private, link-local and
// multicast ranges are simply not part of the rule, so the LAN keeps working; the CCC is carved out of the
// blocked ranges (block rules beat allow rules in Windows Firewall, so an allow rule would not be enough).
// Windows then reports "No Internet" (globe icon) because its connectivity probe is blocked.
#include <winsock2.h>
#include <ws2tcpip.h>

#include <map>
#include <mutex>
#include <set>

#include "win_common.hpp"
#include "xg/isolation.hpp"
#include "xg/log.hpp"

namespace xg::win {
namespace {

constexpr const char* kRuleName = "XephyGuard Isolate";

// Runs `netsh <args>` without a console window; returns the exit code (-1 if it could not be started).
int run_netsh(const std::string& args) {
    wchar_t sysdir[MAX_PATH] = {0};
    GetSystemDirectoryW(sysdir, MAX_PATH);
    std::wstring cmd = L"\"" + std::wstring(sysdir) + L"\\netsh.exe\" " + to_wide(args);
    STARTUPINFOW si{};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
        return -1;
    WaitForSingleObject(pi.hProcess, 30000);
    DWORD code = DWORD(-1);
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return int(code);
}

class NetworkIsolator final : public INetworkIsolator {
public:
    bool isolate(const std::vector<std::string>& exempt_hosts) override {
        std::lock_guard lk(mu_);
        // Resolve now (before the block exists). Resolution may fail later (DNS is blocked), so remember results.
        for (const auto& h : exempt_hosts) {
            auto ips = resolve(h);
            if (!ips.empty()) known_ips_[h] = std::move(ips);
        }
        std::set<std::string> exempt;
        for (const auto& h : exempt_hosts)
            if (auto it = known_ips_.find(h); it != known_ips_.end()) exempt.insert(it->second.begin(), it->second.end());

        std::string remote;
        for (const auto& r : isolation::blocked_ipv4_ranges({exempt.begin(), exempt.end()})) {
            if (!remote.empty()) remote += ",";
            remote += r;
        }
        remote += std::string(",") + isolation::blocked_ipv6_prefix();

        run_netsh("advfirewall set allprofiles state on");  // a student cannot switch the firewall off (not admin)
        run_netsh(std::string("advfirewall firewall delete rule name=\"") + kRuleName + "\"");
        const int rc = run_netsh(std::string("advfirewall firewall add rule name=\"") + kRuleName +
                                 "\" dir=out action=block enable=yes profile=any protocol=any remoteip=" + remote);
        if (rc != 0) log::error("isolate: netsh add rule failed (exit {})", rc);
        else log::info("isolate: outbound Internet blocked (LAN + {} exempt address(es) still reachable)", exempt.size());
        return rc == 0;
    }

    bool restore() override {
        std::lock_guard lk(mu_);
        // delete returns 1 when the rule is already gone: that is fine.
        run_netsh(std::string("advfirewall firewall delete rule name=\"") + kRuleName + "\"");
        log::info("isolate: outbound Internet restored");
        return true;
    }

    bool active() override {
        std::lock_guard lk(mu_);
        return run_netsh(std::string("advfirewall firewall show rule name=\"") + kRuleName + "\"") == 0;
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

}  // namespace xg::win
