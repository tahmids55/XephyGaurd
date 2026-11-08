// Windows network monitor: GetAdaptersAddresses, GetExtendedTcpTable, NotifyIpInterfaceChange,
// plus Live OS DNS Cache (CNAME-aware) + TLS ClientHello SNI + Per-Socket Kernel TCP EStats.
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <tcpestats.h>
#include <tcpmib.h>
#include <windns.h>
#include <mstcpip.h>
#include <psapi.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

#include "win_common.hpp"
#include "xg/util.hpp"

namespace xg::win {
namespace {

typedef struct _DNS_CACHE_ENTRY {
    struct _DNS_CACHE_ENTRY* pNext;
    PWSTR pszName;
    WORD wType;
    WORD wDataLength;
    DWORD dwFlags;
} DNS_CACHE_ENTRY, *PDNS_CACHE_ENTRY;

typedef BOOL(WINAPI* DnsGetCacheDataTableFn)(PDNS_CACHE_ENTRY* ppEntry);

std::string addr_str(const SOCKADDR* sa) {
    char buf[INET6_ADDRSTRLEN] = {0};
    if (sa->sa_family == AF_INET)
        inet_ntop(AF_INET, &reinterpret_cast<const sockaddr_in*>(sa)->sin_addr, buf, sizeof buf);
    else if (sa->sa_family == AF_INET6)
        inet_ntop(AF_INET6, &reinterpret_cast<const sockaddr_in6*>(sa)->sin6_addr, buf, sizeof buf);
    return buf;
}

std::string ipv4_str(DWORD addr) {
    char buf[INET_ADDRSTRLEN] = {0};
    in_addr a{};
    a.s_addr = addr;
    inet_ntop(AF_INET, &a, buf, sizeof buf);
    return buf;
}

std::string ipv6_str(const UCHAR addr[16]) {
    char buf[INET6_ADDRSTRLEN] = {0};
    in6_addr a{};
    std::memcpy(&a, addr, 16);
    inet_ntop(AF_INET6, &a, buf, sizeof buf);
    return buf;
}

// Force Chromium/Firefox browsers to use the Windows OS DNS resolver instead of internal DoH.
void ensure_browser_os_dns() {
    auto set_chromium = [](const wchar_t* subkey) {
        HKEY hk = nullptr;
        if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, subkey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &hk, nullptr) == ERROR_SUCCESS) {
            DWORD zero = 0;
            RegSetValueExW(hk, L"BuiltInDnsClientEnabled", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&zero), sizeof(zero));
            const wchar_t* off = L"off";
            RegSetValueExW(hk, L"DnsOverHttpsMode", 0, REG_SZ, reinterpret_cast<const BYTE*>(off), DWORD((wcslen(off) + 1) * sizeof(wchar_t)));
            RegCloseKey(hk);
        }
    };
    set_chromium(L"SOFTWARE\\Policies\\Google\\Chrome");
    set_chromium(L"SOFTWARE\\Policies\\Microsoft\\Edge");
    set_chromium(L"SOFTWARE\\Policies\\BraveSoftware\\Brave");

    HKEY ff = nullptr;
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Mozilla\\Firefox\\DNSOverHTTPS", 0, nullptr, 0,
                        KEY_SET_VALUE, nullptr, &ff, nullptr) == ERROR_SUCCESS) {
        DWORD zero = 0, one = 1;
        RegSetValueExW(ff, L"Enabled", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&zero), sizeof(zero));
        RegSetValueExW(ff, L"Locked", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&one), sizeof(one));
        RegCloseKey(ff);
    }
}

