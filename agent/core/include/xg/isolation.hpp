#pragma once
// Network isolation helpers shared by the Windows (Firewall) and Linux (nftables) enforcers.
//
// Isolation = "no Internet, LAN still works": every outbound packet to a public address is dropped,
// while private / link-local / multicast ranges stay reachable. The CCC (and anything else the caller
// exempts) is carved out of the blocked ranges so the agent can always be managed and restored.
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace xg::isolation {

std::optional<uint32_t> parse_ipv4(const std::string& s);
std::string ipv4_to_string(uint32_t ip);

/// Public (non-LAN) IPv4 space as "a.b.c.d-e.f.g.h" ranges, with every address in `exempt_ips` removed.
/// Non-IPv4 entries in `exempt_ips` are ignored.
std::vector<std::string> blocked_ipv4_ranges(const std::vector<std::string>& exempt_ips);

/// True for an address outside the LAN: the public IPv4 space above (IPv4-mapped IPv6 included) or IPv6
/// global unicast. False for private, loopback, link-local, multicast and anything that does not parse.
bool is_public_ip(const std::string& ip);

/// IPv6 global-unicast space (everything else - link-local, ULA, multicast - stays reachable).
inline const char* blocked_ipv6_prefix() { return "2000::/3"; }

}  // namespace xg::isolation
