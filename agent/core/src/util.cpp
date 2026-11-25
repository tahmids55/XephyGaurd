#include "xg/util.hpp"

#include <openssl/rand.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#ifndef _WIN32
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#else
#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#endif

namespace xg {

#ifdef _WIN32
namespace {
// Owner-only on Windows: SYSTEM and Administrators full control, inheritance from ProgramData cut off
// (the installer grants Users read there, I-AGENT-2).
constexpr const wchar_t* kSecretSddl = L"D:P(A;;FA;;;SY)(A;;FA;;;BA)";

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n > 0 ? n : 0), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}

struct SecretSd {
    PSECURITY_DESCRIPTOR sd = nullptr;
    SecretSd() { ConvertStringSecurityDescriptorToSecurityDescriptorW(kSecretSddl, SDDL_REVISION_1, &sd, nullptr); }
    ~SecretSd() { if (sd) LocalFree(sd); }
    SecretSd(const SecretSd&) = delete;
    SecretSd& operator=(const SecretSd&) = delete;
};
}  // namespace
#endif

bool protect_secret_file(const std::string& path) {
#ifndef _WIN32
    return ::chmod(path.c_str(), 0600) == 0;
#else
    SecretSd s;
    if (!s.sd) return false;
    BOOL present = FALSE, defaulted = FALSE;
    PACL dacl = nullptr;
    if (!GetSecurityDescriptorDacl(s.sd, &present, &dacl, &defaulted) || !present) return false;
    std::wstring w = widen(path);
    return SetNamedSecurityInfoW(w.data(), SE_FILE_OBJECT,
                                 DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, nullptr, nullptr,
                                 dacl, nullptr) == ERROR_SUCCESS;
#endif
}

std::string to_lower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return out;
}

std::string trim(std::string_view s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return std::string(s.substr(b, e - b));
}

std::vector<std::string> split(std::string_view s, char sep, bool skip_empty) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= s.size()) {
        size_t pos = s.find(sep, start);
        if (pos == std::string_view::npos) pos = s.size();
        auto part = s.substr(start, pos - start);
        if (!skip_empty || !part.empty()) out.emplace_back(part);
        start = pos + 1;
    }
    return out;
}

static bool ieq(char a, char b) {
    return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
}

bool istarts_with(std::string_view s, std::string_view p) {
    return s.size() >= p.size() && std::equal(p.begin(), p.end(), s.begin(), ieq);
}

bool iends_with(std::string_view s, std::string_view suf) {
    return s.size() >= suf.size() && std::equal(suf.begin(), suf.end(), s.end() - suf.size(), ieq);
}

bool icontains(std::string_view h, std::string_view n) {
    if (n.empty()) return true;
    return std::search(h.begin(), h.end(), n.begin(), n.end(), ieq) != h.end();
}

std::string path_basename(std::string_view path) {
    auto pos = path.find_last_of("/\\");
    return std::string(pos == std::string_view::npos ? path : path.substr(pos + 1));
}

std::string normalize_exe(std::string_view path) {
    auto b = to_lower(path_basename(path));
    if (b.size() > 4 && b.ends_with(".exe")) b.resize(b.size() - 4);
    return b;
}

bool glob_match(std::string_view pat, std::string_view txt) {
    size_t p = 0, t = 0, star = std::string_view::npos, mark = 0;
    while (t < txt.size()) {
        if (p < pat.size() && (pat[p] == '?' || ieq(pat[p], txt[t]))) { ++p; ++t; }
        else if (p < pat.size() && pat[p] == '*') { star = p++; mark = t; }
        else if (star != std::string_view::npos) { p = star + 1; t = ++mark; }
        else return false;
    }
    while (p < pat.size() && pat[p] == '*') ++p;
    return p == pat.size();
}

