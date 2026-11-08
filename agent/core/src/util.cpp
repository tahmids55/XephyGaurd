#include "xg/util.hpp"

#include <openssl/rand.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace xg {

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

bool write_file_atomic(const std::string& path, const std::string& content) {
    namespace fs = std::filesystem;
    std::error_code ec;
    auto parent = fs::path(path).parent_path();
    if (!parent.empty()) fs::create_directories(parent, ec);
    std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f << content;
        if (!f.good()) return false;
    }
    fs::rename(tmp, path, ec);
    if (ec) { fs::remove(tmp, ec); return false; }
    return true;
}

}  // namespace xg
