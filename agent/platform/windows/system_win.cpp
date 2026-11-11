// System info, GPU (NVML), SCM service host and helper launcher for Windows.
#include <winsock2.h>
#include <iphlpapi.h>
#include <psapi.h>
#include <userenv.h>
#include <wtsapi32.h>

#include <algorithm>
#include <atomic>
#include <map>
#include <mutex>

#include "win_common.hpp"
#include "xg/log.hpp"
#include "xg/util.hpp"

namespace xg::win {
namespace {

// ---------------------------------------------------------------------------------- system info
class SystemInfo final : public ISystemInfo {
public:
    std::string hostname() override {
        wchar_t buf[256];
        DWORD n = 256;
        return GetComputerNameExW(ComputerNameDnsHostname, buf, &n) ? to_utf8(buf) : "";
    }
    std::string os_name() override { return "Windows"; }
    std::string os_version() override {
        using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
        auto fn = reinterpret_cast<RtlGetVersionFn>(
            reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion")));
        OSVERSIONINFOW v{};
        v.dwOSVersionInfoSize = sizeof v;
        if (fn && fn(&v) == 0)
            return std::to_string(v.dwMajorVersion) + "." + std::to_string(v.dwMinorVersion) + "." +
                   std::to_string(v.dwBuildNumber);
        return "unknown";
    }
    std::vector<std::string> mac_addresses() override {
        std::vector<std::string> out;
        ULONG len = 16 * 1024;
        std::vector<BYTE> buf(len);
        auto* aa = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data());
        if (GetAdaptersAddresses(AF_UNSPEC, 0, nullptr, aa, &len) != NO_ERROR) return out;
        for (auto* a = aa; a; a = a->Next) {
            if (a->IfType != IF_TYPE_ETHERNET_CSMACD && a->IfType != IF_TYPE_IEEE80211) continue;
            if (a->PhysicalAddressLength != 6) continue;
            char m[18];
            snprintf(m, sizeof m, "%02x:%02x:%02x:%02x:%02x:%02x", a->PhysicalAddress[0], a->PhysicalAddress[1],
                     a->PhysicalAddress[2], a->PhysicalAddress[3], a->PhysicalAddress[4], a->PhysicalAddress[5]);
            out.emplace_back(m);
        }
        std::sort(out.begin(), out.end());
        return out;
    }
    std::vector<UserSession> sessions() override {
        std::vector<UserSession> out;
        WTS_SESSION_INFOW* s = nullptr;
        DWORD n = 0;
        if (!WTSEnumerateSessionsW(WTS_CURRENT_SERVER_HANDLE, 0, 1, &s, &n)) return out;
        const DWORD console = WTSGetActiveConsoleSessionId();
        for (DWORD i = 0; i < n; ++i) {
            if (s[i].SessionId == 0) continue;
            if (s[i].State != WTSActive && s[i].State != WTSDisconnected) continue;
            LPWSTR name = nullptr;
            DWORD bytes = 0;
            std::string user;
            if (WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, s[i].SessionId, WTSUserName, &name, &bytes) && name) {
                user = to_utf8(name);
                WTSFreeMemory(name);
            }
            if (user.empty()) continue;
            UserSession us;
            us.user = user;
            us.uid = s[i].SessionId;
            us.home = profiles_dir() + "\\" + user;
            us.session_type = s[i].SessionId == console ? "console" : "rdp";
            us.display = to_utf8(s[i].pWinStationName ? s[i].pWinStationName : L"");
            us.active = s[i].State == WTSActive;
            out.push_back(std::move(us));
        }
        WTSFreeMemory(s);
        return out;
    }
    int64_t uptime_s() override { return int64_t(GetTickCount64() / 1000); }
    std::string self_exe_path() override {
        wchar_t buf[MAX_PATH * 2];
        DWORD n = GetModuleFileNameW(nullptr, buf, DWORD(std::size(buf)));
        return to_utf8(std::wstring(buf, n));
    }
    SelfUsage self_usage() override {
        SelfUsage u;
        PROCESS_MEMORY_COUNTERS pmc{};
        if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof pmc)) u.rss_bytes = pmc.WorkingSetSize;
        FILETIME c, e, k, us;
        if (GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &us)) {
            auto ft = [](FILETIME f) { return (uint64_t(f.dwHighDateTime) << 32) | f.dwLowDateTime; };
            u.cpu_time_ms = (ft(k) + ft(us)) / 10000;
        }
        return u;
    }
    ~SystemInfo() override {
        if (ai_overview_suppressed_) {
            set_browser_ai_overview_suppressed(false);
        }
    }
    void set_exam_mode(bool active) override {
        HKEY hk = nullptr;
        if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\System",
                            0, nullptr, 0, KEY_SET_VALUE, nullptr, &hk, nullptr) == ERROR_SUCCESS) {
            if (active) {
                DWORD one = 1;
                RegSetValueExW(hk, L"DisableTaskMgr", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&one), sizeof(one));
            } else {
                RegDeleteValueW(hk, L"DisableTaskMgr");
            }
            RegCloseKey(hk);
        }
    }
    void set_browser_ai_overview_suppressed(bool active) override {
        ai_overview_suppressed_ = active;

        // Chromium-based browsers: Google Chrome, Microsoft Edge, Brave Browser, Opera, Vivaldi
        const wchar_t* chromium_keys[] = {
            L"SOFTWARE\\Policies\\Google\\Chrome",
            L"SOFTWARE\\Policies\\Microsoft\\Edge",
            L"SOFTWARE\\Policies\\BraveSoftware\\Brave",
            L"SOFTWARE\\Policies\\Opera Software\\Opera",
            L"SOFTWARE\\Policies\\Vivaldi"
        };
        for (const auto* key_path : chromium_keys) {
            set_chromium_search_policy(key_path, active);
        }

        // Mozilla Firefox
        set_firefox_search_policy(active);

        log::info("browser policy: ai overview suppression {}", active ? "enabled (&udm=14 enforced)" : "disabled");
    }

