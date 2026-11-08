#include "xg/isolation.hpp"

#include <algorithm>
#include <utility>

namespace xg::isolation {

std::optional<uint32_t> parse_ipv4(const std::string& s) {
    uint32_t parts[4] = {0, 0, 0, 0};
    int idx = 0;
    size_t digits = 0;
    uint32_t cur = 0;
    for (char c : s) {
        if (c >= '0' && c <= '9') {
            cur = cur * 10 + uint32_t(c - '0');
            if (cur > 255 || ++digits > 3) return std::nullopt;
        } else if (c == '.') {
            if (digits == 0 || idx >= 3) return std::nullopt;
            parts[idx++] = cur;
            cur = 0;
            digits = 0;
        } else {
            return std::nullopt;
        }
    }
    if (digits == 0 || idx != 3) return std::nullopt;
    parts[3] = cur;
    return (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8) | parts[3];
}

std::string ipv4_to_string(uint32_t ip) {
    return std::to_string((ip >> 24) & 0xFF) + "." + std::to_string((ip >> 16) & 0xFF) + "." +
           std::to_string((ip >> 8) & 0xFF) + "." + std::to_string(ip & 0xFF);
}

namespace {
constexpr uint32_t ip(uint32_t a, uint32_t b, uint32_t c, uint32_t d) { return (a << 24) | (b << 16) | (c << 8) | d; }
}  // namespace

std::vector<std::string> blocked_ipv4_ranges(const std::vector<std::string>& exempt_ips) {
    // Everything except: 0/8, 10/8, 127/8, 169.254/16, 172.16/12, 192.168/16 and 224.0.0.0+ (multicast/broadcast).
    std::vector<std::pair<uint32_t, uint32_t>> ranges = {
        {ip(1, 0, 0, 0), ip(9, 255, 255, 255)},
        {ip(11, 0, 0, 0), ip(126, 255, 255, 255)},
        {ip(128, 0, 0, 0), ip(169, 253, 255, 255)},
        {ip(169, 255, 0, 0), ip(172, 15, 255, 255)},
        {ip(172, 32, 0, 0), ip(192, 167, 255, 255)},
        {ip(192, 169, 0, 0), ip(223, 255, 255, 255)},
    };
    std::vector<uint32_t> exempt;
    for (const auto& s : exempt_ips)
        if (auto v = parse_ipv4(s)) exempt.push_back(*v);
    std::sort(exempt.begin(), exempt.end());
    exempt.erase(std::unique(exempt.begin(), exempt.end()), exempt.end());

    for (uint32_t x : exempt) {
        std::vector<std::pair<uint32_t, uint32_t>> next;
        for (const auto& [lo, hi] : ranges) {
            if (x < lo || x > hi) {
                next.emplace_back(lo, hi);
                continue;
            }
            if (x > lo) next.emplace_back(lo, x - 1);
            if (x < hi) next.emplace_back(x + 1, hi);
        }
        ranges = std::move(next);
    }

    std::vector<std::string> out;
    for (const auto& [lo, hi] : ranges) out.push_back(ipv4_to_string(lo) + "-" + ipv4_to_string(hi));
    return out;
}

}  // namespace xg::isolation
