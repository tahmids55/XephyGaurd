#include <cstdlib>
#include <string>

#include "xg/config.hpp"
#include "xg/interfaces.hpp"

namespace xg {

#ifdef _WIN32
static std::string program_data() {
    const char* p = std::getenv("ProgramData");
    return p ? std::string(p) : std::string("C:\\ProgramData");
}
std::string default_config_dir() { return program_data() + "\\XephyGuard"; }
std::string default_data_dir() { return program_data() + "\\XephyGuard\\data"; }
std::string default_log_dir() { return program_data() + "\\XephyGuard\\logs"; }
std::string default_ipc_endpoint() { return "\\\\.\\pipe\\XephyGuardHelper"; }
std::string default_config_path() { return default_config_dir() + "\\agent.toml"; }
#else
std::string default_config_dir() { return "/etc/xephyguard"; }
std::string default_data_dir() { return "/var/lib/xephyguard"; }
std::string default_log_dir() { return "/var/log/xephyguard"; }
std::string default_ipc_endpoint() { return "/run/xephyguard/helper.sock"; }
std::string default_config_path() { return "/etc/xephyguard/agent.toml"; }
#endif

}  // namespace xg
