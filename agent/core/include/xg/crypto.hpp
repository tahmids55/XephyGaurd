#pragma once
#include <optional>
#include <string>
#include <vector>

namespace xg::crypto {

std::string sha256_hex(const std::string& data);
std::optional<std::string> sha256_file_hex(const std::string& path);
std::string sha1_raw(const std::string& data);
std::string base64_encode(const std::string& data);
std::optional<std::string> base64_decode(const std::string& b64);
std::string random_bytes(size_t n);
std::string hmac_sha256_hex(const std::string& key, const std::string& data);

/// Verifies `sig_b64` (RSA/ECDSA over SHA-256) of `data` with the public key in the PEM certificate.
bool verify_signature(const std::string& data, const std::string& sig_b64, const std::string& cert_pem);

/// Verifies `sig_b64` over `data` using a dedicated `codeSigning` leaf certificate (`signer_cert_pem`)
/// chained to `root_ca_pem`. When `signer_cert_pem` is empty, falls back to `verify_signature` with `root_ca_pem`.
bool verify_signature_with_chain(const std::string& data, const std::string& sig_b64,
                                 const std::string& root_ca_pem, const std::string& signer_cert_pem = "");

/// Generates an EC P-256 private key (PEM, written with 0600 permissions) and returns a CSR PEM.
std::optional<std::string> generate_key_and_csr(const std::string& key_path, const std::string& common_name,
                                                std::string* err = nullptr);
/// SHA-256 fingerprint (hex, lowercase) of the DER encoding of the first certificate in `pem`.
std::optional<std::string> cert_fingerprint(const std::string& pem);
/// Seconds until notAfter of the certificate, or nullopt.
std::optional<long> cert_seconds_left(const std::string& pem);

struct UpdateManifest {
    std::string version;
    std::string os;
    std::string arch;
    std::string sha256;
    int canary_percent = 100;
};

/// Deterministic canary bucket from SHA-256 of `pc_identifier` modulo 100 < `canary_percent`.
bool is_canary_eligible(const std::string& pc_identifier, int canary_percent);

/// Verifies a signed agent update manifest (`sig_b64` over `manifest_json` using `verify_signature_with_chain`)
/// and verifies that `sha256_hex(artifact_bytes)` matches the manifest's `sha256`.
bool verify_update_manifest(const std::string& manifest_json, const std::string& sig_b64,
                            const std::string& root_ca_pem, const std::string& signer_cert_pem,
                            const std::string& artifact_bytes, UpdateManifest* out,
                            std::string* err = nullptr);

}  // namespace xg::crypto
