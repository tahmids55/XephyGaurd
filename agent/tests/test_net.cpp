#include <catch2/catch_test_macros.hpp>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include "xg/crypto.hpp"
#include "xg/discovery.hpp"
#include "xg/http.hpp"
#include "xg/pusher.hpp"
#include "xg/tls.hpp"
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

TEST_CASE("TLS hostname/IP verification is always enabled even when ca_file is set (R9)") {
    net::TlsOptions opts;
    opts.ca_file = "/etc/xephyguard/ca.crt";
    opts.expected_host = "ccc.university.edu";
    opts.insecure_skip_verify = false;
    CHECK(net::should_verify_peer(opts));

    // Mint a self-signed certificate for "wrong.university.edu" and verify that
    // configure_verify_host enforces hostname matching even when ca_file is non-empty.
    EVP_PKEY* key = EVP_EC_gen("P-256");
    X509* cert = X509_new();
    X509_set_version(cert, 2);
    ASN1_INTEGER_set(X509_get_serialNumber(cert), 1);
    X509_gmtime_adj(X509_getm_notBefore(cert), 0);
    X509_gmtime_adj(X509_getm_notAfter(cert), 3600);
    X509_set_pubkey(cert, key);
    X509_NAME_add_entry_by_txt(X509_get_subject_name(cert), "CN", MBSTRING_ASC,
                               reinterpret_cast<const unsigned char*>("wrong.university.edu"), -1, -1, 0);
    X509_set_issuer_name(cert, X509_get_subject_name(cert));
    X509_sign(cert, key, EVP_sha256());

    X509_STORE* store = X509_STORE_new();
    X509_STORE_add_cert(store, cert);

    SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
    REQUIRE(ctx != nullptr);
    SSL* ssl = SSL_new(ctx);
    REQUIRE(ssl != nullptr);

    CHECK(net::configure_verify_host(ssl, "ccc.university.edu", opts));

    X509_STORE_CTX* sctx = X509_STORE_CTX_new();
    REQUIRE(X509_STORE_CTX_init(sctx, store, cert, nullptr) == 1);
    X509_VERIFY_PARAM_set1(X509_STORE_CTX_get0_param(sctx), SSL_get0_param(ssl));
    CHECK(X509_verify_cert(sctx) == 0);
    CHECK(X509_STORE_CTX_get_error(sctx) == X509_V_ERR_HOSTNAME_MISMATCH);
    X509_STORE_CTX_free(sctx);
    SSL_free(ssl);

    // Matching hostname succeeds even with ca_file set.
    SSL* ssl_match = SSL_new(ctx);
    opts.expected_host = "wrong.university.edu";
    CHECK(net::configure_verify_host(ssl_match, "wrong.university.edu", opts));
    X509_STORE_CTX* sctx_ok = X509_STORE_CTX_new();
    REQUIRE(X509_STORE_CTX_init(sctx_ok, store, cert, nullptr) == 1);
    X509_VERIFY_PARAM_set1(X509_STORE_CTX_get0_param(sctx_ok), SSL_get0_param(ssl_match));
    CHECK(X509_verify_cert(sctx_ok) == 1);
    X509_STORE_CTX_free(sctx_ok);
    SSL_free(ssl_match);

    // IP literal verification also configures IP matching when ca_file is set.
    SSL* ssl_ip = SSL_new(ctx);
    REQUIRE(ssl_ip != nullptr);
    opts.expected_host = "192.168.10.2";
    CHECK(net::configure_verify_host(ssl_ip, "192.168.10.2", opts));
    X509_STORE_CTX* sctx_ip = X509_STORE_CTX_new();
    REQUIRE(X509_STORE_CTX_init(sctx_ip, store, cert, nullptr) == 1);
    X509_VERIFY_PARAM_set1(X509_STORE_CTX_get0_param(sctx_ip), SSL_get0_param(ssl_ip));
    CHECK(X509_verify_cert(sctx_ip) == 0);
    CHECK(X509_STORE_CTX_get_error(sctx_ip) == X509_V_ERR_IP_ADDRESS_MISMATCH);
    X509_STORE_CTX_free(sctx_ip);
    SSL_free(ssl_ip);

    // Only insecure_skip_verify disables peer/host verification.
    SSL* ssl_insecure = SSL_new(ctx);
    REQUIRE(ssl_insecure != nullptr);
    opts.insecure_skip_verify = true;
    CHECK_FALSE(net::should_verify_peer(opts));
    CHECK_FALSE(net::configure_verify_host(ssl_insecure, "ccc.university.edu", opts));
    SSL_free(ssl_insecure);

    SSL_CTX_free(ctx);
    X509_STORE_free(store);
    X509_free(cert);
    EVP_PKEY_free(key);
}