std::string expand_path(std::string_view path, std::string_view home) {
    std::string s(path);
    if (!s.empty() && s[0] == '~' && (s.size() == 1 || s[1] == '/' || s[1] == '\\')) s = std::string(home) + s.substr(1);
    // %VAR%
    for (size_t a = s.find('%'); a != std::string::npos; a = s.find('%', a)) {
        size_t b = s.find('%', a + 1);
        if (b == std::string::npos) break;
        std::string name = s.substr(a + 1, b - a - 1);
        std::string val;
        if (name == "USERPROFILE" || name == "HOME") val = std::string(home);
        else if (name == "APPDATA" && !home.empty()) val = std::string(home) + "\\AppData\\Roaming";
        else if (name == "LOCALAPPDATA" && !home.empty()) val = std::string(home) + "\\AppData\\Local";
        else if (const char* e = std::getenv(name.c_str())) val = e;
        else { a = b + 1; continue; }
        s.replace(a, b - a + 1, val);
        a += val.size();
    }
    // $VAR (only at the start of a component)
    if (s.rfind("$HOME", 0) == 0) s = std::string(home) + s.substr(5);
    return s;
}

std::string hex_encode(const unsigned char* d, size_t n) {
    static const char* hx = "0123456789abcdef";
    std::string out(n * 2, '0');
    for (size_t i = 0; i < n; ++i) { out[2 * i] = hx[d[i] >> 4]; out[2 * i + 1] = hx[d[i] & 15]; }
    return out;
}

std::string random_hex(size_t bytes) {
    std::vector<unsigned char> b(bytes);
    RAND_bytes(b.data(), int(bytes));
    return hex_encode(b.data(), b.size());
}

std::string read_file(const std::string& path, bool* ok) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { if (ok) *ok = false; return {}; }
    std::ostringstream ss;
    ss << f.rdbuf();
    if (ok) *ok = true;
    return ss.str();
}

bool write_file_atomic(const std::string& path, const std::string& content, FileMode mode) {
    namespace fs = std::filesystem;
    std::error_code ec;
    auto parent = fs::path(path).parent_path();
    if (!parent.empty()) fs::create_directories(parent, ec);
#ifndef _WIN32
    // Unique temp name, created exclusively (no pre-planted file or symlink is followed) with the final mode,
    // so a secret is never readable by others, not even between write and rename (I-AGENT-14).
    const mode_t perm = mode == FileMode::secret ? 0600 : 0644;
    const std::string tmp = path + ".tmp." + random_hex(6);
    const int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, perm);
    if (fd < 0) return false;
    bool ok = ::fchmod(fd, perm) == 0;  // the umask may have narrowed it; never widens beyond `perm`
    for (size_t off = 0; ok && off < content.size();) {
        const ssize_t n = ::write(fd, content.data() + off, content.size() - off);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) ok = false;
        else off += size_t(n);
    }
    ok = ok && ::fsync(fd) == 0;
    ok = (::close(fd) == 0) && ok;
    if (!ok || ::rename(tmp.c_str(), path.c_str()) != 0) {
        ::unlink(tmp.c_str());
        return false;
    }
    if (const int dfd = ::open(parent.empty() ? "." : parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC); dfd >= 0) {
        ::fsync(dfd);
        ::close(dfd);
    }
    return true;
#else
    // Unique temp created exclusively (CREATE_NEW) and, for secrets, with the protected owner-only DACL from
    // the first byte; MoveFileEx keeps that DACL on the final name.
    SecretSd s;
    SECURITY_ATTRIBUTES sa{sizeof sa, mode == FileMode::secret ? s.sd : nullptr, FALSE};
    if (mode == FileMode::secret && !s.sd) return false;
    const std::wstring tmp = widen(path + ".tmp." + random_hex(6));
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, &sa, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    bool ok = true;
    for (size_t off = 0; ok && off < content.size();) {
        DWORD n = 0;
        const DWORD want = DWORD(std::min<size_t>(content.size() - off, 1u << 30));
        ok = WriteFile(h, content.data() + off, want, &n, nullptr) && n > 0;
        off += n;
    }
    ok = ok && FlushFileBuffers(h);
    ok = CloseHandle(h) && ok;
    if (!ok || !MoveFileExW(tmp.c_str(), widen(path).c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tmp.c_str());
        return false;
    }
    (void)ec;
    return true;
#endif
}

