#include "xg/config.hpp"

#include <cstdlib>
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
    std::string win_helper;
    const char* exe_dir_pf = std::getenv("ProgramFiles");
    std::string fallback_pf = (exe_dir_pf && *exe_dir_pf) ? std::string(exe_dir_pf) : "C:\\Program Files";
    win_helper = fallback_pf + "\\XephyGuard\\xephyguard-helper.exe";
    if (std::filesystem::exists(default_config_dir() + "\\xephyguard-helper.exe")) {
        win_helper = default_config_dir() + "\\xephyguard-helper.exe";
    }
    c.helper_path = win_helper;
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
    if (auto v = t["ccc"]["http_port"].value<int64_t>()) c.http_port = uint16_t(*v);
    if (auto v = t["ccc"]["discovery_port"].value<int64_t>()) c.discovery_port = uint16_t(*v);
    str("ccc", "ca_fingerprint", c.ca_fingerprint);
    if (auto v = t["screen"]["port"].value<int64_t>()) c.screen_port = uint16_t(*v);
    str("screen", "path", c.screen_path);
    if (auto v = t["screen"]["tls"].value<bool>()) c.screen_tls = *v;
    str("screen", "token", c.screen_token);
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
    if (const char* eh = std::getenv("XG_CCC_HOST"); eh && *eh) c.ccc_host = eh;
    if (const char* ep = std::getenv("XG_HTTP_PORT"); ep && *ep) {
        try { c.http_port = static_cast<uint16_t>(std::stoul(ep)); } catch (...) {}
    }
    if (const char* sp = std::getenv("XG_SCREEN_PORT"); sp && *sp) {
        try { c.screen_port = static_cast<uint16_t>(std::stoul(sp)); } catch (...) {}
    }
    if (const char* spath = std::getenv("XG_SCREEN_PATH"); spath && *spath) c.screen_path = spath;
    if (const char* stls = std::getenv("XG_SCREEN_TLS"); stls && *stls) {
        c.screen_tls = (std::string(stls) == "1" || std::string(stls) == "true");
    }
    if (const char* stok = std::getenv("XG_SCREEN_TOKEN"); stok && *stok) c.screen_token = stok;
    if (ok) *ok = true;
    return c;
}

bool Config::save(const std::string& path) const {
    toml::table t{
        {"ccc", toml::table{{"host", ccc_host}, {"api_port", int64_t(api_port)},
                            {"http_port", int64_t(http_port)}, {"discovery_port", int64_t(discovery_port)},
                            {"ca_fingerprint", ca_fingerprint}}},
        {"screen", toml::table{{"port", int64_t(screen_port)}, {"path", screen_path},
                               {"tls", screen_tls}, {"token", screen_token}}},
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