// Returns the matched AI domain if `d` is an active AI prompt/completion/web domain (excluding telemetry).
bool is_ai_prompt_domain(const std::string& d) {
    if (d.empty()) return false;
    // Exclude background telemetry / experiment endpoints that fire when the IDE is idle
    if (d.find("telemetry") != std::string::npos || d.find("exp.individual.") != std::string::npos ||
        d.find("exp-tas.com") != std::string::npos || d.find("events.data.microsoft.com") != std::string::npos ||
        d.find("applicationinsights") != std::string::npos) {
        return false;
    }
    static const char* kAiDomains[] = {
        "api.githubcopilot.com",
        "api.individual.githubcopilot.com",
        "api.business.githubcopilot.com",
        "api.enterprise.githubcopilot.com",
        "copilot-proxy.githubusercontent.com",
        "api.jetbrains.ai",
        "grazie.ai",
        "grazie.aws.intellij.net",
        "api.anthropic.com",
        "claude.ai",
        "chatgpt.com",
        "chat.openai.com",
        "api.openai.com",
        "gemini.google.com",
        "aistudio.google.com",
        "generativelanguage.googleapis.com",
        "cloudaicompanion.googleapis.com",
        "notebooklm.google.com",
        "chat.deepseek.com",
        "api.deepseek.com",
        "deepseek.com",
        "perplexity.ai",
        "grok.com",
        "api.x.ai",
        "codeium.com",
        "windsurf.com",
        "cursor.sh",
        "tabnine.com",
        "poe.com",
        "phind.com",
        "v0.dev",
        "bolt.new",
        "lovable.dev",
        "blackbox.ai",
        "openrouter.ai",
        "groq.com",
        // Microsoft Copilot (app + web)
        "copilot.microsoft.com",
        "copilot.cloud.microsoft",
        "m365.cloud.microsoft",
        "copilot.bing.com",
        "sydney.bing.com",
        // Google Antigravity / Gemini Code Assist / Gemini web
        "cloudcode-pa.googleapis.com",
        "daily-cloudcode-pa.googleapis.com",
        "autopush-cloudcode-pa.googleapis.com",
        "antigravity.google",
        "ai.google.dev",
        "gemini.google",
        // Kimi / Moonshot
        "kimi.com",
        "kimi.ai",
        "moonshot.cn",
        "moonshot.ai",
        // Qwen / Alibaba
        "qwen.com",
        "qwen.ai",
        "qwenlm.ai",
        "tongyi.aliyun.com",
        "dashscope.aliyuncs.com",
        // ByteDance / Tencent / Zhipu / MiniMax / Baidu / others
        "doubao.com",
        "coze.com",
        "coze.cn",
        "trae.ai",
        "traeapi.us",
        "yuanbao.tencent.com",
        "hunyuan.tencent.com",
        "z.ai",
        "chatglm.cn",
        "bigmodel.cn",
        "minimax.io",
        "minimaxi.com",
        "hailuoai.com",
        "yiyan.baidu.com",
        "ernie.baidu.com",
        "baichuan-ai.com",
        "stepfun.com",
        "sensenova.cn",
        "skywork.ai",
        // Western assistants / coding agents / inference APIs
        "mistral.ai",
        "cohere.com",
        "cohere.ai",
        "x.ai",
        "meta.ai",
        "manus.im",
        "genspark.ai",
        "monica.im",
        "sider.ai",
        "felo.ai",
        "lmarena.ai",
        "venice.ai",
        "fireworks.ai",
        "together.ai",
        "together.xyz",
        "deepinfra.com",
        "cerebras.ai",
        "sambanova.ai",
        "replicate.com",
        "kiro.dev",
        "augmentcode.com",
        "supermaven.com",
        "cline.bot",
        "qodo.ai",
        "ampcode.com",
        "character.ai",
        "pplx.ai",
        "claude.com"
    };
    for (const char* pat : kAiDomains) {
        if (d == pat || iends_with(d, std::string(".") + pat)) return true;
    }
    return false;
}

// Parse TLS ClientHello SNI extension (0x0000) from a TCP payload.
std::string parse_tls_sni(const uint8_t* buf, size_t len) {
    if (len < 43) return {};
    if (buf[0] != 0x16 || buf[1] != 0x03) return {};
    size_t rec_len = (size_t(buf[3]) << 8) | buf[4];
    size_t p = 5;
    size_t end = std::min(len, p + rec_len);
    if (p + 38 > end || buf[p] != 0x01) return {};
    p += 4 + 2 + 32;  // Handshake header + version + random
    if (p >= end) return {};
    p += 1 + buf[p];  // Session ID
    if (p + 2 > end) return {};
    p += 2 + ((size_t(buf[p]) << 8) | buf[p + 1]);  // Cipher suites
    if (p + 1 > end) return {};
    p += 1 + buf[p];  // Compression methods
    if (p + 2 > end) return {};
    size_t ext_total = (size_t(buf[p]) << 8) | buf[p + 1];
    p += 2;
    size_t ext_end = std::min(end, p + ext_total);
    while (p + 4 <= ext_end) {
        uint16_t ext_type = (uint16_t(buf[p]) << 8) | buf[p + 1];
        uint16_t ext_len = (uint16_t(buf[p + 2]) << 8) | buf[p + 3];
        p += 4;
        if (ext_type == 0x0000 && p + 5 <= ext_end) {
            uint8_t name_type = buf[p + 2];
            uint16_t name_len = (uint16_t(buf[p + 3]) << 8) | buf[p + 4];
            if (name_type == 0 && name_len > 0 && name_len < 256 && p + 5 + name_len <= ext_end) {
                return to_lower(std::string(reinterpret_cast<const char*>(buf + p + 5), name_len));
            }
        }
        p += ext_len;
    }
    return {};
}