bool trusted_pac_url(const std::string& url, const std::string& ccc_host) {
    constexpr std::string_view kScheme = "https://";
    if (ccc_host.empty() || url.size() > 2048 || !istarts_with(url, kScheme)) return false;
    for (unsigned char c : url)
        if (c <= 0x20 || c >= 0x7f || c == '"' || c == '\\' || c == '@') return false;  // no userinfo/odd bytes
    const std::string rest = url.substr(kScheme.size());
    const size_t slash = rest.find('/');
    if (slash == std::string::npos) return false;
    std::string authority = rest.substr(0, slash);
    std::string host = authority;
    if (!authority.empty() && authority.front() == '[') {  // [v6]:port
        const size_t close = authority.find(']');
        if (close == std::string::npos) return false;
        host = authority.substr(1, close - 1);
        const std::string tail = authority.substr(close + 1);
        if (!tail.empty() && (tail[0] != ':' || tail.size() == 1)) return false;
    } else if (const size_t colon = authority.rfind(':'); colon != std::string::npos) {
        host = authority.substr(0, colon);
        const std::string port = authority.substr(colon + 1);
        if (port.empty() || port.size() > 5 || port.find_first_not_of("0123456789") != std::string::npos) return false;
    }
    std::string want = ccc_host;
    if (want.size() > 2 && want.front() == '[' && want.back() == ']') want = want.substr(1, want.size() - 2);
    return to_lower(host) == to_lower(want);
}

std::string make_private_temp_dir(const std::string& prefix) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path base = fs::temp_directory_path(ec);
    if (ec) return {};
#ifndef _WIN32
    std::string tmpl = (base / (prefix + "-XXXXXX")).string();
    if (!::mkdtemp(tmpl.data())) return {};
    return tmpl;
#else
    for (int i = 0; i < 8; ++i) {
        const fs::path p = base / (prefix + "-" + random_hex(8));
        if (fs::create_directory(p, ec)) return p.string();  // false if it already existed
    }
    return {};
#endif
}

std::string parse_tls_sni(const uint8_t* buf, size_t len) {
    if (!buf || len < 43) return {};
    if (buf[0] != 0x16 || buf[1] != 0x03) return {};
    const size_t rec_len = (size_t(buf[3]) << 8) | buf[4];
    size_t p = 5;
    const size_t end = std::min(len, p + rec_len);
    if (p + 38 > end || buf[p] != 0x01) return {};
    p += 4 + 2 + 32;  // Handshake header + version + random
    if (p >= end) return {};
    p += 1 + buf[p];  // Session ID
    if (p + 2 > end) return {};
    p += 2 + ((size_t(buf[p]) << 8) | buf[p + 1]);  // Cipher suites
    if (p + 1 > end) return {};
    p += 1 + buf[p];  // Compression methods
    if (p + 2 > end) return {};
    const size_t ext_total = (size_t(buf[p]) << 8) | buf[p + 1];
    p += 2;
    const size_t ext_end = std::min(end, p + ext_total);
    while (p + 4 <= ext_end) {
        const uint16_t ext_type = (uint16_t(buf[p]) << 8) | buf[p + 1];
        const uint16_t ext_len = (uint16_t(buf[p + 2]) << 8) | buf[p + 3];
        p += 4;
        if (ext_type == 0x0000 && p + 5 <= ext_end) {
            const uint8_t name_type = buf[p + 2];
            const uint16_t name_len = (uint16_t(buf[p + 3]) << 8) | buf[p + 4];
            if (name_type == 0 && name_len > 0 && name_len < 256 && p + 5 + name_len <= ext_end) {
                return to_lower(std::string(reinterpret_cast<const char*>(buf + p + 5), name_len));
            }
        }
        p += ext_len;
    }
    return {};
}

bool is_ai_telemetry_domain(std::string_view d, const std::vector<std::string>& patterns) {
    if (d.empty()) return true;
    return std::any_of(patterns.begin(), patterns.end(),
                       [&](const std::string& p) { return !p.empty() && icontains(d, p); });
}

}  // namespace xg
