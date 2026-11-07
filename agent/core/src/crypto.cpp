#include "xg/crypto.hpp"

#include <openssl/bio.h>
#include <openssl/ec.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <openssl/x509.h>
#include <openssl/x509_vfy.h>
#include <openssl/x509v3.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <memory>

#include "xg/event.hpp"
#include "xg/util.hpp"

#ifndef _WIN32
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace xg::crypto {

namespace {
template <class T, void (*F)(T*)>
struct Deleter { void operator()(T* p) const { F(p); } };
using BioPtr = std::unique_ptr<BIO, Deleter<BIO, BIO_free_all>>;
using X509Ptr = std::unique_ptr<X509, Deleter<X509, X509_free>>;
using X509StorePtr = std::unique_ptr<X509_STORE, Deleter<X509_STORE, X509_STORE_free>>;
using X509StoreCtxPtr = std::unique_ptr<X509_STORE_CTX, Deleter<X509_STORE_CTX, X509_STORE_CTX_free>>;
using PkeyPtr = std::unique_ptr<EVP_PKEY, Deleter<EVP_PKEY, EVP_PKEY_free>>;
using MdCtxPtr = std::unique_ptr<EVP_MD_CTX, Deleter<EVP_MD_CTX, EVP_MD_CTX_free>>;
using ReqPtr = std::unique_ptr<X509_REQ, Deleter<X509_REQ, X509_REQ_free>>;

std::string openssl_err() {
    char buf[256];
    ERR_error_string_n(ERR_get_error(), buf, sizeof buf);
    return buf;
}
X509Ptr load_cert(const std::string& pem) {
    BioPtr bio(BIO_new_mem_buf(pem.data(), int(pem.size())));
    return X509Ptr(PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr));
}
}  // namespace

std::string sha256_hex(const std::string& data) {
    unsigned char md[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(data.data()), data.size(), md);
    return hex_encode(md, sizeof md);
}

