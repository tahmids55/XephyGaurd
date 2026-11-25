#include "xg/config.hpp"

#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <toml++/toml.hpp>

#include "xg/interfaces.hpp"
#include "xg/util.hpp"


namespace xg {

Config Config::defaults() {
    Config c;
    const std::filesystem::path cfg = default_config_dir();
    c.helper_path = default_helper_path();
    c.data_dir = default_data_dir();
    c.log_dir = default_log_dir();
    c.ca_file = (cfg / "ca.crt").string();
    c.cert_file = (cfg / "agent.crt").string();
    c.key_file = (cfg / "agent.key").string();
    c.manifest_file = (cfg / "manifest.sha256").string();
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
    if (auto v = t["screen"]["enabled"].value<bool>()) c.screen_enabled = *v;
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
    if (const char* sen = std::getenv("XG_SCREEN_ENABLE"); sen && *sen) {
        c.screen_enabled = (std::string(sen) == "1" || std::string(sen) == "true");
    }
    if (ok) *ok = true;
    return c;
}

bool Config::save(const std::string& path) const {
    toml::table t{
        {"ccc", toml::table{{"host", ccc_host}, {"api_port", int64_t(api_port)},
                            {"http_port", int64_t(http_port)}, {"discovery_port", int64_t(discovery_port)},
                            {"ca_fingerprint", ca_fingerprint}}},
        {"screen", toml::table{{"enabled", screen_enabled}}},
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
    return write_file_atomic(path, ss.str(), FileMode::secret);  // holds the bearer and screen tokens
}

}  // namespace xg
