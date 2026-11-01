#include "win_common.hpp"  // must precede other Windows SDK headers
#include <sddl.h>
#include <userenv.h>


namespace xg::win {

std::string to_utf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring to_wide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}

std::string last_error_text(DWORD err) {
    wchar_t* buf = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
                   err, 0, reinterpret_cast<LPWSTR>(&buf), 0, nullptr);
    std::string s = buf ? to_utf8(buf) : "error " + std::to_string(err);
    if (buf) LocalFree(buf);
    return s;
}

std::string process_image(DWORD pid) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return {};
    wchar_t buf[MAX_PATH * 2];
    DWORD len = DWORD(std::size(buf));
    std::string out;
    if (QueryFullProcessImageNameW(h, 0, buf, &len)) out = to_utf8(std::wstring(buf, len));
    CloseHandle(h);
    return out;
}

std::string process_user(DWORD pid) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
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
    CloseHandle(h);
    return out;
}

std::string profiles_dir() {
    wchar_t buf[MAX_PATH];
    DWORD len = MAX_PATH;
    if (GetProfilesDirectoryW(buf, &len)) return to_utf8(buf);
    return "C:\\Users";
}

}  // namespace xg::win