namespace {
void add_x509_ext(X509* issuer, X509* subj, int nid, const char* value) {
    X509V3_CTX ctx;
    X509V3_set_ctx_nodb(&ctx);
    X509V3_set_ctx(&ctx, issuer, subj, nullptr, nullptr, 0);
    X509_EXTENSION* ex = X509V3_EXT_conf_nid(nullptr, &ctx, nid, value);
    REQUIRE(ex != nullptr);
    REQUIRE(X509_add_ext(subj, ex, -1) == 1);
    X509_EXTENSION_free(ex);
}

std::string cert_to_pem(X509* cert) {
    BIO* b = BIO_new(BIO_s_mem());
    REQUIRE(PEM_write_bio_X509(b, cert) == 1);
    char* d = nullptr;
    long n = BIO_get_mem_data(b, &d);
    std::string pem(d, size_t(n));
    BIO_free(b);
    return pem;
}

std::string sign_payload_b64(EVP_PKEY* key, const std::string& data) {
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    REQUIRE(EVP_DigestSignInit(ctx, nullptr, EVP_sha256(), nullptr, key) == 1);
    size_t siglen = 0;
    REQUIRE(EVP_DigestSign(ctx, nullptr, &siglen, reinterpret_cast<const unsigned char*>(data.data()), data.size()) == 1);
    std::string sig(siglen, '\0');
    REQUIRE(EVP_DigestSign(ctx, reinterpret_cast<unsigned char*>(sig.data()), &siglen,
                           reinterpret_cast<const unsigned char*>(data.data()), data.size()) == 1);
    sig.resize(siglen);
    EVP_MD_CTX_free(ctx);
    return crypto::base64_encode(sig);
}

X509* make_cert(EVP_PKEY* subj_key, const char* cn, X509* issuer_cert, long serial) {
    X509* cert = X509_new();
    X509_set_version(cert, 2);
    ASN1_INTEGER_set(X509_get_serialNumber(cert), serial);
    X509_gmtime_adj(X509_getm_notBefore(cert), -60);
    X509_gmtime_adj(X509_getm_notAfter(cert), 3600);
    X509_set_pubkey(cert, subj_key);
    X509_NAME_add_entry_by_txt(X509_get_subject_name(cert), "CN", MBSTRING_ASC,
                               reinterpret_cast<const unsigned char*>(cn), -1, -1, 0);
    X509_set_issuer_name(cert, X509_get_subject_name(issuer_cert ? issuer_cert : cert));
    return cert;
}
}  // namespace

