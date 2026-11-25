#include <arpa/inet.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <filesystem>

#include <nlohmann/json.hpp>

#include "linux_common.hpp"
#include "xg/log.hpp"
#include "xg/util.hpp"

namespace xg::lnx {
namespace {

using json = nlohmann::json;

constexpr const char* kChromiumPolicyPaths[] = {
    "/etc/opt/chrome/policies/managed/xephyguard.json",
    "/etc/chromium/policies/managed/xephyguard.json",
    "/etc/opt/edge/policies/managed/xephyguard.json",
};
constexpr const char* kFirefoxPolicyPath = "/etc/firefox/policies/policies.json";
constexpr const char* kResolvedDropInPath = "/etc/systemd/resolved.conf.d/xephyguard.conf";

bool is_routable_ip_literal(const std::string& ip) {
    if (ip.empty()) return false;
    in_addr v4{};
    if (::inet_pton(AF_INET, ip.c_str(), &v4) == 1) {
        const uint32_t host = ntohl(v4.s_addr);
        return (host >> 24) != 127 && host != 0;
    }
    in6_addr v6{};
    if (::inet_pton(AF_INET6, ip.c_str(), &v6) == 1) {
        return !IN6_IS_ADDR_LOOPBACK(&v6) && !IN6_IS_ADDR_UNSPECIFIED(&v6);
    }
    return false;
}

void reload_systemd_resolved() {
    pid_t pid = ::fork();
    if (pid == 0) {
        ::execlp("systemctl", "systemctl", "try-reload-or-restart", "systemd-resolved", static_cast<char*>(nullptr));
        _exit(127);
    }
    if (pid > 0) {
        int status = 0;
        ::waitpid(pid, &status, 0);
    }
}

bool write_chromium_policy_file(const std::string& path, bool ai_suppressed, const std::string& effective_url,
                                const std::string& pac_url, const std::vector<std::string>& blocked,
                                const std::vector<std::string>& ours) {
    const bool is_edge = path.find("/edge/") != std::string::npos;
    bool existed = false;
    const std::string existing_raw = read_file(path, &existed);
    json doc = json::object();
    if (existed && !existing_raw.empty()) {
        auto parsed = json::parse(existing_raw, nullptr, false);
        if (parsed.is_object()) doc = std::move(parsed);
    }
    const json orig = doc;

    doc["DnsOverHttpsMode"] = "off";
    doc["QuicAllowed"] = false;
    doc["GenAILocalFoundationalModelSettings"] = 1;
    if (!is_edge) {
        if (!doc.contains("GenAiDefaultSettings")) doc["GenAiDefaultSettings"] = 2;
        if (!doc.contains("GeminiSettings")) doc["GeminiSettings"] = 1;
        if (!doc.contains("HelpMeWriteSettings")) doc["HelpMeWriteSettings"] = 2;
    }

    if (ai_suppressed) {
        doc["DefaultSearchProviderEnabled"] = true;
        doc["DefaultSearchProviderName"] = "Google";
        doc["DefaultSearchProviderSearchURL"] = effective_url;
        doc["DefaultSearchProviderSuggestURL"] = "https://www.google.com/complete/search?output=chrome&q={searchTerms}";
        doc["DefaultSearchProviderNewTabURL"] = "https://www.google.com";
        doc["DefaultSearchProviderIconURL"] = "https://www.google.com/favicon.ico";
        doc["AIModeSettings"] = 1;
        doc["GenAiDefaultSettings"] = 2;
        doc["GeminiSettings"] = 1;
        doc["HelpMeWriteSettings"] = 2;
    } else {
        doc.erase("DefaultSearchProviderEnabled");
        doc.erase("DefaultSearchProviderName");
        doc.erase("DefaultSearchProviderSearchURL");
        doc.erase("DefaultSearchProviderSuggestURL");
        doc.erase("DefaultSearchProviderNewTabURL");
        doc.erase("DefaultSearchProviderIconURL");
        doc.erase("AIModeSettings");
    }

    if (!pac_url.empty()) {
        doc["ProxyMode"] = "pac_script";
        doc["ProxyPacUrl"] = pac_url;
    } else {
        doc.erase("ProxyMode");
        doc.erase("ProxyPacUrl");
    }

    // Extension blocklist: keep an administrator's own entries, replace only ids this agent manages.
    json list = json::array();
    if (doc.contains("ExtensionInstallBlocklist") && doc["ExtensionInstallBlocklist"].is_array())
        for (const auto& v : doc["ExtensionInstallBlocklist"])
            if (v.is_string() && std::find(ours.begin(), ours.end(), v.get<std::string>()) == ours.end()) list.push_back(v);
    for (const auto& id : blocked) list.push_back(id);
    if (list.empty()) doc.erase("ExtensionInstallBlocklist");
    else doc["ExtensionInstallBlocklist"] = list;

    if (existed && doc == orig) return true;
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    return write_file_atomic(path, doc.dump(2) + "\n");
}

bool write_firefox_policy_file(const std::string& path, bool ai_suppressed, const std::string& effective_url,
                               const std::string& pac_url) {
    bool existed = false;
    const std::string existing_raw = read_file(path, &existed);
    json root = json::object();
    if (existed && !existing_raw.empty()) {
        auto parsed = json::parse(existing_raw, nullptr, false);
        if (parsed.is_object()) root = std::move(parsed);
    }
    if (!root.contains("policies") || !root["policies"].is_object()) root["policies"] = json::object();
    const json orig = root;

    json& pol = root["policies"];
    pol["DNSOverHTTPS"] = {{"Enabled", false}, {"Locked", true}};
    pol["GenerativeAI"] = {{"Enabled", false}, {"Locked", true}};
    if (!pol.contains("DisableTelemetry")) pol["DisableTelemetry"] = true;

    if (!pac_url.empty()) {
        pol["Proxy"] = {{"Mode", "autoConfig"}, {"AutoConfigURL", pac_url}, {"Locked", true}};
    } else {
        pol.erase("Proxy");
    }

    if (ai_suppressed) {
        pol["SearchEngines"] = {
            {"Default", "Google"},
            {"Add", json::array({{
                        {"Name", "Google"},
                        {"URLTemplate", effective_url},
                        {"Method", "GET"},
                        {"IconURL", "https://www.google.com/favicon.ico"},
                        {"Alias", "google"},
                        {"Description", "Google (Web Only)"},
                    }})},
        };
    } else {
        pol.erase("SearchEngines");
    }

    if (existed && root == orig) return true;
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    return write_file_atomic(path, root.dump(2) + "\n");
}

class SystemInfo final : public ISystemInfo {
public:
    json fallback_screen_caps(const std::vector<UserSession>& sessions, bool screen_enabled) override {
        bool has_wayland = false, has_x11 = false;
        for (const auto& s : sessions) {
            if (s.session_type == "wayland") has_wayland = true;
            else if (s.session_type == "x11") has_x11 = true;
        }
        if (has_wayland && !has_x11)
            return {{"session_type", "wayland"}, {"snapshot", false}, {"live", false}, {"delta_tiles", false},
                    {"monitors", 1}, {"secure_desktop", false}};
        const bool x11_ok = has_x11 && screen_enabled;
        return {{"session_type", "x11"}, {"snapshot", x11_ok}, {"live", x11_ok}, {"delta_tiles", x11_ok},
                {"monitors", 1}, {"secure_desktop", false}};
    }
    std::string hostname() override {
        char buf[256] = {0};
        ::gethostname(buf, sizeof buf - 1);
        return buf;
    }
    std::string os_name() override {
        auto v = os_release("NAME");
        return v.empty() ? "Linux" : v;
    }
    std::string os_version() override {
        utsname u{};
        ::uname(&u);
        return os_release("VERSION_ID") + " (kernel " + u.release + ")";
    }
    std::vector<std::string> mac_addresses() override {
        std::vector<std::string> out;
        for (const auto& n : list_dir("/sys/class/net")) {
            if (n == "lo") continue;
            if (read_link("/sys/class/net/" + n).find("/virtual/") != std::string::npos) continue;
            auto mac = trim(read_small("/sys/class/net/" + n + "/address"));
            if (!mac.empty() && mac != "00:00:00:00:00:00") out.push_back(mac);
        }
        std::sort(out.begin(), out.end());
        return out;
    }
    std::vector<UserSession> sessions() override {
        // systemd-logind state files: /run/systemd/sessions/<id>  (KEY=VALUE lines)
        std::vector<UserSession> out;
        for (const auto& id : list_dir("/run/systemd/sessions")) {
            if (id.find('.') != std::string::npos) continue;  // *.ref files
            std::string user, type, display, active, cls, state, uid;
            for (const auto& line : split(read_small("/run/systemd/sessions/" + id), '\n')) {
                auto eq = line.find('=');
                if (eq == std::string::npos) continue;
                auto k = line.substr(0, eq), v = line.substr(eq + 1);
                if (k == "USER") user = v;
                else if (k == "UID") uid = v;
                else if (k == "TYPE") type = v;
                else if (k == "DISPLAY") display = v;
                else if (k == "ACTIVE") active = v;
                else if (k == "CLASS") cls = v;
                else if (k == "STATE") state = v;
            }
            if (cls != "user" || user.empty() || state == "closing") continue;
            UserSession s;
            s.user = user;
            s.uid = uint32_t(std::stoul(uid.empty() ? "0" : uid));
            s.home = user_home(user);
            s.session_type = type.empty() ? "tty" : type;
            s.display = display;
            s.active = active == "1";
            out.push_back(std::move(s));
        }
        return out;
    }
    int64_t uptime_s() override {
        auto s = read_small("/proc/uptime");
        return s.empty() ? 0 : int64_t(std::stod(s));
    }
    std::string self_exe_path() override { return read_link("/proc/self/exe"); }
    SelfUsage self_usage() override {
        SelfUsage u;
        auto statm = split(read_small("/proc/self/statm"), ' ');
        if (statm.size() > 1) u.rss_bytes = std::stoull(statm[1]) * uint64_t(sysconf(_SC_PAGESIZE));
        auto stat = read_small("/proc/self/stat");
        auto r = stat.rfind(')');
        if (r != std::string::npos) {
            auto f = split(stat.substr(r + 2), ' ');
            if (f.size() > 12) {
                const long tck = sysconf(_SC_CLK_TCK);
                u.cpu_time_ms = (std::stoull(f[11]) + std::stoull(f[12])) * 1000ULL / uint64_t(tck);
            }
        }
        return u;
    }

