// Windows process monitor: ToolHelp snapshots (cached per pid), command lines through
// NtQueryInformationProcess(ProcessCommandLineInformation), TerminateProcess for the kill action.
// ETW process-start tracing is a possible upgrade; polling every 2 s meets the 10 s alert target.
#include "win_common.hpp"  // must precede other Windows SDK headers
#include <psapi.h>
#include <tlhelp32.h>
#include <winternl.h>

#include <unordered_map>

#include "xg/util.hpp"

namespace xg::win {
namespace {

using NtQIP = LONG(NTAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG);

std::string command_line(DWORD pid) {
    static NtQIP fn = reinterpret_cast<NtQIP>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationProcess")));
    if (!fn) return {};
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return {};
    std::string out;
    ULONG len = 0;
    constexpr ULONG ProcessCommandLineInformation = 60;
    fn(h, ProcessCommandLineInformation, nullptr, 0, &len);
    if (len > 0 && len < 64 * 1024) {
        std::vector<BYTE> buf(len);
        if (fn(h, ProcessCommandLineInformation, buf.data(), len, &len) >= 0) {
            auto* us = reinterpret_cast<UNICODE_STRING*>(buf.data());
            out = to_utf8(std::wstring(us->Buffer, us->Length / sizeof(wchar_t)));
        }
    }
    CloseHandle(h);
    if (out.size() > 4096) out.resize(4096);
    return out;
}

int64_t start_time(DWORD pid) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return 0;
    FILETIME c{}, e{}, k{}, u{};
    int64_t t = 0;
    if (GetProcessTimes(h, &c, &e, &k, &u)) t = (int64_t(c.dwHighDateTime) << 32) | c.dwLowDateTime;
    CloseHandle(h);
    return t;
}

class ProcessMonitor final : public IProcessMonitor {
public:
    std::vector<ProcessInfo> snapshot() override {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE) return {};
        PROCESSENTRY32W pe{};
        pe.dwSize = sizeof pe;
        std::unordered_map<int64_t, ProcessInfo> next;
        for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe)) {
            const DWORD pid = pe.th32ProcessID;
            if (pid == 0 || pid == 4) continue;
            const int64_t st = start_time(pid);
            auto it = cache_.find(pid);
            if (it != cache_.end() && it->second.start_time == st) {
                next.emplace(pid, it->second);
                continue;
            }
            ProcessInfo p;
            p.pid = pid;
            p.ppid = pe.th32ParentProcessID;
            p.name = to_utf8(pe.szExeFile);
            p.exe_path = process_image(pid);
            p.cmdline = command_line(pid);
            p.user = process_user(pid);
            p.start_time = st;
            next.emplace(pid, std::move(p));
        }
        CloseHandle(snap);
        cache_.swap(next);
        std::vector<ProcessInfo> out;
        out.reserve(cache_.size());
        for (auto& [pid, p] : cache_) {
            HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, DWORD(pid));
            if (h) {
                PROCESS_MEMORY_COUNTERS pmc{};
                if (GetProcessMemoryInfo(h, &pmc, sizeof pmc)) p.rss_bytes = pmc.WorkingSetSize;
                CloseHandle(h);
            }
            out.push_back(p);
        }
        return out;
    }
    bool watch(std::function<void(const ProcessInfo&)>) override { return false; }
    bool kill(int64_t pid) override {
        HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, DWORD(pid));
        if (!h) return false;
        const BOOL ok = TerminateProcess(h, 1);
        CloseHandle(h);
        return ok != FALSE;
    }
    void stop() override {}

private:
    std::unordered_map<int64_t, ProcessInfo> cache_;
};

}  // namespace

std::unique_ptr<IProcessMonitor> make_process_monitor() { return std::make_unique<ProcessMonitor>(); }

}  // namespace xg::win
