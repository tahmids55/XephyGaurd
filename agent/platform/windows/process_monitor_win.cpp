// Windows process monitor: ToolHelp snapshots (cached per pid, single OpenProcess handle per scan),
// CPU % via GetProcessTimes/GetSystemTimes deltas, per-process network KB/s via TCP EStats + I/O counters,
// command lines through NtQueryInformationProcess(ProcessCommandLineInformation), and
// TerminateProcess (including child processes) for the kill action.
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <tcpestats.h>
#include <tcpmib.h>
#include "win_common.hpp"
#include <psapi.h>
#include <sddl.h>
#include <tlhelp32.h>
#include <winternl.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <vector>

#include "xg/util.hpp"

namespace xg::win {
namespace {

using NtQIP = LONG(NTAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG);

uint64_t ft_to_u64(const FILETIME& ft) {
    return (uint64_t(ft.dwHighDateTime) << 32) | uint64_t(ft.dwLowDateTime);
}

struct NetPidSample {
    uint32_t conns = 0;
    uint64_t estats_bytes = 0;
};

// Collect active TCP connection counts and cumulative TCP DataBytesIn+DataBytesOut per PID.
std::unordered_map<int64_t, NetPidSample> sample_tcp_by_pid() {
    std::unordered_map<int64_t, NetPidSample> out;
    // IPv4 TCP connections
    DWORD sz = 0;
    GetExtendedTcpTable(nullptr, &sz, FALSE, AF_INET, TCP_TABLE_OWNER_PID_CONNECTIONS, 0);
    if (sz > 0 && sz < 16 * 1024 * 1024) {
        std::vector<BYTE> buf(sz);
        if (GetExtendedTcpTable(buf.data(), &sz, FALSE, AF_INET, TCP_TABLE_OWNER_PID_CONNECTIONS, 0) == NO_ERROR) {
            auto* tbl = reinterpret_cast<MIB_TCPTABLE_OWNER_PID*>(buf.data());
            for (DWORD i = 0; i < tbl->dwNumEntries; ++i) {
                const auto& r = tbl->table[i];
                if (r.dwOwningPid <= 4) continue;
                auto& s = out[int64_t(r.dwOwningPid)];
                if (r.dwState == MIB_TCP_STATE_ESTAB || r.dwState == MIB_TCP_STATE_CLOSE_WAIT ||
                    r.dwState == MIB_TCP_STATE_FIN_WAIT1 || r.dwState == MIB_TCP_STATE_FIN_WAIT2) {
                    ++s.conns;
                }
                MIB_TCPROW row{};
                row.dwState = r.dwState;
                row.dwLocalAddr = r.dwLocalAddr;
                row.dwLocalPort = r.dwLocalPort;
                row.dwRemoteAddr = r.dwRemoteAddr;
                row.dwRemotePort = r.dwRemotePort;
                TCP_ESTATS_DATA_RW_v0 rw{};
                if (GetPerTcpConnectionEStats(&row, TcpConnectionEstatsData, reinterpret_cast<PUCHAR>(&rw), 0,
                                              sizeof(rw), nullptr, 0, 0, nullptr, 0, 0) == NO_ERROR) {
                    if (!rw.EnableCollection) {
                        rw.EnableCollection = TRUE;
                        SetPerTcpConnectionEStats(&row, TcpConnectionEstatsData, reinterpret_cast<PUCHAR>(&rw), 0,
                                                  sizeof(rw), 0);
                    } else {
                        TCP_ESTATS_DATA_ROD_v0 rod{};
                        if (GetPerTcpConnectionEStats(&row, TcpConnectionEstatsData, nullptr, 0, 0, nullptr, 0, 0,
                                                      reinterpret_cast<PUCHAR>(&rod), 0, sizeof(rod)) == NO_ERROR) {
                            s.estats_bytes += rod.DataBytesIn + rod.DataBytesOut;
                        }
                    }
                }
            }
        }
    }
    // IPv6 TCP connections
    sz = 0;
    GetExtendedTcpTable(nullptr, &sz, FALSE, AF_INET6, TCP_TABLE_OWNER_PID_CONNECTIONS, 0);
    if (sz > 0 && sz < 16 * 1024 * 1024) {
        std::vector<BYTE> buf(sz);
        if (GetExtendedTcpTable(buf.data(), &sz, FALSE, AF_INET6, TCP_TABLE_OWNER_PID_CONNECTIONS, 0) == NO_ERROR) {
            auto* tbl = reinterpret_cast<MIB_TCP6TABLE_OWNER_PID*>(buf.data());
            for (DWORD i = 0; i < tbl->dwNumEntries; ++i) {
                const auto& r = tbl->table[i];
                if (r.dwOwningPid <= 4) continue;
                auto& s = out[int64_t(r.dwOwningPid)];
                if (r.dwState == MIB_TCP_STATE_ESTAB || r.dwState == MIB_TCP_STATE_CLOSE_WAIT) {
                    ++s.conns;
                }
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
                    if (!rw.EnableCollection) {
                        rw.EnableCollection = TRUE;
                        SetPerTcp6ConnectionEStats(&row, TcpConnectionEstatsData, reinterpret_cast<PUCHAR>(&rw), 0,
                                                   sizeof(rw), 0);
                    } else {
                        TCP_ESTATS_DATA_ROD_v0 rod{};
                        if (GetPerTcp6ConnectionEStats(&row, TcpConnectionEstatsData, nullptr, 0, 0, nullptr, 0, 0,
                                                       reinterpret_cast<PUCHAR>(&rod), 0, sizeof(rod)) == NO_ERROR) {
                            s.estats_bytes += rod.DataBytesIn + rod.DataBytesOut;
                        }
                    }
                }
            }
        }
    }
    return out;
}

std::string query_command_line(HANDLE h) {
    static NtQIP fn = reinterpret_cast<NtQIP>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationProcess")));
    if (!fn || !h) return {};
    std::string out;
    ULONG len = 0;
    constexpr ULONG ProcessCommandLineInformation = 60;
    fn(h, ProcessCommandLineInformation, nullptr, 0, &len);
    if (len >= sizeof(UNICODE_STRING) && len < 64 * 1024) {
        std::vector<BYTE> buf(len);
        if (fn(h, ProcessCommandLineInformation, buf.data(), len, &len) >= 0) {
            auto* us = reinterpret_cast<UNICODE_STRING*>(buf.data());
            if (us->Buffer && us->Length >= sizeof(wchar_t)) {
                out = to_utf8(std::wstring(us->Buffer, us->Length / sizeof(wchar_t)));
            }
        }
    }
    if (out.size() > 4096) out.resize(4096);
    return out;
}

std::string query_image_path(HANDLE h) {
    if (!h) return {};
    wchar_t buf[MAX_PATH * 2];
    DWORD len = DWORD(std::size(buf));
    if (QueryFullProcessImageNameW(h, 0, buf, &len) && len > 0) {
        return to_utf8(std::wstring(buf, len));
    }
    return {};
}

std::string query_user_name(HANDLE h) {
    if (!h) return {};
    HANDLE tok = nullptr;
    std::string out;
    if (OpenProcessToken(h, TOKEN_QUERY, &tok)) {
        BYTE buf[256];
        DWORD len = 0;
        if (GetTokenInformation(tok, TokenUser, buf, sizeof buf, &len)) {
            wchar_t name[256], dom[256];
            DWORD nl = 256, dl = 256;
            SID_NAME_USE use;
            if (LookupAccountSidW(nullptr, reinterpret_cast<TOKEN_USER*>(buf)->User.Sid, name, &nl, dom, &dl, &use))
                out = to_utf8(name);
        }
        CloseHandle(tok);
    }
    return out;
}

class ProcessMonitor final : public IProcessMonitor {
public:
    std::vector<ProcessInfo> snapshot() override {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE) return {};

