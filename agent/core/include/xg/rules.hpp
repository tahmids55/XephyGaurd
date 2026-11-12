#pragma once
#include <memory>
#include <optional>
#include <set>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "xg/signatures.hpp"

namespace xg {

struct RuleHit {
    std::string rule_id;
    std::string category;
    std::string description;
    Severity severity = Severity::medium;
};

/// Matches observations against the current signature set. Thread-safe: signatures can be
/// swapped at runtime (hot reload) while monitors are matching.
class RuleEngine {
public:
    RuleEngine() = default;
    explicit RuleEngine(SignatureSet set) { load(std::move(set)); }

    void load(SignatureSet set);
    void set_disabled(std::set<std::string> ids);
    std::string version() const;

    std::vector<RuleHit> match_process(const ProcessInfo& p) const;
    std::optional<RuleHit> match_extension(const ExtensionInfo& e) const;
    /// Port rules that match on the port alone (probe_required=false) or only after a successful probe.
    std::optional<RuleHit> match_port(uint16_t port, bool probe_required = false) const;
    std::optional<RuleHit> match_artifact_file(const std::string& path, uint64_t size_bytes) const;
    /// Returns rules whose `dirs` entry (expanded with `home`) equals or contains `dir`.
    std::optional<RuleHit> match_artifact_dir(const std::string& dir, const std::string& home) const;
    std::optional<RuleHit> classify_adapter(const AdapterInfo& a) const;
    std::optional<RuleHit> second_route_rule() const;
    std::optional<RuleHit> match_device(const DeviceInfo& d) const;
    std::optional<RuleHit> match_ai_domain(const std::string& domain) const;
    std::vector<std::string> ai_domains() const;

    PortProbe probe() const;
    ArtifactScanSpec artifact_scan() const;
    std::vector<ArtifactRule> artifact_rules() const;

private:
    bool enabled(const std::string& id) const { return !disabled_.contains(id); }
    mutable std::shared_mutex mu_;
    SignatureSet set_;
    std::set<std::string> disabled_;
    std::unordered_map<std::string, std::vector<size_t>> exe_index_;   // normalized exe -> process rule idx
};

}  // namespace xg
