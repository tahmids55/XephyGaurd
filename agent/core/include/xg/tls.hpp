#pragma once
#include <cstdint>
#include <memory>
#include <string>

namespace xg::net {

struct TlsOptions {
    std::string ca_file;          // trust anchor (CCC internal CA). Empty => system store
    std::string cert_file;        // client certificate (mTLS); empty during enrollment
    std::string key_file;
    std::string expected_host;    // name or IP checked against the server cert SAN
    std::string pin_sha256;       // optional pin of the server leaf certificate (hex)
    bool insecure_skip_verify = false;  // only for `--insecure` diagnostics, never default
    int timeout_ms = 10000;
    bool tls13_only = true;
};

/// Blocking TLS client socket (TLS 1.3, optional mutual auth) built on OpenSSL.
class TlsConnection {
public:
    TlsConnection();
    ~TlsConnection();
    TlsConnection(const TlsConnection&) = delete;
    TlsConnection& operator=(const TlsConnection&) = delete;

    bool connect(const std::string& host, uint16_t port, const TlsOptions& opts, std::string* err);
    bool write_all(const void* data, size_t len);
    /// Returns bytes read, 0 on orderly close, -1 on error/timeout.
    long read_some(void* buf, size_t len);
    void set_read_timeout(int ms);
    /// True if data is buffered or the socket becomes readable within `ms`.
    bool wait_readable(int ms);
    void close();
    bool is_open() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// Plain TCP connect helper shared with TLS (returns socket fd/handle or -1).
long long tcp_connect(const std::string& host, uint16_t port, int timeout_ms, std::string* err);
void socket_close(long long s);
bool net_init();  // WSAStartup on Windows, no-op elsewhere

}  // namespace xg::net