TEST_CASE("verify_signature_with_chain enforces codeSigning leaf cert chained to root CA (P1-C1)") {
    EVP_PKEY* ca_key = EVP_EC_gen("P-256");
    X509* ca_cert = make_cert(ca_key, "XephyGuard Root CA", nullptr, 1);
    add_x509_ext(ca_cert, ca_cert, NID_basic_constraints, "critical,CA:TRUE");
    add_x509_ext(ca_cert, ca_cert, NID_key_usage, "critical,keyCertSign,cRLSign,digitalSignature");
    add_x509_ext(ca_cert, ca_cert, NID_subject_key_identifier, "hash");
    REQUIRE(X509_sign(ca_cert, ca_key, EVP_sha256()) > 0);
    const std::string ca_pem = cert_to_pem(ca_cert);

    // Dedicated codeSigning leaf certificate signed by Root CA.
    EVP_PKEY* signer_key = EVP_EC_gen("P-256");
    X509* signer_cert = make_cert(signer_key, "XephyGuard Signature Signer", ca_cert, 2);
    add_x509_ext(ca_cert, signer_cert, NID_basic_constraints, "critical,CA:FALSE");
    add_x509_ext(ca_cert, signer_cert, NID_key_usage, "critical,digitalSignature");
    add_x509_ext(ca_cert, signer_cert, NID_ext_key_usage, "critical,codeSigning");
    add_x509_ext(ca_cert, signer_cert, NID_subject_key_identifier, "hash");
    add_x509_ext(ca_cert, signer_cert, NID_authority_key_identifier, "keyid:always");
    REQUIRE(X509_sign(signer_cert, ca_key, EVP_sha256()) > 0);
    const std::string signer_pem = cert_to_pem(signer_cert);

    // Leaf certificate WITHOUT codeSigning EKU (only serverAuth).
    EVP_PKEY* tls_leaf_key = EVP_EC_gen("P-256");
    X509* tls_leaf_cert = make_cert(tls_leaf_key, "XephyGuard Web Server", ca_cert, 3);
    add_x509_ext(ca_cert, tls_leaf_cert, NID_basic_constraints, "critical,CA:FALSE");
    add_x509_ext(ca_cert, tls_leaf_cert, NID_key_usage, "critical,digitalSignature");
    add_x509_ext(ca_cert, tls_leaf_cert, NID_ext_key_usage, "critical,serverAuth");
    REQUIRE(X509_sign(tls_leaf_cert, ca_key, EVP_sha256()) > 0);
    const std::string tls_leaf_pem = cert_to_pem(tls_leaf_cert);

    // Untrusted CA + codeSigning leaf signed by untrusted CA.
    EVP_PKEY* rogue_ca_key = EVP_EC_gen("P-256");
    X509* rogue_ca_cert = make_cert(rogue_ca_key, "Rogue CA", nullptr, 10);
    add_x509_ext(rogue_ca_cert, rogue_ca_cert, NID_basic_constraints, "critical,CA:TRUE");
    add_x509_ext(rogue_ca_cert, rogue_ca_cert, NID_key_usage, "critical,keyCertSign,cRLSign");
    REQUIRE(X509_sign(rogue_ca_cert, rogue_ca_key, EVP_sha256()) > 0);

    EVP_PKEY* rogue_signer_key = EVP_EC_gen("P-256");
    X509* rogue_signer_cert = make_cert(rogue_signer_key, "Rogue Signer", rogue_ca_cert, 11);
    add_x509_ext(rogue_ca_cert, rogue_signer_cert, NID_basic_constraints, "critical,CA:FALSE");
    add_x509_ext(rogue_ca_cert, rogue_signer_cert, NID_key_usage, "critical,digitalSignature");
    add_x509_ext(rogue_ca_cert, rogue_signer_cert, NID_ext_key_usage, "critical,codeSigning");
    REQUIRE(X509_sign(rogue_signer_cert, rogue_ca_key, EVP_sha256()) > 0);
    const std::string rogue_signer_pem = cert_to_pem(rogue_signer_cert);

    const std::string bundle = R"({"version":"2026.11.01","rules":[]})";
    const std::string sig_by_signer = sign_payload_b64(signer_key, bundle);
    const std::string sig_by_ca = sign_payload_b64(ca_key, bundle);
    const std::string sig_by_tls_leaf = sign_payload_b64(tls_leaf_key, bundle);
    const std::string sig_by_rogue = sign_payload_b64(rogue_signer_key, bundle);

    // 1. Valid codeSigning leaf cert signed by Root CA succeeds.
    CHECK(crypto::verify_signature_with_chain(bundle, sig_by_signer, ca_pem, signer_pem));
    CHECK_FALSE(crypto::verify_signature_with_chain(bundle + "!", sig_by_signer, ca_pem, signer_pem));

    // 2. Leaf cert signed by untrusted CA is rejected.
    CHECK_FALSE(crypto::verify_signature_with_chain(bundle, sig_by_rogue, ca_pem, rogue_signer_pem));

    // 3. Leaf cert without codeSigning EKU is rejected.
    CHECK_FALSE(crypto::verify_signature_with_chain(bundle, sig_by_tls_leaf, ca_pem, tls_leaf_pem));

    // 4. CA cert passed as signer_cert_pem is rejected (must be non-CA leaf).
    CHECK_FALSE(crypto::verify_signature_with_chain(bundle, sig_by_ca, ca_pem, ca_pem));

    // 5. Empty signer_cert_pem falls back to root CA verification for backward compatibility.
    CHECK(crypto::verify_signature_with_chain(bundle, sig_by_ca, ca_pem, ""));

    X509_free(rogue_signer_cert);
    EVP_PKEY_free(rogue_signer_key);
    X509_free(rogue_ca_cert);
    EVP_PKEY_free(rogue_ca_key);
    X509_free(tls_leaf_cert);
    EVP_PKEY_free(tls_leaf_key);
    X509_free(signer_cert);
    EVP_PKEY_free(signer_key);
    X509_free(ca_cert);
    EVP_PKEY_free(ca_key);
}

