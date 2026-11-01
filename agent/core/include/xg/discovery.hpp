#pragma once
#include <cstdint>
#include <optional>
#include <string>

namespace xg::net {

struct DiscoveryResult {
    std::string host;           // address the CCC answered from (or advertised)
    uint16_t api_port = 8443;
    std::string ca_fingerprint;
    std::string lab_name;
};

/// Broadcasts "XEPHYGUARD_DISCOVER v1" on UDP `port` and waits for a JSON reply.
std::optional<DiscoveryResult> discover_ccc(uint16_t port, int timeout_ms);
std::optional<DiscoveryResult> parse_discovery_reply(const std::string& payload, const std::string& from_addr);

}  // namespace xg::net