private:
    bool ai_overview_suppressed_ = false;

    static void set_chromium_search_policy(const wchar_t* subkey, bool active) {
        if (active) {
            HKEY hk = nullptr;
            if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, subkey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &hk, nullptr) == ERROR_SUCCESS) {
                DWORD one = 1;
                const wchar_t* search_url = L"https://www.google.com/search?q={searchTerms}&udm=14";
                const wchar_t* suggest_url = L"https://www.google.com/complete/search?output=chrome&q={searchTerms}";
                const wchar_t* new_tab_url = L"https://www.google.com";
                const wchar_t* name = L"Google";
                const wchar_t* icon_url = L"https://www.google.com/favicon.ico";

                RegSetValueExW(hk, L"DefaultSearchProviderEnabled", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&one), sizeof(one));
                RegSetValueExW(hk, L"DefaultSearchProviderSearchURL", 0, REG_SZ, reinterpret_cast<const BYTE*>(search_url), DWORD((wcslen(search_url) + 1) * sizeof(wchar_t)));
                RegSetValueExW(hk, L"DefaultSearchProviderSuggestURL", 0, REG_SZ, reinterpret_cast<const BYTE*>(suggest_url), DWORD((wcslen(suggest_url) + 1) * sizeof(wchar_t)));
                RegSetValueExW(hk, L"DefaultSearchProviderNewTabURL", 0, REG_SZ, reinterpret_cast<const BYTE*>(new_tab_url), DWORD((wcslen(new_tab_url) + 1) * sizeof(wchar_t)));
                RegSetValueExW(hk, L"DefaultSearchProviderName", 0, REG_SZ, reinterpret_cast<const BYTE*>(name), DWORD((wcslen(name) + 1) * sizeof(wchar_t)));
                RegSetValueExW(hk, L"DefaultSearchProviderIconURL", 0, REG_SZ, reinterpret_cast<const BYTE*>(icon_url), DWORD((wcslen(icon_url) + 1) * sizeof(wchar_t)));
                RegCloseKey(hk);
            }
        } else {
            HKEY hk = nullptr;
            if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, subkey, 0, KEY_SET_VALUE, &hk) == ERROR_SUCCESS) {
                RegDeleteValueW(hk, L"DefaultSearchProviderEnabled");
                RegDeleteValueW(hk, L"DefaultSearchProviderSearchURL");
                RegDeleteValueW(hk, L"DefaultSearchProviderSuggestURL");
                RegDeleteValueW(hk, L"DefaultSearchProviderNewTabURL");
                RegDeleteValueW(hk, L"DefaultSearchProviderName");
                RegDeleteValueW(hk, L"DefaultSearchProviderIconURL");
                RegCloseKey(hk);
            }
        }
    }

    static void set_firefox_search_policy(bool active) {
        const wchar_t* base_key = L"SOFTWARE\\Policies\\Mozilla\\Firefox\\SearchEngines";
        if (active) {
            HKEY hk = nullptr;
            if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, base_key, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &hk, nullptr) == ERROR_SUCCESS) {
                const wchar_t* default_engine = L"Google";
                RegSetValueExW(hk, L"Default", 0, REG_SZ, reinterpret_cast<const BYTE*>(default_engine), DWORD((wcslen(default_engine) + 1) * sizeof(wchar_t)));
                RegCloseKey(hk);
            }
            HKEY hk_add = nullptr;
            if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Mozilla\\Firefox\\SearchEngines\\Add\\1", 0, nullptr, 0, KEY_SET_VALUE, nullptr, &hk_add, nullptr) == ERROR_SUCCESS) {
                const wchar_t* name = L"Google";
                const wchar_t* url = L"https://www.google.com/search?q={searchTerms}&udm=14";
                const wchar_t* method = L"GET";
                const wchar_t* icon = L"https://www.google.com/favicon.ico";
                const wchar_t* alias = L"google";
                const wchar_t* desc = L"Google (Web Only)";

                RegSetValueExW(hk_add, L"Name", 0, REG_SZ, reinterpret_cast<const BYTE*>(name), DWORD((wcslen(name) + 1) * sizeof(wchar_t)));
                RegSetValueExW(hk_add, L"URLTemplate", 0, REG_SZ, reinterpret_cast<const BYTE*>(url), DWORD((wcslen(url) + 1) * sizeof(wchar_t)));
                RegSetValueExW(hk_add, L"Method", 0, REG_SZ, reinterpret_cast<const BYTE*>(method), DWORD((wcslen(method) + 1) * sizeof(wchar_t)));
                RegSetValueExW(hk_add, L"IconURL", 0, REG_SZ, reinterpret_cast<const BYTE*>(icon), DWORD((wcslen(icon) + 1) * sizeof(wchar_t)));
                RegSetValueExW(hk_add, L"Alias", 0, REG_SZ, reinterpret_cast<const BYTE*>(alias), DWORD((wcslen(alias) + 1) * sizeof(wchar_t)));
                RegSetValueExW(hk_add, L"Description", 0, REG_SZ, reinterpret_cast<const BYTE*>(desc), DWORD((wcslen(desc) + 1) * sizeof(wchar_t)));
                RegCloseKey(hk_add);
            }
        } else {
            RegDeleteTreeW(HKEY_LOCAL_MACHINE, base_key);
        }
    }
};