    ~SystemInfo() override {
        if (ai_suppressed_ || !proxy_pac_url_.empty()) {
            apply_linux_browser_policies("", false, {}, {}, {}, managed_extensions_);
        }
        if (!lab_dns_ip_.empty()) {
            apply_linux_resolved_dns("", "", true);
        }
    }

    void set_browser_ai_overview_suppressed(bool suppressed, const std::string& search_url = {}) override {
        if (suppressed == ai_suppressed_ && search_url == search_url_) return;
        ai_suppressed_ = suppressed;
        search_url_ = search_url;
        reapply_browser_policies();
    }

    void set_browser_proxy_pac(const std::string& pac_url) override {
        if (pac_url == proxy_pac_url_) return;
        proxy_pac_url_ = pac_url;
        reapply_browser_policies();
    }

    void set_browser_extension_blocklist(const std::vector<std::string>& ids) override {
        if (ids == blocked_extensions_) return;
        for (const auto& id : ids)  // remember every id we ever set, so it can be removed again later
            if (std::find(managed_extensions_.begin(), managed_extensions_.end(), id) == managed_extensions_.end())
                managed_extensions_.push_back(id);
        blocked_extensions_ = ids;
        reapply_browser_policies();
        log::info("browser policy: {} AI extension(s) blocked", blocked_extensions_.size());
    }

