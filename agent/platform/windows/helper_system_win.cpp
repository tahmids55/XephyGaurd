// Helper-side OS settings for the logged-in user (HKCU): Task Manager lockdown and the WinINet PAC URL.
// Moved from apps/helper_main.cpp unchanged (plan step 21, I-AGENT-22).
#include <mutex>
#include <string>

#include "win_common.hpp"

namespace xg::win {

namespace {
void set_user_taskmgr_disabled(bool disabled) {
    HKEY hk = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\System",
                        0, nullptr, 0, KEY_SET_VALUE, nullptr, &hk, nullptr) == ERROR_SUCCESS) {
        if (disabled) {
            DWORD one = 1;
            RegSetValueExW(hk, L"DisableTaskMgr", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&one), sizeof(one));
        } else {
            RegDeleteValueW(hk, L"DisableTaskMgr");
        }
        RegCloseKey(hk);
    }
}

void notify_wininet_proxy_changed() {
    using InternetSetOptionWFn = BOOL(WINAPI*)(void*, DWORD, void*, DWORD);
    if (HMODULE lib = LoadLibraryW(L"wininet.dll")) {
        auto fn = reinterpret_cast<InternetSetOptionWFn>(reinterpret_cast<void*>(GetProcAddress(lib, "InternetSetOptionW")));
        if (fn) {
            constexpr DWORD kSettingsChanged = 39;  // INTERNET_OPTION_SETTINGS_CHANGED
            constexpr DWORD kRefresh = 37;          // INTERNET_OPTION_REFRESH
            fn(nullptr, kSettingsChanged, nullptr, 0);
            fn(nullptr, kRefresh, nullptr, 0);
        }
        FreeLibrary(lib);
    }
    SendNotifyMessageW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, reinterpret_cast<LPARAM>(L"Internet Settings"));
}

void set_user_proxy_pac(const std::string& pac_url) {
    static std::mutex mu;
    static std::string applied_pac;
    std::lock_guard lk(mu);
    if (pac_url == applied_pac) return;

    const wchar_t* subkey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings";
    auto to_wide = [](const std::string& s) -> std::wstring {
        if (s.empty()) return {};
        int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
        if (n <= 1) return {};
        std::wstring out(size_t(n - 1), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, out.data(), n);
        return out;
    };

    if (!pac_url.empty()) {
        const std::wstring wurl = to_wide(pac_url);
        if (wurl.empty()) return;
        HKEY hk = nullptr;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, subkey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &hk, nullptr) ==
            ERROR_SUCCESS) {
            RegSetValueExW(hk, L"AutoConfigURL", 0, REG_SZ, reinterpret_cast<const BYTE*>(wurl.c_str()),
                           DWORD((wurl.size() + 1) * sizeof(wchar_t)));
            RegCloseKey(hk);
            applied_pac = pac_url;
            notify_wininet_proxy_changed();
        }
        return;
    }

    if (!applied_pac.empty()) {
        const std::wstring expected = to_wide(applied_pac);
        HKEY hk = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, subkey, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &hk) == ERROR_SUCCESS) {
            wchar_t buf[1024] = {};
            DWORD type = 0;
            DWORD bytes = sizeof(buf);
            if (RegQueryValueExW(hk, L"AutoConfigURL", nullptr, &type, reinterpret_cast<BYTE*>(buf), &bytes) ==
                    ERROR_SUCCESS &&
                type == REG_SZ && expected == buf) {
                RegDeleteValueW(hk, L"AutoConfigURL");
            }
            RegCloseKey(hk);
        }
        applied_pac.clear();
        notify_wininet_proxy_changed();
    }
}

class HelperSystem final : public IHelperSystem {
public:
    void set_taskmgr_disabled(bool disabled) override { set_user_taskmgr_disabled(disabled); }
    void set_user_proxy(const std::string& pac_url) override { set_user_proxy_pac(pac_url); }
};
}  // namespace

std::unique_ptr<IHelperSystem> make_helper_system() { return std::make_unique<HelperSystem>(); }

}  // namespace xg::win
