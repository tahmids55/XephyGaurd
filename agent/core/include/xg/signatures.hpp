#pragma once
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "xg/event.hpp"
#include "xg/types.hpp"

namespace xg {

struct ProcessRule {
    std::string id, category, description;
    Severity severity = Severity::medium;
    std::vector<std::string> exe;              // normalized (lowercase, no .exe)
    std::vector<std::string> exe_prefix;
    std::vector<std::string> interpreters;
    std::vector<std::string> cmdline_contains;
};

struct ExtensionRule {
    std::string id, ide, description;
    Severity severity = Severity::high;
    std::vector<std::string> ext_ids;          // lowercase
    std::vector<std::string> domains;          // backend host suffixes proving active use (lowercase)
    std::map<std::string, std::vector<std::string>> id_domains;  // per ext id, overrides `domains`
};

struct PortRule {
    std::string id, description;
    Severity severity = Severity::high;
    uint16_t port = 0;
    bool requires_probe = false;   // common dev port: only report if the LLM-API probe answers
};

struct PortProbe {
    bool enabled = true;
    std::vector<std::string> paths;
    Severity severity = Severity::high;
    uint64_t min_rss_mb = 300;
};

struct ArtifactRule {
    std::string id, description;
    Severity severity = Severity::high;
    std::vector<std::string> globs;
    std::vector<std::string> dirs;
    uint64_t min_size_mb = 0;
};

struct ArtifactScanSpec {
    std::vector<std::string> roots;
    int max_depth = 6;
    uint64_t max_files = 200000;
    std::vector<std::string> skip_dirs;
};

struct AdapterRule {
    std::string id, kind, description;
    Severity severity = Severity::medium;
    std::vector<std::string> drivers;
    std::vector<std::string> name_prefixes;
};

struct DeviceRule {
    std::string id, description;
    Severity severity = Severity::low;
    std::vector<std::string> classes;
    std::vector<std::string> vendors;
};

/// Browser tab title patterns for one AI site (all lowercase; see data/signatures/web.yaml).
struct AiSite {
    std::string site;
    std::vector<std::string> equals, prefix, suffix, contains;
};

/// web.yaml `tunnel`: TCP ports that suggest a tunnel the proxy cannot see into (SSH, VPN over TCP, SOCKS, Tor).
struct TunnelHint {
    std::string id;  // rule id, e.g. net.tunnel_flow; empty = no tunnel detection
    Severity severity = Severity::medium;
    std::string description;
    std::vector<uint16_t> tcp_ports;
};

/// data/signatures/web.yaml: browser AI site detection and network hints.
struct WebHints {
    std::vector<AiSite> ai_sites;
    std::vector<std::string> browsers;                // normalized exe names
    std::vector<std::string> browser_title_suffixes;  // lowercase, e.g. " - google chrome"
    std::vector<std::string> telemetry_domains;       // substrings of hosts that never raise AI alerts
    std::map<std::string, std::string> process_domains;  // normalized exe -> domain when a flow has no SNI
    Severity ai_tab_severity = Severity::medium;  // title-only hits corroborate the proxy's decision (F76)
    TunnelHint tunnel;
    /// True if a TCP connection to ip:port should be flagged as a possible tunnel (public address, listed port).
    bool is_tunnel(const std::string& remote_ip, uint16_t remote_port) const;
};

/// Orders bundle versions "YYYY.MM.DD[-HHMMSS][-digest]" (CCC / compile_signatures.py) by date and time.
/// Returns <0, 0, >0 like strcmp, or nullopt when either version does not have that shape.
std::optional<int> compare_bundle_versions(const std::string& a, const std::string& b);

/// The signature bundle compiled by the CCC from data/signatures/*.yaml.
struct SignatureSet {
    std::string version;
    std::vector<ProcessRule> processes;
    std::vector<ExtensionRule> extensions;
    std::vector<PortRule> ports;
    PortProbe probe;
    std::vector<ArtifactRule> artifacts;
    ArtifactScanSpec artifact_scan;
    std::vector<AdapterRule> adapters;
    std::vector<DeviceRule> devices;
    std::vector<std::string> ai_domains;
    std::vector<std::string> ide_domains;
    WebHints web;

    static SignatureSet from_json(const json& j, std::string* err = nullptr);
    size_t rule_count() const;
};

}  // namespace xg
