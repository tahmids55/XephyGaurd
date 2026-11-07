#include "xg/tls.hpp"

#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>

#include <cstring>

#include "xg/util.hpp"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using sock_t = SOCKET;
#define XG_BAD_SOCK INVALID_SOCKET
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
using sock_t = int;
#define XG_BAD_SOCK (-1)
#endif

namespace xg::net {

bool net_init() {
#ifdef _WIN32
    static bool done = [] {
        WSADATA w;
        return WSAStartup(MAKEWORD(2, 2), &w) == 0;
    }();
    return done;
#else
    return true;
#endif
}

void socket_close(long long s) {
    if (s < 0) return;
#ifdef _WIN32
    closesocket(static_cast<SOCKET>(s));
#else
    ::close(int(s));
#endif
}

static void set_timeouts(sock_t s, int ms) {
#ifdef _WIN32
    DWORD t = DWORD(ms);
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&t), sizeof t);
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&t), sizeof t);
#else
    timeval tv{ms / 1000, (ms % 1000) * 1000};
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
#endif
}

long long tcp_connect(const std::string& host, uint16_t port, int timeout_ms, std::string* err) {
    net_init();
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    const std::string ps = std::to_string(port);
    if (getaddrinfo(host.c_str(), ps.c_str(), &hints, &res) != 0 || !res) {
        if (err) *err = "cannot resolve " + host;
        return -1;
    }
    sock_t s = XG_BAD_SOCK;
    for (addrinfo* a = res; a; a = a->ai_next) {
        s = ::socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (s == XG_BAD_SOCK) continue;
#ifdef _WIN32
        u_long nb = 1;
        ioctlsocket(s, FIONBIO, &nb);
        ::connect(s, a->ai_addr, int(a->ai_addrlen));
        fd_set wf; FD_ZERO(&wf); FD_SET(s, &wf);
        fd_set ef; FD_ZERO(&ef); FD_SET(s, &ef);
        timeval tv{timeout_ms / 1000, (timeout_ms % 1000) * 1000};
        int r = select(0, nullptr, &wf, &ef, &tv);
        bool ok = r > 0 && FD_ISSET(s, &wf);
        nb = 0;
        ioctlsocket(s, FIONBIO, &nb);
#else
        int flags = fcntl(s, F_GETFL, 0);
        fcntl(s, F_SETFL, flags | O_NONBLOCK);
        int r = ::connect(s, a->ai_addr, a->ai_addrlen);
        bool ok = r == 0;
        if (!ok && errno == EINPROGRESS) {
            pollfd pfd{s, POLLOUT, 0};
            if (::poll(&pfd, 1, timeout_ms) == 1) {
                int so = 0;
                socklen_t len = sizeof so;
                getsockopt(s, SOL_SOCKET, SO_ERROR, &so, &len);
                ok = so == 0;
            }
        }
        fcntl(s, F_SETFL, flags);
#endif
        if (ok) break;
        socket_close(static_cast<long long>(s));
        s = XG_BAD_SOCK;
    }
    freeaddrinfo(res);
    if (s == XG_BAD_SOCK) {
        if (err) *err = "cannot connect to " + host + ":" + ps;
        return -1;
    }
    int one = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one), sizeof one);
    set_timeouts(s, timeout_ms);
    return static_cast<long long>(s);
}

struct TlsConnection::Impl {
    SSL_CTX* ctx = nullptr;
    SSL* ssl = nullptr;
    long long sock = -1;
    ~Impl() { reset(); }
    void reset() {
        if (ssl) { SSL_shutdown(ssl); SSL_free(ssl); ssl = nullptr; }
        if (ctx) { SSL_CTX_free(ctx); ctx = nullptr; }
        if (sock >= 0) { socket_close(sock); sock = -1; }
    }
};

TlsConnection::TlsConnection() : impl_(std::make_unique<Impl>()) {}
TlsConnection::~TlsConnection() = default;

static std::string ssl_errors() {
    std::string out;
    unsigned long e;
    char buf[256];
    while ((e = ERR_get_error()) != 0) {
        ERR_error_string_n(e, buf, sizeof buf);
        if (!out.empty()) out += "; ";
        out += buf;
    }
    return out.empty() ? "unknown TLS error" : out;
}

bool configure_verify_host(void* ssl_handle, const std::string& host, const TlsOptions& o) {
    if (!ssl_handle || !should_verify_peer(o)) return false;
    SSL* ssl = static_cast<SSL*>(ssl_handle);
    const std::string& vhost = o.expected_host.empty() ? host : o.expected_host;
    if (vhost.empty()) return false;
    X509_VERIFY_PARAM* param = SSL_get0_param(ssl);
    in6_addr a6{};
    in_addr a4{};
    if (inet_pton(AF_INET, vhost.c_str(), &a4) == 1 || inet_pton(AF_INET6, vhost.c_str(), &a6) == 1)
        return X509_VERIFY_PARAM_set1_ip_asc(param, vhost.c_str()) == 1;
    return X509_VERIFY_PARAM_set1_host(param, vhost.c_str(), 0) == 1;
}

