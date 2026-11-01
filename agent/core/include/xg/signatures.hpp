#pragma once
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

    static SignatureSet from_json(const json& j, std::string* err = nullptr);
    size_t rule_count() const;
};

}  // namespace xg
