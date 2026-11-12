#include <cstdlib>
#include <string>

#include "xg/config.hpp"
#include "xg/interfaces.hpp"

namespace xg {

#ifdef _WIN32
static std::string env_or_win(const char* name, const std::string& fallback) {
    const char* p = std::getenv(name);
    return (p && *p) ? std::string(p) : fallback;
}
static std::string program_data() { return env_or_win("ProgramData", "C:\\ProgramData"); }
std::string default_config_dir() { return env_or_win("XG_CONFIG_DIR", program_data() + "\\XephyGuard"); }
std::string default_data_dir() { return env_or_win("XG_DATA_DIR", program_data() + "\\XephyGuard\\data"); }
std::string default_log_dir() { return env_or_win("XG_LOG_DIR", program_data() + "\\XephyGuard\\logs"); }
std::string default_ipc_endpoint() { return env_or_win("XG_IPC_ENDPOINT", "\\\\.\\pipe\\XephyGuardHelper"); }
std::string default_config_path() { return default_config_dir() + "\\agent.toml"; }
#else
static std::string env_or_unix(const char* name, const std::string& fallback) {
    const char* p = std::getenv(name);
    return (p && *p) ? std::string(p) : fallback;
}
std::string default_config_dir() { return env_or_unix("XG_CONFIG_DIR", "/etc/xephyguard"); }
std::string default_data_dir() { return env_or_unix("XG_STATE_DIR", "/var/lib/xephyguard"); }
std::string default_log_dir() { return env_or_unix("XG_LOG_DIR", "/var/log/xephyguard"); }
std::string default_ipc_endpoint() { return env_or_unix("XG_IPC_ENDPOINT", "/run/xephyguard/helper.sock"); }
std::string default_config_path() { return default_config_dir() + "/agent.toml"; }
#endif

}  // namespace xg
