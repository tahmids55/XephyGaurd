#include "xg/signatures.hpp"

#include "xg/util.hpp"

namespace xg {

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
Severity sev(const json& j) { return parse_severity(j.value("severity", std::string("medium"))); }
}  // namespace

SignatureSet SignatureSet::from_json(const json& j, std::string* err) {
    SignatureSet s;
    if (!j.is_object()) {
        if (err) *err = "signature bundle is not an object";
        return s;
    }
    s.version = j.value("version", std::string("0"));
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
    } catch (const std::exception& e) {
        if (err) *err = e.what();
    }
    return s;
}

size_t SignatureSet::rule_count() const {
    return processes.size() + extensions.size() + ports.size() + artifacts.size() + adapters.size() + devices.size();
}

}  // namespace xg
