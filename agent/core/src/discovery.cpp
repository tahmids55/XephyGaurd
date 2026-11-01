#include "xg/discovery.hpp"

#include <cstring>

#include "xg/event.hpp"
#include "xg/tls.hpp"
#include "xg/util.hpp"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace xg::net {

std::optional<DiscoveryResult> parse_discovery_reply(const std::string& payload, const std::string& from) {
    auto j = json::parse(payload, nullptr, false);
    if (j.is_discarded() || !j.is_object() || j.value("service", "") != "xephyguard-ccc") return std::nullopt;
    DiscoveryResult r;
    r.host = j.value("host", from);
    if (r.host.empty()) r.host = from;
    r.api_port = uint16_t(j.value("api_port", 8443));
    r.ca_fingerprint = to_lower(j.value("ca_fingerprint", ""));
    r.lab_name = j.value("lab", "");
    return r;
}

std::optional<DiscoveryResult> discover_ccc(uint16_t port, int timeout_ms) {
    net_init();
#ifdef _WIN32
    SOCKET s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) return std::nullopt;
    BOOL yes = TRUE;
#else
    int s = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) return std::nullopt;
    int yes = 1;
#endif
    setsockopt(s, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&yes), sizeof yes);
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port = htons(port);
    to.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    const std::string msg = R"({"probe":"XEPHYGUARD_DISCOVER","v":1})";
    std::optional<DiscoveryResult> out;
    const int64_t deadline = steady_ms() + timeout_ms;
    int64_t next_send = 0;
    while (steady_ms() < deadline && !out) {
        if (steady_ms() >= next_send) {
            sendto(s, msg.data(), int(msg.size()), 0, reinterpret_cast<sockaddr*>(&to), sizeof to);
            next_send = steady_ms() + 1000;
        }
#ifdef _WIN32
        WSAPOLLFD pfd{s, POLLRDNORM, 0};
        int pr = WSAPoll(&pfd, 1, 250);
#else
        pollfd pfd{s, POLLIN, 0};
        int pr = ::poll(&pfd, 1, 250);
#endif
        if (pr <= 0) continue;
        char buf[2048];
        sockaddr_in from{};
        socklen_t fl = sizeof from;
        int n = int(recvfrom(s, buf, sizeof buf - 1, 0, reinterpret_cast<sockaddr*>(&from), &fl));
        if (n <= 0) continue;
        char addr[INET_ADDRSTRLEN] = {0};
        inet_ntop(AF_INET, &from.sin_addr, addr, sizeof addr);
        out = parse_discovery_reply(std::string(buf, size_t(n)), addr);
    }
    socket_close(static_cast<long long>(s));
    return out;
}

}  // namespace xg::net
