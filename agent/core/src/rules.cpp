#include "xg/rules.hpp"

#include <algorithm>
#include <mutex>
#include <set>

#include "xg/util.hpp"

namespace xg {

namespace {
RuleHit hit_of(const std::string& id, const std::string& cat, const std::string& desc, Severity s) {
    return RuleHit{id, cat, desc, s};
}
bool any_prefix(const std::string& s, const std::vector<std::string>& prefixes) {
    return std::any_of(prefixes.begin(), prefixes.end(), [&](const auto& p) { return istarts_with(s, p); });
}
bool any_contains(const std::string& s, const std::vector<std::string>& needles) {
    return std::any_of(needles.begin(), needles.end(), [&](const auto& n) { return icontains(s, n); });
}
bool in(const std::string& s, const std::vector<std::string>& v) { return std::find(v.begin(), v.end(), s) != v.end(); }
}  // namespace

void RuleEngine::load(SignatureSet set) {
    std::unique_lock lk(mu_);
    set_ = std::move(set);
    exe_index_.clear();
    for (size_t i = 0; i < set_.processes.size(); ++i)
        for (const auto& e : set_.processes[i].exe) exe_index_[e].push_back(i);
}

void RuleEngine::set_disabled(std::set<std::string> ids) {
    std::unique_lock lk(mu_);
    disabled_ = std::move(ids);
}

std::string RuleEngine::version() const {
    std::shared_lock lk(mu_);
    return set_.version;
}

std::vector<RuleHit> RuleEngine::match_process(const ProcessInfo& p) const {
    std::shared_lock lk(mu_);
    std::vector<RuleHit> out;
    std::vector<bool> seen(set_.processes.size(), false);
    auto add = [&](size_t i) {
        if (seen[i]) return;
        seen[i] = true;
        const auto& r = set_.processes[i];
        if (enabled(r.id)) out.push_back(hit_of(r.id, r.category, r.description, r.severity));
    };
    // Candidate names: exe basename and the short process name (they differ for scripts / renamed binaries).
    std::vector<std::string> names;
    if (!p.exe_path.empty()) names.push_back(normalize_exe(p.exe_path));
    if (!p.name.empty()) {
        auto n = normalize_exe(p.name);
        if (names.empty() || names[0] != n) names.push_back(n);
    }
    for (const auto& n : names)
        if (auto it = exe_index_.find(n); it != exe_index_.end())
            for (size_t i : it->second) add(i);

    for (size_t i = 0; i < set_.processes.size(); ++i) {
        if (seen[i]) continue;
        const auto& r = set_.processes[i];
        bool m = false;
        for (const auto& n : names)
            if (any_prefix(n, r.exe_prefix)) { m = true; break; }
        if (!m && !r.cmdline_contains.empty() && !p.cmdline.empty()) {
            bool interp_ok = r.interpreters.empty();
            if (!interp_ok)
                for (const auto& n : names) {
                    // python3.12 -> also accept the "python" family when listed
                    if (in(n, r.interpreters)) { interp_ok = true; break; }
                }
            if (interp_ok) m = any_contains(p.cmdline, r.cmdline_contains);
        }
        if (m) add(i);
    }
    return out;
}

std::optional<RuleHit> RuleEngine::match_extension(const ExtensionInfo& e) const {
    std::shared_lock lk(mu_);
    const auto id = to_lower(e.id);
    const auto ide = to_lower(e.ide);
    // VS Code forks share the VS Code extension format.
    const bool vscode_like = ide == "vscode" || ide == "vscodium" || ide == "cursor" || ide == "windsurf" ||
                             ide == "code-oss" || ide == "vscode-insiders";
    const bool browser_like = ide == "chrome" || ide == "chromium" || ide == "edge" || ide == "brave" || ide == "browser";
    for (const auto& r : set_.extensions) {
        if (!enabled(r.id)) continue;
        const bool ide_ok = r.ide == ide || (r.ide == "vscode" && vscode_like) || (r.ide == "browser" && browser_like);
        if (!ide_ok) continue;
        for (const auto& x : r.ext_ids)
            if (id == x || (r.ide == "jetbrains" && istarts_with(id, x))) return hit_of(r.id, "extension", r.description, r.severity);
    }
    return std::nullopt;
}

std::optional<RuleHit> RuleEngine::match_port(uint16_t port, bool probe_required) const {
    std::shared_lock lk(mu_);
    for (const auto& r : set_.ports)
        if (r.port == port && enabled(r.id) && r.requires_probe == probe_required)
            return hit_of(r.id, "local_llm", r.description, r.severity);
    return std::nullopt;
}

std::optional<RuleHit> RuleEngine::match_artifact_file(const std::string& path, uint64_t size) const {
    std::shared_lock lk(mu_);
    const auto base = path_basename(path);
    for (const auto& r : set_.artifacts) {
        if (!enabled(r.id) || r.globs.empty()) continue;
        if (size < r.min_size_mb * 1024ull * 1024ull) continue;
        for (const auto& g : r.globs)
            if (glob_match(g, base)) return hit_of(r.id, "local_model", r.description, r.severity);
    }
    return std::nullopt;
}

std::optional<RuleHit> RuleEngine::match_artifact_dir(const std::string& dir, const std::string& home) const {
    std::shared_lock lk(mu_);
    for (const auto& r : set_.artifacts) {
        if (!enabled(r.id)) continue;
        for (const auto& d : r.dirs) {
            auto exp = expand_path(d, home);
            if (to_lower(exp) == to_lower(dir)) return hit_of(r.id, "local_model", r.description, r.severity);
        }
    }
    return std::nullopt;
}

std::optional<RuleHit> RuleEngine::classify_adapter(const AdapterInfo& a) const {
    std::shared_lock lk(mu_);
    const auto drv = to_lower(a.driver);
    const auto desc = to_lower(a.description);
    for (const auto& r : set_.adapters) {
        if (!enabled(r.id) || r.kind == "route") continue;
        bool m = false;
        for (const auto& d : r.drivers)
            if (!d.empty() && (drv == d || icontains(desc, d))) { m = true; break; }
        if (!m && any_prefix(a.name, r.name_prefixes)) m = true;
        if (!m && r.kind == "wifi" && a.is_wireless) m = true;
        if (m) return hit_of(r.id, "network", r.description, r.severity);
    }
    return std::nullopt;
}

std::optional<RuleHit> RuleEngine::second_route_rule() const {
    std::shared_lock lk(mu_);
    for (const auto& r : set_.adapters)
        if (r.kind == "route" && enabled(r.id)) return hit_of(r.id, "network", r.description, r.severity);
    return std::nullopt;
}

std::optional<RuleHit> RuleEngine::match_device(const DeviceInfo& d) const {
    std::shared_lock lk(mu_);
    const auto vendor = to_lower(d.vendor_id);
    const auto cls = to_lower(d.devclass);
    // Vendor (phone) rules are more specific: check them first.
    for (const auto& r : set_.devices)
        if (enabled(r.id) && !vendor.empty() && in(vendor, r.vendors)) return hit_of(r.id, "device", r.description, r.severity);
    for (const auto& r : set_.devices)
        if (enabled(r.id) && !cls.empty() && in(cls, r.classes)) return hit_of(r.id, "device", r.description, r.severity);
    return std::nullopt;
}

static bool domain_matches_suffix(const std::string& host, const std::string& pat) {
    if (host == pat) return true;
    return host.size() > pat.size() && host[host.size() - pat.size() - 1] == '.' &&
           host.compare(host.size() - pat.size(), pat.size(), pat) == 0;
}

std::optional<RuleHit> RuleEngine::match_ai_domain(const std::string& domain) const {
    const std::string dom = to_lower(domain);
    if (dom.empty()) return std::nullopt;
    std::shared_lock lk(mu_);
    for (const auto& pat : set_.ide_domains) {
        if (domain_matches_suffix(dom, pat)) {
            return RuleHit{"net.ide_ai", "ide_ai_backend", "IDE AI backend stream (" + dom + ")", Severity::high};
        }
    }
    for (const auto& pat : set_.ai_domains) {
        if (domain_matches_suffix(dom, pat)) {
            return RuleHit{"net.browser_ai", "browser_ai", "AI web service stream (" + dom + ")", Severity::medium};
        }
    }
    return std::nullopt;
}

std::vector<std::string> RuleEngine::ai_domains() const {
    std::shared_lock lk(mu_);
    std::vector<std::string> out;
    out.reserve(set_.ide_domains.size() + set_.ai_domains.size());
    std::set<std::string> seen;
    for (const auto& d : set_.ide_domains) {
        if (seen.insert(d).second) out.push_back(d);
    }
    for (const auto& d : set_.ai_domains) {
        if (seen.insert(d).second) out.push_back(d);
    }
    return out;
}

PortProbe RuleEngine::probe() const { std::shared_lock lk(mu_); return set_.probe; }
ArtifactScanSpec RuleEngine::artifact_scan() const { std::shared_lock lk(mu_); return set_.artifact_scan; }
std::vector<ArtifactRule> RuleEngine::artifact_rules() const { std::shared_lock lk(mu_); return set_.artifacts; }

}  // namespace xg