TEST_CASE("name-constrained interception sub-CA cannot issue valid cert for CCC or IP literals (P1-C1)") {
    EVP_PKEY* root_key = EVP_EC_gen("P-256");
    X509* root_cert = make_cert(root_key, "XephyGuard Root CA", nullptr, 1);
    add_x509_ext(root_cert, root_cert, NID_basic_constraints, "critical,CA:TRUE");
    add_x509_ext(root_cert, root_cert, NID_key_usage, "critical,keyCertSign,cRLSign");
    REQUIRE(X509_sign(root_cert, root_key, EVP_sha256()) > 0);

    // Name-constrained mitmproxy interception sub-CA: only permitted for .google.com / google.com,
    // and explicitly excluded for all IPv4 addresses (0.0.0.0/0.0.0.0).
    EVP_PKEY* sub_key = EVP_EC_gen("P-256");
    X509* sub_cert = make_cert(sub_key, "XephyGuard Interception Sub-CA", root_cert, 2);
    add_x509_ext(root_cert, sub_cert, NID_basic_constraints, "critical,CA:TRUE,pathlen:0");
    add_x509_ext(root_cert, sub_cert, NID_key_usage, "critical,keyCertSign,cRLSign,digitalSignature");
    add_x509_ext(root_cert, sub_cert, NID_name_constraints,
                 "critical,permitted;DNS:.google.com,permitted;DNS:google.com,excluded;IP:0.0.0.0/0.0.0.0");
    REQUIRE(X509_sign(sub_cert, root_key, EVP_sha256()) > 0);

    auto verify_leaf_issued_by_sub = [&](const char* cn, const char* san) -> int {
        EVP_PKEY* leaf_key = EVP_EC_gen("P-256");
        X509* leaf = make_cert(leaf_key, cn, sub_cert, 100);
        add_x509_ext(sub_cert, leaf, NID_basic_constraints, "critical,CA:FALSE");
        add_x509_ext(sub_cert, leaf, NID_key_usage, "critical,digitalSignature,keyEncipherment");
        add_x509_ext(sub_cert, leaf, NID_ext_key_usage, "serverAuth");
        add_x509_ext(sub_cert, leaf, NID_subject_alt_name, san);
        REQUIRE(X509_sign(leaf, sub_key, EVP_sha256()) > 0);

        X509_STORE* store = X509_STORE_new();
        X509_STORE_add_cert(store, root_cert);
        STACK_OF(X509)* chain = sk_X509_new_null();
        sk_X509_push(chain, sub_cert);

        X509_STORE_CTX* sctx = X509_STORE_CTX_new();
        REQUIRE(X509_STORE_CTX_init(sctx, store, leaf, chain) == 1);
        int rc = X509_verify_cert(sctx);

        X509_STORE_CTX_free(sctx);
        sk_X509_free(chain);
        X509_STORE_free(store);
        X509_free(leaf);
        EVP_PKEY_free(leaf_key);
        return rc;
    };

    // Permitted search domain succeeds.
    CHECK(verify_leaf_issued_by_sub("www.google.com", "DNS:www.google.com") == 1);
    // Non-search hostname (e.g. xephyguard-ccc) is rejected by nameConstraints.
    CHECK(verify_leaf_issued_by_sub("xephyguard-ccc", "DNS:xephyguard-ccc") == 0);
    // Any IPv4 literal (e.g. 192.168.1.1) is rejected by nameConstraints.
    CHECK(verify_leaf_issued_by_sub("192.168.1.1", "IP:192.168.1.1") == 0);

    X509_free(sub_cert);
    EVP_PKEY_free(sub_key);
    X509_free(root_cert);
    EVP_PKEY_free(root_key);
}