std::optional<std::string> sha256_file_hex(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return std::nullopt;
    MdCtxPtr ctx(EVP_MD_CTX_new());
    EVP_DigestInit_ex(ctx.get(), EVP_sha256(), nullptr);
    char buf[64 * 1024];
    while (f) {
        f.read(buf, sizeof buf);
        if (f.gcount() > 0) EVP_DigestUpdate(ctx.get(), buf, size_t(f.gcount()));
    }
    unsigned char md[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
    EVP_DigestFinal_ex(ctx.get(), md, &len);
    return hex_encode(md, len);
}

std::string sha1_raw(const std::string& data) {
    unsigned char md[SHA_DIGEST_LENGTH];
    SHA1(reinterpret_cast<const unsigned char*>(data.data()), data.size(), md);
    return std::string(reinterpret_cast<char*>(md), sizeof md);
}

std::string base64_encode(const std::string& data) {
    std::string out(4 * ((data.size() + 2) / 3) + 1, '\0');
    int n = EVP_EncodeBlock(reinterpret_cast<unsigned char*>(out.data()),
                            reinterpret_cast<const unsigned char*>(data.data()), int(data.size()));
    out.resize(size_t(n));
    return out;
}

std::optional<std::string> base64_decode(const std::string& in) {
    std::string b64;
    for (char c : in) if (c != '\n' && c != '\r' && c != ' ') b64 += c;
    if (b64.size() % 4 != 0) return std::nullopt;
    std::string out(3 * b64.size() / 4 + 1, '\0');
    int n = EVP_DecodeBlock(reinterpret_cast<unsigned char*>(out.data()),
                            reinterpret_cast<const unsigned char*>(b64.data()), int(b64.size()));
    if (n < 0) return std::nullopt;
    size_t pad = 0;
    if (!b64.empty() && b64.back() == '=') ++pad;
    if (b64.size() > 1 && b64[b64.size() - 2] == '=') ++pad;
    out.resize(size_t(n) - pad);
    return out;
}

std::string random_bytes(size_t n) {
    std::string s(n, '\0');
    RAND_bytes(reinterpret_cast<unsigned char*>(s.data()), int(n));
    return s;
}

std::string hmac_sha256_hex(const std::string& key, const std::string& data) {
    unsigned char md[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
    HMAC(EVP_sha256(), key.data(), int(key.size()), reinterpret_cast<const unsigned char*>(data.data()), data.size(), md,
         &len);
    return hex_encode(md, len);
}

bool verify_signature(const std::string& data, const std::string& sig_b64, const std::string& cert_pem) {
    auto sig = base64_decode(sig_b64);
    if (!sig) return false;
    auto cert = load_cert(cert_pem);
    if (!cert) return false;
    PkeyPtr key(X509_get_pubkey(cert.get()));
    if (!key) return false;
    MdCtxPtr ctx(EVP_MD_CTX_new());
    if (EVP_DigestVerifyInit(ctx.get(), nullptr, EVP_sha256(), nullptr, key.get()) != 1) return false;
    return EVP_DigestVerify(ctx.get(), reinterpret_cast<const unsigned char*>(sig->data()), sig->size(),
                            reinterpret_cast<const unsigned char*>(data.data()), data.size()) == 1;
}

bool verify_signature_with_chain(const std::string& data, const std::string& sig_b64,
                                 const std::string& root_ca_pem, const std::string& signer_cert_pem) {
    if (signer_cert_pem.empty()) {
        return verify_signature(data, sig_b64, root_ca_pem);
    }
    auto root_ca = load_cert(root_ca_pem);
    auto signer = load_cert(signer_cert_pem);
    if (!root_ca || !signer) return false;

    X509StorePtr store(X509_STORE_new());
    if (!store || X509_STORE_add_cert(store.get(), root_ca.get()) != 1) return false;

    X509StoreCtxPtr sctx(X509_STORE_CTX_new());
    if (!sctx || X509_STORE_CTX_init(sctx.get(), store.get(), signer.get(), nullptr) != 1) return false;
    if (X509_verify_cert(sctx.get()) != 1) return false;

    const uint32_t ex_flags = X509_get_extension_flags(signer.get());
    if ((ex_flags & EXFLAG_CA) != 0) return false;
    if ((ex_flags & EXFLAG_XKUSAGE) == 0 || (X509_get_extended_key_usage(signer.get()) & XKU_CODE_SIGN) == 0) {
        return false;
    }
    if ((ex_flags & EXFLAG_KUSAGE) != 0 && (X509_get_key_usage(signer.get()) & KU_DIGITAL_SIGNATURE) == 0) {
        return false;
    }

    return verify_signature(data, sig_b64, signer_cert_pem);
}

std::optional<std::string> generate_key_and_csr(const std::string& key_path, const std::string& cn, std::string* err) {
    PkeyPtr key(EVP_EC_gen("P-256"));
    if (!key) { if (err) *err = "keygen: " + openssl_err(); return std::nullopt; }
    // Write the key with restrictive permissions from the start.
    BioPtr kb(BIO_new(BIO_s_mem()));
    PEM_write_bio_PrivateKey(kb.get(), key.get(), nullptr, nullptr, 0, nullptr, nullptr);
    char* kdata = nullptr;
    long klen = BIO_get_mem_data(kb.get(), &kdata);
    std::string kpem(kdata, size_t(klen));
#ifndef _WIN32
    int fd = ::open(key_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0 || ::write(fd, kpem.data(), kpem.size()) != ssize_t(kpem.size())) {
        if (fd >= 0) ::close(fd);
        if (err) *err = "cannot write key file " + key_path;
        return std::nullopt;
    }
    ::fchmod(fd, 0600);
    ::close(fd);
#else
    if (!write_file_atomic(key_path, kpem)) { if (err) *err = "cannot write key file"; return std::nullopt; }
#endif
    ReqPtr req(X509_REQ_new());
    X509_REQ_set_version(req.get(), 0);
    X509_NAME* name = X509_REQ_get_subject_name(req.get());
    X509_NAME_add_entry_by_txt(name, "O", MBSTRING_ASC, reinterpret_cast<const unsigned char*>("XephyGuard Agent"), -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, reinterpret_cast<const unsigned char*>(cn.c_str()), -1, -1, 0);
    X509_REQ_set_pubkey(req.get(), key.get());
    if (!X509_REQ_sign(req.get(), key.get(), EVP_sha256())) { if (err) *err = "csr sign: " + openssl_err(); return std::nullopt; }
    BioPtr out(BIO_new(BIO_s_mem()));
    PEM_write_bio_X509_REQ(out.get(), req.get());
    char* d = nullptr;
    long n = BIO_get_mem_data(out.get(), &d);
    return std::string(d, size_t(n));
}

std::optional<std::string> cert_fingerprint(const std::string& pem) {
    auto cert = load_cert(pem);
    if (!cert) return std::nullopt;
    unsigned char md[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
    if (X509_digest(cert.get(), EVP_sha256(), md, &len) != 1) return std::nullopt;
    return hex_encode(md, len);
}

std::optional<long> cert_seconds_left(const std::string& pem) {
    auto cert = load_cert(pem);
    if (!cert) return std::nullopt;
    int days = 0, secs = 0;
    if (ASN1_TIME_diff(&days, &secs, nullptr, X509_get0_notAfter(cert.get())) != 1) return std::nullopt;
    return long(days) * 86400L + secs;
}

bool is_canary_eligible(const std::string& pc_identifier, int canary_percent) {
    if (canary_percent <= 0) return false;
    if (canary_percent >= 100) return true;
    unsigned char md[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(pc_identifier.data()), pc_identifier.size(), md);
    uint64_t val = 0;
    for (int i = 0; i < 8; ++i) {
        val = (val << 8) | static_cast<uint64_t>(md[i]);
    }
    return static_cast<int>(val % 100ULL) < canary_percent;
}

bool verify_update_manifest(const std::string& manifest_json, const std::string& sig_b64,
                            const std::string& root_ca_pem, const std::string& signer_cert_pem,
                            const std::string& artifact_bytes, UpdateManifest* out,
                            std::string* err) {
    if (!verify_signature_with_chain(manifest_json, sig_b64, root_ca_pem, signer_cert_pem)) {
        if (err) *err = "invalid manifest signature";
        return false;
    }
    auto j = json::parse(manifest_json, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        if (err) *err = "manifest is not valid JSON object";
        return false;
    }
    UpdateManifest m;
    m.version = trim(j.value("version", std::string{}));
    m.os = to_lower(trim(j.value("os", std::string{})));
    m.arch = to_lower(trim(j.value("arch", std::string{})));
    m.sha256 = to_lower(trim(j.value("sha256", std::string{})));
    if (j.contains("canary_percent") && j["canary_percent"].is_number_integer()) {
        m.canary_percent = std::clamp(j["canary_percent"].get<int>(), 0, 100);
    }
    if (m.version.empty()) {
        if (err) *err = "manifest version is empty";
        return false;
    }
    const bool valid_hex = m.sha256.size() == 64 &&
                           std::all_of(m.sha256.begin(), m.sha256.end(), [](unsigned char c) {
                               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
                           });
    if (!valid_hex) {
        if (err) *err = "manifest sha256 is not a 64-character hex string";
        return false;
    }
    const std::string actual_sha256 = sha256_hex(artifact_bytes);
    if (actual_sha256 != m.sha256) {
        if (err) *err = "artifact sha256 mismatch";
        return false;
    }
    if (out) *out = std::move(m);
    return true;
}

}  // namespace xg::crypto
