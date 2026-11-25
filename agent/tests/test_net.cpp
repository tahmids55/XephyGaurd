#include <catch2/catch_test_macros.hpp>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <unistd.h>
#include <atomic>
#include <chrono>
#include <thread>
#endif

#include "fakes.hpp"
#include "xg/agent.hpp"
#include "xg/crypto.hpp"
#include "xg/discovery.hpp"
#include "xg/http.hpp"
#include "xg/pusher.hpp"
#include "xg/util.hpp"
#include "xg/websocket.hpp"

using namespace xg;

TEST_CASE("websocket frame codec") {
    const uint8_t mk[4] = {1, 2, 3, 4};
    for (size_t len : {size_t(5), size_t(200), size_t(70000)}) {
        std::string payload(len, 'x');
        auto f = net::ws_encode_frame(net::WsOpcode::text, payload, true, mk);
        size_t consumed = 0;
        bool err = false;
        auto fr = net::ws_decode_frame(f, &consumed, &err);
        REQUIRE(fr);
        CHECK(consumed == f.size());
        CHECK(fr->payload == payload);
        CHECK(fr->fin);
        // Truncated input asks for more bytes instead of failing.
        CHECK_FALSE(net::ws_decode_frame(f.substr(0, f.size() - 1), &consumed, &err));
        CHECK_FALSE(err);
    }
    // RFC 6455 section 1.3 example
    CHECK(net::ws_accept_key("dGhlIHNhbXBsZSBub25jZQ==") == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
}

TEST_CASE("http response parsing") {
    net::HttpResponse r;
    size_t used = 0;
    std::string raw = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 2\r\n\r\n{}EXTRA";
    REQUIRE(net::parse_http_response(raw, r, &used));
    CHECK(r.status == 200);
    CHECK(r.body == "{}");
    CHECK(used == raw.size() - 5);
    std::string chunked = "HTTP/1.1 201 Created\r\nTransfer-Encoding: chunked\r\n\r\n4\r\nWiki\r\n5\r\npedia\r\n0\r\n\r\n";
    REQUIRE(net::parse_http_response(chunked, r, &used));
    CHECK(r.body == "Wikipedia");
    CHECK(r.status == 201);
    CHECK_FALSE(net::parse_http_response("HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\nabc", r, &used));
}

TEST_CASE("http parsing rejects hostile responses without throwing or looping") {
    net::HttpResponse r;
    size_t used = 0;
    auto bad = [&](const std::string& raw) {
        bool malformed = false;
        bool ok = true;
        REQUIRE_NOTHROW(ok = net::parse_http_response(raw, r, &used, &malformed));
        CHECK_FALSE(ok);
        CHECK(malformed);
    };
    // Huge hex chunk size that would wrap pos + len + 2 (I-AGENT-4).
    bad("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nffffffffffffffff\r\nabc\r\n0\r\n\r\n");
    bad("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nfffffffffffffffffff\r\nabc\r\n");
    // Non-numeric lengths (I-AGENT-11: std::stoul used to throw).
    bad("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nzz\r\nabc\r\n");
    bad("HTTP/1.1 200 OK\r\nContent-Length: abc\r\n\r\n{}");
    bad("HTTP/1.1 200 OK\r\nContent-Length: -1\r\n\r\n{}");
    bad("HTTP/1.1 200 OK\r\nContent-Length: 99999999999999999999999\r\n\r\n{}");
    bad("HTTP/1.1 200 OK\r\nContent-Length: 999999999999\r\n\r\n{}");
    // Chunk data not followed by CRLF.
    bad("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n2\r\nabcd\r\n0\r\n\r\n");
    // Bad status lines.
    bad("HTTP/1.1 abc OK\r\nContent-Length: 0\r\n\r\n");
    bad("garbage line\r\nContent-Length: 0\r\n\r\n");
    // Header block that never ends.
    bad("HTTP/1.1 200 OK\r\nX: " + std::string(net::kMaxHeaderBytes + 10, 'a'));

    // Chunk extensions are accepted; an incomplete chunk asks for more data, not an error.
    bool malformed = true;
    REQUIRE(net::parse_http_response("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n3;x=1\r\nabc\r\n0\r\n\r\n", r,
                                     &used, &malformed));
    CHECK(r.body == "abc");
    CHECK_FALSE(net::parse_http_response("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n10\r\nabc", r, &used,
                                         &malformed));
    CHECK_FALSE(malformed);
}

TEST_CASE("crypto helpers") {
    CHECK(crypto::sha256_hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(crypto::base64_decode(crypto::base64_encode("hello world!")).value() == "hello world!");
    CHECK(crypto::base64_decode(crypto::base64_encode("ab")).value() == "ab");
    CHECK(net::pusher_auth_signature("key", "secret", "1234.5678", "private-pc.1") ==
          "key:" + crypto::hmac_sha256_hex("secret", "1234.5678:private-pc.1"));
}

TEST_CASE("signature bundle verification with a CA certificate") {
    // Build a throwaway self-signed CA and sign a bundle, as the CCC does with openssl_sign().
    EVP_PKEY* key = EVP_EC_gen("P-256");
    X509* cert = X509_new();
    X509_set_version(cert, 2);
    ASN1_INTEGER_set(X509_get_serialNumber(cert), 1);
    X509_gmtime_adj(X509_getm_notBefore(cert), 0);
    X509_gmtime_adj(X509_getm_notAfter(cert), 3600);
    X509_set_pubkey(cert, key);
    X509_NAME_add_entry_by_txt(X509_get_subject_name(cert), "CN", MBSTRING_ASC,
                               reinterpret_cast<const unsigned char*>("Test CA"), -1, -1, 0);
    X509_set_issuer_name(cert, X509_get_subject_name(cert));
    X509_sign(cert, key, EVP_sha256());
    BIO* b = BIO_new(BIO_s_mem());
    PEM_write_bio_X509(b, cert);
    char* d = nullptr;
    long n = BIO_get_mem_data(b, &d);
    const std::string pem(d, size_t(n));
    BIO_free(b);

    const std::string bundle = R"({"version":"v1"})";
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    EVP_DigestSignInit(ctx, nullptr, EVP_sha256(), nullptr, key);
    size_t siglen = 0;
    EVP_DigestSign(ctx, nullptr, &siglen, reinterpret_cast<const unsigned char*>(bundle.data()), bundle.size());
    std::string sig(siglen, '\0');
    EVP_DigestSign(ctx, reinterpret_cast<unsigned char*>(sig.data()), &siglen,
                   reinterpret_cast<const unsigned char*>(bundle.data()), bundle.size());
    sig.resize(siglen);
    EVP_MD_CTX_free(ctx);

    CHECK(crypto::verify_signature(bundle, crypto::base64_encode(sig), pem));
    CHECK_FALSE(crypto::verify_signature(bundle + " ", crypto::base64_encode(sig), pem));
    CHECK(crypto::cert_fingerprint(pem)->size() == 64);
    CHECK(crypto::cert_seconds_left(pem).value() > 3000);
    X509_free(cert);
    EVP_PKEY_free(key);
}

TEST_CASE("discovery reply parsing") {
    auto r = net::parse_discovery_reply(
        R"({"service":"xephyguard-ccc","host":"192.168.10.2","api_port":8443,"ca_fingerprint":"AB12","lab":"Lab 3"})",
        "10.0.0.1");
    REQUIRE(r);
    CHECK(r->host == "192.168.10.2");
    CHECK(r->ca_fingerprint == "ab12");
    CHECK_FALSE(net::parse_discovery_reply(R"({"service":"other"})", "x"));
    // Wrong types from an unauthenticated LAN reply fall back instead of throwing.
    std::optional<net::DiscoveryResult> w;
    REQUIRE_NOTHROW(w = net::parse_discovery_reply(
                        R"({"service":"xephyguard-ccc","host":5,"api_port":"x","ca_fingerprint":[],"lab":{}})", "10.0.0.9"));
    REQUIRE(w);
    CHECK(w->host == "10.0.0.9");
    CHECK(w->api_port == 8443);
}

#ifndef _WIN32
namespace {
// Local TCP server on 127.0.0.1 that runs `serve(fd)` for one connection.
struct OneShotServer {
    int lfd = -1;
    uint16_t port = 0;
    std::thread th;
    template <class F>
    explicit OneShotServer(F serve) {
        lfd = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        ::bind(lfd, reinterpret_cast<sockaddr*>(&a), sizeof a);
        ::listen(lfd, 1);
        socklen_t al = sizeof a;
        ::getsockname(lfd, reinterpret_cast<sockaddr*>(&a), &al);
        port = ntohs(a.sin_port);
        th = std::thread([this, serve] {
            int c = ::accept(lfd, nullptr, nullptr);
            if (c < 0) return;
            char b[1024];
            (void)::recv(c, b, sizeof b, 0);
            serve(c);
            ::close(c);
        });
    }
    ~OneShotServer() {
        ::shutdown(lfd, SHUT_RDWR);
        th.join();
        ::close(lfd);
    }
};
}  // namespace

TEST_CASE("plain GET probe has a total deadline and a size cap") {
    SECTION("drip-feeding server is cut off after the total deadline (I-AGENT-12)") {
        std::atomic<bool> done{false};
        OneShotServer srv([&](int c) {
            const char hdr[] = "HTTP/1.1 200 OK\r\nX: ";
            ::send(c, hdr, sizeof hdr - 1, MSG_NOSIGNAL);
            for (int i = 0; i < 60 && !done; ++i) {
                ::send(c, "a", 1, MSG_NOSIGNAL);
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        });
        const auto t0 = std::chrono::steady_clock::now();
        auto r = net::http_get_plain("127.0.0.1", srv.port, "/v1/models", 400, net::kProbeLimits);
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        done = true;
        CHECK(r.status == 0);
        CHECK(ms < 1500);
    }
    SECTION("oversized response is refused") {
        OneShotServer srv([](int c) {
            std::string big = "HTTP/1.1 200 OK\r\nContent-Length: 200000\r\n\r\n" + std::string(200000, 'x');
            ::send(c, big.data(), big.size(), MSG_NOSIGNAL);
        });
        auto r = net::http_get_plain("127.0.0.1", srv.port, "/", 400, net::kProbeLimits);
        CHECK(r.status == 0);
        CHECK(r.error == "response too large");
    }
    SECTION("normal small reply still parses") {
        OneShotServer srv([](int c) {
            const char ok[] = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 2\r\n\r\n{}";
            ::send(c, ok, sizeof ok - 1, MSG_NOSIGNAL);
        });
        auto r = net::http_get_plain("127.0.0.1", srv.port, "/", 400, net::kProbeLimits);
        CHECK(r.status == 200);
        CHECK(r.body == "{}");
    }
}

namespace {
std::string self_signed_ca_pem() {
    EVP_PKEY* key = EVP_EC_gen("P-256");
    X509* cert = X509_new();
    X509_set_version(cert, 2);
    ASN1_INTEGER_set(X509_get_serialNumber(cert), 1);
    X509_gmtime_adj(X509_getm_notBefore(cert), 0);
    X509_gmtime_adj(X509_getm_notAfter(cert), 3600);
    X509_set_pubkey(cert, key);
    X509_NAME_add_entry_by_txt(X509_get_subject_name(cert), "CN", MBSTRING_ASC,
                               reinterpret_cast<const unsigned char*>("Rogue CA"), -1, -1, 0);
    X509_set_issuer_name(cert, X509_get_subject_name(cert));
    X509_sign(cert, key, EVP_sha256());
    BIO* b = BIO_new(BIO_s_mem());
    PEM_write_bio_X509(b, cert);
    char* d = nullptr;
    long n = BIO_get_mem_data(b, &d);
    std::string pem(d, size_t(n));
    BIO_free(b);
    X509_free(cert);
    EVP_PKEY_free(key);
    return pem;
}
}  // namespace

TEST_CASE("enrollment never trusts a CA fingerprint from discovery (I-AGENT-3)") {
    // A discovery reply only sets the address.
    Config cfg = Config::defaults();
    net::DiscoveryResult d;
    d.host = "10.9.9.9";
    d.api_port = 9443;
    d.ca_fingerprint = "deadbeef";
    apply_discovery(cfg, d);
    CHECK(cfg.ccc_host == "10.9.9.9");
    CHECK(cfg.api_port == 9443);
    CHECK(cfg.ca_fingerprint.empty());

    // Without --ca-fingerprint or --trust-on-first-use, a downloaded CA is refused before anything is trusted.
    const auto dir = std::filesystem::temp_directory_path() / ("xge-" + random_hex(4));
    std::filesystem::create_directories(dir);
    const std::string pem = self_signed_ca_pem();
    OneShotServer srv([&](int c) {
        const std::string r = "HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(pem.size()) + "\r\n\r\n" + pem;
        ::send(c, r.data(), r.size(), MSG_NOSIGNAL);
    });
    cfg.ccc_host = "127.0.0.1";
    cfg.http_port = srv.port;
    cfg.ca_file = (dir / "ca.crt").string();
    cfg.data_dir = dir.string();
    test::FakeSystem sys;
    EnrollOptions o;
    o.key = "XG-TEST";
    o.wait = false;
    CHECK(enroll(cfg, (dir / "agent.toml").string(), o, sys) == 5);
    CHECK_FALSE(std::filesystem::exists(cfg.ca_file));
    std::filesystem::remove_all(dir);
}

TEST_CASE("WebSocketClient supports plain TCP binary duplex frames and extra headers") {
    int lfd = ::socket(AF_INET, SOCK_STREAM, 0);
    REQUIRE(lfd >= 0);
    int yes = 1;
    ::setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    REQUIRE(::bind(lfd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) == 0);
    REQUIRE(::listen(lfd, 1) == 0);
    socklen_t alen = sizeof addr;
    REQUIRE(::getsockname(lfd, reinterpret_cast<sockaddr*>(&addr), &alen) == 0);
    const uint16_t port = ntohs(addr.sin_port);

    std::string seen_hdr;
    std::string seen_bin;
    std::thread srv([&]() {
        int cfd = ::accept(lfd, nullptr, nullptr);
        if (cfd < 0) return;
        std::string req;
        char buf[2048];
        while (req.find("\r\n\r\n") == std::string::npos) {
            ssize_t n = ::recv(cfd, buf, sizeof buf, 0);
            if (n <= 0) { ::close(cfd); return; }
            req.append(buf, size_t(n));
        }
        seen_hdr = req;
        auto pos = req.find("Sec-WebSocket-Key:");
        auto eol = req.find("\r\n", pos);
        std::string key = req.substr(pos + 18, eol - (pos + 18));
        while (!key.empty() && key.front() == ' ') key.erase(key.begin());
        std::string resp = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " +
                           net::ws_accept_key(key) + "\r\n\r\n";
        ::send(cfd, resp.data(), resp.size(), 0);

        const uint8_t nomask[4] = {0, 0, 0, 0};
        auto out_frame = net::ws_encode_frame(net::WsOpcode::binary, std::string("\x47\x58\x01\x04", 4), false, nomask);
        ::send(cfd, out_frame.data(), out_frame.size(), 0);

        std::string in_buf;
        for (int i = 0; i < 20; ++i) {
            ssize_t n = ::recv(cfd, buf, sizeof buf, 0);
            if (n <= 0) break;
            in_buf.append(buf, size_t(n));
            size_t consumed = 0;
            bool perr = false;
            if (auto fr = net::ws_decode_frame(in_buf, &consumed, &perr)) {
                if (fr->opcode == net::WsOpcode::binary) {
                    seen_bin = fr->payload;
                    break;
                }
                in_buf.erase(0, consumed);
            }
        }
        ::close(cfd);
    });

    net::WebSocketClient ws;
    net::TlsOptions opts;
    opts.timeout_ms = 2000;
    std::string err;
    REQUIRE(ws.connect("127.0.0.1", port, "/agent?stream_id=7", opts, &err,
                       {{"X-Agent-Token", "tok-xyz"}}, false));
    auto rx = ws.receive(1000);
    REQUIRE(rx.has_value());
    CHECK(*rx == std::string("\x47\x58\x01\x04", 4));
    const std::string payload = std::string("\x01\x02\x03\x04", 4);
    REQUIRE(ws.send_binary(payload.data(), payload.size()));
    ws.close();
    srv.join();
    ::close(lfd);

    CHECK(seen_hdr.find("X-Agent-Token: tok-xyz\r\n") != std::string::npos);
    CHECK(seen_bin == payload);
}
TEST_CASE("an interruptible WebSocket receive blocks without polling and interrupt() ends it") {
    int lfd = ::socket(AF_INET, SOCK_STREAM, 0);
    REQUIRE(lfd >= 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    REQUIRE(::bind(lfd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) == 0);
    REQUIRE(::listen(lfd, 1) == 0);
    socklen_t alen = sizeof addr;
    REQUIRE(::getsockname(lfd, reinterpret_cast<sockaddr*>(&addr), &alen) == 0);
    const uint16_t port = ntohs(addr.sin_port);

    std::thread srv([&]() {  // completes the handshake, then stays silent until the client goes away
        int cfd = ::accept(lfd, nullptr, nullptr);
        if (cfd < 0) return;
        std::string req;
        char buf[2048];
        while (req.find("\r\n\r\n") == std::string::npos) {
            ssize_t n = ::recv(cfd, buf, sizeof buf, 0);
            if (n <= 0) { ::close(cfd); return; }
            req.append(buf, size_t(n));
        }
        auto pos = req.find("Sec-WebSocket-Key:");
        auto eol = req.find("\r\n", pos);
        std::string key = req.substr(pos + 18, eol - (pos + 18));
        while (!key.empty() && key.front() == ' ') key.erase(key.begin());
        std::string resp = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " +
                           net::ws_accept_key(key) + "\r\n\r\n";
        ::send(cfd, resp.data(), resp.size(), 0);
        while (::recv(cfd, buf, sizeof buf, 0) > 0) {}
        ::close(cfd);
    });

    net::WebSocketClient ws;
    net::TlsOptions opts;
    opts.timeout_ms = 2000;
    std::string err;
    REQUIRE(ws.connect("127.0.0.1", port, "/x", opts, &err, {}, false));

    long wakeups = -1;
    std::optional<std::string> rx;
    std::chrono::milliseconds took{0};
    std::thread rcv([&]() {
        rusage a{}, b{};
        getrusage(RUSAGE_THREAD, &a);
        const auto t0 = std::chrono::steady_clock::now();
        rx = ws.receive(10000, true);
        took = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0);
        getrusage(RUSAGE_THREAD, &b);
        wakeups = b.ru_nvcsw - a.ru_nvcsw;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    ws.interrupt();
    rcv.join();
    ws.close();
    srv.join();
    ::close(lfd);

    CHECK_FALSE(rx.has_value());
    CHECK(took.count() >= 400);   // it waited for the interrupt, it did not time out or spin out early
    CHECK(took.count() < 2000);   // and the interrupt ended it at once
    CHECK(wakeups <= 3);          // one blocking poll; 50 ms slices would show about 10 here
    CHECK_FALSE(ws.open());
}

#endif
