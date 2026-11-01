#include "xg/config.hpp"

#include <filesystem>
#include <sstream>
#include <toml++/toml.hpp>

#include "xg/interfaces.hpp"
#include "xg/util.hpp"

#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace xg {

Config Config::defaults() {
    Config c;
    const std::string cfg = default_config_dir();
#ifdef _WIN32
    const char sep = '\\';
    c.helper_path = "C:\\Program Files\\XephyGuard\\xephyguard-helper.exe";
#else
    const char sep = '/';
    c.helper_path = "/usr/lib/xephyguard/xephyguard-helper";
#endif
    c.data_dir = default_data_dir();
    c.log_dir = default_log_dir();
    c.ca_file = cfg + sep + "ca.crt";
    c.cert_file = cfg + sep + "agent.crt";
    c.key_file = cfg + sep + "agent.key";
    c.manifest_file = cfg + sep + "manifest.sha256";
    c.ipc_path = default_ipc_endpoint();
    return c;
}

Config Config::load(const std::string& path, bool* ok, std::string* err) {
    Config c = defaults();
    toml::table t;
    try {
        t = toml::parse_file(path);
    } catch (const toml::parse_error& e) {
        if (ok) *ok = false;
        if (err) { std::ostringstream ss; ss << e; *err = ss.str(); }
        return c;
    }
    auto str = [&](const char* section, const char* key, std::string& dst) {
        if (auto v = t[section][key].value<std::string>()) dst = *v;
    };
    str("ccc", "host", c.ccc_host);
    if (auto v = t["ccc"]["api_port"].value<int64_t>()) c.api_port = uint16_t(*v);
    if (auto v = t["ccc"]["discovery_port"].value<int64_t>()) c.discovery_port = uint16_t(*v);
    str("ccc", "ca_fingerprint", c.ca_fingerprint);
    str("identity", "pc_id", c.pc_id);
    str("identity", "token", c.token);
    str("identity", "enrollment_id", c.enrollment_id);
    str("identity", "poll_token", c.poll_token);
    str("paths", "data_dir", c.data_dir);
    str("paths", "log_dir", c.log_dir);
    str("paths", "ca_file", c.ca_file);
    str("paths", "cert_file", c.cert_file);
    str("paths", "key_file", c.key_file);
    str("paths", "helper", c.helper_path);
    str("paths", "manifest", c.manifest_file);
    str("paths", "ipc", c.ipc_path);
    str("logging", "level", c.log_level);
    if (ok) *ok = true;
    return c;
}

bool Config::save(const std::string& path) const {
    toml::table t{
        {"ccc", toml::table{{"host", ccc_host}, {"api_port", int64_t(api_port)},
                            {"discovery_port", int64_t(discovery_port)}, {"ca_fingerprint", ca_fingerprint}}},
        {"identity", toml::table{{"pc_id", pc_id}, {"token", token}, {"enrollment_id", enrollment_id},
                                 {"poll_token", poll_token}}},
        {"paths", toml::table{{"data_dir", data_dir}, {"log_dir", log_dir}, {"ca_file", ca_file},
                              {"cert_file", cert_file}, {"key_file", key_file}, {"helper", helper_path},
                              {"manifest", manifest_file}, {"ipc", ipc_path}}},
        {"logging", toml::table{{"level", log_level}}},
    };
    std::ostringstream ss;
    ss << "# XephyGuard agent configuration. Managed by the agent; edit only with administrator rights.\n"
       << "# Changes to this file are reported to the CCC as a tamper event.\n\n"
       << t << "\n";
    if (!write_file_atomic(path, ss.str())) return false;
#ifndef _WIN32
    ::chmod(path.c_str(), 0600);
#endif
    return true;
}

}  // namespace xg
