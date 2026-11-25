#include "xg/signatures.hpp"

#include <algorithm>

#include "xg/isolation.hpp"
#include "xg/json_read.hpp"
#include "xg/util.hpp"

namespace xg {

namespace {
// "2026.11.17-101500-ab12cd" -> 20261117101500; "2026.11.17-ab12cd" -> 20261117000000.
std::optional<uint64_t> version_key(const std::string& v) {
    auto digits = [&](size_t pos, size_t n) -> std::optional<uint64_t> {
        if (pos + n > v.size()) return std::nullopt;
        uint64_t x = 0;
        for (size_t i = pos; i < pos + n; ++i) {
            if (v[i] < '0' || v[i] > '9') return std::nullopt;
            x = x * 10 + uint64_t(v[i] - '0');
        }
        return x;
    };
    if (v.size() < 10 || v[4] != '.' || v[7] != '.') return std::nullopt;
    auto y = digits(0, 4), m = digits(5, 2), d = digits(8, 2);
    if (!y || !m || !d) return std::nullopt;
    uint64_t t = 0;
    if (v.size() >= 17 && v[10] == '-' && (v.size() == 17 || v[17] == '-'))
        if (auto hms = digits(11, 6)) t = *hms;
    return ((*y * 100 + *m) * 100 + *d) * 1000000 + t;
}
}  // namespace

std::optional<int> compare_bundle_versions(const std::string& a, const std::string& b) {
    const auto ka = version_key(a), kb = version_key(b);
    if (!ka || !kb) return std::nullopt;
    return *ka < *kb ? -1 : (*ka > *kb ? 1 : 0);
}

Severity parse_severity(const std::string& s) {
    auto l = to_lower(s);
    if (l == "info") return Severity::info;
    if (l == "low") return Severity::low;
    if (l == "high") return Severity::high;
    if (l == "critical") return Severity::critical;
    return Severity::medium;
}

const char* to_string(Severity s) {
    switch (s) {
        case Severity::info: return "info";
        case Severity::low: return "low";
        case Severity::medium: return "medium";
        case Severity::high: return "high";
        case Severity::critical: return "critical";
    }
    return "medium";
}

namespace {
std::vector<std::string> strs(const json& j, const char* key, bool lower = true, bool exe = false) {
    std::vector<std::string> out;
    if (!j.contains(key) || !j[key].is_array()) return out;
    for (const auto& v : j[key]) {
        if (!v.is_string()) continue;
        std::string s = v.get<std::string>();
        if (exe) s = normalize_exe(s);
        else if (lower) s = to_lower(s);
        out.push_back(std::move(s));
    }
    return out;
}
std::string str(const json& j, const char* key) {
    return j.contains(key) && j[key].is_string() ? j[key].get<std::string>() : std::string{};
}
Severity sev(const json& j) { return parse_severity(jr::str(j, "severity", "medium")); }
}  // namespace

SignatureSet SignatureSet::from_json(const json& j, std::string* err) {
    SignatureSet s;
    if (!j.is_object()) {
        if (err) *err = "signature bundle is not an object";
        return s;
    }
    s.version = jr::str(j, "version", "0");
    try {
        for (const auto& r : j.value("processes", json::array())) {
            ProcessRule p;
            p.id = str(r, "id");
            if (p.id.empty()) continue;
            p.category = str(r, "category");
            p.description = str(r, "description");
            p.severity = sev(r);
            const json m = r.value("match", json::object());
            p.exe = strs(m, "exe", true, true);
            p.exe_prefix = strs(m, "exe_prefix");
            p.interpreters = strs(m, "interpreters", true, true);
            p.cmdline_contains = strs(m, "cmdline_contains");
            s.processes.push_back(std::move(p));
        }
        for (const auto& r : j.value("extensions", json::array())) {
            ExtensionRule e{str(r, "id"), to_lower(str(r, "ide")), str(r, "description"), sev(r), strs(r, "ext_ids")};
            e.domains = strs(r, "domains");
            if (const json& idd = jr::obj(r, "id_domains"); !idd.empty())
                for (const auto& [ext, doms] : idd.items()) e.id_domains[to_lower(ext)] = strs(idd, ext.c_str());
            if (!e.id.empty()) s.extensions.push_back(std::move(e));
        }
        const json ports = j.value("ports", json::object());
        for (const auto& r : ports.value("rules", json::array())) {
            PortRule p{str(r, "id"), str(r, "description"), sev(r), uint16_t(r.value("port", 0)),
                       r.value("requires_probe", false)};
            if (!p.id.empty() && p.port) s.ports.push_back(std::move(p));
        }
        if (ports.contains("probe")) {
            const auto& pr = ports["probe"];
            s.probe.enabled = pr.value("enabled", true);
            s.probe.paths = strs(pr, "paths", false);
            s.probe.severity = sev(pr);
            s.probe.min_rss_mb = pr.value("min_rss_mb", uint64_t(300));
        }
        const json arts = j.value("artifacts", json::object());
        for (const auto& r : arts.value("rules", json::array())) {
            ArtifactRule a{str(r, "id"), str(r, "description"), sev(r), strs(r, "globs"), strs(r, "dirs", false),
                           r.value("min_size_mb", uint64_t(0))};
            if (!a.id.empty()) s.artifacts.push_back(std::move(a));
        }
        if (arts.contains("scan")) {
            const auto& sc = arts["scan"];
            s.artifact_scan.roots = strs(sc, "roots", false);
            s.artifact_scan.max_depth = sc.value("max_depth", 6);
            s.artifact_scan.max_files = sc.value("max_files", uint64_t(200000));
            s.artifact_scan.skip_dirs = strs(sc, "skip_dirs", false);
        }
        for (const auto& r : j.value("adapters", json::array())) {
            AdapterRule a{str(r, "id"), str(r, "kind"), str(r, "description"), sev(r), strs(r, "drivers"),
                          strs(r, "name_prefixes")};
            if (!a.id.empty()) s.adapters.push_back(std::move(a));
        }
        for (const auto& r : j.value("devices", json::array())) {
            DeviceRule d{str(r, "id"), str(r, "description"), sev(r), strs(r, "classes"), strs(r, "vendors")};
            if (!d.id.empty()) s.devices.push_back(std::move(d));
        }
        if (j.contains("domains") && j["domains"].is_object()) {
            const auto& dm = j["domains"];
            for (const auto& v : dm.value("ai", json::array())) {
                if (v.is_string()) {
                    std::string host = to_lower(v.get<std::string>());
                    if (!host.empty()) s.ai_domains.push_back(std::move(host));
                }
            }
            for (const auto& v : dm.value("ide", json::array())) {
                if (v.is_string()) {
                    std::string host = to_lower(v.get<std::string>());
                    if (!host.empty()) s.ide_domains.push_back(std::move(host));
                }
            }
        }
        if (const json& w = jr::obj(j, "web"); !w.empty()) {
            for (const auto& a : w.value("ai_sites", json::array())) {
                AiSite site{str(a, "site"), strs(a, "equals"), strs(a, "prefix"), strs(a, "suffix"), strs(a, "contains")};
                if (!site.site.empty() && !(site.equals.empty() && site.prefix.empty() && site.suffix.empty() &&
                                            site.contains.empty()))
                    s.web.ai_sites.push_back(std::move(site));
            }
            s.web.browsers = strs(w, "browsers", true, true);
            s.web.browser_title_suffixes = strs(w, "browser_title_suffixes");
            s.web.telemetry_domains = strs(w, "telemetry_domains");
            s.web.ai_tab_severity = parse_severity(jr::str(w, "ai_tab_severity", "medium"));
            for (const auto& [proc, dom] : jr::obj(w, "process_domains").items())
                if (dom.is_string() && !dom.get<std::string>().empty())
                    s.web.process_domains[normalize_exe(proc)] = to_lower(dom.get<std::string>());
            if (const json& t = jr::obj(w, "tunnel"); !t.empty()) {
                const std::string id = jr::str(t, "id");
                if (id.rfind("net.", 0) == 0) s.web.tunnel.id = id;
                s.web.tunnel.severity = parse_severity(jr::str(t, "severity", "medium"));
                s.web.tunnel.description = jr::str(t, "description", "Connection to an outside host on a tunnel port");
                if (const json* tp = jr::field(t, "tcp_ports"); tp && tp->is_array())
                    for (const auto& v : *tp)
                        // 80/443 are normal web traffic and never a tunnel signal on their own.
                        if (v.is_number_integer() && v.get<int64_t>() > 0 && v.get<int64_t>() < 65536 &&
                            v.get<int64_t>() != 80 && v.get<int64_t>() != 443)
                            s.web.tunnel.tcp_ports.push_back(uint16_t(v.get<int64_t>()));
            }
        }
    } catch (const std::exception& e) {
        if (err) *err = e.what();
    }
    return s;
}

bool WebHints::is_tunnel(const std::string& remote_ip, uint16_t remote_port) const {
    if (tunnel.id.empty()) return false;
    if (std::find(tunnel.tcp_ports.begin(), tunnel.tcp_ports.end(), remote_port) == tunnel.tcp_ports.end()) return false;
    return isolation::is_public_ip(remote_ip);
}

size_t SignatureSet::rule_count() const {
    return processes.size() + extensions.size() + ports.size() + artifacts.size() + adapters.size() + devices.size();
}

}  // namespace xg