TEST_CASE("verify_update_manifest and is_canary_eligible enforce signed rollout rules (P3-C2)") {
    EVP_PKEY* ca_key = EVP_EC_gen("P-256");
    X509* ca_cert = make_cert(ca_key, "XephyGuard Root CA", nullptr, 1);
    add_x509_ext(ca_cert, ca_cert, NID_basic_constraints, "critical,CA:TRUE");
    add_x509_ext(ca_cert, ca_cert, NID_key_usage, "critical,keyCertSign,cRLSign,digitalSignature");
    REQUIRE(X509_sign(ca_cert, ca_key, EVP_sha256()) > 0);
    const std::string ca_pem = cert_to_pem(ca_cert);

    EVP_PKEY* signer_key = EVP_EC_gen("P-256");
    X509* signer_cert = make_cert(signer_key, "XephyGuard Release Signer", ca_cert, 2);
    add_x509_ext(ca_cert, signer_cert, NID_basic_constraints, "critical,CA:FALSE");
    add_x509_ext(ca_cert, signer_cert, NID_key_usage, "critical,digitalSignature");
    add_x509_ext(ca_cert, signer_cert, NID_ext_key_usage, "critical,codeSigning");
    REQUIRE(X509_sign(signer_cert, ca_key, EVP_sha256()) > 0);
    const std::string signer_pem = cert_to_pem(signer_cert);

    const std::string artifact_bytes = "ELF-xephyguard-agent-binary-1.2.0-payload";
    const std::string artifact_sha = crypto::sha256_hex(artifact_bytes);
    const std::string manifest_json = json{
        {"version", "1.2.0"},
        {"os", "linux"},
        {"arch", "x86_64"},
        {"sha256", artifact_sha},
        {"canary_percent", 5},
    }.dump();
    const std::string valid_sig = sign_payload_b64(signer_key, manifest_json);

    // 1. Valid signed manifest + matching binary hash succeeds.
    crypto::UpdateManifest parsed{};
    std::string err;
    REQUIRE(crypto::verify_update_manifest(manifest_json, valid_sig, ca_pem, signer_pem, artifact_bytes, &parsed, &err));
    CHECK(err.empty());
    CHECK(parsed.version == "1.2.0");
    CHECK(parsed.os == "linux");
    CHECK(parsed.arch == "x86_64");
    CHECK(parsed.sha256 == artifact_sha);
    CHECK(parsed.canary_percent == 5);

    // 2. Tampered binary hash is rejected.
    std::string tamper_err;
    CHECK_FALSE(crypto::verify_update_manifest(manifest_json, valid_sig, ca_pem, signer_pem,
                                               artifact_bytes + "-tampered", &parsed, &tamper_err));
    CHECK(tamper_err.find("sha256") != std::string::npos);

    // 3. Invalid signature is rejected.
    std::string sig_err;
    const std::string bad_sig = sign_payload_b64(ca_key, manifest_json);
    CHECK_FALSE(crypto::verify_update_manifest(manifest_json, bad_sig, ca_pem, signer_pem, artifact_bytes, &parsed, &sig_err));
    CHECK(sig_err.find("signature") != std::string::npos);

    // 4. Canary eligibility is deterministic across 0%, 5%, and 100%.
    CHECK_FALSE(crypto::is_canary_eligible("lab-pc-01", 0));
    CHECK(crypto::is_canary_eligible("lab-pc-01", 100));
    const bool first_eval = crypto::is_canary_eligible("lab-pc-01", 5);
    CHECK(crypto::is_canary_eligible("lab-pc-01", 5) == first_eval);

    int canary_hits = 0;
    for (int i = 0; i < 200; ++i) {
        if (crypto::is_canary_eligible("lab-pc-" + std::to_string(i), 5)) ++canary_hits;
    }
    CHECK(canary_hits > 0);
    CHECK(canary_hits < 50);

    X509_free(signer_cert);
    EVP_PKEY_free(signer_key);
    X509_free(ca_cert);
    EVP_PKEY_free(ca_key);
}