bool TlsConnection::connect(const std::string& host, uint16_t port, const TlsOptions& o, std::string* err) {
    impl_->reset();
    impl_->ctx = SSL_CTX_new(TLS_client_method());
    if (!impl_->ctx) { if (err) *err = ssl_errors(); return false; }
    SSL_CTX_set_min_proto_version(impl_->ctx, o.tls13_only ? TLS1_3_VERSION : TLS1_2_VERSION);
    if (o.insecure_skip_verify) {
        SSL_CTX_set_verify(impl_->ctx, SSL_VERIFY_NONE, nullptr);
    } else {
        SSL_CTX_set_verify(impl_->ctx, SSL_VERIFY_PEER, nullptr);
        if (!o.ca_file.empty()) {
            if (SSL_CTX_load_verify_locations(impl_->ctx, o.ca_file.c_str(), nullptr) != 1) {
                if (err) *err = "cannot load CA " + o.ca_file + ": " + ssl_errors();
                return false;
            }
        } else {
            SSL_CTX_set_default_verify_paths(impl_->ctx);
        }
    }
    if (!o.cert_file.empty()) {
        if (SSL_CTX_use_certificate_chain_file(impl_->ctx, o.cert_file.c_str()) != 1 ||
            SSL_CTX_use_PrivateKey_file(impl_->ctx, o.key_file.c_str(), SSL_FILETYPE_PEM) != 1) {
            if (err) *err = "cannot load client certificate: " + ssl_errors();
            return false;
        }
    }
    impl_->sock = tcp_connect(host, port, o.timeout_ms, err);
    if (impl_->sock < 0) return false;
    impl_->ssl = SSL_new(impl_->ctx);
    SSL_set_fd(impl_->ssl, int(impl_->sock));
    const std::string& vhost = o.expected_host.empty() ? host : o.expected_host;
    if (!o.insecure_skip_verify) {
        configure_verify_host(impl_->ssl, host, o);
    }
    SSL_set_tlsext_host_name(impl_->ssl, vhost.c_str());
    if (SSL_connect(impl_->ssl) != 1) {
        long vr = SSL_get_verify_result(impl_->ssl);
        if (err) *err = "TLS handshake failed: " + ssl_errors() +
                        (vr != X509_V_OK ? std::string(" (verify: ") + X509_verify_cert_error_string(vr) + ")" : "");
        impl_->reset();
        return false;
    }
    if (!o.pin_sha256.empty()) {
        X509* peer = SSL_get1_peer_certificate(impl_->ssl);
        unsigned char md[EVP_MAX_MD_SIZE];
        unsigned int len = 0;
        bool match = peer && X509_digest(peer, EVP_sha256(), md, &len) == 1 && to_lower(o.pin_sha256) == hex_encode(md, len);
        if (peer) X509_free(peer);
        if (!match) {
            if (err) *err = "server certificate does not match pinned fingerprint";
            impl_->reset();
            return false;
        }
    }
    return true;
}

bool TlsConnection::write_all(const void* data, size_t len) {
    if (!impl_->ssl) return false;
    const char* p = static_cast<const char*>(data);
    while (len > 0) {
        size_t written = 0;
        if (SSL_write_ex(impl_->ssl, p, len, &written) != 1) return false;
        p += written;
        len -= written;
    }
    return true;
}

long TlsConnection::read_some(void* buf, size_t len) {
    if (!impl_->ssl) return -1;
    size_t n = 0;
    int r = SSL_read_ex(impl_->ssl, buf, len, &n);
    if (r == 1) return long(n);
    int e = SSL_get_error(impl_->ssl, r);
    if (e == SSL_ERROR_ZERO_RETURN) return 0;
    return -1;
}

void TlsConnection::set_read_timeout(int ms) {
    if (impl_->sock >= 0) set_timeouts(static_cast<sock_t>(impl_->sock), ms);
}

bool TlsConnection::wait_readable(int ms) {
    if (!impl_->ssl) return false;
    if (SSL_pending(impl_->ssl) > 0) return true;
#ifdef _WIN32
    WSAPOLLFD pfd{static_cast<SOCKET>(impl_->sock), POLLRDNORM, 0};
    return WSAPoll(&pfd, 1, ms) > 0;
#else
    pollfd pfd{int(impl_->sock), POLLIN, 0};
    return ::poll(&pfd, 1, ms) > 0;
#endif
}

void TlsConnection::close() { impl_->reset(); }
bool TlsConnection::is_open() const { return impl_->ssl != nullptr; }

}  // namespace xg::net