    void set_lab_dns(const std::string& dns_ip) override {
        if (dns_ip == lab_dns_ip_) return;
        lab_dns_ip_ = dns_ip;
        apply_linux_resolved_dns("", lab_dns_ip_, true);
    }

private:
    void reapply_browser_policies() {
        apply_linux_browser_policies("", ai_suppressed_, search_url_, proxy_pac_url_, blocked_extensions_,
                                     managed_extensions_);
    }

    bool ai_suppressed_ = false;
    std::string search_url_;
    std::string proxy_pac_url_;
    std::vector<std::string> blocked_extensions_, managed_extensions_;
    std::string lab_dns_ip_;

    static std::string os_release(const std::string& key) {
        for (const auto& line : split(read_small("/etc/os-release"), '\n'))
            if (line.rfind(key + "=", 0) == 0) {
                auto v = line.substr(key.size() + 1);
                if (v.size() >= 2 && v.front() == '"') v = v.substr(1, v.size() - 2);
                return v;
            }
        return {};
    }
};

}  // namespace

bool apply_linux_browser_policies(const std::string& root_prefix, bool ai_suppressed, const std::string& search_url,
                                  const std::string& pac_url, const std::vector<std::string>& blocked_extensions,
                                  const std::vector<std::string>& managed_extensions) {
    const std::string effective_url =
        search_url.empty() ? "https://www.google.com/search?q={searchTerms}&udm=14" : search_url;
    bool all_ok = true;
    for (const char* rel : kChromiumPolicyPaths) {
        if (!write_chromium_policy_file(root_prefix + rel, ai_suppressed, effective_url, pac_url, blocked_extensions,
                                        managed_extensions)) {
            all_ok = false;
        }
    }
    if (!write_firefox_policy_file(root_prefix + kFirefoxPolicyPath, ai_suppressed, effective_url, pac_url)) {
        all_ok = false;
    }
    if (!all_ok) {
        log::debug("browser policy: one or more Linux managed policy paths were not writable");
    }
    return all_ok;
}

bool apply_linux_resolved_dns(const std::string& root_prefix, const std::string& dns_ip, bool reload_service) {
    const std::string path = root_prefix + kResolvedDropInPath;
    bool existed = false;
    const std::string prev = read_file(path, &existed);

    if (dns_ip.empty() || !is_routable_ip_literal(dns_ip)) {
        if (existed) {
            std::error_code ec;
            std::filesystem::remove(path, ec);
            if (!ec && reload_service) reload_systemd_resolved();
        }
        return dns_ip.empty();
    }

    const std::string body =
        "# Managed by XephyGuard agent (gateway DNS lock)\n"
        "[Resolve]\n"
        "DNS=" + dns_ip + "\n"
        "FallbackDNS=\n"
        "DNSOverTLS=no\n";
    if (existed && prev == body) return true;

    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    const bool wrote = write_file_atomic(path, body);
    if (wrote && reload_service) reload_systemd_resolved();
    return wrote;
}

std::unique_ptr<ISystemInfo> make_system_info() { return std::make_unique<SystemInfo>(); }

}  // namespace xg::lnx