// ------------------------------------------------------------------------------------------- GPU
struct NvmlUtil { unsigned int gpu, memory; };
struct NvmlMem { unsigned long long total, free, used; };

class GpuMonitor final : public IGpuMonitor {
public:
    GpuMonitor() {
        lib_ = LoadLibraryExW(L"nvml.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!lib_) lib_ = LoadLibraryW(L"C:\\Program Files\\NVIDIA Corporation\\NVSMI\\nvml.dll");
        if (!lib_) return;
        auto init = reinterpret_cast<int (*)()>(reinterpret_cast<void*>(GetProcAddress(lib_, "nvmlInit_v2")));
        auto handle = reinterpret_cast<int (*)(unsigned, void**)>(
            reinterpret_cast<void*>(GetProcAddress(lib_, "nvmlDeviceGetHandleByIndex_v2")));
        util_ = reinterpret_cast<int (*)(void*, NvmlUtil*)>(
            reinterpret_cast<void*>(GetProcAddress(lib_, "nvmlDeviceGetUtilizationRates")));
        mem_ = reinterpret_cast<int (*)(void*, NvmlMem*)>(
            reinterpret_cast<void*>(GetProcAddress(lib_, "nvmlDeviceGetMemoryInfo")));
        if (!init || !handle || init() != 0 || handle(0, &dev_) != 0) dev_ = nullptr;
    }
    ~GpuMonitor() override { if (lib_) FreeLibrary(lib_); }
    std::optional<Sample> sample() override {
        if (!dev_ || !util_ || !mem_) return std::nullopt;
        NvmlUtil u{};
        NvmlMem m{};
        if (util_(dev_, &u) != 0 || mem_(dev_, &m) != 0) return std::nullopt;
        return Sample{double(u.gpu), double(m.used) / 1048576.0, double(m.total) / 1048576.0, "nvidia"};
    }

private:
    HMODULE lib_ = nullptr;
    void* dev_ = nullptr;
    int (*util_)(void*, NvmlUtil*) = nullptr;
    int (*mem_)(void*, NvmlMem*) = nullptr;
};

// ------------------------------------------------------------------------------- service host
SERVICE_STATUS_HANDLE g_status_handle = nullptr;
SERVICE_STATUS g_status{};
std::atomic<bool>* g_stop = nullptr;
const std::function<int(std::atomic<bool>&)>* g_body = nullptr;
int g_rc = 0;

void set_state(DWORD state, DWORD wait_hint = 0) {
    g_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_status.dwCurrentState = state;
    g_status.dwControlsAccepted = state == SERVICE_RUNNING ? SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN : 0;
    g_status.dwWaitHint = wait_hint;
    if (g_status_handle) SetServiceStatus(g_status_handle, &g_status);
}

DWORD WINAPI ctrl_handler(DWORD ctrl, DWORD, LPVOID, LPVOID) {
    if (ctrl == SERVICE_CONTROL_STOP || ctrl == SERVICE_CONTROL_SHUTDOWN) {
        set_state(SERVICE_STOP_PENDING, 10000);
        if (g_stop) g_stop->store(true);
        return NO_ERROR;
    }
    return ctrl == SERVICE_CONTROL_INTERROGATE ? NO_ERROR : ERROR_CALL_NOT_IMPLEMENTED;
}

void WINAPI service_main(DWORD, LPWSTR*) {
    g_status_handle = RegisterServiceCtrlHandlerExW(L"XephyGuardAgent", ctrl_handler, nullptr);
    set_state(SERVICE_START_PENDING, 10000);
    std::atomic<bool> stop{false};
    g_stop = &stop;
    g_rc = (*g_body)(stop);
    g_stop = nullptr;
    g_status.dwWin32ExitCode = g_rc == 0 ? NO_ERROR : ERROR_SERVICE_SPECIFIC_ERROR;
    g_status.dwServiceSpecificExitCode = DWORD(g_rc);
    set_state(SERVICE_STOPPED);
}

BOOL WINAPI console_handler(DWORD) {
    if (g_stop) g_stop->store(true);
    return TRUE;
}

class ServiceHost final : public IServiceHost {
public:
    int run(const std::function<int(std::atomic<bool>&)>& body) override {
        g_body = &body;
        SERVICE_TABLE_ENTRYW table[] = {{const_cast<LPWSTR>(L"XephyGuardAgent"), service_main}, {nullptr, nullptr}};
        if (StartServiceCtrlDispatcherW(table)) return g_rc;
        if (GetLastError() != ERROR_FAILED_SERVICE_CONTROLLER_CONNECT) return 1;
        // Started from a console (`xephyguard-agent run --foreground`).
        std::atomic<bool> stop{false};
        g_stop = &stop;
        SetConsoleCtrlHandler(console_handler, TRUE);
        int rc = body(stop);
        g_stop = nullptr;
        return rc;
    }
    void notify_ready() override { set_state(SERVICE_RUNNING); }
    void notify_stopping() override { set_state(SERVICE_STOP_PENDING, 10000); }
    void watchdog_ping() override {}  // SCM recovery actions restart the service on failure
};

// ---------------------------------------------------------------------------- helper launcher
class HelperLauncher final : public IHelperLauncher {
public:
    explicit HelperLauncher(std::string path) : path_(std::move(path)) {}
    ~HelperLauncher() override {
        for (auto& [sid, h] : procs_) CloseHandle(h);
    }
    // Launches xephyguard-helper.exe into each interactive session with the user's own token
    // (WTSQueryUserToken + CreateProcessAsUser). No Run key or Startup shortcut exists to delete.
    int ensure_helpers(const std::vector<UserSession>& sessions, const std::vector<uint32_t>& connected) override {
        int launched = 0;
        for (const auto& s : sessions) {
            if (!s.active) continue;
            if (std::find(connected.begin(), connected.end(), s.uid) != connected.end()) continue;
            auto it = procs_.find(s.uid);
            if (it != procs_.end()) {
                if (WaitForSingleObject(it->second, 0) == WAIT_TIMEOUT) continue;  // still starting
                CloseHandle(it->second);
                procs_.erase(it);
            }
            HANDLE user_tok = nullptr, primary = nullptr;
            if (!WTSQueryUserToken(s.uid, &user_tok)) continue;
            DuplicateTokenEx(user_tok, MAXIMUM_ALLOWED, nullptr, SecurityIdentification, TokenPrimary, &primary);
            CloseHandle(user_tok);
            if (!primary) continue;
            void* env = nullptr;
            CreateEnvironmentBlock(&env, primary, FALSE);
            STARTUPINFOW si{};
            si.cb = sizeof si;
            si.lpDesktop = const_cast<LPWSTR>(L"winsta0\\default");
            PROCESS_INFORMATION pi{};
            std::wstring cmd = L"\"" + to_wide(path_) + L"\"";
            if (CreateProcessAsUserW(primary, nullptr, cmd.data(), nullptr, nullptr, FALSE,
                                     CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW, env, nullptr, &si, &pi)) {
                CloseHandle(pi.hThread);
                procs_[s.uid] = pi.hProcess;
                ++launched;
                log::info("helper: launched in session {} for {}", s.uid, s.user);
            } else {
                log::warn("helper: CreateProcessAsUser failed: {}", last_error_text());
            }
            if (env) DestroyEnvironmentBlock(env);
            CloseHandle(primary);
        }
        return launched;
    }

private:
    std::string path_;
    std::map<uint32_t, HANDLE> procs_;
};

}  // namespace

std::unique_ptr<ISystemInfo> make_system_info() { return std::make_unique<SystemInfo>(); }
std::unique_ptr<IGpuMonitor> make_gpu_monitor() { return std::make_unique<GpuMonitor>(); }
std::unique_ptr<IServiceHost> make_service_host() { return std::make_unique<ServiceHost>(); }
std::unique_ptr<IHelperLauncher> make_helper_launcher(std::string helper_path) {
    return std::make_unique<HelperLauncher>(std::move(helper_path));
}

}  // namespace xg::win
