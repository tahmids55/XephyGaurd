// Windows network monitor: GetAdaptersAddresses, GetExtendedTcpTable, NotifyIpInterfaceChange.
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <psapi.h>

#include <algorithm>
#include <atomic>
#include <functional>
#include <map>
#include <vector>

#include "win_common.hpp"
#include "xg/util.hpp"

namespace xg::win {
namespace {

std::string addr_str(const SOCKADDR* sa) {
    char buf[INET6_ADDRSTRLEN] = {0};
    if (sa->sa_family == AF_INET)
        inet_ntop(AF_INET, &reinterpret_cast<const sockaddr_in*>(sa)->sin_addr, buf, sizeof buf);
    else if (sa->sa_family == AF_INET6)
        inet_ntop(AF_INET6, &reinterpret_cast<const sockaddr_in6*>(sa)->sin6_addr, buf, sizeof buf);
    return buf;
}

class NetworkMonitor final : public INetworkMonitor {
public:
    ~NetworkMonitor() override { stop(); }

    std::vector<AdapterInfo> adapters() override {
        ULONG len = 32 * 1024;
        std::vector<BYTE> buf(len);
        const ULONG flags = GAA_FLAG_INCLUDE_GATEWAYS | GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
        auto* aa = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data());
        ULONG r = GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, aa, &len);
        if (r == ERROR_BUFFER_OVERFLOW) {
            buf.resize(len);
            aa = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data());
            r = GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, aa, &len);
        }
        std::vector<AdapterInfo> out;
        if (r != NO_ERROR) return out;
        for (auto* a = aa; a; a = a->Next) {
            if (a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
            AdapterInfo i;
            i.name = to_utf8(a->FriendlyName);
            i.description = to_utf8(a->Description);
            const auto desc = to_lower(i.description);
            // Map the description to a driver-like token so the shared adapter rules apply.
            if (icontains(desc, "remote ndis")) i.driver = "rndis";
            else if (icontains(desc, "apple mobile device ethernet")) i.driver = "ipheth";
            else if (icontains(desc, "wintun") || icontains(desc, "wireguard")) i.driver = "wintun";
            else if (icontains(desc, "tap-windows") || icontains(desc, "tap-win32")) i.driver = "tap-windows";
            else if (a->IfType == IF_TYPE_PPP || a->IfType == IF_TYPE_TUNNEL) i.driver = "tun";
            else if (icontains(desc, "usb") && icontains(desc, "ethernet")) i.driver = "r8152";
            for (ULONG k = 0; k < a->PhysicalAddressLength; ++k) {
                char hx[4];
                snprintf(hx, sizeof hx, k ? ":%02x" : "%02x", a->PhysicalAddress[k]);
                i.mac += hx;
            }
            for (auto* u = a->FirstUnicastAddress; u; u = u->Next) {
                auto s = addr_str(u->Address.lpSockaddr);
                if (!s.starts_with("fe80")) i.addresses.push_back(s);
            }
            std::sort(i.addresses.begin(), i.addresses.end());
            i.up = a->OperStatus == IfOperStatusUp;
            i.is_default_route = i.up && a->FirstGatewayAddress != nullptr;
            i.is_wireless = a->IfType == IF_TYPE_IEEE80211;
            i.is_virtual = icontains(desc, "virtual") || icontains(desc, "hyper-v") || icontains(desc, "vmware") ||
                           icontains(desc, "virtualbox");
            out.push_back(std::move(i));
        }
        return out;
    }

    std::vector<ListeningSocket> listening_sockets() override {
        std::vector<ListeningSocket> out;
        auto collect = [&](ULONG af) {
            DWORD size = 0;
            GetExtendedTcpTable(nullptr, &size, FALSE, af, TCP_TABLE_OWNER_PID_LISTENER, 0);
            std::vector<BYTE> buf(size + 1024);
            if (GetExtendedTcpTable(buf.data(), &size, FALSE, af, TCP_TABLE_OWNER_PID_LISTENER, 0) != NO_ERROR) return;
            if (af == AF_INET) {
                auto* t = reinterpret_cast<MIB_TCPTABLE_OWNER_PID*>(buf.data());
                for (DWORD i = 0; i < t->dwNumEntries; ++i) {
                    ListeningSocket s;
                    s.port = ntohs(u_short(t->table[i].dwLocalPort));
                    in_addr a{};
                    a.s_addr = t->table[i].dwLocalAddr;
                    char b[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &a, b, sizeof b);
                    s.address = b;
                    s.pid = t->table[i].dwOwningPid;
                    out.push_back(s);
                }
            } else {
                auto* t = reinterpret_cast<MIB_TCP6TABLE_OWNER_PID*>(buf.data());
                for (DWORD i = 0; i < t->dwNumEntries; ++i) {
                    ListeningSocket s;
                    s.port = ntohs(u_short(t->table[i].dwLocalPort));
                    s.address = "::";
                    s.pid = t->table[i].dwOwningPid;
                    out.push_back(s);
                }
            }
        };
        collect(AF_INET);
        collect(AF_INET6);
        for (auto& s : out) {
            s.process = path_basename(process_image(DWORD(s.pid)));
            HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, DWORD(s.pid));
            if (h) {
                PROCESS_MEMORY_COUNTERS pmc{};
                if (GetProcessMemoryInfo(h, &pmc, sizeof pmc)) s.rss_bytes = pmc.WorkingSetSize;
                CloseHandle(h);
            }
        }
        return out;
    }

    bool watch(std::function<void()> on_change) override {
        cb_ = std::move(on_change);
        NotifyIpInterfaceChange(AF_UNSPEC, &NetworkMonitor::iface_cb, this, FALSE, &iface_handle_);
        NotifyRouteChange2(AF_UNSPEC, &NetworkMonitor::route_cb, this, FALSE, &route_handle_);
        return iface_handle_ != nullptr;
    }

    void stop() override {
        if (iface_handle_) CancelMibChangeNotify2(iface_handle_);
        if (route_handle_) CancelMibChangeNotify2(route_handle_);
        iface_handle_ = route_handle_ = nullptr;
    }

private:
    static void WINAPI iface_cb(PVOID ctx, PMIB_IPINTERFACE_ROW, MIB_NOTIFICATION_TYPE) {
        auto* self = static_cast<NetworkMonitor*>(ctx);
        if (self->cb_) self->cb_();
    }
    static void WINAPI route_cb(PVOID ctx, PMIB_IPFORWARD_ROW2, MIB_NOTIFICATION_TYPE) {
        auto* self = static_cast<NetworkMonitor*>(ctx);
        if (self->cb_) self->cb_();
    }
    std::function<void()> cb_;
    HANDLE iface_handle_ = nullptr;
    HANDLE route_handle_ = nullptr;
};

}  // namespace

std::unique_ptr<INetworkMonitor> make_network_monitor() { return std::make_unique<NetworkMonitor>(); }

}  // namespace xg::win
