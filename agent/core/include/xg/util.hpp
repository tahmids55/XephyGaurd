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
/// Who may read a file the agent writes. `secret`: owner only (0600 on POSIX); `shared`: world-readable 0644
/// (CA cert, browser policies). The temp file is created with this mode from the start (never wider), fsynced,
/// then renamed over `path`. On Windows `secret` means a protected SYSTEM + Administrators only DACL.
enum class FileMode { shared, secret };
bool write_file_atomic(const std::string& path, const std::string& content, FileMode mode = FileMode::shared);
/// Re-applies owner-only access to an existing secret (0600 / SYSTEM+Administrators protected DACL), e.g. for
/// files written by older versions. Returns false if it could not.
bool protect_secret_file(const std::string& path);
/// True if `url` is https://<ccc_host>[:port]/... (case-insensitive host, no userinfo, no other characters).
bool trusted_pac_url(const std::string& url, const std::string& ccc_host);

/// Creates a new, empty directory only the caller can use (mkdtemp 0700 on POSIX) under the system temp dir.
/// Returns an empty string on failure. Never reuses an existing directory.
std::string make_private_temp_dir(const std::string& prefix);
/// Extracts the lowercase TLS ClientHello SNI hostname (extension 0x0000), or empty if absent.
std::string parse_tls_sni(const uint8_t* data, size_t len);
/// True for passive telemetry/experiment hosts that must not raise AI alerts: `domain` contains one of
/// `patterns` (bundle web.telemetry_domains). An empty domain counts as telemetry.
bool is_ai_telemetry_domain(std::string_view domain, const std::vector<std::string>& patterns);

}  // namespace xg
