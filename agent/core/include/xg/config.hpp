#pragma once
#include <cstdint>
#include <string>

namespace xg {

/// Agent configuration, stored as TOML at /etc/xephyguard/agent.toml (Linux) or
/// %ProgramData%\XephyGuard\agent.toml (Windows). Owned by root/SYSTEM, mode 0600.
struct Config {
    // CCC location. Empty ccc_host => use auto-discovery.
    std::string ccc_host;
    uint16_t api_port = 8443;          // mTLS agent endpoint (HTTPS + WSS)
    uint16_t http_port = 80;           // plain HTTP endpoint for /ca.crt download
#ifdef _WIN32
    bool screen_enabled = true;
#else
    bool screen_enabled = true;        // Linux Xorg live stream (idle ~0% CPU until viewer attaches)
#endif
    uint16_t discovery_port = 47474;   // UDP broadcast discovery
    std::string ca_fingerprint;        // SHA-256 of the CA pinned at enrollment (hex); checked by the integrity scan

    // Identity (filled by enrollment)
    std::string pc_id;
    std::string token;
    std::string enrollment_id;
    std::string poll_token;

    // Paths
    std::string data_dir;              // queue DB, cached policy
    std::string log_dir;
    std::string ca_file;
    std::string cert_file;
    std::string key_file;
    std::string helper_path;
    std::string manifest_file;         // expected SHA-256 of installed binaries
    std::string ipc_path;              // Unix socket path / named pipe name

    std::string log_level = "info";

    static Config defaults();
    static Config load(const std::string& path, bool* ok = nullptr, std::string* err = nullptr);
    bool save(const std::string& path) const;
    bool enrolled() const { return !pc_id.empty() && !token.empty(); }
};

/// Default location of agent.toml for this OS.
std::string default_config_path();

}  // namespace xg
