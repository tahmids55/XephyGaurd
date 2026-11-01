#include <catch2/catch_test_macros.hpp>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

#include "xg/crypto.hpp"
#include "xg/discovery.hpp"
#include "xg/http.hpp"
#include "xg/pusher.hpp"
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
}
