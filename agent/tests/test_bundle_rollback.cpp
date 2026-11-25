// Signature bundle rollback guard (I-AGENT-30, plan step 32).
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <thread>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

#include "fakes.hpp"
#include "xg/agent.hpp"
#include "xg/crypto.hpp"
#include "xg/util.hpp"

using namespace xg;
namespace fs = std::filesystem;

namespace {
struct TestCa {
    EVP_PKEY* key = EVP_EC_gen("P-256");
    std::string pem;
    TestCa() {
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
        pem.assign(d, size_t(n));
        BIO_free(b);
        X509_free(cert);
    }
    ~TestCa() { EVP_PKEY_free(key); }
    json envelope(const std::string& version) {
        json bundle = test::sample_signatures();
        bundle["version"] = version;
        const std::string text = bundle.dump();
        EVP_MD_CTX* ctx = EVP_MD_CTX_new();
        EVP_DigestSignInit(ctx, nullptr, EVP_sha256(), nullptr, key);
        size_t len = 0;
        EVP_DigestSign(ctx, nullptr, &len, reinterpret_cast<const unsigned char*>(text.data()), text.size());
        std::string sig(len, '\0');
        EVP_DigestSign(ctx, reinterpret_cast<unsigned char*>(sig.data()), &len,
                       reinterpret_cast<const unsigned char*>(text.data()), text.size());
        sig.resize(len);
        EVP_MD_CTX_free(ctx);
        return {{"version", version}, {"bundle", text}, {"signature", crypto::base64_encode(sig)}};
    }
};
}  // namespace

TEST_CASE("bundle versions order by date and time") {
    CHECK(compare_bundle_versions("2026.11.17-101500-ab12cd", "2026.11.17-101501-000000") == -1);
    CHECK(compare_bundle_versions("2026.11.18-ab12cd", "2026.11.17-235959-ffffff") == 1);
    CHECK(compare_bundle_versions("2026.11.17-101500-ab12cd", "2026.11.17-101500-ffffff") == 0);
    CHECK_FALSE(compare_bundle_versions("test-1", "2026.11.17-ab12cd"));
}

TEST_CASE("agent refuses an older signed bundle (rollback) but takes newer ones") {
    const fs::path dir = fs::temp_directory_path() / ("xgr-" + random_hex(4));
    fs::create_directories(dir);
    TestCa ca;
    REQUIRE(write_file_atomic((dir / "ca.crt").string(), ca.pem));
    ServicePlatform plat;
    plat.system = std::make_unique<test::FakeSystem>();
    Config cfg = Config::defaults();
    cfg.data_dir = dir.string();
    cfg.ca_file = (dir / "ca.crt").string();
    {
        Agent agent(cfg, (dir / "agent.toml").string(), plat);
        VectorSink sink;
        agent.set_sink_for_tests(&sink);
        REQUIRE(agent.apply_signatures(ca.envelope("2026.11.17-120000-aaaaaa")));
        CHECK(agent.rules().version() == "2026.11.17-120000-aaaaaa");
        CHECK_FALSE(agent.apply_signatures(ca.envelope("2026.11.16-090000-bbbbbb")));
        CHECK(agent.rules().version() == "2026.11.17-120000-aaaaaa");
        CHECK(agent.apply_signatures(ca.envelope("2026.11.17-130000-cccccc")));
        CHECK(agent.rules().version() == "2026.11.17-130000-cccccc");
    }
    fs::remove_all(dir);
}

TEST_CASE("integrity check reports a CA that differs from the enrollment pin (plan step 20)") {
    const fs::path dir = fs::temp_directory_path() / ("xgp-" + random_hex(4));
    fs::create_directories(dir);
    TestCa pinned, swapped;
    REQUIRE(write_file_atomic((dir / "ca.crt").string(), swapped.pem));
    ServicePlatform plat;
    plat.system = std::make_unique<test::FakeSystem>();
    Config cfg = Config::defaults();
    cfg.data_dir = dir.string();
    cfg.ca_file = (dir / "ca.crt").string();
    cfg.ca_fingerprint = *crypto::cert_fingerprint(pinned.pem);
    cfg.pc_id = "1";
    cfg.token = "t";
    cfg.ccc_host = "127.0.0.1";
    cfg.api_port = 1;  // nothing listens: uplink just fails
    VectorSink sink;
    {
        Agent agent(cfg, (dir / "agent.toml").string(), plat);
        agent.set_sink_for_tests(&sink);
        std::atomic<bool> stop{false};
        std::thread t([&] { agent.run(stop); });
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        stop = true;
        t.join();
    }
    bool seen = false;
    for (const auto& e : sink.events)
        seen = seen || (e.type == ev::tamper && e.data["kind"] == "ca_changed" && e.data.value("pinned", false));
    CHECK(seen);
    fs::remove_all(dir);
}
