#include "xg/log.hpp"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>

#include "xg/util.hpp"

namespace xg::log {
namespace {
std::mutex g_mu;
std::ofstream g_file;
std::string g_path;
Level g_level = Level::info;
bool g_stderr = true;
constexpr std::uintmax_t kMaxBytes = 5 * 1024 * 1024;

const char* name(Level l) {
    switch (l) {
        case Level::debug: return "DEBUG";
        case Level::info: return "INFO ";
        case Level::warn: return "WARN ";
        case Level::error: return "ERROR";
    }
    return "?";
}

void rotate_if_needed() {
    if (g_path.empty() || !g_file.is_open()) return;
    std::error_code ec;
    if (std::filesystem::file_size(g_path, ec) < kMaxBytes || ec) return;
    g_file.close();
    std::filesystem::rename(g_path, g_path + ".1", ec);
    g_file.open(g_path, std::ios::app);
}
}  // namespace

void init(const std::string& path, Level level, bool also_stderr) {
    std::lock_guard lk(g_mu);
    g_level = level;
    g_stderr = also_stderr;
    g_path = path;
    if (!path.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
        g_file.open(path, std::ios::app);
    }
}

void set_level(Level level) { std::lock_guard lk(g_mu); g_level = level; }

Level parse_level(std::string_view s) {
    if (s == "debug") return Level::debug;
    if (s == "warn" || s == "warning") return Level::warn;
    if (s == "error") return Level::error;
    return Level::info;
}

void write(Level level, std::string_view msg) {
    std::lock_guard lk(g_mu);
    if (level < g_level) return;
    auto t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char ts[32];
    std::strftime(ts, sizeof ts, "%Y-%m-%dT%H:%M:%SZ", &tm);
    if (g_file.is_open()) {
        g_file << ts << ' ' << name(level) << ' ' << msg << '\n';
        g_file.flush();
        rotate_if_needed();
    }
    if (g_stderr) std::cerr << ts << ' ' << name(level) << ' ' << msg << '\n';
}

}  // namespace xg::log