class NetworkMonitor final : public INetworkMonitor {
public:
    NetworkMonitor() {
        ensure_browser_os_dns();
        dns_lib_ = LoadLibraryW(L"dnsapi.dll");
        if (dns_lib_) {
            dns_cache_fn_ = reinterpret_cast<DnsGetCacheDataTableFn>(
                reinterpret_cast<void*>(GetProcAddress(dns_lib_, "DnsGetCacheDataTable")));
        }
        start_sni_sniffer();
    }

    ~NetworkMonitor() override {
        stop();
        if (dns_lib_) FreeLibrary(dns_lib_);
    }

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
            const auto fname = to_lower(i.name);
            if (icontains(desc, "remote ndis") || icontains(desc, "rndis") || icontains(desc, "usb ncm") ||
                icontains(desc, "cdc_ncm") || icontains(desc, "cdc ether") || icontains(desc, "mobile broadband") ||
                a->IfType == 243 || a->IfType == 244)
                i.driver = "rndis";
            else if (icontains(desc, "apple mobile device ethernet")) i.driver = "ipheth";
            else if (icontains(desc, "wintun") || icontains(desc, "wireguard") || icontains(desc, "nordlynx") ||
                     icontains(desc, "tailscale") || icontains(desc, "cloudflare") || icontains(desc, "warp") ||
                     icontains(desc, "protonvpn") || icontains(desc, "mullvad") || icontains(fname, "wireguard") ||
                     icontains(fname, "tailscale"))
                i.driver = "wintun";
            else if (icontains(desc, "tap-windows") || icontains(desc, "tap-win32") || icontains(desc, "ovpn-dco") ||
                     icontains(desc, "openvpn") || icontains(desc, "anyconnect") || icontains(desc, "zerotier") ||
                     icontains(desc, "fortissl"))
                i.driver = "tap-windows";
            else if (a->IfType == IF_TYPE_PPP || a->IfType == IF_TYPE_TUNNEL) i.driver = "tun";
            else if (icontains(desc, "usb") &&
                     (icontains(desc, "ethernet") || icontains(desc, "gigabit") || icontains(desc, "lan") ||
                      icontains(desc, "realtek") || icontains(desc, "asix")))
                i.driver = "r8152";
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
                    s.address = ipv4_str(t->table[i].dwLocalAddr);
                    s.pid = t->table[i].dwOwningPid;
                    out.push_back(s);
                }
            } else {
                auto* t = reinterpret_cast<MIB_TCP6TABLE_OWNER_PID*>(buf.data());
                for (DWORD i = 0; i < t->dwNumEntries; ++i) {
                    ListeningSocket s;
                    s.port = ntohs(u_short(t->table[i].dwLocalPort));
                    s.address = ipv6_str(t->table[i].ucLocalAddr);
                    if (s.address.empty()) s.address = "::";
                    s.pid = t->table[i].dwOwningPid;
                    out.push_back(s);
                }
            }
        };
        collect(AF_INET);
        collect(AF_INET6);
        std::map<int64_t, std::pair<std::string, uint64_t>> pid_cache;
        for (auto& s : out) {
            if (s.pid <= 4) continue;
            auto it = pid_cache.find(s.pid);
            if (it == pid_cache.end()) {
                std::string proc = path_basename(process_image(DWORD(s.pid)));
                uint64_t rss = 0;
                HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, DWORD(s.pid));
                if (h) {
                    PROCESS_MEMORY_COUNTERS pmc{};
                    if (GetProcessMemoryInfo(h, &pmc, sizeof pmc)) rss = pmc.WorkingSetSize;
                    CloseHandle(h);
                }
                it = pid_cache.emplace(s.pid, std::make_pair(std::move(proc), rss)).first;
            }
            s.process = it->second.first;
            s.rss_bytes = it->second.second;
        }
        return out;
    }

    std::vector<OutboundAiFlow> active_ai_flows() override {
        refresh_dns_cache();

        struct SocketTraffic {
            int64_t pid = 0;
            std::string remote_ip;
            uint16_t remote_port = 0;
            uint64_t sent = 0;
            uint64_t recv = 0;
        };
        std::vector<SocketTraffic> active_sockets;
        std::set<std::string> live_keys;

        // 1. IPv4 Established Connections + Kernel EStats
        DWORD sz = 0;
        GetExtendedTcpTable(nullptr, &sz, FALSE, AF_INET, TCP_TABLE_OWNER_PID_CONNECTIONS, 0);
        if (sz > 0 && sz < 16 * 1024 * 1024) {
            std::vector<BYTE> buf(sz);
            if (GetExtendedTcpTable(buf.data(), &sz, FALSE, AF_INET, TCP_TABLE_OWNER_PID_CONNECTIONS, 0) == NO_ERROR) {
                auto* tbl = reinterpret_cast<MIB_TCPTABLE_OWNER_PID*>(buf.data());
                for (DWORD i = 0; i < tbl->dwNumEntries; ++i) {
                    const auto& r = tbl->table[i];
                    if (r.dwState != MIB_TCP_STATE_ESTAB || r.dwOwningPid <= 4) continue;
                    const uint16_t rport = ntohs(u_short(r.dwRemotePort));
                    if (rport != 443) continue;
                    const std::string rip = ipv4_str(r.dwRemoteAddr);
                    if (rip.starts_with("127.") || rip.starts_with("192.168.") || rip.starts_with("10.") || rip.starts_with("0."))
                        continue;

                    const uint16_t lport = ntohs(u_short(r.dwLocalPort));
                    const std::string key = "4:" + std::to_string(lport) + "->" + rip + ":" + std::to_string(rport);
                    live_keys.insert(key);

                    MIB_TCPROW row{};
                    row.dwState = r.dwState;
                    row.dwLocalAddr = r.dwLocalAddr;
                    row.dwLocalPort = r.dwLocalPort;
                    row.dwRemoteAddr = r.dwRemoteAddr;
                    row.dwRemotePort = r.dwRemotePort;

                    TCP_ESTATS_DATA_RW_v0 rw{};
                    if (GetPerTcpConnectionEStats(&row, TcpConnectionEstatsData, reinterpret_cast<PUCHAR>(&rw), 0,
                                                  sizeof(rw), nullptr, 0, 0, nullptr, 0, 0) == NO_ERROR) {
                        bool enabled = (rw.EnableCollection != 0);
                        if (!enabled) {
                            rw.EnableCollection = TRUE;
                            enabled = (SetPerTcpConnectionEStats(&row, TcpConnectionEstatsData, reinterpret_cast<PUCHAR>(&rw), 0,
                                                                 sizeof(rw), 0) == NO_ERROR);
                        }
                        if (enabled) {
                            TCP_ESTATS_DATA_ROD_v0 rod{};
                            if (GetPerTcpConnectionEStats(&row, TcpConnectionEstatsData, nullptr, 0, 0, nullptr, 0, 0,
                                                          reinterpret_cast<PUCHAR>(&rod), 0, sizeof(rod)) == NO_ERROR) {
                                auto& prev = conn_bytes_[key];
                                if (!prev.initialized) {
                                    prev.initialized = true;
                                    prev.out = rod.DataBytesOut;
                                    prev.in = rod.DataBytesIn;
                                } else {
                                    uint64_t d_out = (rod.DataBytesOut >= prev.out) ? (rod.DataBytesOut - prev.out) : 0;
                                    uint64_t d_in = (rod.DataBytesIn >= prev.in) ? (rod.DataBytesIn - prev.in) : 0;
                                    prev.out = rod.DataBytesOut;
                                    prev.in = rod.DataBytesIn;
                                    if ((d_out > 0 || d_in > 0) && d_out < 500ull * 1024 * 1024 && d_in < 500ull * 1024 * 1024) {
                                        active_sockets.push_back({int64_t(r.dwOwningPid), rip, rport, d_out, d_in});
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // 2. IPv6 Established Connections + Kernel EStats
        sz = 0;
        GetExtendedTcpTable(nullptr, &sz, FALSE, AF_INET6, TCP_TABLE_OWNER_PID_CONNECTIONS, 0);
        if (sz > 0 && sz < 16 * 1024 * 1024) {
            std::vector<BYTE> buf(sz);
            if (GetExtendedTcpTable(buf.data(), &sz, FALSE, AF_INET6, TCP_TABLE_OWNER_PID_CONNECTIONS, 0) == NO_ERROR) {
                auto* tbl = reinterpret_cast<MIB_TCP6TABLE_OWNER_PID*>(buf.data());
                for (DWORD i = 0; i < tbl->dwNumEntries; ++i) {
                    const auto& r = tbl->table[i];
                    if (r.dwState != MIB_TCP_STATE_ESTAB || r.dwOwningPid <= 4) continue;
                    const uint16_t rport = ntohs(u_short(r.dwRemotePort));
                    if (rport != 443) continue;
                    const std::string rip = ipv6_str(r.ucRemoteAddr);
                    if (rip == "::1" || rip.starts_with("fe80")) continue;

                    const uint16_t lport = ntohs(u_short(r.dwLocalPort));
                    const std::string key = "6:" + std::to_string(lport) + "->" + rip + ":" + std::to_string(rport);
                    live_keys.insert(key);

                    MIB_TCP6ROW row{};
                    row.State = static_cast<MIB_TCP_STATE>(r.dwState);
                    std::memcpy(&row.LocalAddr, r.ucLocalAddr, 16);
                    row.dwLocalScopeId = r.dwLocalScopeId;
                    row.dwLocalPort = r.dwLocalPort;
                    std::memcpy(&row.RemoteAddr, r.ucRemoteAddr, 16);
                    row.dwRemoteScopeId = r.dwRemoteScopeId;
                    row.dwRemotePort = r.dwRemotePort;

                    TCP_ESTATS_DATA_RW_v0 rw{};
                    if (GetPerTcp6ConnectionEStats(&row, TcpConnectionEstatsData, reinterpret_cast<PUCHAR>(&rw), 0,
                                                   sizeof(rw), nullptr, 0, 0, nullptr, 0, 0) == NO_ERROR) {
                        bool enabled = (rw.EnableCollection != 0);
                        if (!enabled) {
                            rw.EnableCollection = TRUE;
                            enabled = (SetPerTcp6ConnectionEStats(&row, TcpConnectionEstatsData, reinterpret_cast<PUCHAR>(&rw), 0,
                                                                  sizeof(rw), 0) == NO_ERROR);
                        }
                        if (enabled) {
                            TCP_ESTATS_DATA_ROD_v0 rod{};
                            if (GetPerTcp6ConnectionEStats(&row, TcpConnectionEstatsData, nullptr, 0, 0, nullptr, 0, 0,
                                                           reinterpret_cast<PUCHAR>(&rod), 0, sizeof(rod)) == NO_ERROR) {
                                auto& prev = conn_bytes_[key];
                                if (!prev.initialized) {
                                    prev.initialized = true;
                                    prev.out = rod.DataBytesOut;
                                    prev.in = rod.DataBytesIn;
                                } else {
                                    uint64_t d_out = (rod.DataBytesOut >= prev.out) ? (rod.DataBytesOut - prev.out) : 0;
                                    uint64_t d_in = (rod.DataBytesIn >= prev.in) ? (rod.DataBytesIn - prev.in) : 0;
                                    prev.out = rod.DataBytesOut;
                                    prev.in = rod.DataBytesIn;
                                    if ((d_out > 0 || d_in > 0) && d_out < 500ull * 1024 * 1024 && d_in < 500ull * 1024 * 1024) {
                                        active_sockets.push_back({int64_t(r.dwOwningPid), rip, rport, d_out, d_in});
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // Prune closed sockets from conn_bytes_
        if (conn_bytes_.size() > 2048) {
            for (auto it = conn_bytes_.begin(); it != conn_bytes_.end();) {
                if (!live_keys.contains(it->first)) it = conn_bytes_.erase(it);
                else ++it;
            }
        }

        // Aggregate per (pid, domain) and filter out 28-byte idle keep-alive pings
        std::map<std::pair<int64_t, std::string>, OutboundAiFlow> grouped;
        for (const auto& s : active_sockets) {
            // Ignore tiny TCP/HTTP2 keep-alive pings (typically 24-90 bytes)
            if (s.sent < 350 && s.recv < 600) continue;

            const std::string proc = normalize_exe(process_image(DWORD(s.pid)));
            std::string matched_domain;
            {
                std::lock_guard lk(dns_mu_);
                if (auto it = ip_to_domains_.find(s.remote_ip); it != ip_to_domains_.end()) {
                    for (const auto& d : it->second) {
                        if (is_ai_prompt_domain(d)) {
                            matched_domain = d;
                            break;
                        }
                    }
                }
            }
            // Fallback for dedicated AI binaries whose keep-alive socket opened before agent start
            if (matched_domain.empty()) {
                if (proc == "copilot-runtime" || proc == "copilot-language-server" || proc == "copilot") {
                    matched_domain = "api.githubcopilot.com";
                } else if (proc == "claude") {
                    matched_domain = "api.anthropic.com";
                }
            }
            if (matched_domain.empty()) continue;

            auto& g = grouped[{s.pid, matched_domain}];
            g.pid = s.pid;
            g.process = proc;
            g.remote_ip = s.remote_ip;
            g.remote_port = s.remote_port;
            g.domain = matched_domain;
            g.bytes_sent += s.sent;
            g.bytes_recv += s.recv;
        }

        std::vector<OutboundAiFlow> result;
        for (auto& [k, v] : grouped) {
            if (v.bytes_sent >= 350 || v.bytes_recv >= 600) {
                result.push_back(std::move(v));
            }
        }
        return result;
    }

    bool watch(std::function<void()> on_change) override {
        cb_ = std::move(on_change);
        NotifyIpInterfaceChange(AF_UNSPEC, &NetworkMonitor::iface_cb, this, FALSE, &iface_handle_);
        NotifyRouteChange2(AF_UNSPEC, &NetworkMonitor::route_cb, this, FALSE, &route_handle_);
        return iface_handle_ != nullptr;
    }

    void stop() override {
        sni_stop_ = true;
        if (sni_sock_ != INVALID_SOCKET) {
            closesocket(sni_sock_);
            sni_sock_ = INVALID_SOCKET;
        }
        if (sni_thread_.joinable()) sni_thread_.join();
        if (iface_handle_) CancelMibChangeNotify2(iface_handle_);
        if (route_handle_) CancelMibChangeNotify2(route_handle_);
        iface_handle_ = route_handle_ = nullptr;
    }

private:
    struct ConnCounter {
        uint64_t out = 0;
        uint64_t in = 0;
        bool initialized = false;
    };

    void refresh_dns_cache() {
        if (!dns_cache_fn_) return;
        const int64_t now = steady_ms();
        PDNS_CACHE_ENTRY head = nullptr;
        if (!dns_cache_fn_(&head) || !head) return;

        PDNS_CACHE_ENTRY cur = head;
        while (cur) {
            if (cur->pszName && (cur->wType == DNS_TYPE_A || cur->wType == DNS_TYPE_AAAA || cur->wType == DNS_TYPE_CNAME)) {
                const std::string domain = to_lower(to_utf8(cur->pszName));
                if (is_ai_prompt_domain(domain)) {
                    auto it = last_dns_query_.find(domain + ":" + std::to_string(cur->wType));
                    if (it == last_dns_query_.end() || (now - it->second) > 15000) {
                        last_dns_query_[domain + ":" + std::to_string(cur->wType)] = now;
                        PDNS_RECORDW rec = nullptr;
                        if (DnsQuery_W(cur->pszName, cur->wType, DNS_QUERY_STANDARD, nullptr,
                                       reinterpret_cast<PDNS_RECORD*>(&rec), nullptr) == 0 && rec) {
                            std::lock_guard lk(dns_mu_);
                            for (PDNS_RECORDW r = rec; r; r = r->pNext) {
                                if (r->wType == DNS_TYPE_A) {
                                    ip_to_domains_[ipv4_str(r->Data.A.IpAddress)].insert(domain);
                                } else if (r->wType == DNS_TYPE_AAAA) {
                                    ip_to_domains_[ipv6_str(r->Data.AAAA.Ip6Address.IP6Byte)].insert(domain);
                                }
                            }
                            DnsRecordListFree(reinterpret_cast<PDNS_RECORD>(rec), DnsFreeRecordList);
                        }
                    }
                }
            }
            PDNS_CACHE_ENTRY next = cur->pNext;
            LocalFree(cur->pszName);
            LocalFree(cur);
            cur = next;
        }
        if (last_dns_query_.size() > 2048) last_dns_query_.clear();
    }

    void start_sni_sniffer() {
        sni_stop_ = false;
        sni_thread_ = std::thread([this] {
            // Find primary non-loopback IPv4 address for raw socket TLS ClientHello SNI capture
            std::string bind_ip;
            for (const auto& a : adapters()) {
                if (!a.up || !a.is_default_route) continue;
                for (const auto& addr : a.addresses) {
                    if (addr.find(':') == std::string::npos && !addr.starts_with("127.") && !addr.starts_with("169.254.")) {
                        bind_ip = addr;
                        break;
                    }
                }
                if (!bind_ip.empty()) break;
            }
            if (bind_ip.empty()) return;

            SOCKET s = socket(AF_INET, SOCK_RAW, IPPROTO_IP);
            if (s == INVALID_SOCKET) return;
            sni_sock_ = s;

            sockaddr_in sa{};
            sa.sin_family = AF_INET;
            inet_pton(AF_INET, bind_ip.c_str(), &sa.sin_addr);
            if (bind(s, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) != 0) {
                closesocket(s);
                sni_sock_ = INVALID_SOCKET;
                return;
            }
            DWORD opt = RCVALL_IPLEVEL;
            DWORD bytes = 0;
            if (WSAIoctl(s, SIO_RCVALL, &opt, sizeof(opt), nullptr, 0, &bytes, nullptr, nullptr) != 0) {
                opt = RCVALL_ON;
                if (WSAIoctl(s, SIO_RCVALL, &opt, sizeof(opt), nullptr, 0, &bytes, nullptr, nullptr) != 0) {
                    closesocket(s);
                    sni_sock_ = INVALID_SOCKET;
                    return;
                }
            }
            DWORD tv = 500;
            setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));

            std::vector<uint8_t> buf(65535);
            while (!sni_stop_) {
                int n = recv(s, reinterpret_cast<char*>(buf.data()), int(buf.size()), 0);
                if (n < 60) continue;
                size_t ihl = (buf[0] & 0x0F) * 4;
                if (buf[9] != IPPROTO_TCP || ihl + 20 > size_t(n)) continue;
                uint16_t dst_port = (uint16_t(buf[ihl + 2]) << 8) | buf[ihl + 3];
                if (dst_port != 443) continue;
                size_t tcp_hdr_len = ((buf[ihl + 12] >> 4) & 0x0F) * 4;
                size_t offset = ihl + tcp_hdr_len;
                if (offset + 43 >= size_t(n)) continue;

                std::string sni = parse_tls_sni(buf.data() + offset, size_t(n) - offset);
                if (!sni.empty() && is_ai_prompt_domain(sni)) {
                    DWORD dst_ip_raw = 0;
                    std::memcpy(&dst_ip_raw, buf.data() + 16, 4);
                    std::string dst_ip = ipv4_str(dst_ip_raw);
                    std::lock_guard lk(dns_mu_);
                    ip_to_domains_[dst_ip].insert(sni);
                }
            }
        });
    }

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
    HMODULE dns_lib_ = nullptr;
    DnsGetCacheDataTableFn dns_cache_fn_ = nullptr;
    std::mutex dns_mu_;
    std::map<std::string, std::set<std::string>> ip_to_domains_;
    std::map<std::string, int64_t> last_dns_query_;
    std::map<std::string, ConnCounter> conn_bytes_;
    std::atomic<bool> sni_stop_{false};
    SOCKET sni_sock_ = INVALID_SOCKET;
    std::thread sni_thread_;
};

}  // namespace

std::unique_ptr<INetworkMonitor> make_network_monitor() { return std::make_unique<NetworkMonitor>(); }

}  // namespace xg::win