        FILETIME sys_idle{}, sys_k{}, sys_u{};
        uint64_t total_sys_ticks = 0;
        if (GetSystemTimes(&sys_idle, &sys_k, &sys_u)) {
            total_sys_ticks = ft_to_u64(sys_k) + ft_to_u64(sys_u);
        }
        const uint64_t delta_sys_ticks =
            (last_sys_ticks_ > 0 && total_sys_ticks > last_sys_ticks_) ? (total_sys_ticks - last_sys_ticks_) : 0;
        last_sys_ticks_ = total_sys_ticks;

        const int64_t now_tick_ms = steady_ms();
        const double elapsed_sec =
            (last_sample_ms_ > 0 && now_tick_ms > last_sample_ms_) ? double(now_tick_ms - last_sample_ms_) / 1000.0 : 0.0;
        last_sample_ms_ = now_tick_ms;

        const auto net_by_pid = sample_tcp_by_pid();

        PROCESSENTRY32W pe{};
        pe.dwSize = sizeof pe;
        std::unordered_map<int64_t, ProcessInfo> next;
        std::unordered_map<int64_t, PidCounters> next_counters;
        std::vector<ProcessInfo> out;
        out.reserve(cache_.empty() ? 256 : cache_.size() + 32);

        for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe)) {
            const DWORD pid = pe.th32ProcessID;
            if (pid == 0 || pid == 4) continue;

            HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
            int64_t st = 0;
            uint64_t rss = 0;
            uint64_t proc_cpu_ticks = 0;
            uint64_t other_io_bytes = 0;
            if (h) {
                FILETIME c{}, e{}, k{}, u{};
                if (GetProcessTimes(h, &c, &e, &k, &u)) {
                    st = (int64_t(c.dwHighDateTime) << 32) | c.dwLowDateTime;
                    proc_cpu_ticks = ft_to_u64(k) + ft_to_u64(u);
                }
                PROCESS_MEMORY_COUNTERS pmc{};
                if (GetProcessMemoryInfo(h, &pmc, sizeof pmc)) {
                    rss = pmc.WorkingSetSize;
                }
                IO_COUNTERS io{};
                if (GetProcessIoCounters(h, &io)) {
                    other_io_bytes = io.OtherTransferCount;
                }
            }

            uint32_t conns = 0;
            uint64_t estats_bytes = 0;
            if (auto nit = net_by_pid.find(pid); nit != net_by_pid.end()) {
                conns = nit->second.conns;
                estats_bytes = nit->second.estats_bytes;
            }

            double cpu_pct = 0.0;
            double net_kbps = 0.0;
            if (auto cit = counters_.find(pid); cit != counters_.end() && st != 0 && cit->second.start_time == st) {
                if (delta_sys_ticks > 0 && proc_cpu_ticks >= cit->second.cpu_ticks) {
                    cpu_pct = (double(proc_cpu_ticks - cit->second.cpu_ticks) / double(delta_sys_ticks)) * 100.0;
                    cpu_pct = std::clamp(std::round(cpu_pct * 10.0) / 10.0, 0.0, 100.0);
                }
                if (elapsed_sec >= 0.15) {
                    uint64_t net_delta = 0;
                    if (estats_bytes >= cit->second.estats_bytes && cit->second.estats_bytes > 0) {
                        net_delta = estats_bytes - cit->second.estats_bytes;
                    } else if (conns > 0 && other_io_bytes >= cit->second.other_io_bytes) {
                        net_delta = other_io_bytes - cit->second.other_io_bytes;
                    }
                    net_kbps = std::round((double(net_delta) / 1024.0 / elapsed_sec) * 10.0) / 10.0;
                }
            }
            next_counters[pid] = PidCounters{st, proc_cpu_ticks, estats_bytes, other_io_bytes};

            auto it = cache_.find(pid);
            if (it != cache_.end() && st != 0 && it->second.start_time == st) {
                if (h) CloseHandle(h);
                it->second.rss_bytes = rss;
                it->second.cpu_pct = cpu_pct;
                it->second.net_kbps = net_kbps;
                it->second.net_conns = conns;
                out.push_back(it->second);
                next.emplace(pid, std::move(it->second));
                continue;
            }

            ProcessInfo p;
            p.pid = pid;
            p.ppid = pe.th32ParentProcessID;
            p.name = to_utf8(pe.szExeFile);
            p.exe_path = query_image_path(h);
            p.cmdline = query_command_line(h);
            p.user = query_user_name(h);
            p.rss_bytes = rss;
            p.cpu_pct = cpu_pct;
            p.net_kbps = net_kbps;
            p.net_conns = conns;
            p.start_time = st;
            if (h) CloseHandle(h);

            out.push_back(p);
            next.emplace(pid, std::move(p));
        }
        CloseHandle(snap);
        cache_.swap(next);
        counters_.swap(next_counters);
        return out;
    }

    bool watch(std::function<void(const ProcessInfo&)>) override { return false; }

    bool kill(int64_t pid) override {
        if (pid <= 4) return false;
        // Collect direct child processes first so wrapper CLIs/apps don't leave worker children alive.
        std::vector<DWORD> children;
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W pe{};
            pe.dwSize = sizeof pe;
            for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe)) {
                if (pe.th32ParentProcessID == DWORD(pid) && pe.th32ProcessID != DWORD(pid) && pe.th32ProcessID > 4) {
                    children.push_back(pe.th32ProcessID);
                }
            }
            CloseHandle(snap);
        }
        HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, DWORD(pid));
        bool killed_main = false;
        if (h) {
            killed_main = TerminateProcess(h, 1) != FALSE;
            CloseHandle(h);
        }
        for (DWORD child_pid : children) {
            if (HANDLE ch = OpenProcess(PROCESS_TERMINATE, FALSE, child_pid)) {
                TerminateProcess(ch, 1);
                CloseHandle(ch);
            }
        }
        return killed_main;
    }

    void stop() override {}

private:
    struct PidCounters {
        int64_t start_time = 0;
        uint64_t cpu_ticks = 0;
        uint64_t estats_bytes = 0;
        uint64_t other_io_bytes = 0;
    };
    std::unordered_map<int64_t, ProcessInfo> cache_;
    std::unordered_map<int64_t, PidCounters> counters_;
    uint64_t last_sys_ticks_ = 0;
    int64_t last_sample_ms_ = 0;
};

}  // namespace

std::unique_ptr<IProcessMonitor> make_process_monitor() { return std::make_unique<ProcessMonitor>(); }

}  // namespace xg::win
