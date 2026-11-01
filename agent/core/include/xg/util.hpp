#pragma once
#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace xg {

/// Wall-clock milliseconds since the Unix epoch (may jump when the clock is changed).
inline int64_t now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}
/// Monotonic milliseconds (never jumps).
inline int64_t steady_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

std::string to_lower(std::string_view s);
std::string trim(std::string_view s);
std::vector<std::string> split(std::string_view s, char sep, bool skip_empty = true);
bool istarts_with(std::string_view s, std::string_view prefix);
bool iends_with(std::string_view s, std::string_view suffix);
bool icontains(std::string_view haystack, std::string_view needle);
/// Basename of a path, accepting both '/' and '\\' separators.
std::string path_basename(std::string_view path);
/// Lowercased basename with a trailing ".exe" removed: "C:\\x\\Ollama.EXE" -> "ollama".
std::string normalize_exe(std::string_view path);
/// Simple glob with '*' and '?' (case-insensitive).
bool glob_match(std::string_view pattern, std::string_view text);
/// Expand a leading "~" to `home` and %VAR% / $VAR environment references.
std::string expand_path(std::string_view path, std::string_view home);
std::string hex_encode(const unsigned char* data, size_t len);
std::string random_hex(size_t bytes);
std::string read_file(const std::string& path, bool* ok = nullptr);
bool write_file_atomic(const std::string& path, const std::string& content);

}  // namespace xg
